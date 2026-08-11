#include "qoi_encoder.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "format.h"
#include "util.h"

#define IMG2BIN_QOI_OP_INDEX 0x00u
#define IMG2BIN_QOI_OP_DIFF  0x40u
#define IMG2BIN_QOI_OP_LUMA  0x80u
#define IMG2BIN_QOI_OP_RUN   0xC0u
#define IMG2BIN_QOI_OP_RGB   0xFEu
#define IMG2BIN_QOI_OP_RGBA  0xFFu
#define IMG2BIN_QOI_RUN_MAX  62u

typedef struct img2bin_qoi_format_spec_s {
  unsigned int r_bits;
  unsigned int g_bits;
  unsigned int b_bits;
  unsigned int a_bits;
  int has_alpha;
  int supports_rgb_chunk;
} img2bin_qoi_format_spec_t;

typedef struct img2bin_qoi_pixel_s {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
  unsigned char full[4];
  size_t full_size;
  unsigned char rgb[4];
  size_t rgb_size;
  int supports_rgb_chunk;
} img2bin_qoi_pixel_t;

typedef struct img2bin_qoi_index_list_s {
  size_t count;
  size_t capacity;
  size_t *offsets;
} img2bin_qoi_index_list_t;

static uint8_t img2bin_qoi_quantize_channel(uint8_t value, unsigned int bits)
{
  unsigned int max_value = (1u << bits) - 1u;
  return (uint8_t)(((unsigned int)value * max_value + 127u) / 255u);
}

static uint8_t img2bin_qoi_quantize_alpha_1(uint8_t alpha)
{
  return alpha >= 128u ? 1u : 0u;
}

static img2bin_rgb_t img2bin_qoi_blend_to_background(img2bin_rgba_t pixel, img2bin_rgb_t background)
{
  img2bin_rgb_t blended;
  unsigned int alpha = pixel.a;
  unsigned int inverse = 255u - alpha;

  blended.r = (uint8_t)((pixel.r * alpha + background.r * inverse + 127u) / 255u);
  blended.g = (uint8_t)((pixel.g * alpha + background.g * inverse + 127u) / 255u);
  blended.b = (uint8_t)((pixel.b * alpha + background.b * inverse + 127u) / 255u);
  return blended;
}

static void img2bin_qoi_write_u16(uint16_t value, img2bin_endianness_t endianness, unsigned char *out)
{
  if (endianness == IMG2BIN_ENDIAN_BIG) {
    out[0] = (unsigned char)((value >> 8) & 0xFFu);
    out[1] = (unsigned char)(value & 0xFFu);
  } else {
    out[0] = (unsigned char)(value & 0xFFu);
    out[1] = (unsigned char)((value >> 8) & 0xFFu);
  }
}

static void img2bin_qoi_write_u24_be(size_t value, unsigned char *out)
{
  out[0] = (unsigned char)((value >> 16) & 0xFFu);
  out[1] = (unsigned char)((value >> 8) & 0xFFu);
  out[2] = (unsigned char)(value & 0xFFu);
}

static void img2bin_qoi_write_u32_be(size_t value, unsigned char *out)
{
  out[0] = (unsigned char)((value >> 24) & 0xFFu);
  out[1] = (unsigned char)((value >> 16) & 0xFFu);
  out[2] = (unsigned char)((value >> 8) & 0xFFu);
  out[3] = (unsigned char)(value & 0xFFu);
}

static int img2bin_qoi_get_format_spec(img2bin_pixel_format_t format, img2bin_qoi_format_spec_t *spec)
{
  if (spec == NULL) {
    return 0;
  }

  memset(spec, 0, sizeof(*spec));
  switch (format) {
    case IMG2BIN_FMT_ARGB8888:
      spec->r_bits = 8u;
      spec->g_bits = 8u;
      spec->b_bits = 8u;
      spec->a_bits = 8u;
      spec->has_alpha = 1;
      spec->supports_rgb_chunk = 1;
      return 1;
    case IMG2BIN_FMT_ARGB6666:
      spec->r_bits = 6u;
      spec->g_bits = 6u;
      spec->b_bits = 6u;
      spec->a_bits = 6u;
      spec->has_alpha = 1;
      return 1;
    case IMG2BIN_FMT_ARGB4444:
      spec->r_bits = 4u;
      spec->g_bits = 4u;
      spec->b_bits = 4u;
      spec->a_bits = 4u;
      spec->has_alpha = 1;
      return 1;
    case IMG2BIN_FMT_ARGB2222:
      spec->r_bits = 2u;
      spec->g_bits = 2u;
      spec->b_bits = 2u;
      spec->a_bits = 2u;
      spec->has_alpha = 1;
      return 1;
    case IMG2BIN_FMT_ARGB8565:
      spec->r_bits = 5u;
      spec->g_bits = 6u;
      spec->b_bits = 5u;
      spec->a_bits = 8u;
      spec->has_alpha = 1;
      spec->supports_rgb_chunk = 1;
      return 1;
    case IMG2BIN_FMT_RGB888:
      spec->r_bits = 8u;
      spec->g_bits = 8u;
      spec->b_bits = 8u;
      spec->supports_rgb_chunk = 1;
      return 1;
    case IMG2BIN_FMT_RGB565:
      spec->r_bits = 5u;
      spec->g_bits = 6u;
      spec->b_bits = 5u;
      spec->supports_rgb_chunk = 1;
      return 1;
    case IMG2BIN_FMT_RGB332:
      spec->r_bits = 3u;
      spec->g_bits = 3u;
      spec->b_bits = 2u;
      spec->supports_rgb_chunk = 1;
      return 1;
    case IMG2BIN_FMT_RAGB5155:
      spec->r_bits = 5u;
      spec->g_bits = 5u;
      spec->b_bits = 5u;
      spec->a_bits = 1u;
      spec->has_alpha = 1;
      return 1;
    default:
      return 0;
  }
}

