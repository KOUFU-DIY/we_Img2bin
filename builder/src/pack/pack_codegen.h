#ifndef IMG2BIN_PACK_CODEGEN_H
#define IMG2BIN_PACK_CODEGEN_H

#include <stddef.h>

#include "util.h"

typedef struct img2bin_pack_bin_info_s {
  char stem[256];
  char format_name[32];
  char algorithm[32];
  char endianness[4];
  unsigned int width;
  unsigned int height;
} img2bin_pack_bin_info_t;

typedef struct img2bin_pack_codegen_options_s {
  int split;
  const char *base_name;
  size_t bytes_per_line;
} img2bin_pack_codegen_options_t;

int img2bin_pack_parse_bin_name(const char *file_name, img2bin_pack_bin_info_t *out_info);
int img2bin_pack_sanitize_symbol(const char *name, char *buffer, size_t buffer_size);
void img2bin_pack_codegen_options_init(img2bin_pack_codegen_options_t *options);

int img2bin_pack_generate_sources(
  const img2bin_string_list_t *bin_paths,
  const char *output_directory,
  const img2bin_pack_codegen_options_t *options,
  img2bin_string_list_t *out_generated_files,
  char *error_buffer,
  size_t error_buffer_size);

#endif
