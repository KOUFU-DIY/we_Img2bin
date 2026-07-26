#include "raw_encoder.h"

#include <limits.h>
#include <stdlib.h>

#include "format.h"
#include "util.h"

static uint8_t img2bin_quantize_channel(uint8_t value, unsigned int bits)
{
  unsigned int max_value = (1u << bits) - 1u;
  return (uint8_t)(((unsigned int)value * max_value + 127u) / 255u);
}

static uint8_t img2bin_quantize_alpha_1(uint8_t alpha)
{
  return alpha >= 128u ? 1u : 0u;
}

static img2bin_rgb_t img2bin_blend_to_background(img2bin_rgba_t pixel, img2bin_rgb_t background)
{
  img2bin_rgb_t blended;
  unsigned int alpha = pixel.a;
  unsigned int inverse = 255u - alpha;

  blended.r = (uint8_t)((pixel.r * alpha + background.r * inverse + 127u) / 255u);
  blended.g = (uint8_t)((pixel.g * alpha + background.g * inverse + 127u) / 255u);
  blended.b = (uint8_t)((pixel.b * alpha + background.b * inverse + 127u) / 255u);
  return blended;
}

static void img2bin_write_u16(uint16_t value, img2bin_endianness_t endianness, unsigned char *out)
{
  if (endianness == IMG2BIN_ENDIAN_BIG) {
    out[0] = (unsigned char)((value >> 8) & 0xFFu);
    out[1] = (unsigned char)(value & 0xFFu);
  } else {
    out[0] = (unsigned char)(value & 0xFFu);
    out[1] = (unsigned char)((value >> 8) & 0xFFu);
  }
}

static void img2bin_write_argb8888(img2bin_rgba_t pixel, img2bin_endianness_t endianness, unsigned char *out)
{
  if (endianness == IMG2BIN_ENDIAN_BIG) {
    out[0] = pixel.a;
    out[1] = pixel.r;
    out[2] = pixel.g;
    out[3] = pixel.b;
  } else {
    out[0] = pixel.b;
    out[1] = pixel.g;
    out[2] = pixel.r;
    out[3] = pixel.a;
  }
}

static void img2bin_write_argb6666(img2bin_rgba_t pixel, img2bin_endianness_t endianness, unsigned char *out)
{
  uint8_t a = img2bin_quantize_channel(pixel.a, 6);
  uint8_t r = img2bin_quantize_channel(pixel.r, 6);
  uint8_t g = img2bin_quantize_channel(pixel.g, 6);
  uint8_t b = img2bin_quantize_channel(pixel.b, 6);
  unsigned char bytes[3];

  bytes[0] = (unsigned char)((a << 2) | (r >> 4));
  bytes[1] = (unsigned char)(((r & 0x0Fu) << 4) | (g >> 2));
  bytes[2] = (unsigned char)(((g & 0x03u) << 6) | b);

  if (endianness == IMG2BIN_ENDIAN_BIG) {
    out[0] = bytes[0];
    out[1] = bytes[1];
    out[2] = bytes[2];
  } else {
    out[0] = bytes[2];
    out[1] = bytes[1];
    out[2] = bytes[0];
  }
}

static void img2bin_write_argb4444(img2bin_rgba_t pixel, img2bin_endianness_t endianness, unsigned char *out)
{
  uint8_t a = img2bin_quantize_channel(pixel.a, 4);
  uint8_t r = img2bin_quantize_channel(pixel.r, 4);
  uint8_t g = img2bin_quantize_channel(pixel.g, 4);
  uint8_t b = img2bin_quantize_channel(pixel.b, 4);
  unsigned char bytes[2];

  bytes[0] = (unsigned char)((a << 4) | r);
  bytes[1] = (unsigned char)((g << 4) | b);

  if (endianness == IMG2BIN_ENDIAN_BIG) {
    out[0] = bytes[0];
    out[1] = bytes[1];
  } else {
    out[0] = bytes[1];
    out[1] = bytes[0];
  }
}

static void img2bin_write_argb2222(img2bin_rgba_t pixel, unsigned char *out)
{
  uint8_t a = img2bin_quantize_channel(pixel.a, 2);
  uint8_t r = img2bin_quantize_channel(pixel.r, 2);
  uint8_t g = img2bin_quantize_channel(pixel.g, 2);
  uint8_t b = img2bin_quantize_channel(pixel.b, 2);

  out[0] = (unsigned char)((a << 6) | (r << 4) | (g << 2) | b);
}

static void img2bin_write_argb8565(img2bin_rgba_t pixel, img2bin_endianness_t endianness, unsigned char *out)
{
  uint8_t r = img2bin_quantize_channel(pixel.r, 5);
  uint8_t g = img2bin_quantize_channel(pixel.g, 6);
  uint8_t b = img2bin_quantize_channel(pixel.b, 5);
  uint16_t rgb565 = (uint16_t)((r << 11) | (g << 5) | b);

  if (endianness == IMG2BIN_ENDIAN_BIG) {
    out[0] = pixel.a;
    out[1] = (unsigned char)((rgb565 >> 8) & 0xFFu);
    out[2] = (unsigned char)(rgb565 & 0xFFu);
  } else {
    out[0] = (unsigned char)(rgb565 & 0xFFu);
    out[1] = (unsigned char)((rgb565 >> 8) & 0xFFu);
    out[2] = pixel.a;
  }
}

