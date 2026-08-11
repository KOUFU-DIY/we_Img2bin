#include "img2bin_decode.h"

#include <string.h>

typedef struct img2bin_decode_spec_s {
  uint8_t bytes_per_pixel; /* 亚字节格式（A4/A2/A1）为 0 */
  uint8_t r_bits;
  uint8_t g_bits;
  uint8_t b_bits;
  uint8_t a_bits; /* 0 = 无 Alpha */
  uint8_t supports_rgb_chunk;
  uint8_t rgb_chunk_size;
  uint8_t bits_per_pixel;
  uint8_t is_alpha_only; /* Alpha 蒙版家族：仅 raw 算法 + 行打包语义 */
} img2bin_decode_spec_t;

typedef struct img2bin_decode_state_s {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} img2bin_decode_state_t;

typedef struct img2bin_decode_index_entry_s {
  img2bin_decode_state_t state;
  uint8_t used;
} img2bin_decode_index_entry_t;

static const img2bin_decode_spec_t IMG2BIN_DECODE_SPECS[IMG2BIN_DECODE_FMT_COUNT] = {
  { 4, 8, 8, 8, 8, 1, 3, 32, 0 }, /* ARGB8888 */
  { 3, 6, 6, 6, 6, 0, 0, 24, 0 }, /* ARGB6666 */
  { 2, 4, 4, 4, 4, 0, 0, 16, 0 }, /* ARGB4444 */
  { 1, 2, 2, 2, 2, 0, 0, 8, 0 },  /* ARGB2222 */
  { 3, 5, 6, 5, 8, 1, 2, 24, 0 }, /* ARGB8565 */
  { 3, 8, 8, 8, 0, 1, 3, 24, 0 }, /* RGB888 */
  { 2, 5, 6, 5, 0, 1, 2, 16, 0 }, /* RGB565 */
  { 1, 3, 3, 2, 0, 1, 1, 8, 0 },  /* RGB332 */
  { 2, 5, 5, 5, 1, 0, 0, 16, 0 }, /* RAGB5155 */
  { 1, 0, 0, 0, 8, 0, 0, 8, 1 },  /* A8 */
  { 0, 0, 0, 0, 4, 0, 0, 4, 1 },  /* A4 */
  { 0, 0, 0, 0, 2, 0, 0, 2, 1 },  /* A2 */
  { 0, 0, 0, 0, 1, 0, 0, 1, 1 }   /* A1 */
};

static const img2bin_decode_spec_t *img2bin_decode_get_spec(img2bin_decode_format_t format)
{
  if ((int)format < 0 || format >= IMG2BIN_DECODE_FMT_COUNT) {
    return 0;
  }
  return &IMG2BIN_DECODE_SPECS[format];
}

size_t img2bin_decode_bytes_per_pixel(img2bin_decode_format_t format)
{
  const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(format);
  return spec != 0 ? spec->bytes_per_pixel : 0u;
}

size_t img2bin_decode_bits_per_pixel(img2bin_decode_format_t format)
{
  const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(format);
  return spec != 0 ? spec->bits_per_pixel : 0u;
}

size_t img2bin_decode_row_stride(img2bin_decode_format_t format, uint16_t width)
{
  const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(format);

  if (spec == 0 || width == 0u) {
    return 0u;
  }
  return ((size_t)width * spec->bits_per_pixel + 7u) / 8u;
}

static uint8_t img2bin_decode_channel_max(uint8_t bits)
{
  return (uint8_t)((1u << bits) - 1u);
}

static void img2bin_decode_default_previous(const img2bin_decode_spec_t *spec, img2bin_decode_state_t *state)
{
  state->r = 0u;
  state->g = 0u;
  state->b = 0u;
  state->a = spec->a_bits > 0u ? img2bin_decode_channel_max(spec->a_bits) : 255u;
}

static unsigned int img2bin_decode_hash(const img2bin_decode_state_t *state)
{
  return ((unsigned int)state->r * 3u +
          (unsigned int)state->g * 5u +
          (unsigned int)state->b * 7u +
          (unsigned int)state->a * 11u) & 63u;
}

static void img2bin_decode_pack_u16(uint16_t value, img2bin_decode_endianness_t endianness, uint8_t *out)
{
  if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
  } else {
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
  }
}

static uint16_t img2bin_decode_unpack_u16(const uint8_t *bytes, img2bin_decode_endianness_t endianness)
{
  if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
  }
  return (uint16_t)(((uint16_t)bytes[1] << 8) | (uint16_t)bytes[0]);
}

