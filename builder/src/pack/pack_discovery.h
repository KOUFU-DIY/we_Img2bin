#ifndef IMG2BIN_PACK_DISCOVERY_H
#define IMG2BIN_PACK_DISCOVERY_H

#include <stddef.h>

#include "filesystem.h"

#define IMG2BIN_PACK_MAX_TOOLS 32

typedef struct img2bin_pack_tool_s {
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char file_name[128];
  char tool_id[64];
  char algorithm_code[32];
  int supports_index_interval;
} img2bin_pack_tool_t;

typedef struct img2bin_pack_tool_list_s {
  img2bin_pack_tool_t items[IMG2BIN_PACK_MAX_TOOLS];
  size_t count;
} img2bin_pack_tool_list_t;

int img2bin_pack_discover_tools(
  const char *tools_directory,
  img2bin_pack_tool_list_t *out_tools,
  char *error_buffer,
  size_t error_buffer_size);

const img2bin_pack_tool_t *img2bin_pack_find_tool(const img2bin_pack_tool_list_t *tools, const char *id_or_algorithm);

#endif
