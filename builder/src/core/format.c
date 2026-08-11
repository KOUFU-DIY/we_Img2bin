#include "format.h"

#include "util.h"

static const img2bin_format_info_t IMG2BIN_FORMAT_INFOS[IMG2BIN_FMT_COUNT] = {
  { IMG2BIN_FMT_ARGB8888, "argb8888", "ARGB8888", "ARGB8888", 4, 1, 0, 1, 32, 0 },
  { IMG2BIN_FMT_ARGB6666, "argb6666", "ARGB6666", "ARGB6666", 3, 1, 0, 1, 24, 0 },
  { IMG2BIN_FMT_ARGB4444, "argb4444", "ARGB4444", "ARGB4444", 2, 1, 0, 1, 16, 0 },
  { IMG2BIN_FMT_ARGB2222, "argb2222", "ARGB2222", "ARGB2222", 1, 1, 0, 0, 8, 0 },
  { IMG2BIN_FMT_ARGB8565, "argb8565", "ARGB8565", "ARGB8565", 3, 1, 0, 1, 24, 0 },
  { IMG2BIN_FMT_RGB888, "rgb888", "RGB888", "RGB888", 3, 0, 1, 1, 24, 0 },
  { IMG2BIN_FMT_RGB565, "rgb565", "RGB565", "RGB565", 2, 0, 1, 1, 16, 0 },
  { IMG2BIN_FMT_RGB332, "rgb332", "RGB332", "RGB332", 1, 0, 1, 0, 8, 0 },
  { IMG2BIN_FMT_RAGB5155, "ragb5155", "RAGB5155", "RAGB5155", 2, 1, 0, 1, 16, 0 },
  { IMG2BIN_FMT_A8, "a8", "A8 透明度蒙版", "A8 Alpha Mask", 1, 1, 0, 0, 8, 1 },
  { IMG2BIN_FMT_A4, "a4", "A4 透明度蒙版", "A4 Alpha Mask", 0, 1, 0, 0, 4, 1 },
  { IMG2BIN_FMT_A2, "a2", "A2 透明度蒙版", "A2 Alpha Mask", 0, 1, 0, 0, 2, 1 },
  { IMG2BIN_FMT_A1, "a1", "A1 透明度蒙版", "A1 Alpha Mask", 0, 1, 0, 0, 1, 1 }
};

const img2bin_format_info_t *img2bin_get_format_info(img2bin_pixel_format_t id)
{
  if (id < 0 || id >= IMG2BIN_FMT_COUNT) {
    return NULL;
  }

  return &IMG2BIN_FORMAT_INFOS[(int)id];
}

const img2bin_format_info_t *img2bin_get_format_infos(size_t *count)
{
  if (count != NULL) {
    *count = IMG2BIN_FMT_COUNT;
  }

  return IMG2BIN_FORMAT_INFOS;
}

int img2bin_parse_format_name(const char *name, img2bin_pixel_format_t *out_format)
{
  size_t index;

  if (name == NULL || out_format == NULL) {
    return 0;
  }

  for (index = 0; index < IMG2BIN_FMT_COUNT; ++index) {
    if (img2bin_stricmp(name, IMG2BIN_FORMAT_INFOS[index].name) == 0) {
      *out_format = IMG2BIN_FORMAT_INFOS[index].id;
      return 1;
    }
  }

  return 0;
}

/* 像素格式 -> 通用头低 nibble。0x2/0x3 属于旧枚举的 RGB555/RGB444（本工具无），
   0xF 保留给 OLED 点阵；0xB~0xE 分配给 Alpha 蒙版家族 A8/A4/A2/A1。
   此表是协议常量，与枚举顺序无关，改动需同步参考解码器与 docs/user/README-schema.md。 */
int img2bin_get_format_header_nibble(img2bin_pixel_format_t format)
{
  switch (format) {
    case IMG2BIN_FMT_RGB565: return 0x0;
    case IMG2BIN_FMT_RGB888: return 0x1;
    case IMG2BIN_FMT_RGB332: return 0x4;
    case IMG2BIN_FMT_ARGB8888: return 0x5;
    case IMG2BIN_FMT_ARGB6666: return 0x6;
    case IMG2BIN_FMT_ARGB4444: return 0x7;
    case IMG2BIN_FMT_ARGB8565: return 0x8;
    case IMG2BIN_FMT_ARGB2222: return 0x9;
    case IMG2BIN_FMT_RAGB5155: return 0xA;
    case IMG2BIN_FMT_A8: return 0xB;
    case IMG2BIN_FMT_A4: return 0xC;
    case IMG2BIN_FMT_A2: return 0xD;
    case IMG2BIN_FMT_A1: return 0xE;
    default: return -1;
  }
}

size_t img2bin_format_row_stride(img2bin_pixel_format_t format, unsigned int width)
{
  const img2bin_format_info_t *info = img2bin_get_format_info(format);

  if (info == NULL || width == 0u) {
    return 0;
  }
  if ((size_t)width > (SIZE_MAX - 7u) / info->bits_per_pixel) {
    return 0;
  }

  return ((size_t)width * info->bits_per_pixel + 7u) / 8u;
}

size_t img2bin_format_payload_size(img2bin_pixel_format_t format, unsigned int width, unsigned int height)
{
  size_t row_stride = img2bin_format_row_stride(format, width);

  if (row_stride == 0 || height == 0u) {
    return 0;
  }
  if ((size_t)height > SIZE_MAX / row_stride) {
    return 0;
  }

  return (size_t)height * row_stride;
}

int img2bin_build_resource_header(
  unsigned int algorithm_nibble,
  img2bin_pixel_format_t format,
  unsigned int width,
  unsigned int height,
  unsigned char *out_header)
{
  int format_nibble = img2bin_get_format_header_nibble(format);

  if (out_header == NULL || format_nibble < 0 || algorithm_nibble > 0x0Fu ||
      width == 0u || height == 0u || width > 0xFFFFu || height > 0xFFFFu) {
    return 0;
  }

  out_header[0] = (unsigned char)IMG2BIN_RESOURCE_TYPE_IMAGE;
  out_header[1] = (unsigned char)((algorithm_nibble << 4) | (unsigned int)format_nibble);
  out_header[2] = (unsigned char)((width >> 8) & 0xFFu);
  out_header[3] = (unsigned char)(width & 0xFFu);
  out_header[4] = (unsigned char)((height >> 8) & 0xFFu);
  out_header[5] = (unsigned char)(height & 0xFFu);
  return 1;
}