static void img2bin_decode_pack_pixel(
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  const img2bin_decode_state_t *state,
  uint8_t *out)
{
  switch (format) {
    case IMG2BIN_DECODE_FMT_ARGB8888:
      if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
        out[0] = state->a;
        out[1] = state->r;
        out[2] = state->g;
        out[3] = state->b;
      } else {
        out[0] = state->b;
        out[1] = state->g;
        out[2] = state->r;
        out[3] = state->a;
      }
      break;
    case IMG2BIN_DECODE_FMT_ARGB6666:
    {
      uint8_t bytes[3];
      bytes[0] = (uint8_t)((state->a << 2) | (state->r >> 4));
      bytes[1] = (uint8_t)(((state->r & 0x0Fu) << 4) | (state->g >> 2));
      bytes[2] = (uint8_t)(((state->g & 0x03u) << 6) | state->b);
      if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
        out[0] = bytes[0];
        out[1] = bytes[1];
        out[2] = bytes[2];
      } else {
        out[0] = bytes[2];
        out[1] = bytes[1];
        out[2] = bytes[0];
      }
      break;
    }
    case IMG2BIN_DECODE_FMT_ARGB4444:
    {
      uint8_t bytes[2];
      bytes[0] = (uint8_t)((state->a << 4) | state->r);
      bytes[1] = (uint8_t)((state->g << 4) | state->b);
      if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
        out[0] = bytes[0];
        out[1] = bytes[1];
      } else {
        out[0] = bytes[1];
        out[1] = bytes[0];
      }
      break;
    }
    case IMG2BIN_DECODE_FMT_ARGB2222:
      out[0] = (uint8_t)((state->a << 6) | (state->r << 4) | (state->g << 2) | state->b);
      break;
    case IMG2BIN_DECODE_FMT_ARGB8565:
    {
      uint16_t rgb565 = (uint16_t)(((uint16_t)state->r << 11) | ((uint16_t)state->g << 5) | (uint16_t)state->b);
      if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
        out[0] = state->a;
        img2bin_decode_pack_u16(rgb565, endianness, &out[1]);
      } else {
        img2bin_decode_pack_u16(rgb565, endianness, out);
        out[2] = state->a;
      }
      break;
    }
    case IMG2BIN_DECODE_FMT_RGB888:
      if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
        out[0] = state->r;
        out[1] = state->g;
        out[2] = state->b;
      } else {
        out[0] = state->b;
        out[1] = state->g;
        out[2] = state->r;
      }
      break;
    case IMG2BIN_DECODE_FMT_RGB565:
      img2bin_decode_pack_u16(
        (uint16_t)(((uint16_t)state->r << 11) | ((uint16_t)state->g << 5) | (uint16_t)state->b),
        endianness,
        out);
      break;
    case IMG2BIN_DECODE_FMT_RGB332:
      out[0] = (uint8_t)((state->r << 5) | (state->g << 2) | state->b);
      break;
    case IMG2BIN_DECODE_FMT_RAGB5155:
      img2bin_decode_pack_u16(
        (uint16_t)(((uint16_t)state->r << 11) | ((uint16_t)state->a << 10) | ((uint16_t)state->g << 5) | (uint16_t)state->b),
        endianness,
        out);
      break;
    default:
      break;
  }
}

