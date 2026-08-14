#ifndef IMG2BIN_QOI_ENCODER_H
#define IMG2BIN_QOI_ENCODER_H

#include <stddef.h>

#include "cli.h"
#include "format.h"
#include "image_io.h"

int img2bin_encode_qoi_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size);

int img2bin_encode_qoif_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size);

/* indexQOI V3：固定按行索引（无间隔参数）+ 行去重 + 静态调色盘。 */
int img2bin_encode_indexqoi_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size);

#endif
