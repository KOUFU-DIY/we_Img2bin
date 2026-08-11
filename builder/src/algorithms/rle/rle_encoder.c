#include "rle_encoder.h"

#include <stdlib.h>
#include <string.h>

#include "raw_encoder.h"
#include "util.h"

static size_t img2bin_rle_count_run(
  const unsigned char *groups,
  size_t group_count,
  size_t group_size,
  size_t start_index)
{
  size_t length = 1;
  const unsigned char *base = groups + (start_index * group_size);

  while (start_index + length < group_count) {
    const unsigned char *next = groups + ((start_index + length) * group_size);
    if (memcmp(base, next, group_size) != 0) {
      break;
    }
    ++length;
  }

  return length;
}

int img2bin_encode_rle_image(
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
  unsigned char *raw_buffer = NULL;
  unsigned char *encoded = NULL;
  size_t raw_size = 0;
  size_t group_count = 0;
  size_t max_size = 0;
  size_t read_index = 0;
  size_t write_index = 0;

  if (info == NULL || out_buffer == NULL || out_size == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Invalid original RLE encode request.");
    return 0;
  }

  if (info->is_alpha_only) {
    img2bin_set_error(error_buffer, error_buffer_size, "Alpha mask formats are only supported by the raw tool.");
    return 0;
  }

  if (!img2bin_encode_raw_image(
        format,
        endianness,
        background,
        image,
        &raw_buffer,
        &raw_size,
        error_buffer,
        error_buffer_size)) {
    return 0;
  }

  if (info->bytes_per_pixel == 0 || raw_size % info->bytes_per_pixel != 0) {
    free(raw_buffer);
    img2bin_set_error(error_buffer, error_buffer_size, "Raw payload size is invalid for original RLE encoding.");
    return 0;
  }

  group_count = raw_size / info->bytes_per_pixel;
  if (group_count > SIZE_MAX / (info->bytes_per_pixel + 1u)) {
    free(raw_buffer);
    img2bin_set_error(error_buffer, error_buffer_size, "Original RLE output would be too large.");
    return 0;
  }

  max_size = group_count * (info->bytes_per_pixel + 1u) + 1u;
  encoded = (unsigned char *)malloc(max_size);
  if (encoded == NULL) {
    free(raw_buffer);
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while encoding original RLE image.");
    return 0;
  }

  while (read_index < group_count) {
    size_t run_length = img2bin_rle_count_run(raw_buffer, group_count, info->bytes_per_pixel, read_index);
    const unsigned char *group = raw_buffer + (read_index * info->bytes_per_pixel);

    while (run_length > 0u) {
      size_t chunk_length = run_length > 255u ? 255u : run_length;

      encoded[write_index++] = (unsigned char)chunk_length;
      memcpy(encoded + write_index, group, info->bytes_per_pixel);
      write_index += info->bytes_per_pixel;
      read_index += chunk_length;
      run_length -= chunk_length;
    }
  }

  encoded[write_index++] = 0x00u;
  free(raw_buffer);

  *out_buffer = encoded;
  *out_size = write_index;
  return 1;
}