static void img2bin_qoi_pack_rgb565(uint8_t r, uint8_t g, uint8_t b, img2bin_endianness_t endianness, unsigned char *out)
{
  uint16_t value = (uint16_t)(((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b);
  img2bin_qoi_write_u16(value, endianness, out);
}

/* 由“已量化到裁剪域的分量”打包出该格式的完整像素字节（含大小端），
   供 make_pixel 与 indexQOI V2 调色盘条目共用。 */
static void img2bin_qoi_pack_quantized(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  uint8_t r,
  uint8_t g,
  uint8_t b,
  uint8_t a,
  img2bin_qoi_pixel_t *out_pixel)
{
  memset(out_pixel, 0, sizeof(*out_pixel));
  out_pixel->r = r;
  out_pixel->g = g;
  out_pixel->b = b;
  out_pixel->a = a;

  switch (format) {
    case IMG2BIN_FMT_ARGB8888:
      if (endianness == IMG2BIN_ENDIAN_BIG) {
        out_pixel->full[0] = out_pixel->a;
        out_pixel->full[1] = out_pixel->r;
        out_pixel->full[2] = out_pixel->g;
        out_pixel->full[3] = out_pixel->b;
        out_pixel->rgb[0] = out_pixel->r;
        out_pixel->rgb[1] = out_pixel->g;
        out_pixel->rgb[2] = out_pixel->b;
      } else {
        out_pixel->full[0] = out_pixel->b;
        out_pixel->full[1] = out_pixel->g;
        out_pixel->full[2] = out_pixel->r;
        out_pixel->full[3] = out_pixel->a;
        out_pixel->rgb[0] = out_pixel->b;
        out_pixel->rgb[1] = out_pixel->g;
        out_pixel->rgb[2] = out_pixel->r;
      }
      out_pixel->full_size = 4u;
      out_pixel->rgb_size = 3u;
      out_pixel->supports_rgb_chunk = 1;
      break;
    case IMG2BIN_FMT_ARGB6666:
    {
      unsigned char bytes[3];

      bytes[0] = (unsigned char)((out_pixel->a << 2) | (out_pixel->r >> 4));
      bytes[1] = (unsigned char)(((out_pixel->r & 0x0Fu) << 4) | (out_pixel->g >> 2));
      bytes[2] = (unsigned char)(((out_pixel->g & 0x03u) << 6) | out_pixel->b);
      if (endianness == IMG2BIN_ENDIAN_BIG) {
        memcpy(out_pixel->full, bytes, 3u);
      } else {
        out_pixel->full[0] = bytes[2];
        out_pixel->full[1] = bytes[1];
        out_pixel->full[2] = bytes[0];
      }
      out_pixel->full_size = 3u;
      break;
    }
    case IMG2BIN_FMT_ARGB4444:
    {
      unsigned char bytes[2];

      bytes[0] = (unsigned char)((out_pixel->a << 4) | out_pixel->r);
      bytes[1] = (unsigned char)((out_pixel->g << 4) | out_pixel->b);
      if (endianness == IMG2BIN_ENDIAN_BIG) {
        memcpy(out_pixel->full, bytes, 2u);
      } else {
        out_pixel->full[0] = bytes[1];
        out_pixel->full[1] = bytes[0];
      }
      out_pixel->full_size = 2u;
      break;
    }
    case IMG2BIN_FMT_ARGB2222:
      out_pixel->full[0] = (unsigned char)((out_pixel->a << 6) | (out_pixel->r << 4) | (out_pixel->g << 2) | out_pixel->b);
      out_pixel->full_size = 1u;
      break;
    case IMG2BIN_FMT_ARGB8565:
      if (endianness == IMG2BIN_ENDIAN_BIG) {
        out_pixel->full[0] = out_pixel->a;
        img2bin_qoi_pack_rgb565(out_pixel->r, out_pixel->g, out_pixel->b, endianness, &out_pixel->full[1]);
      } else {
        img2bin_qoi_pack_rgb565(out_pixel->r, out_pixel->g, out_pixel->b, endianness, out_pixel->full);
        out_pixel->full[2] = out_pixel->a;
      }
      img2bin_qoi_pack_rgb565(out_pixel->r, out_pixel->g, out_pixel->b, endianness, out_pixel->rgb);
      out_pixel->full_size = 3u;
      out_pixel->rgb_size = 2u;
      out_pixel->supports_rgb_chunk = 1;
      break;
    case IMG2BIN_FMT_RGB888:
      if (endianness == IMG2BIN_ENDIAN_BIG) {
        out_pixel->full[0] = out_pixel->r;
        out_pixel->full[1] = out_pixel->g;
        out_pixel->full[2] = out_pixel->b;
      } else {
        out_pixel->full[0] = out_pixel->b;
        out_pixel->full[1] = out_pixel->g;
        out_pixel->full[2] = out_pixel->r;
      }
      memcpy(out_pixel->rgb, out_pixel->full, 3u);
      out_pixel->full_size = 3u;
      out_pixel->rgb_size = 3u;
      out_pixel->supports_rgb_chunk = 1;
      break;
    case IMG2BIN_FMT_RGB565:
      img2bin_qoi_pack_rgb565(out_pixel->r, out_pixel->g, out_pixel->b, endianness, out_pixel->full);
      memcpy(out_pixel->rgb, out_pixel->full, 2u);
      out_pixel->full_size = 2u;
      out_pixel->rgb_size = 2u;
      out_pixel->supports_rgb_chunk = 1;
      break;
    case IMG2BIN_FMT_RGB332:
      out_pixel->full[0] = (unsigned char)((out_pixel->r << 5) | (out_pixel->g << 2) | out_pixel->b);
      out_pixel->rgb[0] = out_pixel->full[0];
      out_pixel->full_size = 1u;
      out_pixel->rgb_size = 1u;
      out_pixel->supports_rgb_chunk = 1;
      break;
    case IMG2BIN_FMT_RAGB5155:
    {
      uint16_t value = (uint16_t)(((uint16_t)out_pixel->r << 11) | ((uint16_t)out_pixel->a << 10) | ((uint16_t)out_pixel->g << 5) | (uint16_t)out_pixel->b);
      img2bin_qoi_write_u16(value, endianness, out_pixel->full);
      out_pixel->full_size = 2u;
      break;
    }
    default:
      break;
  }
}

static void img2bin_qoi_make_pixel(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  img2bin_rgba_t source,
  img2bin_qoi_pixel_t *out_pixel)
{
  img2bin_qoi_format_spec_t spec;
  img2bin_rgb_t blended;
  uint8_t r = 0u;
  uint8_t g = 0u;
  uint8_t b = 0u;
  uint8_t a = 0u;

  img2bin_qoi_get_format_spec(format, &spec);
  blended = img2bin_qoi_blend_to_background(source, background);

  if (spec.has_alpha) {
    a = (spec.a_bits == 1u)
          ? img2bin_qoi_quantize_alpha_1(source.a)
          : img2bin_qoi_quantize_channel(source.a, spec.a_bits);
    r = img2bin_qoi_quantize_channel(source.r, spec.r_bits);
    g = img2bin_qoi_quantize_channel(source.g, spec.g_bits);
    b = img2bin_qoi_quantize_channel(source.b, spec.b_bits);
  } else {
    a = 255u;
    r = img2bin_qoi_quantize_channel(blended.r, spec.r_bits);
    g = img2bin_qoi_quantize_channel(blended.g, spec.g_bits);
    b = img2bin_qoi_quantize_channel(blended.b, spec.b_bits);
  }

  img2bin_qoi_pack_quantized(format, endianness, r, g, b, a, out_pixel);
}

static void img2bin_qoi_make_default_previous(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_qoi_pixel_t *out_pixel)
{
  img2bin_rgba_t source;
  img2bin_rgb_t background = { 0, 0, 0 };

  source.r = 0u;
  source.g = 0u;
  source.b = 0u;
  source.a = 255u;
  img2bin_qoi_make_pixel(format, endianness, background, source, out_pixel);
}

static int img2bin_qoi_pixels_equal(const img2bin_qoi_pixel_t *left, const img2bin_qoi_pixel_t *right)
{
  return left->r == right->r &&
         left->g == right->g &&
         left->b == right->b &&
         left->a == right->a &&
         left->full_size == right->full_size &&
         memcmp(left->full, right->full, left->full_size) == 0;
}

static unsigned int img2bin_qoi_hash_pixel(const img2bin_qoi_pixel_t *pixel, const img2bin_qoi_format_spec_t *spec)
{
  unsigned int alpha = spec->has_alpha ? pixel->a : 255u;
  return (pixel->r * 3u + pixel->g * 5u + pixel->b * 7u + alpha * 11u) & 63u;
}

static int img2bin_qoi_can_emit_diff(const img2bin_qoi_pixel_t *current, const img2bin_qoi_pixel_t *previous)
{
  int dr = (int)current->r - (int)previous->r;
  int dg = (int)current->g - (int)previous->g;
  int db = (int)current->b - (int)previous->b;

  return dr >= -2 && dr <= 1 &&
         dg >= -2 && dg <= 1 &&
         db >= -2 && db <= 1;
}

static int img2bin_qoi_can_emit_luma(const img2bin_qoi_pixel_t *current, const img2bin_qoi_pixel_t *previous)
{
  int dr = (int)current->r - (int)previous->r;
  int dg = (int)current->g - (int)previous->g;
  int db = (int)current->b - (int)previous->b;
  int dr_dg = dr - dg;
  int db_dg = db - dg;

  return dg >= -32 && dg <= 31 &&
         dr_dg >= -8 && dr_dg <= 7 &&
         db_dg >= -8 && db_dg <= 7;
}

static void img2bin_qoi_index_list_init(img2bin_qoi_index_list_t *list)
{
  if (list == NULL) {
    return;
  }

  memset(list, 0, sizeof(*list));
}

static void img2bin_qoi_index_list_free(img2bin_qoi_index_list_t *list)
{
  if (list == NULL) {
    return;
  }

  free(list->offsets);
  list->offsets = NULL;
  list->count = 0;
  list->capacity = 0;
}

static int img2bin_qoi_index_list_append(img2bin_qoi_index_list_t *list, size_t offset)
{
  size_t *new_offsets = NULL;
  size_t new_capacity = 0;

  if (list == NULL) {
    return 0;
  }

  if (list->count == list->capacity) {
    new_capacity = list->capacity == 0u ? 64u : list->capacity * 2u;
    new_offsets = (size_t *)realloc(list->offsets, new_capacity * sizeof(*new_offsets));
    if (new_offsets == NULL) {
      return 0;
    }
    list->offsets = new_offsets;
    list->capacity = new_capacity;
  }

  list->offsets[list->count++] = offset;
  return 1;
}

static void img2bin_qoi_emit_run(unsigned char *output, size_t *output_size, size_t *run)
{
  if (output == NULL || output_size == NULL || run == NULL || *run == 0u) {
    return;
  }

  output[(*output_size)++] = (unsigned char)(IMG2BIN_QOI_OP_RUN | (unsigned char)(*run - 1u));
  *run = 0u;
}

static int img2bin_encode_qoi_payload(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  int enable_lookup_index,
  const char *variant_name,
  unsigned char **out_payload,
  size_t *out_payload_size,
  char *error_buffer,
  size_t error_buffer_size)
{
  const img2bin_format_info_t *info = img2bin_get_format_info(format);
  img2bin_qoi_format_spec_t spec;
  img2bin_qoi_pixel_t previous;
  img2bin_qoi_pixel_t index[64];
  size_t pixel_count = 0;
  size_t max_size = 0;
  unsigned char *output = NULL;
  size_t output_size = 0;
  size_t pixel_index = 0;
  size_t run = 0;

  if (info == NULL || image == NULL || out_payload == NULL || out_payload_size == NULL || image->pixels == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Invalid %s encode request.", variant_name);
    return 0;
  }

  if (!img2bin_qoi_get_format_spec(format, &spec)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Unsupported %s format.", variant_name);
    return 0;
  }

  if (image->width <= 0 || image->height <= 0) {
    img2bin_set_error(error_buffer, error_buffer_size, "Image dimensions must be positive.");
    return 0;
  }

  pixel_count = (size_t)image->width * (size_t)image->height;
  if (pixel_count > SIZE_MAX / 5u) {
    img2bin_set_error(error_buffer, error_buffer_size, "%s output would be too large.", variant_name);
    return 0;
  }

  max_size = pixel_count * 5u;
  output = (unsigned char *)malloc(max_size > 0u ? max_size : 1u);
  if (output == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding %s image.", variant_name);
    return 0;
  }

  memset(index, 0, sizeof(index));
  img2bin_qoi_make_default_previous(format, endianness, &previous);

  for (pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
    const unsigned char *source = image->pixels + (pixel_index * 4u);
    img2bin_rgba_t rgba;
    img2bin_qoi_pixel_t current;
    unsigned int hash_index = 0;

    rgba.r = source[0];
    rgba.g = source[1];
    rgba.b = source[2];
    rgba.a = source[3];
    img2bin_qoi_make_pixel(format, endianness, background, rgba, &current);

    if (img2bin_qoi_pixels_equal(&current, &previous)) {
      ++run;
      if (run == IMG2BIN_QOI_RUN_MAX || pixel_index + 1u == pixel_count) {
        img2bin_qoi_emit_run(output, &output_size, &run);
      }
      continue;
    }

    img2bin_qoi_emit_run(output, &output_size, &run);

    hash_index = img2bin_qoi_hash_pixel(&current, &spec);
    if (enable_lookup_index && img2bin_qoi_pixels_equal(&index[hash_index], &current)) {
      output[output_size++] = (unsigned char)(IMG2BIN_QOI_OP_INDEX | hash_index);
      previous = current;
      continue;
    }

    if (enable_lookup_index) {
      index[hash_index] = current;
    }

    if (current.a == previous.a && img2bin_qoi_can_emit_diff(&current, &previous)) {
      int dr = (int)current.r - (int)previous.r;
      int dg = (int)current.g - (int)previous.g;
      int db = (int)current.b - (int)previous.b;

      output[output_size++] = (unsigned char)(
        IMG2BIN_QOI_OP_DIFF |
        (unsigned char)((dr + 2) << 4) |
        (unsigned char)((dg + 2) << 2) |
        (unsigned char)(db + 2));
    } else if (current.a == previous.a && img2bin_qoi_can_emit_luma(&current, &previous)) {
      int dr = (int)current.r - (int)previous.r;
      int dg = (int)current.g - (int)previous.g;
      int db = (int)current.b - (int)previous.b;
      int dr_dg = dr - dg;
      int db_dg = db - dg;

      output[output_size++] = (unsigned char)(IMG2BIN_QOI_OP_LUMA | (unsigned char)(dg + 32));
      output[output_size++] = (unsigned char)(((dr_dg + 8) << 4) | (db_dg + 8));
    } else if (current.a == previous.a && current.supports_rgb_chunk) {
      output[output_size++] = IMG2BIN_QOI_OP_RGB;
      memcpy(output + output_size, current.rgb, current.rgb_size);
      output_size += current.rgb_size;
    } else {
      output[output_size++] = IMG2BIN_QOI_OP_RGBA;
      memcpy(output + output_size, current.full, current.full_size);
      output_size += current.full_size;
    }

    previous = current;
  }

  *out_payload = output;
  *out_payload_size = output_size;
  return 1;
}

int img2bin_encode_qoi_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size)
{
  return img2bin_encode_qoi_payload(
    format,
    endianness,
    background,
    image,
    1,
    "original QOI",
    out_buffer,
    out_size,
    error_buffer,
    error_buffer_size);
}

int img2bin_encode_qoif_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size)
{
  return img2bin_encode_qoi_payload(
    format,
    endianness,
    background,
    image,
    0,
    "original QOI without index",
    out_buffer,
    out_size,
    error_buffer,
    error_buffer_size);
}

