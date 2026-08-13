#include "indexqoimask_encoder.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "format.h"
#include "util.h"

/* =====================  索引QOI_MASK v1.0（A8 蒙版）  =====================
 *
 * payload 布局（多字节字段恒大端；宽高在 6 字节通用资源头里，不在 payload 内）：
 *   [0]     标志位：b1:b0 量化位数 q（00=8bit 01=7bit 10=6bit 11=5bit），b7..b2 恒 0
 *   [1..2]  u16 行索引项数量 m
 *   [3..4]  u32 行索引项数量（必须等于 高−m）
 *   [...]   u16 行索引表（m 项 × 2B）
 *   [...]   u32 行索引表（(高−m) 项 × 4B）
 *   [1B]    字典数量 n（0..64）
 *   [nB]    字典（每项 1B，存 q 位域值）
 *   [...]   像素流（各行数据）
 *
 * 量化域：v = alpha >> (8−q)；行首字节、字典项、ALPHA 后接字节全部是 q 位域值。
 * 行结构：行首 1 字节 = 首像素原始值（无 tag，初始化 prev），其后为 tag 流，
 * 累计输出满宽度个像素立即结束（无对齐、无结束符）。行完全独立（prev 不跨行）。
 * tag（高 2 位判别）：
 *   00 INDEX  b5..b0 = 字典下标（< n），1 像素，prev=字典值
 *   01 DIFF   d0=b5..b3、d1=b2..b0，各 3bit [-4,+3]（存 d+4），2 像素
 *   10 DELTA  6bit [-32,+31]（存 d+32），1 像素
 *   11 RUN    6bit 计数 c∈[0,62]，复制 prev c+1 次；c=63（整字节 0xFF）为
 *      ALPHA 转义：后接 1 字节 q 位域原始值，1 像素
 * DIFF/DELTA 在 q 位域内加减、不回绕（编码器保证中间值都落在 [0, 2^q−1]）；
 * 行尾只剩 1 像素时不使用 DIFF。
 *
 * 行索引：每项 = 该行数据相对像素流起点的字节偏移。内容完全相同的行只存一份
 * 数据（行去重按量化后的行内容判定），多行索引可指向同一偏移。m 取"偏移值仍
 * ≤65535 的最长行前缀"的行数；第 m 行起（0 基）无论偏移大小一律进 u32 表。
 *
 * 字典（两遍法，保证输出唯一，可对拍）：第一遍在无字典条件下模拟编码唯一行，
 * 统计"RUN/DIFF/DELTA 均无法编码"（即将跌入 ALPHA）的像素值频次；出现 ≥2 次
 * （净收益 = 次数×1 − 1 字节存储成本 > 0）的值才有资格进字典，频次降序、同频
 * 次按 q 位域值降序取前 64。第二遍带字典正式编码。两遍的 op 分类完全一致：
 * INDEX 与 ALPHA 都是"单像素、prev=该像素值"，不影响 RUN/DIFF/DELTA 的判定。
 */

#define IMG2BIN_INDEXQOIMASK_DICT_MAX 64u
#define IMG2BIN_INDEXQOIMASK_DEFAULT_QUANTIZE_BITS 6u
#define IMG2BIN_INDEXQOIMASK_FIXED_HEADER_SIZE 5u

typedef struct img2bin_indexqoimask_dict_stat_s {
  unsigned int value;
  size_t frequency;
} img2bin_indexqoimask_dict_stat_t;

/* 频次降序；同频次按 q 位域值降序（与 indexQOI 调色盘的确定性排序约定一致）。 */
static int img2bin_indexqoimask_dict_stat_compare(const void *lhs_ptr, const void *rhs_ptr)
{
  const img2bin_indexqoimask_dict_stat_t *lhs = (const img2bin_indexqoimask_dict_stat_t *)lhs_ptr;
  const img2bin_indexqoimask_dict_stat_t *rhs = (const img2bin_indexqoimask_dict_stat_t *)rhs_ptr;

  if (lhs->frequency != rhs->frequency) {
    return lhs->frequency > rhs->frequency ? -1 : 1;
  }
  if (lhs->value != rhs->value) {
    return lhs->value > rhs->value ? -1 : 1;
  }
  return 0;
}

