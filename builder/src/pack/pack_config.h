#ifndef IMG2BIN_PACK_CONFIG_H
#define IMG2BIN_PACK_CONFIG_H

#include <stddef.h>

#include "format.h"

#define IMG2BIN_PACK_MAX_FOLDER_RULES 64
#define IMG2BIN_PACK_PATH_SIZE 1024

typedef struct img2bin_pack_folder_rule_s {
  char folder_name[256];
  char tool[64];
  char formats[256];
  int endianness;
  char bg_color[16];
  long index_interval;
  char output[IMG2BIN_PACK_PATH_SIZE];
} img2bin_pack_folder_rule_t;

typedef struct img2bin_pack_config_s {
  char root[IMG2BIN_PACK_PATH_SIZE];
  char output[IMG2BIN_PACK_PATH_SIZE];
  char tools_dir[IMG2BIN_PACK_PATH_SIZE];
  char formats[256];
  int endianness;
  char bg_color[16];
  long index_interval;
  int codegen_enabled;
  int codegen_split;
  char codegen_base_name[128];
  size_t folder_rule_count;
  img2bin_pack_folder_rule_t folder_rules[IMG2BIN_PACK_MAX_FOLDER_RULES];
} img2bin_pack_config_t;

void img2bin_pack_config_init(img2bin_pack_config_t *config);
int img2bin_pack_config_load_file(
  img2bin_pack_config_t *config,
  const char *path,
  char *error_buffer,
  size_t error_buffer_size);
img2bin_pack_folder_rule_t *img2bin_pack_config_find_rule(img2bin_pack_config_t *config, const char *folder_name);

#endif