/* =====================  indexQOI V2（静态调色盘）  =====================
 *
 * payload 布局：
 *   [14字节索引头][u16索引区][u24索引区][u32索引区][调色盘][QOI数据流][0xA0 0x88]
 * 索引头（恒大端）：
 *   [0] 头长度 0x0E（同时是版本标识；V1 为 0x0D）
 *   [1..2] 宽  [3..4] 高  [5..6] 像素索引间隔
 *   [7..8] u16索引区字节数  [9..10] u24区字节数  [11..12] u32区字节数
 *   [13] 调色盘条目数 0..64（0 = 无调色盘）
 * 调色盘每项 = 一个完整原始格式像素（含 Alpha，字节序同 0xFF 全量）。
 * 数据流 op：op<条目数 静态查盘；0x40 DIFF；0x80 LUMA；0xC0 RUN；
 *   0xFE 剥透明度全量（仅 ARGB8888/ARGB8565）；0xFF 原始全量。
 * 段首（索引点）只允许 调色盘op 或 0xFF，RUN 不跨段，空降解码自包含。
 * 两遍法选盘：第一遍统计各颜色进盘可省字节，净收益 > 每像素字节数才有
 * 资格；收益降序、同收益按 32 位有符号颜色键 (r<<24)|(g<<16)|(b<<8)|a
 * 降序取前 64，保证选盘结果确定可对拍。
 */

