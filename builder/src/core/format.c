#include "format.h"

#include "util.h"

static const img2bin_format_info_t IMG2BIN_FORMAT_INFOS[IMG2BIN_FMT_COUNT] = {
  { IMG2BIN_FMT_ARGB8888, "argb8888", "ARGB8888", "ARGB8888", 4, 1, 0, 1 },
  { IMG2BIN_FMT_ARGB6666, "argb6666", "ARGB6666", "ARGB6666", 3, 1, 0, 1 },
  { IMG2BIN_FMT_ARGB4444, "argb4444", "ARGB4444", "ARGB4444", 2, 1, 0, 1 },
  { IMG2BIN_FMT_ARGB2222, "argb2222", "ARGB2222", "ARGB2222", 1, 1, 0, 0 },
  { IMG2BIN_FMT_ARGB8565, "argb8565", "ARGB8565", "ARGB8565", 3, 1, 0, 1 },
  { IMG2BIN_FMT_RGB888, "rgb888", "RGB888", "RGB888", 3, 0, 1, 1 },
  { IMG2BIN_FMT_RGB565, "rgb565", "RGB565", "RGB565", 2, 0, 1, 1 },
  { IMG2BIN_FMT_RGB332, "rgb332", "RGB332", "RGB332", 1, 0, 1, 0 },
  { IMG2BIN_FMT_RAGB5155, "ragb5155", "RAGB5155", "RAGB5155", 2, 1, 0, 1 }
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
