#ifndef IMG2BIN_IMPRLE_ENCODER_H
#define IMG2BIN_IMPRLE_ENCODER_H

#include <stddef.h>

#include "cli.h"
#include "format.h"
#include "image_io.h"

int img2bin_encode_imprle_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size);

#endif
