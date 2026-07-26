#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "image_io.h"

#include <stdlib.h>

#include "filesystem.h"
#include "util.h"

int img2bin_load_image(const char *path, img2bin_image_t *out_image, char *error_buffer, size_t error_buffer_size)
{
  unsigned char *file_buffer = NULL;
  unsigned char *pixels = NULL;
  int width = 0;
  int height = 0;
  int channels = 0;
  size_t file_size = 0;

  if (path == NULL || out_image == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Invalid image input.");
    return 0;
  }

  out_image->width = 0;
  out_image->height = 0;
  out_image->pixels = NULL;

  if (!img2bin_read_file(path, &file_buffer, &file_size, error_buffer, error_buffer_size)) {
    return 0;
  }

  pixels = stbi_load_from_memory(file_buffer, (int)file_size, &width, &height, &channels, 4);
  free(file_buffer);

  if (pixels == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to decode image: %s", stbi_failure_reason());
    return 0;
  }

  out_image->width = width;
  out_image->height = height;
  out_image->pixels = pixels;
  (void)channels;

  return 1;
}

void img2bin_free_image(img2bin_image_t *image)
{
  if (image == NULL) {
    return;
  }

  if (image->pixels != NULL) {
    stbi_image_free(image->pixels);
  }

  image->width = 0;
  image->height = 0;
  image->pixels = NULL;
}
