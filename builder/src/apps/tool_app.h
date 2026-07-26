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