#define IMG2BIN_INDEXQOI_HEADER_SIZE 14u
#define IMG2BIN_INDEXQOI_PALETTE_MAX 64u
#define IMG2BIN_INDEXQOI_END_MARKER_0 0xA0u
#define IMG2BIN_INDEXQOI_END_MARKER_1 0x88u

typedef struct img2bin_indexqoi_stat_s {
  uint32_t key;
  size_t savings;
  uint8_t used;
} img2bin_indexqoi_stat_t;

typedef struct img2bin_indexqoi_stat_map_s {
  img2bin_indexqoi_stat_t *slots;
  size_t capacity; /* 2 的幂 */
  size_t used;
} img2bin_indexqoi_stat_map_t;

typedef struct img2bin_indexqoi_palette_s {
  size_t count;
  uint32_t keys[IMG2BIN_INDEXQOI_PALETTE_MAX];
  img2bin_qoi_pixel_t pixels[IMG2BIN_INDEXQOI_PALETTE_MAX];
} img2bin_indexqoi_palette_t;

static uint32_t img2bin_indexqoi_color_key(const img2bin_qoi_pixel_t *pixel)
{
  return ((uint32_t)pixel->r << 24) |
         ((uint32_t)pixel->g << 16) |
         ((uint32_t)pixel->b << 8) |
         (uint32_t)pixel->a;
}