static void img2bin_write_rgb888(img2bin_rgb_t pixel, img2bin_endianness_t endianness, unsigned char *out)
{
  if (endianness == IMG2BIN_ENDIAN_BIG) {
    out[0] = pixel.r;
    out[1] = pixel.g;
    out[2] = pixel.b;
  } else {
    out[0] = pixel.b;
    out[1] = pixel.g;
    out[2] = pixel.r;
  }
}

static void img2bin_write_rgb565(img2bin_rgb_t pixel, img2bin_endianness_t endianness, unsigned char *out)
{
  uint8_t r = img2bin_quantize_channel(pixel.r, 5);
  uint8_t g = img2bin_quantize_channel(pixel.g, 6);
  uint8_t b = img2bin_quantize_channel(pixel.b, 5);
  uint16_t value = (uint16_t)((r << 11) | (g << 5) | b);
  img2bin_write_u16(value, endianness, out);
}

static void img2bin_write_rgb332(img2bin_rgb_t pixel, unsigned char *out)
{
  uint8_t r = img2bin_quantize_channel(pixel.r, 3);
  uint8_t g = img2bin_quantize_channel(pixel.g, 3);
  uint8_t b = img2bin_quantize_channel(pixel.b, 2);

  out[0] = (unsigned char)((r << 5) | (g << 2) | b);
}

static void img2bin_write_ragb5155(img2bin_rgba_t pixel, img2bin_endianness_t endianness, unsigned char *out)
{
  uint8_t r = img2bin_quantize_channel(pixel.r, 5);
  uint8_t g = img2bin_quantize_channel(pixel.g, 5);
  uint8_t b = img2bin_quantize_channel(pixel.b, 5);
  uint8_t a = img2bin_quantize_alpha_1(pixel.a);
  uint16_t value = (uint16_t)((r << 11) | (a << 10) | (g << 5) | b);

  img2bin_write_u16(value, endianness, out);
}

int img2bin_encode_raw_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size)
{
  const img2bin_format_info_t *info = img2bin_get_format_info(format);
  size_t pixel_count = 0;
  size_t total_size = 0;
  unsigned char *output = NULL;
  size_t pixel_index = 0;

  if (info == NULL || image == NULL || out_buffer == NULL || out_size == NULL || image->pixels == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Invalid raw encode request.");
    return 0;
  }

  if (image->width <= 0 || image->height <= 0) {
    img2bin_set_error(error_buffer, error_buffer_size, "Image dimensions must be positive.");
    return 0;
  }

  pixel_count = (size_t)image->width * (size_t)image->height;
  if (pixel_count > SIZE_MAX / info->bytes_per_pixel) {
    img2bin_set_error(error_buffer, error_buffer_size, "Encoded image is too large.");
    return 0;
  }

  total_size = pixel_count * info->bytes_per_pixel;
  output = (unsigned char *)malloc(total_size);
  if (output == NULL && total_size > 0) {
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding image.");
    return 0;
  }

  for (pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
    const unsigned char *source = image->pixels + (pixel_index * 4u);
    img2bin_rgba_t rgba;
    img2bin_rgb_t rgb;
    unsigned char *destination = output + (pixel_index * info->bytes_per_pixel);

    rgba.r = source[0];
    rgba.g = source[1];
    rgba.b = source[2];
    rgba.a = source[3];
    rgb = img2bin_blend_to_background(rgba, background);

    switch (format) {
      case IMG2BIN_FMT_ARGB8888:
        img2bin_write_argb8888(rgba, endianness, destination);
        break;
      case IMG2BIN_FMT_ARGB6666:
        img2bin_write_argb6666(rgba, endianness, destination);
        break;
      case IMG2BIN_FMT_ARGB4444:
        img2bin_write_argb4444(rgba, endianness, destination);
        break;
      case IMG2BIN_FMT_ARGB2222:
        img2bin_write_argb2222(rgba, destination);
        break;
      case IMG2BIN_FMT_ARGB8565:
        img2bin_write_argb8565(rgba, endianness, destination);
        break;
      case IMG2BIN_FMT_RGB888:
        img2bin_write_rgb888(rgb, endianness, destination);
        break;
      case IMG2BIN_FMT_RGB565:
        img2bin_write_rgb565(rgb, endianness, destination);
        break;
      case IMG2BIN_FMT_RGB332:
        img2bin_write_rgb332(rgb, destination);
        break;
      case IMG2BIN_FMT_RAGB5155:
        img2bin_write_ragb5155(rgba, endianness, destination);
        break;
      default:
        free(output);
        img2bin_set_error(error_buffer, error_buffer_size, "Unsupported raw format.");
        return 0;
    }
  }

  *out_buffer = output;
  *out_size = total_size;
  return 1;
}