static void img2bin_decode_unpack_full(
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  const uint8_t *bytes,
  img2bin_decode_state_t *state)
{
  switch (format) {
    case IMG2BIN_DECODE_FMT_ARGB8888:
      if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
        state->a = bytes[0];
        state->r = bytes[1];
        state->g = bytes[2];
        state->b = bytes[3];
      } else {
        state->b = bytes[0];
        state->g = bytes[1];
        state->r = bytes[2];
        state->a = bytes[3];
      }
      break;
    case IMG2BIN_DECODE_FMT_ARGB6666:
    {
      uint8_t b0 = endianness == IMG2BIN_DECODE_BIG_ENDIAN ? bytes[0] : bytes[2];
      uint8_t b1 = bytes[1];
      uint8_t b2 = endianness == IMG2BIN_DECODE_BIG_ENDIAN ? bytes[2] : bytes[0];
      state->a = (uint8_t)(b0 >> 2);
      state->r = (uint8_t)(((b0 & 0x03u) << 4) | (b1 >> 4));
      state->g = (uint8_t)(((b1 & 0x0Fu) << 2) | (b2 >> 6));
      state->b = (uint8_t)(b2 & 0x3Fu);
      break;
    }
    case IMG2BIN_DECODE_FMT_ARGB4444:
    {
      uint8_t b0 = endianness == IMG2BIN_DECODE_BIG_ENDIAN ? bytes[0] : bytes[1];
      uint8_t b1 = endianness == IMG2BIN_DECODE_BIG_ENDIAN ? bytes[1] : bytes[0];
      state->a = (uint8_t)(b0 >> 4);
      state->r = (uint8_t)(b0 & 0x0Fu);
      state->g = (uint8_t)(b1 >> 4);
      state->b = (uint8_t)(b1 & 0x0Fu);
      break;
    }
    case IMG2BIN_DECODE_FMT_ARGB2222:
      state->a = (uint8_t)((bytes[0] >> 6) & 0x03u);
      state->r = (uint8_t)((bytes[0] >> 4) & 0x03u);
      state->g = (uint8_t)((bytes[0] >> 2) & 0x03u);
      state->b = (uint8_t)(bytes[0] & 0x03u);
      break;
    case IMG2BIN_DECODE_FMT_ARGB8565:
    {
      uint16_t rgb565 = 0u;
      if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
        state->a = bytes[0];
        rgb565 = img2bin_decode_unpack_u16(&bytes[1], endianness);
      } else {
        rgb565 = img2bin_decode_unpack_u16(bytes, endianness);
        state->a = bytes[2];
      }
      state->r = (uint8_t)((rgb565 >> 11) & 0x1Fu);
      state->g = (uint8_t)((rgb565 >> 5) & 0x3Fu);
      state->b = (uint8_t)(rgb565 & 0x1Fu);
      break;
    }
    case IMG2BIN_DECODE_FMT_RGB888:
      if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
        state->r = bytes[0];
        state->g = bytes[1];
        state->b = bytes[2];
      } else {
        state->b = bytes[0];
        state->g = bytes[1];
        state->r = bytes[2];
      }
      state->a = 255u;
      break;
    case IMG2BIN_DECODE_FMT_RGB565:
    {
      uint16_t value = img2bin_decode_unpack_u16(bytes, endianness);
      state->r = (uint8_t)((value >> 11) & 0x1Fu);
      state->g = (uint8_t)((value >> 5) & 0x3Fu);
      state->b = (uint8_t)(value & 0x1Fu);
      state->a = 255u;
      break;
    }
    case IMG2BIN_DECODE_FMT_RGB332:
      state->r = (uint8_t)((bytes[0] >> 5) & 0x07u);
      state->g = (uint8_t)((bytes[0] >> 2) & 0x07u);
      state->b = (uint8_t)(bytes[0] & 0x03u);
      state->a = 255u;
      break;
    case IMG2BIN_DECODE_FMT_RAGB5155:
    {
      uint16_t value = img2bin_decode_unpack_u16(bytes, endianness);
      state->r = (uint8_t)((value >> 11) & 0x1Fu);
      state->a = (uint8_t)((value >> 10) & 0x01u);
      state->g = (uint8_t)((value >> 5) & 0x1Fu);
      state->b = (uint8_t)(value & 0x1Fu);
      break;
    }
    default:
      break;
  }
}

/* OP_RGB(0xFE) 后接的颜色块：只更新 RGB，Alpha 保持不变。 */
static void img2bin_decode_unpack_rgb(
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  const uint8_t *bytes,
  img2bin_decode_state_t *state)
{
  switch (format) {
    case IMG2BIN_DECODE_FMT_ARGB8888:
      if (endianness == IMG2BIN_DECODE_BIG_ENDIAN) {
        state->r = bytes[0];
        state->g = bytes[1];
        state->b = bytes[2];
      } else {
        state->b = bytes[0];
        state->g = bytes[1];
        state->r = bytes[2];
      }
      break;
    case IMG2BIN_DECODE_FMT_ARGB8565:
    {
      uint16_t rgb565 = img2bin_decode_unpack_u16(bytes, endianness);
      state->r = (uint8_t)((rgb565 >> 11) & 0x1Fu);
      state->g = (uint8_t)((rgb565 >> 5) & 0x3Fu);
      state->b = (uint8_t)(rgb565 & 0x1Fu);
      break;
    }
    case IMG2BIN_DECODE_FMT_RGB888:
    case IMG2BIN_DECODE_FMT_RGB565:
    case IMG2BIN_DECODE_FMT_RGB332:
      img2bin_decode_unpack_full(format, endianness, bytes, state);
      break;
    default:
      break;
  }
}

static void img2bin_decode_store_index(img2bin_decode_index_entry_t *table, const img2bin_decode_state_t *state)
{
  unsigned int slot = img2bin_decode_hash(state);
  table[slot].state = *state;
  table[slot].used = 1u;
}

static img2bin_decode_status_t img2bin_decode_check_output(
  const img2bin_decode_spec_t *spec,
  size_t pixel_count,
  size_t output_capacity,
  size_t *out_expected)
{
  /* Alpha 蒙版家族按行打包，pixel_count 不足以确定 payload 大小；
     这里是全部按像素计数的流式解码器的共同入口，统一挡下（含 A8）。 */
  if (spec->is_alpha_only || spec->bytes_per_pixel == 0u) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }
  if (pixel_count > SIZE_MAX / spec->bytes_per_pixel) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }
  *out_expected = pixel_count * spec->bytes_per_pixel;
  if (*out_expected > output_capacity) {
    return IMG2BIN_DECODE_ERR_OUTPUT_TOO_SMALL;
  }
  return IMG2BIN_DECODE_OK;
}

