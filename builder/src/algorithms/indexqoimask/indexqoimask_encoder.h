#ifndef IMG2BIN_INDEXQOIMASK_ENCODER_H
#define IMG2BIN_INDEXQOIMASK_ENCODER_H

#include <stddef.h>

#include "cli.h"
#include "format.h"
#include "image_io.h"

/* 索引QOI_MASK（A8 透明蒙版专用）编码：返回纯 payload（不含 6 字节通用资源头）。
   仅支持 IMG2BIN_FMT_A8；quantize_bits 取 5..8，传 0 使用默认档位 6。
   endianness/background 不参与编码（payload 内多字节字段恒大端、只存 Alpha），
   仅为与工具骨架的 encode 适配签名保持一致。 */
int img2bin_encode_indexqoimask_image(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  unsigned int quantize_bits,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size);

#endif