static size_t img2bin_indexqoi_stat_slot(const img2bin_indexqoi_stat_map_t *map, uint32_t key)
{
  size_t slot = (size_t)(key * 2654435761u) & (map->capacity - 1u);

  while (map->slots[slot].used && map->slots[slot].key != key) {
    slot = (slot + 1u) & (map->capacity - 1u);
  }
  return slot;
}

static int img2bin_indexqoi_stat_map_grow(img2bin_indexqoi_stat_map_t *map)
{
  img2bin_indexqoi_stat_map_t grown;
  size_t index = 0u;

  if (map->capacity > SIZE_MAX / 2u / sizeof(*grown.slots)) {
    return 0;
  }
  grown.capacity = map->capacity * 2u;
  grown.used = map->used;
  grown.slots = (img2bin_indexqoi_stat_t *)calloc(grown.capacity, sizeof(*grown.slots));
  if (grown.slots == NULL) {
    return 0;
  }

  for (index = 0u; index < map->capacity; ++index) {
    if (map->slots[index].used) {
      grown.slots[img2bin_indexqoi_stat_slot(&grown, map->slots[index].key)] = map->slots[index];
    }
  }

  free(map->slots);
  *map = grown;
  return 1;
}

static int img2bin_indexqoi_stat_map_add(img2bin_indexqoi_stat_map_t *map, uint32_t key, size_t savings)
{
  size_t slot = 0u;

  if (map->used * 2u >= map->capacity && !img2bin_indexqoi_stat_map_grow(map)) {
    return 0;
  }

  slot = img2bin_indexqoi_stat_slot(map, key);
  if (!map->slots[slot].used) {
    map->slots[slot].used = 1u;
    map->slots[slot].key = key;
    map->slots[slot].savings = 0u;
    ++map->used;
  }
  map->slots[slot].savings += savings;
  return 1;
}

/* 收益降序；同收益按 32 位有符号颜色键降序。 */
static int img2bin_indexqoi_stat_compare(const void *lhs_ptr, const void *rhs_ptr)
{
  const img2bin_indexqoi_stat_t *lhs = (const img2bin_indexqoi_stat_t *)lhs_ptr;
  const img2bin_indexqoi_stat_t *rhs = (const img2bin_indexqoi_stat_t *)rhs_ptr;

  if (lhs->savings != rhs->savings) {
    return lhs->savings > rhs->savings ? -1 : 1;
  }
  if ((int32_t)lhs->key != (int32_t)rhs->key) {
    return (int32_t)lhs->key > (int32_t)rhs->key ? -1 : 1;
  }
  return 0;
}

static int img2bin_indexqoi_palette_find(const img2bin_indexqoi_palette_t *palette, uint32_t key)
{
  size_t index = 0u;

  for (index = 0u; index < palette->count; ++index) {
    if (palette->keys[index] == key) {
      return (int)index;
    }
  }
  return -1;
}