img2bin_decode_status_t img2bin_decode_raw(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(format);
  size_t expected = 0u;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;

  if (spec == 0 || (input == 0 && input_size > 0u) || (output == 0 && output_capacity > 0u) || out_written == 0) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }

  *out_written = 0u;
  status = img2bin_decode_check_output(spec, pixel_count, output_capacity, &expected);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }

  if (input_size < expected) {
    return IMG2BIN_DECODE_ERR_TRUNCATED;
  }
  if (input_size > expected) {
    return IMG2BIN_DECODE_ERR_TRAILING_DATA;
  }

  if (expected > 0u) {
    memcpy(output, input, expected);
  }
  *out_written = expected;
  return IMG2BIN_DECODE_OK;
}

img2bin_decode_status_t img2bin_decode_raw_alpha(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  uint16_t width,
  uint16_t height,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(format);
  size_t row_stride = 0u;
  size_t expected = 0u;

  if (spec == 0 || !spec->is_alpha_only || width == 0u || height == 0u ||
      (input == 0 && input_size > 0u) || (output == 0 && output_capacity > 0u) || out_written == 0) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }

  *out_written = 0u;
  row_stride = img2bin_decode_row_stride(format, width);
  if (row_stride == 0u || row_stride > SIZE_MAX / (size_t)height) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }
  expected = row_stride * (size_t)height;

  if (expected > output_capacity) {
    return IMG2BIN_DECODE_ERR_OUTPUT_TOO_SMALL;
  }
  if (input_size < expected) {
    return IMG2BIN_DECODE_ERR_TRUNCATED;
  }
  if (input_size > expected) {
    return IMG2BIN_DECODE_ERR_TRAILING_DATA;
  }

  memcpy(output, input, expected);
  *out_written = expected;
  return IMG2BIN_DECODE_OK;
}

img2bin_decode_status_t img2bin_decode_rle(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(format);
  size_t expected = 0u;
  size_t cursor = 0u;
  size_t produced = 0u;
  size_t group_size = 0u;
  size_t repeat = 0u;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;

  if (spec == 0 || input == 0 || output == 0 || out_written == 0) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }

  *out_written = 0u;
  group_size = spec->bytes_per_pixel;
  status = img2bin_decode_check_output(spec, pixel_count, output_capacity, &expected);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }

  for (;;) {
    uint8_t count = 0u;

    if (cursor >= input_size) {
      return IMG2BIN_DECODE_ERR_TRUNCATED;
    }
    count = input[cursor++];
    if (count == 0u) {
      break;
    }

    if (cursor + group_size > input_size) {
      return IMG2BIN_DECODE_ERR_TRUNCATED;
    }
    if (produced + count > pixel_count) {
      return IMG2BIN_DECODE_ERR_CORRUPT;
    }

    for (repeat = 0u; repeat < count; ++repeat) {
      memcpy(output + (produced + repeat) * group_size, input + cursor, group_size);
    }
    produced += count;
    cursor += group_size;
  }

  if (produced != pixel_count) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }
  if (cursor != input_size) {
    return IMG2BIN_DECODE_ERR_TRAILING_DATA;
  }

  *out_written = expected;
  return IMG2BIN_DECODE_OK;
}

img2bin_decode_status_t img2bin_decode_imprle(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(format);
  size_t expected = 0u;
  size_t cursor = 0u;
  size_t produced = 0u;
  size_t group_size = 0u;
  size_t repeat = 0u;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;

  if (spec == 0 || input == 0 || output == 0 || out_written == 0) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }

  *out_written = 0u;
  group_size = spec->bytes_per_pixel;
  status = img2bin_decode_check_output(spec, pixel_count, output_capacity, &expected);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }

  for (;;) {
    uint8_t tag = 0u;
    size_t count = 0u;

    if (cursor >= input_size) {
      return IMG2BIN_DECODE_ERR_TRUNCATED;
    }
    tag = input[cursor++];
    if (tag == 0u) {
      break;
    }

    count = (size_t)(tag & 0x7Fu);
    if (count == 0u) {
      return IMG2BIN_DECODE_ERR_CORRUPT;
    }
    if (produced + count > pixel_count) {
      return IMG2BIN_DECODE_ERR_CORRUPT;
    }

    if ((tag & 0x80u) != 0u) {
      if (cursor + group_size > input_size) {
        return IMG2BIN_DECODE_ERR_TRUNCATED;
      }
      for (repeat = 0u; repeat < count; ++repeat) {
        memcpy(output + (produced + repeat) * group_size, input + cursor, group_size);
      }
      produced += count;
      cursor += group_size;
    } else {
      if (cursor + count * group_size > input_size) {
        return IMG2BIN_DECODE_ERR_TRUNCATED;
      }
      memcpy(output + produced * group_size, input + cursor, count * group_size);
      produced += count;
      cursor += count * group_size;
    }
  }

  if (produced != pixel_count) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }
  if (cursor != input_size) {
    return IMG2BIN_DECODE_ERR_TRAILING_DATA;
  }

  *out_written = expected;
  return IMG2BIN_DECODE_OK;
}

