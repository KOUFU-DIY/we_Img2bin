#include "app.h"

#include "rle_encoder.h"
#include "tool_app.h"

static int img2bin_rle_encode_adapter(
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
  return img2bin_encode_rle_image(format, endianness, background, image, out_buffer, out_size, error_buffer, error_buffer_size);
}

static const img2bin_tool_descriptor_t g_img2bin_rle_tool = {
  "img2bin_rle",
  "img2bin_rle",
  "original rle image converter",
  "原始RLE取模",
  "Original RLE Image Converter",
  "输出多种原始RLE压缩像素格式 bin 文件。",
  "Export bin files in multiple original RLE compressed pixel formats.",
  "RLE压缩",
  "RLE Compression",
  30,
  "rle",
  "rle",
  "rle",
  "rle",
  "img2bin_rle-manifest.json",
  0,
  IMG2BIN_HEADER_ALGO_RLE,
  img2bin_rle_encode_adapter
};

int img2bin_rle_get_info_json(char *buffer, size_t buffer_size)
{
  return img2bin_tool_get_info_json(&g_img2bin_rle_tool, buffer, buffer_size);
}

int img2bin_rle_run_with_executable_path(int argc, const char *const *argv, const char *executable_path_override)
{
  return img2bin_tool_run_with_executable_path(&g_img2bin_rle_tool, argc, argv, executable_path_override);
}

int img2bin_rle_run(int argc, const char *const *argv)
{
  return img2bin_tool_run(&g_img2bin_rle_tool, argc, argv);
}
