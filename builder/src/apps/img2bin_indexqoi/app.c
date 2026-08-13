#include "app.h"

#include "qoi_encoder.h"
#include "tool_app.h"

static int img2bin_indexqoi_encode_adapter(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  const img2bin_cli_options_t *options,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size)
{
  unsigned int index_interval = 0u;

  if (options != NULL && options->index_interval_specified) {
    index_interval = options->index_interval;
  }

  return img2bin_encode_indexqoi_image(
    format,
    endianness,
    background,
    image,
    index_interval,
    out_buffer,
    out_size,
    error_buffer,
    error_buffer_size);
}

static const img2bin_tool_descriptor_t g_img2bin_indexqoi_tool = {
  "img2bin_indexqoi",
  "img2bin_indexqoi",
  "indexed qoi image converter",
  "索引QOI取模",
  "Indexed QOI Image Converter",
  "输出带像素索引表与静态调色盘的索引QOI V2压缩像素格式 bin 文件。",
  "Export bin files in indexed QOI V2 with a static palette and jump index.",
  "QOI压缩",
  "QOI Compression",
  60,
  "indexqoi",
  "indexqoi",
  "indexed_qoi",
  "indexqoi",
  "img2bin_indexqoi-manifest.json",
  1,
  0,
  0,
  0,
  IMG2BIN_HEADER_ALGO_INDEXQOI,
  img2bin_indexqoi_encode_adapter
};

int img2bin_indexqoi_get_info_json(char *buffer, size_t buffer_size)
{
  return img2bin_tool_get_info_json(&g_img2bin_indexqoi_tool, buffer, buffer_size);
}

int img2bin_indexqoi_run_with_executable_path(int argc, const char *const *argv, const char *executable_path_override)
{
  return img2bin_tool_run_with_executable_path(&g_img2bin_indexqoi_tool, argc, argv, executable_path_override);
}

int img2bin_indexqoi_run(int argc, const char *const *argv)
{
  return img2bin_tool_run(&g_img2bin_indexqoi_tool, argc, argv);
}