static img2bin_decode_status_t img2bin_decode_qoi_stream(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t pixel_count,
  int allow_index,
  const uint8_t *palette,   /* indexQOI V2 静态调色盘起点；qoi/qoif 传 0 */
  uint8_t palette_count,    /* 0..64；op < palette_count 时查盘 */
  int expect_end_marker,    /* indexQOI V2：解码完成后流尾必须是 0xA0 0x88 */
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(format);
  img2bin_decode_index_entry_t table[64];
  img2bin_decode_state_t state;
  size_t expected = 0u;
  size_t cursor = 0u;
  size_t produced = 0u;
  uint8_t max_r = 0u;
  uint8_t max_g = 0u;
  uint8_t max_b = 0u;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;

  if (spec == 0 || input == 0 || output == 0 || out_written == 0) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }

  *out_written = 0u;
  status = img2bin_decode_check_output(spec, pixel_count, output_capacity, &expected);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }

  memset(table, 0, sizeof(table));
  img2bin_decode_default_previous(spec, &state);
  max_r = img2bin_decode_channel_max(spec->r_bits);
  max_g = img2bin_decode_channel_max(spec->g_bits);
  max_b = img2bin_decode_channel_max(spec->b_bits);

  while (produced < pixel_count) {
    uint8_t opcode = 0u;
    int emit_current = 1;

    if (cursor >= input_size) {
      return IMG2BIN_DECODE_ERR_TRUNCATED;
    }
    opcode = input[cursor++];

    if (opcode == 0xFEu) { /* OP_RGB：颜色原始块，Alpha 不变 */
      if (!spec->supports_rgb_chunk) {
        return IMG2BIN_DECODE_ERR_CORRUPT;
      }
      if (cursor + spec->rgb_chunk_size > input_size) {
        return IMG2BIN_DECODE_ERR_TRUNCATED;
      }
      img2bin_decode_unpack_rgb(format, endianness, input + cursor, &state);
      cursor += spec->rgb_chunk_size;
      if (allow_index) {
        img2bin_decode_store_index(table, &state);
      }
    } else if (opcode == 0xFFu) { /* OP_RGBA：完整像素原始块 */
      if (cursor + spec->bytes_per_pixel > input_size) {
        return IMG2BIN_DECODE_ERR_TRUNCATED;
      }
      img2bin_decode_unpack_full(format, endianness, input + cursor, &state);
      cursor += spec->bytes_per_pixel;
      if (allow_index) {
        img2bin_decode_store_index(table, &state);
      }
    } else if ((opcode & 0xC0u) == 0x00u) { /* OP_INDEX（qoi）/ 调色盘 op（indexQOI V2） */
      if (allow_index) {
        if (!table[opcode].used) {
          return IMG2BIN_DECODE_ERR_CORRUPT;
        }
        state = table[opcode].state;
      } else if (palette != 0 && opcode < palette_count) {
        /* 静态查盘：条目就是完整原始格式像素（含 Alpha），无任何 RAM 字典 */
        img2bin_decode_unpack_full(format, endianness, palette + (size_t)opcode * spec->bytes_per_pixel, &state);
      } else {
        return IMG2BIN_DECODE_ERR_CORRUPT;
      }
    } else if ((opcode & 0xC0u) == 0x40u) { /* OP_DIFF */
      int dr = (int)((opcode >> 4) & 0x03u) - 2;
      int dg = (int)((opcode >> 2) & 0x03u) - 2;
      int db = (int)(opcode & 0x03u) - 2;
      int r = (int)state.r + dr;
      int g = (int)state.g + dg;
      int b = (int)state.b + db;

      if (r < 0 || r > (int)max_r || g < 0 || g > (int)max_g || b < 0 || b > (int)max_b) {
        return IMG2BIN_DECODE_ERR_CORRUPT;
      }
      state.r = (uint8_t)r;
      state.g = (uint8_t)g;
      state.b = (uint8_t)b;
      if (allow_index) {
        img2bin_decode_store_index(table, &state);
      }
    } else if ((opcode & 0xC0u) == 0x80u) { /* OP_LUMA */
      int dg = (int)(opcode & 0x3Fu) - 32;
      int dr = 0;
      int db = 0;
      int r = 0;
      int g = 0;
      int b = 0;
      uint8_t second = 0u;

      if (cursor >= input_size) {
        return IMG2BIN_DECODE_ERR_TRUNCATED;
      }
      second = input[cursor++];
      dr = dg + ((int)((second >> 4) & 0x0Fu) - 8);
      db = dg + ((int)(second & 0x0Fu) - 8);
      r = (int)state.r + dr;
      g = (int)state.g + dg;
      b = (int)state.b + db;

      if (r < 0 || r > (int)max_r || g < 0 || g > (int)max_g || b < 0 || b > (int)max_b) {
        return IMG2BIN_DECODE_ERR_CORRUPT;
      }
      state.r = (uint8_t)r;
      state.g = (uint8_t)g;
      state.b = (uint8_t)b;
      if (allow_index) {
        img2bin_decode_store_index(table, &state);
      }
    } else { /* OP_RUN: 0xC0..0xFD */
      size_t run = (size_t)(opcode & 0x3Fu) + 1u;
      size_t repeat = 0u;

      if (produced + run > pixel_count) {
        return IMG2BIN_DECODE_ERR_CORRUPT;
      }
      for (repeat = 0u; repeat < run; ++repeat) {
        img2bin_decode_pack_pixel(format, endianness, &state, output + (produced + repeat) * spec->bytes_per_pixel);
      }
      produced += run;
      emit_current = 0;
    }

    if (emit_current) {
      img2bin_decode_pack_pixel(format, endianness, &state, output + produced * spec->bytes_per_pixel);
      ++produced;
    }
  }

  if (expect_end_marker) {
    if (input_size - cursor < 2u) {
      return IMG2BIN_DECODE_ERR_TRUNCATED;
    }
    if (input[cursor] != 0xA0u || input[cursor + 1u] != 0x88u) {
      return IMG2BIN_DECODE_ERR_CORRUPT;
    }
    cursor += 2u;
  }
  if (cursor != input_size) {
    return IMG2BIN_DECODE_ERR_TRAILING_DATA;
  }

  *out_written = expected;
  return IMG2BIN_DECODE_OK;
}

