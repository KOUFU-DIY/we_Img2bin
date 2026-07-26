#ifndef IMG2BIN_FORMAT_H
#define IMG2BIN_FORMAT_H

#include <stddef.h>
#include <stdint.h>

typedef enum img2bin_endianness_e {
  IMG2BIN_ENDIAN_BIG = 0,
  IMG2BIN_ENDIAN_LITTLE = 1
} img2bin_endianness_t;

typedef enum img2bin_pixel_format_e {
  IMG2BIN_FMT_ARGB8888 = 0,
  IMG2BIN_FMT_ARGB6666,
  IMG2BIN_FMT_ARGB4444,
  IMG2BIN_FMT_ARGB2222,
  IMG2BIN_FMT_ARGB8565,
  IMG2BIN_FMT_RGB888,
  IMG2BIN_FMT_RGB565,
  IMG2BIN_FMT_RGB332,
  IMG2BIN_FMT_RAGB5155,
  IMG2BIN_FMT_COUNT
} img2bin_pixel_format_t;

typedef struct img2bin_rgba_s {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} img2bin_rgba_t;

typedef struct img2bin_rgb_s {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} img2bin_rgb_t;

typedef struct img2bin_format_info_s {
  img2bin_pixel_format_t id;
  const char *name;
  const char *display_name_zh_cn;
  const char *display_name_en;
  size_t bytes_per_pixel;
  int stores_alpha;
  int uses_background_color;
  int endianness_affects_output;
} img2bin_format_info_t;

const img2bin_format_info_t *img2bin_get_format_info(img2bin_pixel_format_t id);
const img2bin_format_info_t *img2bin_get_format_infos(size_t *count);
int img2bin_parse_format_name(const char *name, img2bin_pixel_format_t *out_format);

#endif
