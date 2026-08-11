#include "app.h"

#include "raw_encoder.h"
#include "tool_app.h"

static int img2bin_raw_encode_adapter(
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
  (void)options;
  return img2bin_encode_raw_image(format, endianness, background, image, out_buffer, out_size, error_buffer, error_buffer_size);
}

static const img2bin_tool_descriptor_t g_img2bin_raw_tool = {
  "img2bin_raw",
  "img2bin_raw",
  "raw pixel format image converter",
  "无压缩取模",
  "Raw Image Converter",
  "输出多种无压缩像素格式 bin 文件。",
  "Export bin files in multiple uncompressed pixel formats.",
  "无压缩",
  "Uncompressed",
  10,
  "raw",
  "raw",
  "none",
  "raw",
  "img2bin_raw-manifest.json",
  0,
  1,
  IMG2BIN_HEADER_ALGO_RAW,
  img2bin_raw_encode_adapter
};

int img2bin_raw_get_info_json(char *buffer, size_t buffer_size)
{
  return img2bin_tool_get_info_json(&g_img2bin_raw_tool, buffer, buffer_size);
}

int img2bin_raw_run_with_executable_path(int argc, const char *const *argv, const char *executable_path_override)
{
  return img2bin_tool_run_with_executable_path(&g_img2bin_raw_tool, argc, argv, executable_path_override);
}

int img2bin_raw_run(int argc, const char *const *argv)
{
  return img2bin_tool_run(&g_img2bin_raw_tool, argc, argv);
}
