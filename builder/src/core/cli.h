#ifndef IMG2BIN_CLI_H
#define IMG2BIN_CLI_H

#include <stddef.h>

#include "format.h"

#define IMG2BIN_CLI_MAX_POSITIONAL_INPUTS 1024

typedef struct img2bin_cli_options_s {
  const char *input_path;
  const char *output_path;
  img2bin_endianness_t endianness;
  img2bin_rgb_t background;
  unsigned int index_interval;
  int show_help;
  int show_info;
  int list_formats;
  int index_interval_specified;
  /* 默认 0：不写 manifest 日志；--manifest 置 1 后所有运行形态都会
     在输出目录写 img2bin_<工具>-manifest.json。 */
  int write_manifest;
  /* --formats all 置 1：格式清单是"全部"语义，工具骨架据此把本工具
     不支持的格式静默过滤掉；显式点名的格式不支持时则报 CLI 错误。 */
  int formats_all;
  size_t format_count;
  size_t positional_input_count;
  const char *positional_inputs[IMG2BIN_CLI_MAX_POSITIONAL_INPUTS];
  img2bin_pixel_format_t formats[IMG2BIN_FMT_COUNT];
} img2bin_cli_options_t;

void img2bin_cli_init(img2bin_cli_options_t *options);
int img2bin_parse_cli(int argc, const char *const *argv, img2bin_cli_options_t *options, char *error_buffer, size_t error_buffer_size);
void img2bin_print_help(void);
void img2bin_print_formats(void);

#endif
