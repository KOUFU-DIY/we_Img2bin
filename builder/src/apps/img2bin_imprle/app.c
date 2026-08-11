#include "app.h"

#include "imprle_encoder.h"
#include "tool_app.h"

static int img2bin_imprle_encode_adapter(
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
  return img2bin_encode_imprle_image(format, endianness, background, image, out_buffer, out_size, error_buffer, error_buffer_size);
}

static const img2bin_tool_descriptor_t g_img2bin_imprle_tool = {
  "img2bin_imprle",
  "img2bin_imprle",
  "improved rle image converter",
  "改进RLE取模",
  "Improved RLE Image Converter",
  "输出多种改进RLE压缩像素格式 bin 文件。",
  "Export bin files in multiple improved RLE compressed pixel formats.",
  "RLE压缩",
  "RLE Compression",
  20,
  "imprle",
  "imprle",
  "improved_rle",
  "imprle",
  "img2bin_imprle-manifest.json",
  0,
  0,
  IMG2BIN_HEADER_ALGO_IMPRLE,
  img2bin_imprle_encode_adapter
};

int img2bin_imprle_get_info_json(char *buffer, size_t buffer_size)
{
  return img2bin_tool_get_info_json(&g_img2bin_imprle_tool, buffer, buffer_size);
}

int img2bin_imprle_run_with_executable_path(int argc, const char *const *argv, const char *executable_path_override)
{
  return img2bin_tool_run_with_executable_path(&g_img2bin_imprle_tool, argc, argv, executable_path_override);
}

int img2bin_imprle_run(int argc, const char *const *argv)
{
  return img2bin_tool_run(&g_img2bin_imprle_tool, argc, argv);
}