img2bin_decode_status_t img2bin_decode_qoi(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  return img2bin_decode_qoi_stream(input, input_size, format, endianness, pixel_count, 1, 0, 0u, 0, output, output_capacity, out_written);
}

img2bin_decode_status_t img2bin_decode_qoif(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  return img2bin_decode_qoi_stream(input, input_size, format, endianness, pixel_count, 0, 0, 0u, 0, output, output_capacity, out_written);
}

static int img2bin_decode_format_from_nibble(uint8_t nibble, img2bin_decode_format_t *out_format)
{
  switch (nibble) {
    case 0x0: *out_format = IMG2BIN_DECODE_FMT_RGB565; return 1;
    case 0x1: *out_format = IMG2BIN_DECODE_FMT_RGB888; return 1;
    case 0x4: *out_format = IMG2BIN_DECODE_FMT_RGB332; return 1;
    case 0x5: *out_format = IMG2BIN_DECODE_FMT_ARGB8888; return 1;
    case 0x6: *out_format = IMG2BIN_DECODE_FMT_ARGB6666; return 1;
    case 0x7: *out_format = IMG2BIN_DECODE_FMT_ARGB4444; return 1;
    case 0x8: *out_format = IMG2BIN_DECODE_FMT_ARGB8565; return 1;
    case 0x9: *out_format = IMG2BIN_DECODE_FMT_ARGB2222; return 1;
    case 0xA: *out_format = IMG2BIN_DECODE_FMT_RAGB5155; return 1;
    case 0xB: *out_format = IMG2BIN_DECODE_FMT_A8; return 1;
    case 0xC: *out_format = IMG2BIN_DECODE_FMT_A4; return 1;
    case 0xD: *out_format = IMG2BIN_DECODE_FMT_A2; return 1;
    case 0xE: *out_format = IMG2BIN_DECODE_FMT_A1; return 1;
    default: return 0; /* 0x2/0x3 旧枚举 RGB555/RGB444，0xF OLED 点阵保留 */
  }
}

img2bin_decode_status_t img2bin_decode_header(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_header_t *out_header)
{
  img2bin_decode_header_t header;

  if (input == 0 || out_header == 0) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }
  if (input_size < IMG2BIN_DECODE_HEADER_SIZE) {
    return IMG2BIN_DECODE_ERR_TRUNCATED;
  }

  header.resource_type = input[0];
  header.algorithm_nibble = (uint8_t)(input[1] >> 4);
  header.format_nibble = (uint8_t)(input[1] & 0x0Fu);
  header.width = (uint16_t)(((uint16_t)input[2] << 8) | (uint16_t)input[3]);
  header.height = (uint16_t)(((uint16_t)input[4] << 8) | (uint16_t)input[5]);

  if (header.resource_type != IMG2BIN_DECODE_RESOURCE_TYPE_IMAGE) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }
  if (header.algorithm_nibble > (uint8_t)IMG2BIN_DECODE_ALGO_QOIF) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }
  if (!img2bin_decode_format_from_nibble(header.format_nibble, &header.format)) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }
  if (header.width == 0u || header.height == 0u) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }

  *out_header = header;
  return IMG2BIN_DECODE_OK;
}