/* 单行编码。out 为 NULL 时只模拟并把 ALPHA 兜底值频次累计到 alpha_freq
   （第一遍）；dict_lookup 为 NULL 时 INDEX 不可用。返回该行编码字节数。 */
static size_t img2bin_indexqoimask_encode_row(
  const unsigned char *row,
  size_t width,
  const int *dict_lookup,
  size_t *alpha_freq,
  unsigned char *out)
{
  size_t pos = 0u;
  size_t i = 1u;
  unsigned char prev = row[0];

  if (out != NULL) {
    out[pos] = prev;
  }
  ++pos;

  while (i < width) {
    size_t run = 0u;

    while (i + run < width && row[i + run] == prev && run < 63u) {
      ++run;
    }
    if (run >= 2u) {
      if (out != NULL) {
        out[pos] = (unsigned char)(0xC0u | (unsigned int)(run - 1u));
      }
      ++pos;
      i += run;
      continue;
    }

    if (i + 1u < width) {
      int d0 = (int)row[i] - (int)prev;
      int d1 = (int)row[i + 1u] - (int)row[i];

      if (d0 >= -4 && d0 <= 3 && d1 >= -4 && d1 <= 3) {
        if (out != NULL) {
          out[pos] = (unsigned char)(0x40u | ((unsigned int)(d0 + 4) << 3) | (unsigned int)(d1 + 4));
        }
        ++pos;
        prev = row[i + 1u];
        i += 2u;
        continue;
      }
    }

    if (run == 1u) {
      /* 与 prev 相同的孤立像素且 DIFF 不可用（行尾或 d1 越界）：RUN 计数 0。 */
      if (out != NULL) {
        out[pos] = 0xC0u;
      }
      ++pos;
      i += 1u;
      continue;
    }

    {
      int delta = (int)row[i] - (int)prev;

      if (delta >= -32 && delta <= 31) {
        if (out != NULL) {
          out[pos] = (unsigned char)(0x80u | (unsigned int)(delta + 32));
        }
        ++pos;
        prev = row[i];
        i += 1u;
        continue;
      }
    }

    if (dict_lookup != NULL && dict_lookup[row[i]] >= 0) {
      if (out != NULL) {
        out[pos] = (unsigned char)dict_lookup[row[i]];
      }
      ++pos;
      prev = row[i];
      i += 1u;
      continue;
    }

    if (alpha_freq != NULL) {
      ++alpha_freq[row[i]];
    }
    if (out != NULL) {
      out[pos] = 0xFFu;
      out[pos + 1u] = row[i];
    }
    pos += 2u;
    prev = row[i];
    i += 1u;
  }

  return pos;
}

static unsigned int img2bin_indexqoimask_hash_row(const unsigned char *row, size_t width)
{
  unsigned int hash = 2166136261u;
  size_t index = 0u;

  for (index = 0u; index < width; ++index) {
    hash ^= row[index];
    hash *= 16777619u;
  }
  return hash;
}

static void img2bin_indexqoimask_write_u16_be(size_t value, unsigned char *out)
{
  out[0] = (unsigned char)((value >> 8) & 0xFFu);
  out[1] = (unsigned char)(value & 0xFFu);
}

static void img2bin_indexqoimask_write_u32_be(size_t value, unsigned char *out)
{
  out[0] = (unsigned char)((value >> 24) & 0xFFu);
  out[1] = (unsigned char)((value >> 16) & 0xFFu);
  out[2] = (unsigned char)((value >> 8) & 0xFFu);
  out[3] = (unsigned char)(value & 0xFFu);
}

