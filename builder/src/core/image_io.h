#ifndef IMG2BIN_IMAGE_IO_H
#define IMG2BIN_IMAGE_IO_H

#include <stddef.h>

typedef struct img2bin_image_s {
  int width;
  int height;
  unsigned char *pixels;
} img2bin_image_t;

int img2bin_load_image(const char *path, img2bin_image_t *out_image, char *error_buffer, size_t error_buffer_size);
void img2bin_free_image(img2bin_image_t *image);

#endif