img2bin_decode_status_t img2bin_decode_image(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_endianness_t endianness,
  img2bin_decode_header_t *out_header,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  img2bin_decode_header_t header;
  img2bin_indexqoi_header_t inner_header;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;
  const uint8_t *payload = 0;
  size_t payload_size = 0u;
  size_t pixel_count = 0u;

  status = img2bin_decode_header(input, input_size, &header);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }

  payload = input + IMG2BIN_DECODE_HEADER_SIZE;
  payload_size = input_size - IMG2BIN_DECODE_HEADER_SIZE;
  pixel_count = (size_t)header.width * (size_t)header.height;

  /* Alpha 蒙版家族只有 raw 算法；头里出现其他组合视为损坏流。 */
  {
    const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(header.format);

    if (spec != 0 && spec->is_alpha_only) {
      if (header.algorithm_nibble != (uint8_t)IMG2BIN_DECODE_ALGO_RAW) {
        return IMG2BIN_DECODE_ERR_CORRUPT;
      }
      status = img2bin_decode_raw_alpha(
        payload, payload_size, header.format, header.width, header.height, output, output_capacity, out_written);
      if (status == IMG2BIN_DECODE_OK && out_header != 0) {
        *out_header = header;
      }
      return status;
    }
  }

  switch ((img2bin_decode_algorithm_t)header.algorithm_nibble) {
    case IMG2BIN_DECODE_ALGO_RAW:
      status = img2bin_decode_raw(payload, payload_size, header.format, pixel_count, output, output_capacity, out_written);
      break;
    case IMG2BIN_DECODE_ALGO_RLE:
      status = img2bin_decode_rle(payload, payload_size, header.format, pixel_count, output, output_capacity, out_written);
      break;
    case IMG2BIN_DECODE_ALGO_IMPRLE:
      status = img2bin_decode_imprle(payload, payload_size, header.format, pixel_count, output, output_capacity, out_written);
      break;
    case IMG2BIN_DECODE_ALGO_QOI:
      status = img2bin_decode_qoi(payload, payload_size, header.format, endianness, pixel_count, output, output_capacity, out_written);
      break;
    case IMG2BIN_DECODE_ALGO_QOIF:
      status = img2bin_decode_qoif(payload, payload_size, header.format, endianness, pixel_count, output, output_capacity, out_written);
      break;
    case IMG2BIN_DECODE_ALGO_INDEXQOI:
      status = img2bin_decode_indexqoi_header(payload, payload_size, &inner_header);
      if (status != IMG2BIN_DECODE_OK) {
        return status;
      }
      if (inner_header.width != header.width || inner_header.height != header.height) {
        return IMG2BIN_DECODE_ERR_CORRUPT;
      }
      status = img2bin_decode_indexqoi(payload, payload_size, header.format, endianness, output, output_capacity, out_written);
      break;
    default:
      return IMG2BIN_DECODE_ERR_CORRUPT;
  }

  if (status == IMG2BIN_DECODE_OK && out_header != 0) {
    *out_header = header;
  }
  return status;
}

img2bin_decode_status_t img2bin_decode_image_from_slot(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_endianness_t endianness,
  size_t slot,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  img2bin_decode_header_t header;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;

  status = img2bin_decode_header(input, input_size, &header);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }
  if (header.algorithm_nibble != (uint8_t)IMG2BIN_DECODE_ALGO_INDEXQOI) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }

  return img2bin_decode_indexqoi_from_slot(
    input + IMG2BIN_DECODE_HEADER_SIZE,
    input_size - IMG2BIN_DECODE_HEADER_SIZE,
    header.format,
    endianness,
    slot,
    output,
    output_capacity,
    out_written);
}

img2bin_decode_status_t img2bin_decode_indexqoi_header(
  const uint8_t *input,
  size_t input_size,
  img2bin_indexqoi_header_t *out_header)
{
  img2bin_indexqoi_header_t header;
  size_t expected_slots = 0u;
  size_t pixel_count = 0u;

  if (input == 0 || out_header == 0) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }
  if (input_size < 14u) {
    return IMG2BIN_DECODE_ERR_TRUNCATED;
  }
  /* [0] 头长度兼作版本标识：V2 恒为 0x0E（V1 的 0x0D 视为不支持的旧版本） */
  if (input[0] != 0x0Eu) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }

  header.width = img2bin_decode_unpack_u16(&input[1], IMG2BIN_DECODE_BIG_ENDIAN);
  header.height = img2bin_decode_unpack_u16(&input[3], IMG2BIN_DECODE_BIG_ENDIAN);
  header.index_interval = img2bin_decode_unpack_u16(&input[5], IMG2BIN_DECODE_BIG_ENDIAN);
  header.u16_bytes = img2bin_decode_unpack_u16(&input[7], IMG2BIN_DECODE_BIG_ENDIAN);
  header.u24_bytes = img2bin_decode_unpack_u16(&input[9], IMG2BIN_DECODE_BIG_ENDIAN);
  header.u32_bytes = img2bin_decode_unpack_u16(&input[11], IMG2BIN_DECODE_BIG_ENDIAN);
  header.palette_count = input[13];

  if (header.width == 0u || header.height == 0u || header.index_interval == 0u) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }
  if (header.u16_bytes % 2u != 0u || header.u24_bytes % 3u != 0u || header.u32_bytes % 4u != 0u) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }
  /* 调色盘最多 64 项；超出会与 0x40 起的 DIFF op 空间冲突 */
  if (header.palette_count > 64u) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }

  header.slot_count = (size_t)(header.u16_bytes / 2u) + (size_t)(header.u24_bytes / 3u) + (size_t)(header.u32_bytes / 4u);
  header.palette_offset = 14u + (size_t)header.u16_bytes + (size_t)header.u24_bytes + (size_t)header.u32_bytes;

  pixel_count = (size_t)header.width * (size_t)header.height;
  expected_slots = (pixel_count - 1u) / (size_t)header.index_interval + 1u;
  if (header.slot_count != expected_slots) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }
  if (header.palette_offset > input_size) {
    return IMG2BIN_DECODE_ERR_TRUNCATED;
  }

  *out_header = header;
  return IMG2BIN_DECODE_OK;
}