int img2bin_encode_indexqoimask_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned int quantize_bits,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size)
{
  unsigned int q = quantize_bits == 0u ? IMG2BIN_INDEXQOIMASK_DEFAULT_QUANTIZE_BITS : quantize_bits;
  size_t width = 0u;
  size_t height = 0u;
  size_t pixel_count = 0u;
  unsigned char *quantized = NULL;
  size_t *row_unique = NULL;      /* 行号 -> 唯一行槽号 */
  size_t *unique_first_row = NULL; /* 唯一行槽号 -> 首现行号 */
  size_t *hash_slots = NULL;       /* 开放寻址：存 唯一行槽号+1，0 = 空 */
  size_t hash_capacity = 0u;
  size_t unique_count = 0u;
  size_t alpha_freq[256];
  int dict_lookup[256];
  unsigned char dict_values[IMG2BIN_INDEXQOIMASK_DICT_MAX];
  size_t dict_count = 0u;
  unsigned char *stream = NULL;
  size_t *unique_offsets = NULL;
  size_t stream_size = 0u;
  size_t row_worst_size = 0u;
  size_t u16_count = 0u;
  size_t header_size = 0u;
  size_t total_size = 0u;
  unsigned char *output = NULL;
  unsigned char *cursor = NULL;
  size_t index = 0u;
  size_t y = 0u;

  (void)endianness;
  (void)background;

  if (image == NULL || out_buffer == NULL || out_size == NULL || image->pixels == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Invalid indexed QOI mask encode request.");
    return 0;
  }

  if (format != IMG2BIN_FMT_A8) {
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI mask only supports the a8 alpha mask format.");
    return 0;
  }

  if (image->width <= 0 || image->height <= 0) {
    img2bin_set_error(error_buffer, error_buffer_size, "Image dimensions must be positive.");
    return 0;
  }

  if ((unsigned int)image->width > 0xFFFFu || (unsigned int)image->height > 0xFFFFu) {
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI mask requires width and height to fit in 16 bits.");
    return 0;
  }

  if (q < 5u || q > 8u) {
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI mask quantization depth must be 5, 6, 7 or 8 bits.");
    return 0;
  }

  width = (size_t)image->width;
  height = (size_t)image->height;
  if (width > SIZE_MAX / height) {
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI mask output would be too large.");
    return 0;
  }
  pixel_count = width * height;

  quantized = (unsigned char *)malloc(pixel_count);
  row_unique = (size_t *)malloc(height * sizeof(*row_unique));
  unique_first_row = (size_t *)malloc(height * sizeof(*unique_first_row));
  if (quantized == NULL || row_unique == NULL || unique_first_row == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding indexed QOI mask image.");
    goto fail;
  }

  /* 量化到 q 位域：v = alpha >> (8−q)。 */
  for (index = 0u; index < pixel_count; ++index) {
    quantized[index] = (unsigned char)(image->pixels[index * 4u + 3u] >> (8u - q));
  }

  /* 行去重（按量化后的行内容）：FNV-1a 哈希 + 开放寻址。 */
  hash_capacity = 16u;
  while (hash_capacity < height * 2u) {
    hash_capacity *= 2u;
  }
  hash_slots = (size_t *)calloc(hash_capacity, sizeof(*hash_slots));
  if (hash_slots == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding indexed QOI mask image.");
    goto fail;
  }

  for (y = 0u; y < height; ++y) {
    const unsigned char *row = quantized + y * width;
    size_t slot = (size_t)img2bin_indexqoimask_hash_row(row, width) & (hash_capacity - 1u);

    for (;;) {
      if (hash_slots[slot] == 0u) {
        hash_slots[slot] = unique_count + 1u;
        unique_first_row[unique_count] = y;
        row_unique[y] = unique_count;
        ++unique_count;
        break;
      }
      if (memcmp(quantized + unique_first_row[hash_slots[slot] - 1u] * width, row, width) == 0) {
        row_unique[y] = hash_slots[slot] - 1u;
        break;
      }
      slot = (slot + 1u) & (hash_capacity - 1u);
    }
  }
  free(hash_slots);
  hash_slots = NULL;

  /* 第一遍：只对唯一行统计 ALPHA 兜底值频次（重复行只存一份，收益按存储计）。 */
  memset(alpha_freq, 0, sizeof(alpha_freq));
  for (index = 0u; index < unique_count; ++index) {
    (void)img2bin_indexqoimask_encode_row(quantized + unique_first_row[index] * width, width, NULL, alpha_freq, NULL);
  }

  /* 选字典：频次 ≥2（净收益 > 每项 1 字节成本）才进；频次降序、同频次值降序。 */
  {
    img2bin_indexqoimask_dict_stat_t candidates[256];
    size_t candidate_count = 0u;
    unsigned int domain = 1u << q;

    for (index = 0u; index < (size_t)domain; ++index) {
      if (alpha_freq[index] >= 2u) {
        candidates[candidate_count].value = (unsigned int)index;
        candidates[candidate_count].frequency = alpha_freq[index];
        ++candidate_count;
      }
    }
    qsort(candidates, candidate_count, sizeof(*candidates), img2bin_indexqoimask_dict_stat_compare);

    dict_count = candidate_count < IMG2BIN_INDEXQOIMASK_DICT_MAX ? candidate_count : IMG2BIN_INDEXQOIMASK_DICT_MAX;
    for (index = 0u; index < 256u; ++index) {
      dict_lookup[index] = -1;
    }
    for (index = 0u; index < dict_count; ++index) {
      dict_values[index] = (unsigned char)candidates[index].value;
      dict_lookup[candidates[index].value] = (int)index;
    }
  }

  /* 第二遍：带字典正式编码唯一行，记录相对像素流起点的偏移。 */
  row_worst_size = width * 2u - 1u; /* 行首 1 字节 + 其余像素最坏 ALPHA 2 字节 */
  if (unique_count > 0u && row_worst_size > SIZE_MAX / unique_count) {
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI mask output would be too large.");
    goto fail;
  }
  stream = (unsigned char *)malloc(unique_count * row_worst_size);
  unique_offsets = (size_t *)malloc(unique_count * sizeof(*unique_offsets));
  if (stream == NULL || unique_offsets == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding indexed QOI mask image.");
    goto fail;
  }

  for (index = 0u; index < unique_count; ++index) {
    unique_offsets[index] = stream_size;
    stream_size += img2bin_indexqoimask_encode_row(
      quantized + unique_first_row[index] * width, width, dict_lookup, NULL, stream + stream_size);
  }

  for (index = 0u; index < unique_count; ++index) {
    if (unique_offsets[index] > 0xFFFFFFFFu) {
      img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI mask row offset exceeded 32-bit addressable range.");
      goto fail;
    }
  }

  /* m = 偏移值仍 ≤65535 的最长行前缀的行数；第 m 行起一律进 u32 表。 */
  for (u16_count = 0u; u16_count < height; ++u16_count) {
    if (unique_offsets[row_unique[u16_count]] > 0xFFFFu) {
      break;
    }
  }

  header_size = IMG2BIN_INDEXQOIMASK_FIXED_HEADER_SIZE + u16_count * 2u + (height - u16_count) * 4u + 1u + dict_count;
  if (stream_size > SIZE_MAX - header_size) {
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI mask output would be too large.");
    goto fail;
  }
  total_size = header_size + stream_size;

  output = (unsigned char *)malloc(total_size);
  if (output == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while building indexed QOI mask output.");
    goto fail;
  }

  output[0] = (unsigned char)(8u - q); /* 00=8bit 01=7bit 10=6bit 11=5bit，b7..b2 恒 0 */
  img2bin_indexqoimask_write_u16_be(u16_count, &output[1]);
  img2bin_indexqoimask_write_u16_be(height - u16_count, &output[3]);

  cursor = output + IMG2BIN_INDEXQOIMASK_FIXED_HEADER_SIZE;
  for (y = 0u; y < u16_count; ++y) {
    img2bin_indexqoimask_write_u16_be(unique_offsets[row_unique[y]], cursor);
    cursor += 2u;
  }
  for (y = u16_count; y < height; ++y) {
    img2bin_indexqoimask_write_u32_be(unique_offsets[row_unique[y]], cursor);
    cursor += 4u;
  }

  *cursor++ = (unsigned char)dict_count;
  if (dict_count > 0u) {
    memcpy(cursor, dict_values, dict_count);
    cursor += dict_count;
  }
  memcpy(cursor, stream, stream_size);

  free(quantized);
  free(row_unique);
  free(unique_first_row);
  free(stream);
  free(unique_offsets);
  *out_buffer = output;
  *out_size = total_size;
  return 1;

fail:
  free(quantized);
  free(row_unique);
  free(unique_first_row);
  free(hash_slots);
  free(stream);
  free(unique_offsets);
  free(output);
  return 0;
}
