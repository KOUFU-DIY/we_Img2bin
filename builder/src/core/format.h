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
  IMG2BIN_FMT_A8,
  IMG2BIN_FMT_A4,
  IMG2BIN_FMT_A2,
  IMG2BIN_FMT_A1,
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

/* bytes_per_pixel 仅对整字节格式有意义；亚字节的 Alpha 蒙版格式（a4/a2/a1）为 0，
   一切大小计算须走 bits_per_pixel + 行打包（img2bin_format_row_stride）。 */
typedef struct img2bin_format_info_s {
  img2bin_pixel_format_t id;
  const char *name;
  const char *display_name_zh_cn;
  const char *display_name_en;
  size_t bytes_per_pixel;
  int stores_alpha;
  int uses_background_color;
  int endianness_affects_output;
  size_t bits_per_pixel;
  int is_alpha_only;
} img2bin_format_info_t;

const img2bin_format_info_t *img2bin_get_format_info(img2bin_pixel_format_t id);
const img2bin_format_info_t *img2bin_get_format_infos(size_t *count);
int img2bin_parse_format_name(const char *name, img2bin_pixel_format_t *out_format);

/* Alpha 蒙版行打包：每行字节对齐，行字节数 = (宽 × bits_per_pixel + 7) / 8。
   对整字节格式同样成立（等于 宽 × bytes_per_pixel）。返回 0 表示格式非法。 */
size_t img2bin_format_row_stride(img2bin_pixel_format_t format, unsigned int width);
/* payload 总字节数 = 高 × 行字节数；溢出或参数非法返回 0。 */
size_t img2bin_format_payload_size(img2bin_pixel_format_t format, unsigned int width, unsigned int height);

/* 6 字节通用资源头：类型(0x00=图片) + 格式码(高nibble算法/低nibble像素格式) + 宽/高(恒大端)。 */
#define IMG2BIN_RESOURCE_HEADER_SIZE 6u
#define IMG2BIN_RESOURCE_TYPE_IMAGE 0x00u

#define IMG2BIN_HEADER_ALGO_RAW 0x0u
#define IMG2BIN_HEADER_ALGO_RLE 0x1u
#define IMG2BIN_HEADER_ALGO_IMPRLE 0x2u
#define IMG2BIN_HEADER_ALGO_QOI 0x3u
#define IMG2BIN_HEADER_ALGO_INDEXQOI 0x4u
#define IMG2BIN_HEADER_ALGO_QOIF 0x5u
#define IMG2BIN_HEADER_ALGO_INDEXQOIMASK 0x6u

int img2bin_get_format_header_nibble(img2bin_pixel_format_t format);
int img2bin_build_resource_header(
  unsigned int algorithm_nibble,
  img2bin_pixel_format_t format,
  unsigned int width,
  unsigned int height,
  unsigned char *out_header);

#endif
