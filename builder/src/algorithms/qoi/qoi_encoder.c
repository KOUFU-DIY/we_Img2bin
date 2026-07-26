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

static void img2bin_qoi_make_pixel(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  img2bin_rgba_t source,
  img2bin_qoi_pixel_t *out_pixel)
{
  img2bin_qoi_format_spec_t spec;
  img2bin_rgb_t blended;

  memset(out_pixel, 0, sizeof(*out_pixel));
  img2bin_qoi_get_format_spec(format, &spec);
  blended = img2bin_qoi_blend_to_background(source, background);

  if (spec.has_alpha) {
    out_pixel->a = (spec.a_bits == 1u)
                     ? img2bin_qoi_quantize_alpha_1(source.a)
                     : img2bin_qoi_quantize_channel(source.a, spec.a_bits);
    out_pixel->r = img2bin_qoi_quantize_channel(source.r, spec.r_bits);
    out_pixel->g = img2bin_qoi_quantize_channel(source.g, spec.g_bits);
    out_pixel->b = img2bin_qoi_quantize_channel(source.b, spec.b_bits);
  } else {
    out_pixel->a = 255u;
    out_pixel->r = img2bin_qoi_quantize_channel(blended.r, spec.r_bits);
    out_pixel->g = img2bin_qoi_quantize_channel(blended.g, spec.g_bits);
    out_pixel->b = img2bin_qoi_quantize_channel(blended.b, spec.b_bits);
  }

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

static void img2bin_qoi_emit_raw_chunk(
  const img2bin_qoi_format_spec_t *spec,
  const img2bin_qoi_pixel_t *pixel,
  unsigned char *output,
  size_t *output_size)
{
  if (spec != NULL && !spec->has_alpha && pixel != NULL && pixel->supports_rgb_chunk) {
    output[(*output_size)++] = IMG2BIN_QOI_OP_RGB;
    memcpy(output + *output_size, pixel->rgb, pixel->rgb_size);
    *output_size += pixel->rgb_size;
    return;
  }

  if (pixel != NULL) {
    output[(*output_size)++] = IMG2BIN_QOI_OP_RGBA;
    memcpy(output + *output_size, pixel->full, pixel->full_size);
    *output_size += pixel->full_size;
  }
}

static int img2bin_encode_qoi_payload(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  int enable_lookup_index,
  unsigned int forced_raw_interval,
  img2bin_qoi_index_list_t *index_list,
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
    int is_forced_raw_index = forced_raw_interval > 0u && (pixel_index % (size_t)forced_raw_interval) == 0u;

    rgba.r = source[0];
    rgba.g = source[1];
    rgba.b = source[2];
    rgba.a = source[3];
    img2bin_qoi_make_pixel(format, endianness, background, rgba, &current);

    if (is_forced_raw_index) {
      img2bin_qoi_emit_run(output, &output_size, &run);
      if (index_list != NULL && !img2bin_qoi_index_list_append(index_list, output_size)) {
        free(output);
        img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding %s image.", variant_name);
        return 0;
      }

      hash_index = img2bin_qoi_hash_pixel(&current, &spec);
      if (enable_lookup_index) {
        index[hash_index] = current;
      }

      img2bin_qoi_emit_raw_chunk(&spec, &current, output, &output_size);
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
    0u,
    NULL,
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
    0u,
    NULL,
    "original QOI without index",
    out_buffer,
    out_size,
    error_buffer,
    error_buffer_size);
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
  img2bin_qoi_index_list_t index_list;
  unsigned int effective_interval = 0u;
  unsigned char *payload = NULL;
  size_t payload_size = 0u;
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

  img2bin_qoi_index_list_init(&index_list);
  if (!img2bin_encode_qoi_payload(
        format,
        endianness,
        background,
        image,
        0,
        effective_interval,
        &index_list,
        "indexed QOI",
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

  if (payload_size > SIZE_MAX - (13u + u16_bytes + u24_bytes + u32_bytes)) {
    free(payload);
    img2bin_qoi_index_list_free(&index_list);
    img2bin_set_error(error_buffer, error_buffer_size, "Indexed QOI output would be too large.");
    return 0;
  }

  total_size = 13u + u16_bytes + u24_bytes + u32_bytes + payload_size;
  output = (unsigned char *)malloc(total_size > 0u ? total_size : 1u);
  if (output == NULL) {
    free(payload);
    img2bin_qoi_index_list_free(&index_list);
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while building indexed QOI output.");
    return 0;
  }

  output[0] = 0x0Du;
  img2bin_qoi_write_u16((uint16_t)image->width, IMG2BIN_ENDIAN_BIG, &output[1]);
  img2bin_qoi_write_u16((uint16_t)image->height, IMG2BIN_ENDIAN_BIG, &output[3]);
  img2bin_qoi_write_u16((uint16_t)effective_interval, IMG2BIN_ENDIAN_BIG, &output[5]);
  img2bin_qoi_write_u16((uint16_t)u16_bytes, IMG2BIN_ENDIAN_BIG, &output[7]);
  img2bin_qoi_write_u16((uint16_t)u24_bytes, IMG2BIN_ENDIAN_BIG, &output[9]);
  img2bin_qoi_write_u16((uint16_t)u32_bytes, IMG2BIN_ENDIAN_BIG, &output[11]);

  cursor = output + 13u;
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

  memcpy(cursor, payload, payload_size);

  free(payload);
  img2bin_qoi_index_list_free(&index_list);
  *out_buffer = output;
  *out_size = total_size;
  return 1;
}