/* 第一遍：无调色盘模拟 op 分类，统计每个颜色若进盘可省的字节数。
   调色盘替换不改变解码出的像素值，因此这里的分类与第二遍完全一致。 */
static int img2bin_indexqoi_collect_stats(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned int interval,
  const img2bin_qoi_format_spec_t *spec,
  img2bin_indexqoi_stat_map_t *map)
{
  img2bin_qoi_pixel_t previous;
  size_t pixel_count = (size_t)image->width * (size_t)image->height;
  size_t pixel_index = 0u;

  img2bin_qoi_make_default_previous(format, endianness, &previous);

  for (pixel_index = 0u; pixel_index < pixel_count; ++pixel_index) {
    const unsigned char *source = image->pixels + (pixel_index * 4u);
    img2bin_rgba_t rgba;
    img2bin_qoi_pixel_t current;

    rgba.r = source[0];
    rgba.g = source[1];
    rgba.b = source[2];
    rgba.a = source[3];
    img2bin_qoi_make_pixel(format, endianness, background, rgba, &current);

    if ((pixel_index % (size_t)interval) == 0u) {
      /* 索引点：0xFF 全量 -> 单字节调色盘 op，省 pix_size */
      if (!img2bin_indexqoi_stat_map_add(map, img2bin_indexqoi_color_key(&current), current.full_size)) {
        return 0;
      }
    } else if (img2bin_qoi_pixels_equal(&current, &previous)) {
      /* RUN 像素不查盘，无收益 */
    } else if (current.a == previous.a && img2bin_qoi_can_emit_diff(&current, &previous)) {
      /* DIFF 1 字节 -> 1 字节，收益 0，不计 */
    } else if (current.a == previous.a && img2bin_qoi_can_emit_luma(&current, &previous)) {
      if (!img2bin_indexqoi_stat_map_add(map, img2bin_indexqoi_color_key(&current), 1u)) {
        return 0;
      }
    } else if (current.a == previous.a && spec->has_alpha && current.supports_rgb_chunk) {
      /* 0xFE 剥透明度全量 -> 调色盘 op，省 rgb_size（ARGB8888=3 / ARGB8565=2） */
      if (!img2bin_indexqoi_stat_map_add(map, img2bin_indexqoi_color_key(&current), current.rgb_size)) {
        return 0;
      }
    } else {
      if (!img2bin_indexqoi_stat_map_add(map, img2bin_indexqoi_color_key(&current), current.full_size)) {
        return 0;
      }
    }

    previous = current;
  }

  return 1;
}

static int img2bin_indexqoi_build_palette(
  const img2bin_indexqoi_stat_map_t *map,
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  size_t pix_size,
  img2bin_indexqoi_palette_t *palette)
{
  img2bin_indexqoi_stat_t *candidates = NULL;
  size_t candidate_count = 0u;
  size_t index = 0u;

  palette->count = 0u;
  if (map->used == 0u) {
    return 1;
  }

  candidates = (img2bin_indexqoi_stat_t *)malloc(map->used * sizeof(*candidates));
  if (candidates == NULL) {
    return 0;
  }

  for (index = 0u; index < map->capacity; ++index) {
    /* 净收益必须超过调色盘自身的存储成本（每项 pix_size 字节） */
    if (map->slots[index].used && map->slots[index].savings > pix_size) {
      candidates[candidate_count++] = map->slots[index];
    }
  }

  qsort(candidates, candidate_count, sizeof(*candidates), img2bin_indexqoi_stat_compare);

  palette->count = candidate_count < IMG2BIN_INDEXQOI_PALETTE_MAX ? candidate_count : IMG2BIN_INDEXQOI_PALETTE_MAX;
  for (index = 0u; index < palette->count; ++index) {
    uint32_t key = candidates[index].key;

    palette->keys[index] = key;
    img2bin_qoi_pack_quantized(
      format,
      endianness,
      (uint8_t)((key >> 24) & 0xFFu),
      (uint8_t)((key >> 16) & 0xFFu),
      (uint8_t)((key >> 8) & 0xFFu),
      (uint8_t)(key & 0xFFu),
      &palette->pixels[index]);
  }

  free(candidates);
  return 1;
}

