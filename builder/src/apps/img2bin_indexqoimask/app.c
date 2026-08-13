#include "app.h"

#include "indexqoimask_encoder.h"
#include "tool_app.h"

static int img2bin_indexqoimask_encode_adapter(
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
  unsigned int quantize_bits = 0u;

  if (options != NULL && options->quantize_bits_specified) {
    quantize_bits = options->quantize_bits;
  }

  return img2bin_encode_indexqoimask_image(
    format,
    endianness,
    background,
    image,
    quantize_bits,
    out_buffer,
    out_size,
    error_buffer,
    error_buffer_size);
}

static const img2bin_tool_descriptor_t g_img2bin_indexqoimask_tool = {
  "img2bin_indexqoimask",
  "img2bin_indexqoimask",
  "indexed qoi alpha mask converter",
  "索引QOI蒙版取模",
  "Indexed QOI Mask Converter",
  "输出按行随机访问的索引QOI_MASK压缩 a8 透明蒙版 bin 文件，支持 5/6/7/8 bit 量化。",
  "Export a8 alpha masks as row-addressable indexed QOI mask bin files with 5/6/7/8-bit quantization.",
  "QOI压缩",
  "QOI Compression",
  70,
  "indexqoimask",
  "indexqoimask",
  "indexed_qoi_mask",
  "indexqoimask",
  "img2bin_indexqoimask-manifest.json",
  0,
  1,
  1,
  1,
  IMG2BIN_HEADER_ALGO_INDEXQOIMASK,
  img2bin_indexqoimask_encode_adapter
};

int img2bin_indexqoimask_get_info_json(char *buffer, size_t buffer_size)
{
  return img2bin_tool_get_info_json(&g_img2bin_indexqoimask_tool, buffer, buffer_size);
}

int img2bin_indexqoimask_run_with_executable_path(int argc, const char *const *argv, const char *executable_path_override)
{
  return img2bin_tool_run_with_executable_path(&g_img2bin_indexqoimask_tool, argc, argv, executable_path_override);
}

int img2bin_indexqoimask_run(int argc, const char *const *argv)
{
  return img2bin_tool_run(&g_img2bin_indexqoimask_tool, argc, argv);
}