img2bin_decode_status_t img2bin_decode_indexqoi_offset(
  const uint8_t *input,
  size_t input_size,
  size_t slot,
  uint32_t *out_offset)
{
  img2bin_indexqoi_header_t header;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;
  size_t u16_count = 0u;
  size_t u24_count = 0u;
  const uint8_t *entry = 0;

  if (out_offset == 0) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }

  status = img2bin_decode_indexqoi_header(input, input_size, &header);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }
  if (slot >= header.slot_count) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }

  u16_count = (size_t)header.u16_bytes / 2u;
  u24_count = (size_t)header.u24_bytes / 3u;

  if (slot < u16_count) {
    entry = input + 14u + slot * 2u;
    *out_offset = ((uint32_t)entry[0] << 8) | (uint32_t)entry[1];
  } else if (slot < u16_count + u24_count) {
    entry = input + 14u + (size_t)header.u16_bytes + (slot - u16_count) * 3u;
    *out_offset = ((uint32_t)entry[0] << 16) | ((uint32_t)entry[1] << 8) | (uint32_t)entry[2];
  } else {
    entry = input + 14u + (size_t)header.u16_bytes + (size_t)header.u24_bytes + (slot - u16_count - u24_count) * 4u;
    *out_offset = ((uint32_t)entry[0] << 24) | ((uint32_t)entry[1] << 16) | ((uint32_t)entry[2] << 8) | (uint32_t)entry[3];
  }

  return IMG2BIN_DECODE_OK;
}

/* 计算调色盘尾（= QOI 数据流起点）；亚字节/Alpha 蒙版格式返回 ARGUMENTS。 */
static img2bin_decode_status_t img2bin_decode_indexqoi_stream_start(
  const img2bin_indexqoi_header_t *header,
  img2bin_decode_format_t format,
  size_t input_size,
  size_t *out_stream_start)
{
  const img2bin_decode_spec_t *spec = img2bin_decode_get_spec(format);
  size_t stream_start = 0u;

  if (spec == 0 || spec->is_alpha_only || spec->bytes_per_pixel == 0u) {
    return IMG2BIN_DECODE_ERR_ARGUMENTS;
  }

  stream_start = header->palette_offset + (size_t)header->palette_count * spec->bytes_per_pixel;
  if (stream_start > input_size) {
    return IMG2BIN_DECODE_ERR_TRUNCATED;
  }

  *out_stream_start = stream_start;
  return IMG2BIN_DECODE_OK;
}

img2bin_decode_status_t img2bin_decode_indexqoi(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  img2bin_indexqoi_header_t header;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;
  size_t stream_start = 0u;

  status = img2bin_decode_indexqoi_header(input, input_size, &header);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }
  status = img2bin_decode_indexqoi_stream_start(&header, format, input_size, &stream_start);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }

  return img2bin_decode_qoi_stream(
    input + stream_start,
    input_size - stream_start,
    format,
    endianness,
    (size_t)header.width * (size_t)header.height,
    0,
    input + header.palette_offset,
    header.palette_count,
    1,
    output,
    output_capacity,
    out_written);
}

img2bin_decode_status_t img2bin_decode_indexqoi_from_slot(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t slot,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written)
{
  img2bin_indexqoi_header_t header;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;
  uint32_t offset = 0u;
  size_t pixel_count = 0u;
  size_t base_pixel = 0u;
  size_t stream_start = 0u;
  size_t stream_size = 0u;

  status = img2bin_decode_indexqoi_header(input, input_size, &header);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }
  status = img2bin_decode_indexqoi_stream_start(&header, format, input_size, &stream_start);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }
  status = img2bin_decode_indexqoi_offset(input, input_size, slot, &offset);
  if (status != IMG2BIN_DECODE_OK) {
    return status;
  }

  pixel_count = (size_t)header.width * (size_t)header.height;
  base_pixel = slot * (size_t)header.index_interval;
  if (base_pixel >= pixel_count) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }

  stream_size = input_size - stream_start;
  if ((size_t)offset > stream_size) {
    return IMG2BIN_DECODE_ERR_CORRUPT;
  }

  return img2bin_decode_qoi_stream(
    input + stream_start + offset,
    stream_size - offset,
    format,
    endianness,
    pixel_count - base_pixel,
    0,
    input + header.palette_offset,
    header.palette_count,
    1,
    output,
    output_capacity,
    out_written);
}