/* 第二遍：带调色盘正式编码 QOI 数据流（含 0xA0 0x88 结尾标志），并记录索引点偏移。 */
static int img2bin_encode_indexqoi_stream(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned int interval,
  const img2bin_indexqoi_palette_t *palette,
  img2bin_qoi_index_list_t *index_list,
  unsigned char **out_stream,
  size_t *out_stream_size,
  char *error_buffer,
  size_t error_buffer_size)
{
  img2bin_qoi_format_spec_t spec;
  img2bin_qoi_pixel_t previous;
  size_t pixel_count = (size_t)image->width * (size_t)image->height;
  unsigned char *output = NULL;
  size_t output_size = 0u;
  size_t pixel_index = 0u;
  size_t run = 0u;

  img2bin_qoi_get_format_spec(format, &spec);

  if (pixel_count > (SIZE_MAX - 2u) / 5u) {
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI output would be too large.");
    return 0;
  }
  output = (unsigned char *)malloc(pixel_count * 5u + 2u);
  if (output == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding indexed QOI image.");
    return 0;
  }

  img2bin_qoi_make_default_previous(format, endianness, &previous);

  for (pixel_index = 0u; pixel_index < pixel_count; ++pixel_index) {
    const unsigned char *source = image->pixels + (pixel_index * 4u);
    img2bin_rgba_t rgba;
    img2bin_qoi_pixel_t current;
    int palette_slot = -1;

    rgba.r = source[0];
    rgba.g = source[1];
    rgba.b = source[2];
    rgba.a = source[3];
    img2bin_qoi_make_pixel(format, endianness, background, rgba, &current);

    if ((pixel_index % (size_t)interval) == 0u) {
      /* 段首：先强制结束 RUN（RUN 不跨段），op 只允许 调色盘op 或 0xFF 全量 */
      img2bin_qoi_emit_run(output, &output_size, &run);
      if (!img2bin_qoi_index_list_append(index_list, output_size)) {
        free(output);
        img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding indexed QOI image.");
        return 0;
      }
      palette_slot = img2bin_indexqoi_palette_find(palette, img2bin_indexqoi_color_key(&current));
      if (palette_slot >= 0) {
        output[output_size++] = (unsigned char)palette_slot;
      } else {
        output[output_size++] = IMG2BIN_QOI_OP_RGBA;
        memcpy(output + output_size, current.full, current.full_size);
        output_size += current.full_size;
      }
      previous = current;
      continue;
    }

    if (img2bin_qoi_pixels_equal(&current, &previous)) {
      ++run;
      if (run == IMG2BIN_QOI_RUN_MAX || pixel_index + 1u == pixel_count) {
        img2bin_qoi_emit_run(output, &output_size, &run);
      }
      continue;
    }

    img2bin_qoi_emit_run(output, &output_size, &run);

    palette_slot = img2bin_indexqoi_palette_find(palette, img2bin_indexqoi_color_key(&current));
    if (palette_slot >= 0) {
      /* 盘命中出单字节 op，不区分透明度是否变化 */
      output[output_size++] = (unsigned char)palette_slot;
    } else if (current.a == previous.a && img2bin_qoi_can_emit_diff(&current, &previous)) {
      int dr = (int)current.r - (int)previous.r;
      int dg = (int)current.g - (int)previous.g;
      int db = (int)current.b - (int)previous.b;

      output[output_size++] = (unsigned char)(
        IMG2BIN_QOI_OP_DIFF |
        (unsigned char)((dr + 2) << 4) |
        (unsigned char)((dg + 2) << 2) |
        (unsigned char)(db + 2));
    } else if (current.a == previous.a && img2bin_qoi_can_emit_luma(&current, &previous)) {
      int dr = (int)current.r - (int)previous.r;
      int dg = (int)current.g - (int)previous.g;
      int db = (int)current.b - (int)previous.b;
      int dr_dg = dr - dg;
      int db_dg = db - dg;

      output[output_size++] = (unsigned char)(IMG2BIN_QOI_OP_LUMA | (unsigned char)(dg + 32));
      output[output_size++] = (unsigned char)(((dr_dg + 8) << 4) | (db_dg + 8));
    } else if (current.a == previous.a && spec.has_alpha && current.supports_rgb_chunk) {
      /* V2 的 0xFE 只用于 ARGB8888/ARGB8565：透明度沿用前像素 */
      output[output_size++] = IMG2BIN_QOI_OP_RGB;
      memcpy(output + output_size, current.rgb, current.rgb_size);
      output_size += current.rgb_size;
    } else {
      output[output_size++] = IMG2BIN_QOI_OP_RGBA;
      memcpy(output + output_size, current.full, current.full_size);
      output_size += current.full_size;
    }

    previous = current;
  }

  output[output_size++] = IMG2BIN_INDEXQOI_END_MARKER_0;
  output[output_size++] = IMG2BIN_INDEXQOI_END_MARKER_1;

  *out_stream = output;
  *out_stream_size = output_size;
  return 1;
}

