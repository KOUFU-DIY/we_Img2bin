#include "app.h"

#include "qoi_encoder.h"
#include "tool_app.h"

static int img2bin_qoi_encode_adapter(
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
  return img2bin_encode_qoi_image(format, endianness, background, image, out_buffer, out_size, error_buffer, error_buffer_size);
}

static const img2bin_tool_descriptor_t g_img2bin_qoi_tool = {
  "img2bin_qoi",
  "img2bin_qoi",
  "original qoi image converter",
  "原始QOI取模",
  "Original QOI Image Converter",
  "输出多种原始QOI压缩像素格式 bin 文件。",
  "Export bin files in multiple original QOI compressed pixel formats.",
  "QOI压缩",
  "QOI Compression",
  40,
  "qoi",
  "qoi",
  "qoi",
  "qoi",
  "img2bin_qoi-manifest.json",
  0,
  img2bin_qoi_encode_adapter
};

int img2bin_qoi_get_info_json(char *buffer, size_t buffer_size)
{
  return img2bin_tool_get_info_json(&g_img2bin_qoi_tool, buffer, buffer_size);
}

int img2bin_qoi_run_with_executable_path(int argc, const char *const *argv, const char *executable_path_override)
{
  return img2bin_tool_run_with_executable_path(&g_img2bin_qoi_tool, argc, argv, executable_path_override);
}

int img2bin_qoi_run(int argc, const char *const *argv)
{
  return img2bin_tool_run(&g_img2bin_qoi_tool, argc, argv);
}
