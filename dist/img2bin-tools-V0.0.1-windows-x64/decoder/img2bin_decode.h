/*
 * img2bin 参考解码器 (reference decoder)
 *
 * 独立文件，不依赖本项目其他代码，也不使用动态内存，可直接拷贝到
 * 下位机工程使用。所有解码函数把压缩流还原为 RAW 打包像素字节流，
 * 输出与 img2bin_raw.exe 在同格式、同字节序下的输出逐字节一致。
 *
 * 宽高、像素格式、字节序需要由外部提供（文件名或 manifest），
 * 只有 indexQOI 自带 13 字节头。协议细节见 docs/user/README-decoder.md。
 */
#ifndef IMG2BIN_DECODE_H
#define IMG2BIN_DECODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum img2bin_decode_format_e {
  IMG2BIN_DECODE_FMT_ARGB8888 = 0,
  IMG2BIN_DECODE_FMT_ARGB6666,
  IMG2BIN_DECODE_FMT_ARGB4444,
  IMG2BIN_DECODE_FMT_ARGB2222,
  IMG2BIN_DECODE_FMT_ARGB8565,
  IMG2BIN_DECODE_FMT_RGB888,
  IMG2BIN_DECODE_FMT_RGB565,
  IMG2BIN_DECODE_FMT_RGB332,
  IMG2BIN_DECODE_FMT_RAGB5155,
  IMG2BIN_DECODE_FMT_COUNT
} img2bin_decode_format_t;

typedef enum img2bin_decode_endianness_e {
  IMG2BIN_DECODE_BIG_ENDIAN = 0,
  IMG2BIN_DECODE_LITTLE_ENDIAN = 1
} img2bin_decode_endianness_t;

typedef enum img2bin_decode_status_e {
  IMG2BIN_DECODE_OK = 0,
  IMG2BIN_DECODE_ERR_ARGUMENTS = 1,
  IMG2BIN_DECODE_ERR_TRUNCATED = 2,
  IMG2BIN_DECODE_ERR_CORRUPT = 3,
  IMG2BIN_DECODE_ERR_OUTPUT_TOO_SMALL = 4,
  IMG2BIN_DECODE_ERR_TRAILING_DATA = 5
} img2bin_decode_status_t;

typedef struct img2bin_indexqoi_header_s {
  uint16_t width;
  uint16_t height;
  uint16_t index_interval;
  uint16_t u16_bytes;
  uint16_t u24_bytes;
  uint16_t u32_bytes;
  size_t slot_count;
  size_t payload_offset;
} img2bin_indexqoi_header_t;

size_t img2bin_decode_bytes_per_pixel(img2bin_decode_format_t format);

img2bin_decode_status_t img2bin_decode_raw(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_rle(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_imprle(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_qoi(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_qoif(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_indexqoi_header(
  const uint8_t *input,
  size_t input_size,
  img2bin_indexqoi_header_t *out_header);

img2bin_decode_status_t img2bin_decode_indexqoi_offset(
  const uint8_t *input,
  size_t input_size,
  size_t slot,
  uint32_t *out_offset);

img2bin_decode_status_t img2bin_decode_indexqoi(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

/* 从第 slot 个索引点开始解码到图片末尾。首像素位置 = slot * index_interval。 */
img2bin_decode_status_t img2bin_decode_indexqoi_from_slot(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t slot,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

#ifdef __cplusplus
}
#endif

#endif
