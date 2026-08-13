#ifndef IMG2BIN_TOOL_APP_H
#define IMG2BIN_TOOL_APP_H

#include <stddef.h>

#include "cli.h"
#include "format.h"
#include "image_io.h"

typedef int (*img2bin_tool_encode_fn)(
  img2bin_pixel_format_t format,
  img2bin_endianness_t endianness,
  img2bin_rgb_t background,
  const img2bin_image_t *image,
  const img2bin_cli_options_t *options,
  unsigned char **out_buffer,
  size_t *out_size,
  char *error_buffer,
  size_t error_buffer_size);

typedef struct img2bin_tool_descriptor_s {
  const char *tool_id;
  const char *exe_name;
  const char *help_summary_en;
  const char *display_name_zh_cn;
  const char *display_name_en;
  const char *description_zh_cn;
  const char *description_en;
  const char *gui_category_zh_cn;
  const char *gui_category_en;
  int priority;
  const char *algorithm_id;
  const char *algorithm_code;
  const char *compression;
  const char *output_token;
  const char *manifest_file_name;
  int supports_index_interval;
  /* --quantize-bits（Alpha 量化位数 5..8）只在 indexQOI_MASK 工具开放。 */
  int supports_quantize_bits;
  /* Alpha 蒙版家族（a8/a4/a2/a1）只在 raw 工具全量开放；为 0 的工具在 CLI、
     --info、--list-formats 层面一致地不提供这些格式。 */
  int supports_alpha_only_formats;
  /* 置 1 的工具只提供/只接受 a8 蒙版格式（indexQOI_MASK 专用）：默认格式
     变为 a8，其余格式显式点名报 CLI 错误、--formats all 静默滤除。 */
  int requires_alpha8_format;
  unsigned int header_algorithm_nibble;
  img2bin_tool_encode_fn encode_image;
} img2bin_tool_descriptor_t;

int img2bin_tool_get_info_json(const img2bin_tool_descriptor_t *tool, char *buffer, size_t buffer_size);
int img2bin_tool_run_with_executable_path(
  const img2bin_tool_descriptor_t *tool,
  int argc,
  const char *const *argv,
  const char *executable_path_override);
int img2bin_tool_run(const img2bin_tool_descriptor_t *tool, int argc, const char *const *argv);

#endif