int img2bin_encode_indexqoi_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned int index_interval,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size)
{
  img2bin_qoi_format_spec_t spec;
  img2bin_indexqoi_stat_map_t stat_map;
  img2bin_indexqoi_palette_t palette;
  img2bin_qoi_index_list_t index_list;
  const img2bin_format_info_t *info = NULL;
  unsigned int effective_interval = 0u;
  unsigned char *payload = NULL;
  size_t payload_size = 0u;
  size_t palette_bytes = 0u;
  size_t u16_bytes = 0u;
  size_t u24_bytes = 0u;
  size_t u32_bytes = 0u;
  size_t total_size = 0u;
  unsigned char *output = NULL;
  unsigned char *cursor = NULL;
  size_t index = 0u;

  if (image == NULL || out_buffer == NULL || out_size == NULL || image->pixels == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Invalid indexed QOI encode request.");
    return 0;
  }

  if (image->width <= 0 || image->height <= 0) {
    img2bin_set_error(error_buffer, error_buffer_size, "Image dimensions must be positive.");
    return 0;
  }

  if ((unsigned int)image->width > 0xFFFFu || (unsigned int)image->height > 0xFFFFu) {
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI requires width and height to fit in 16 bits.");
    return 0;
  }

  effective_interval = index_interval == 0u ? (unsigned int)image->width : index_interval;
  if (effective_interval == 0u || effective_interval > 0xFFFFu) {
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI interval must be in the range 1..65535.");
    return 0;
  }

  info = img2bin_get_format_info(format);
  if (info == NULL || !img2bin_qoi_get_format_spec(format, &spec)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Unsupported indexed QOI format.");
    return 0;
  }

  stat_map.capacity = 1024u;
  stat_map.used = 0u;
  stat_map.slots = (img2bin_indexqoi_stat_t *)calloc(stat_map.capacity, sizeof(*stat_map.slots));
  if (stat_map.slots == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding indexed QOI image.");
    return 0;
  }

  if (!img2bin_indexqoi_collect_stats(format, endianness, background, image, effective_interval, &spec, &stat_map)) {
    free(stat_map.slots);
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding indexed QOI image.");
    return 0;
  }

  if (!img2bin_indexqoi_build_palette(&stat_map, format, endianness, info->bytes_per_pixel, &palette)) {
    free(stat_map.slots);
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding indexed QOI image.");
    return 0;
  }
  free(stat_map.slots);
  stat_map.slots = NULL;

  img2bin_qoi_index_list_init(&index_list);
  if (!img2bin_encode_indexqoi_stream(
        format,
        endianness,
        background,
        image,
        effective_interval,
        &palette,
        &index_list,
        &payload,
        &payload_size,
        error_buffer,
        error_buffer_size)) {
    img2bin_qoi_index_list_free(&index_list);
    return 0;
  }

  for (index = 0u; index < index_list.count; ++index) {
    size_t offset = index_list.offsets[index];

    if (offset <= 0xFFFFu) {
      if (u16_bytes > SIZE_MAX - 2u) {
        free(payload);
        img2bin_qoi_index_list_free(&index_list);
        img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI offset table would be too large.");
        return 0;
      }
      u16_bytes += 2u;
    } else if (offset <= 0xFFFFFFu) {
      if (u24_bytes > SIZE_MAX - 3u) {
        free(payload);
        img2bin_qoi_index_list_free(&index_list);
        img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI offset table would be too large.");
        return 0;
      }
      u24_bytes += 3u;
    } else if (offset <= 0xFFFFFFFFu) {
      if (u32_bytes > SIZE_MAX - 4u) {
        free(payload);
        img2bin_qoi_index_list_free(&index_list);
        img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI offset table would be too large.");
        return 0;
      }
      u32_bytes += 4u;
    } else {
      free(payload);
      img2bin_qoi_index_list_free(&index_list);
      img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI offset exceeded 32-bit addressable range.");
      return 0;
    }
  }

  if (u16_bytes > 0xFFFFu || u24_bytes > 0xFFFFu || u32_bytes > 0xFFFFu) {
    free(payload);
    img2bin_qoi_index_list_free(&index_list);
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI index-table byte lengths must fit in 16-bit header fields.");
    return 0;
  }

  palette_bytes = palette.count * info->bytes_per_pixel;
  if (payload_size > SIZE_MAX - (IMG2BIN_INDEXQOI_HEADER_SIZE + u16_bytes + u24_bytes + u32_bytes + palette_bytes)) {
    free(payload);
    img2bin_qoi_index_list_free(&index_list);
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI output would be too large.");
    return 0;
  }

  total_size = IMG2BIN_INDEXQOI_HEADER_SIZE + u16_bytes + u24_bytes + u32_bytes + palette_bytes + payload_size;
  output = (unsigned char *)malloc(total_size > 0u ? total_size : 1u);
  if (output == NULL) {
    free(payload);
    img2bin_qoi_index_list_free(&index_list);
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while building indexed QOI output.");
    return 0;
  }

  output[0] = (unsigned char)IMG2BIN_INDEXQOI_HEADER_SIZE;
  img2bin_qoi_write_u16((uint16_t)image->width, IMG2BIN_ENDIAN_BIG, &output[1]);
  img2bin_qoi_write_u16((uint16_t)image->height, IMG2BIN_ENDIAN_BIG, &output[3]);
  img2bin_qoi_write_u16((uint16_t)effective_interval, IMG2BIN_ENDIAN_BIG, &output[5]);
  img2bin_qoi_write_u16((uint16_t)u16_bytes, IMG2BIN_ENDIAN_BIG, &output[7]);
  img2bin_qoi_write_u16((uint16_t)u24_bytes, IMG2BIN_ENDIAN_BIG, &output[9]);
  img2bin_qoi_write_u16((uint16_t)u32_bytes, IMG2BIN_ENDIAN_BIG, &output[11]);
  output[13] = (unsigned char)palette.count;

  cursor = output + IMG2BIN_INDEXQOI_HEADER_SIZE;
  for (index = 0u; index < index_list.count; ++index) {
    size_t offset = index_list.offsets[index];
    if (offset <= 0xFFFFu) {
      img2bin_qoi_write_u16((uint16_t)offset, IMG2BIN_ENDIAN_BIG, cursor);
      cursor += 2u;
    }
  }
  for (index = 0u; index < index_list.count; ++index) {
    size_t offset = index_list.offsets[index];
    if (offset > 0xFFFFu && offset <= 0xFFFFFFu) {
      img2bin_qoi_write_u24_be(offset, cursor);
      cursor += 3u;
    }
  }
  for (index = 0u; index < index_list.count; ++index) {
    size_t offset = index_list.offsets[index];
    if (offset > 0xFFFFFFu) {
      img2bin_qoi_write_u32_be(offset, cursor);
      cursor += 4u;
    }
  }

  for (index = 0u; index < palette.count; ++index) {
    memcpy(cursor, palette.pixels[index].full, palette.pixels[index].full_size);
    cursor += palette.pixels[index].full_size;
  }

  memcpy(cursor, payload, payload_size);

  free(payload);
  img2bin_qoi_index_list_free(&index_list);
  *out_buffer = output;
  *out_size = total_size;
  return 1;
}
