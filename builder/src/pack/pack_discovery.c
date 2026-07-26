#include "pack_discovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pack_json.h"
#include "pack_process.h"
#include "pack_util.h"

static int img2bin_pack_is_tool_candidate(const char *file_name)
{
  if (!img2bin_pack_starts_with_ci(file_name, "img2bin_")) {
    return 0;
  }
  /* Never probe ourselves or the test runner: both would spawn further
     processes when executed, which turns discovery into a fork bomb. */
  if (img2bin_pack_starts_with_ci(file_name, "img2bin_pack")) {
    return 0;
  }
  if (img2bin_pack_starts_with_ci(file_name, "img2bin_tests")) {
    return 0;
  }
#ifdef _WIN32
  return img2bin_pack_ends_with_ci(file_name, ".exe");
#else
  return strchr(file_name, '.') == NULL;
#endif
}

static int img2bin_pack_probe_tool(const char *exe_path, const char *file_name, img2bin_pack_tool_t *out_tool)
{
  const char *arguments[2];
  char *captured_output = NULL;
  char error[256];
  int exit_code = -1;
  img2bin_pack_json_value_t *info = NULL;
  const char *tool_id = NULL;
  const char *algorithm_code = NULL;
  int ok = 0;

  arguments[0] = exe_path;
  arguments[1] = "--info";

  if (!img2bin_pack_run_command(arguments, 2, &captured_output, &exit_code, error, sizeof(error))) {
    return 0;
  }
  if (exit_code != 0 || captured_output == NULL) {
    free(captured_output);
    return 0;
  }

  info = img2bin_pack_json_parse(captured_output, error, sizeof(error));
  free(captured_output);
  if (info == NULL) {
    return 0;
  }

  tool_id = img2bin_pack_json_string_at(info, "tool.id", NULL);
  algorithm_code = img2bin_pack_json_string_at(info, "algorithm.algorithm_code", NULL);
  if (tool_id != NULL && tool_id[0] != '\0' && algorithm_code != NULL && algorithm_code[0] != '\0') {
    img2bin_pack_copy_string(out_tool->exe_path, sizeof(out_tool->exe_path), exe_path);
    img2bin_pack_copy_string(out_tool->file_name, sizeof(out_tool->file_name), file_name);
    img2bin_pack_copy_string(out_tool->tool_id, sizeof(out_tool->tool_id), tool_id);
    img2bin_pack_copy_string(out_tool->algorithm_code, sizeof(out_tool->algorithm_code), algorithm_code);
    img2bin_pack_lower_string(out_tool->algorithm_code);
    out_tool->supports_index_interval = img2bin_pack_json_bool_at(info, "capabilities.supports_index_interval", 0);
    ok = 1;
  }

  img2bin_pack_json_free(info);
  return ok;
}

int img2bin_pack_discover_tools(
  const char *tools_directory,
  img2bin_pack_tool_list_t *out_tools,
  char *error_buffer,
  size_t error_buffer_size)
{
  img2bin_string_list_t names;
  char exe_path[IMG2BIN_PATH_CAPACITY];
  size_t index = 0;

  if (tools_directory == NULL || out_tools == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Tool discovery request is invalid.");
    return 0;
  }

  out_tools->count = 0;
  memset(&names, 0, sizeof(names));

  if (!img2bin_is_directory(tools_directory)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Tools directory does not exist: %s", tools_directory);
    return 0;
  }

  if (!img2bin_pack_list_directory(tools_directory, 1, 0, &names, error_buffer, error_buffer_size)) {
    return 0;
  }

  for (index = 0; index < names.count && out_tools->count < IMG2BIN_PACK_MAX_TOOLS; ++index) {
    if (!img2bin_pack_is_tool_candidate(names.items[index])) {
      continue;
    }
    if (!img2bin_path_join(tools_directory, names.items[index], exe_path, sizeof(exe_path))) {
      continue;
    }
    if (img2bin_pack_probe_tool(exe_path, names.items[index], &out_tools->items[out_tools->count])) {
      ++out_tools->count;
    } else {
      fprintf(stderr, "Warning: %s did not answer --info and was skipped.\n", exe_path);
    }
  }

  img2bin_string_list_free(&names);
  return 1;
}

const img2bin_pack_tool_t *img2bin_pack_find_tool(const img2bin_pack_tool_list_t *tools, const char *id_or_algorithm)
{
  size_t index = 0;

  if (tools == NULL || id_or_algorithm == NULL || id_or_algorithm[0] == '\0') {
    return NULL;
  }

  for (index = 0; index < tools->count; ++index) {
    if (img2bin_stricmp(tools->items[index].algorithm_code, id_or_algorithm) == 0 ||
        img2bin_stricmp(tools->items[index].tool_id, id_or_algorithm) == 0) {
      return &tools->items[index];
    }
  }
  return NULL;
}
