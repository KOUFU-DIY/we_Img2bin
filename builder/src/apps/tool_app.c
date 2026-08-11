#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "filesystem.h"
#include "format.h"
#include "image_io.h"
#include "tool_app.h"
#include "util.h"
#include "version.h"

typedef enum img2bin_app_exit_code_e {
  IMG2BIN_APP_EXIT_SUCCESS = 0,
  IMG2BIN_APP_EXIT_CLI_ERROR = 1,
  IMG2BIN_APP_EXIT_INPUT_ERROR = 2,
  IMG2BIN_APP_EXIT_ENCODE_ERROR = 3,
  IMG2BIN_APP_EXIT_WRITE_ERROR = 4,
  IMG2BIN_APP_EXIT_INTERNAL_ERROR = 5,
  IMG2BIN_APP_EXIT_BATCH_PARTIAL_FAILURE = 6
} img2bin_app_exit_code_t;

typedef struct img2bin_runtime_error_s {
  int exit_code;
  int has_file;
  char code[64];
  char stage[32];
  char file_path[IMG2BIN_PATH_CAPACITY];
  char message_zh_cn[256];
  char message_en[256];
  char detail[512];
} img2bin_runtime_error_t;

static void img2bin_runtime_error_init(img2bin_runtime_error_t *error)
{
  if (error == NULL) {
    return;
  }

  memset(error, 0, sizeof(*error));
  error->exit_code = IMG2BIN_APP_EXIT_INTERNAL_ERROR;
}

static void img2bin_runtime_error_set(
  img2bin_runtime_error_t *error,
  const char *code,
  int exit_code,
  const char *stage,
  const char *file_path,
  const char *message_zh_cn,
  const char *message_en,
  const char *detail)
{
  if (error == NULL) {
    return;
  }

  img2bin_runtime_error_init(error);
  error->exit_code = exit_code;
  img2bin_set_error(error->code, sizeof(error->code), "%s", code != NULL ? code : "internal_error");
  img2bin_set_error(error->stage, sizeof(error->stage), "%s", stage != NULL ? stage : "internal");
  img2bin_set_error(error->message_zh_cn, sizeof(error->message_zh_cn), "%s", message_zh_cn != NULL ? message_zh_cn : "发生内部错误。");
  img2bin_set_error(error->message_en, sizeof(error->message_en), "%s", message_en != NULL ? message_en : "An internal error occurred.");

  if (file_path != NULL && file_path[0] != '\0') {
    error->has_file = 1;
    img2bin_set_error(error->file_path, sizeof(error->file_path), "%s", file_path);
  }

  if (detail != NULL && detail[0] != '\0') {
    img2bin_set_error(error->detail, sizeof(error->detail), "%s", detail);
  }
}

typedef struct img2bin_manifest_output_s {
  char format[32];
  char path[IMG2BIN_PATH_CAPACITY];
  size_t bytes;             /* 落盘文件总字节（含 6 字节通用资源头） */
  size_t payload_bytes;     /* 算法 payload 字节（不含通用资源头） */
  size_t raw_payload_bytes; /* 同格式 RAW payload 字节，压缩率的分母 */
} img2bin_manifest_output_t;

typedef struct img2bin_processed_image_s {
  int width;
  int height;
  size_t output_count;
  img2bin_manifest_output_t outputs[IMG2BIN_FMT_COUNT];
} img2bin_processed_image_t;

typedef struct img2bin_manifest_item_s {
  char source_path[IMG2BIN_PATH_CAPACITY];
  int success;
  int width;
  int height;
  size_t output_count;
  img2bin_manifest_output_t outputs[IMG2BIN_FMT_COUNT];
  img2bin_runtime_error_t error;
} img2bin_manifest_item_t;

typedef struct img2bin_manifest_s {
  char output_directory[IMG2BIN_PATH_CAPACITY];
  img2bin_endianness_t endianness;
  size_t requested_format_count;
  img2bin_pixel_format_t requested_formats[IMG2BIN_FMT_COUNT];
  size_t source_images_total;
  size_t source_images_succeeded;
  size_t source_images_failed;
  size_t generated_bin_files_total;
  size_t item_count;
  size_t item_capacity;
  img2bin_manifest_item_t *items;
} img2bin_manifest_t;

typedef struct img2bin_dynamic_buffer_s {
  char *data;
  size_t length;
  size_t capacity;
} img2bin_dynamic_buffer_t;

typedef struct img2bin_run_stats_s {
  size_t source_image_success_count;
  size_t source_image_failure_count;
  size_t nonimage_error_count;
  int first_error_exit_code;
} img2bin_run_stats_t;

static int img2bin_tool_is_valid(const img2bin_tool_descriptor_t *tool)
{
  return tool != NULL &&
         tool->tool_id != NULL &&
         tool->exe_name != NULL &&
         tool->help_summary_en != NULL &&
         tool->display_name_zh_cn != NULL &&
         tool->display_name_en != NULL &&
         tool->description_zh_cn != NULL &&
         tool->description_en != NULL &&
         tool->gui_category_zh_cn != NULL &&
         tool->gui_category_en != NULL &&
         tool->algorithm_id != NULL &&
         tool->algorithm_code != NULL &&
         tool->compression != NULL &&
         tool->output_token != NULL &&
         tool->manifest_file_name != NULL &&
         tool->encode_image != NULL;
}

static void img2bin_processed_image_init(img2bin_processed_image_t *processed)
{
  if (processed == NULL) {
    return;
  }

  memset(processed, 0, sizeof(*processed));
}

static void img2bin_manifest_init(img2bin_manifest_t *manifest, const char *output_directory, const img2bin_cli_options_t *options)
{
  if (manifest == NULL) {
    return;
  }

  memset(manifest, 0, sizeof(*manifest));
  if (output_directory != NULL) {
    img2bin_set_error(manifest->output_directory, sizeof(manifest->output_directory), "%s", output_directory);
  }
  if (options != NULL) {
    size_t index;

    manifest->endianness = options->endianness;
    manifest->requested_format_count = options->format_count;
    for (index = 0; index < options->format_count; ++index) {
      manifest->requested_formats[index] = options->formats[index];
    }
  }
}

static void img2bin_manifest_free(img2bin_manifest_t *manifest)
{
  if (manifest == NULL) {
    return;
  }

  free(manifest->items);
  manifest->items = NULL;
  manifest->item_count = 0;
  manifest->item_capacity = 0;
}

static img2bin_manifest_item_t *img2bin_manifest_append_item(img2bin_manifest_t *manifest)
{
  img2bin_manifest_item_t *items = NULL;
  size_t new_capacity = 0;

  if (manifest == NULL) {
    return NULL;
  }

  if (manifest->item_count == manifest->item_capacity) {
    new_capacity = manifest->item_capacity == 0 ? 16 : manifest->item_capacity * 2;
    items = (img2bin_manifest_item_t *)realloc(manifest->items, new_capacity * sizeof(*items));
    if (items == NULL) {
      return NULL;
    }
    manifest->items = items;
    manifest->item_capacity = new_capacity;
  }

  memset(&manifest->items[manifest->item_count], 0, sizeof(manifest->items[manifest->item_count]));
  ++manifest->item_count;
  return &manifest->items[manifest->item_count - 1];
}

static int img2bin_manifest_record_success(
  img2bin_manifest_t *manifest,
  const char *source_path,
  const img2bin_processed_image_t *processed)
{
  img2bin_manifest_item_t *item = NULL;
  size_t index = 0;

  if (manifest == NULL || source_path == NULL || processed == NULL) {
    return 1;
  }

  item = img2bin_manifest_append_item(manifest);
  if (item == NULL) {
    return 0;
  }

  item->success = 1;
  item->width = processed->width;
  item->height = processed->height;
  item->output_count = processed->output_count;
  img2bin_set_error(item->source_path, sizeof(item->source_path), "%s", source_path);
  for (index = 0; index < processed->output_count && index < IMG2BIN_FMT_COUNT; ++index) {
    item->outputs[index] = processed->outputs[index];
  }

  ++manifest->source_images_total;
  ++manifest->source_images_succeeded;
  manifest->generated_bin_files_total += processed->output_count;
  return 1;
}

static int img2bin_manifest_record_error(
  img2bin_manifest_t *manifest,
  const char *source_path,
  const img2bin_runtime_error_t *error,
  int count_as_source_image,
  size_t generated_outputs_before_failure)
{
  img2bin_manifest_item_t *item = NULL;

  if (manifest == NULL || source_path == NULL || error == NULL) {
    return 1;
  }

  item = img2bin_manifest_append_item(manifest);
  if (item == NULL) {
    return 0;
  }

  item->success = 0;
  img2bin_set_error(item->source_path, sizeof(item->source_path), "%s", source_path);
  item->error = *error;

  if (count_as_source_image) {
    ++manifest->source_images_total;
    ++manifest->source_images_failed;
    manifest->generated_bin_files_total += generated_outputs_before_failure;
  }

  return 1;
}

static void img2bin_dynamic_buffer_init(img2bin_dynamic_buffer_t *buffer)
{
  if (buffer == NULL) {
    return;
  }

  memset(buffer, 0, sizeof(*buffer));
}

static void img2bin_dynamic_buffer_free(img2bin_dynamic_buffer_t *buffer)
{
  if (buffer == NULL) {
    return;
  }

  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
}

static int img2bin_dynamic_buffer_reserve(img2bin_dynamic_buffer_t *buffer, size_t needed_capacity)
{
  char *new_data = NULL;
  size_t new_capacity = 0;

  if (buffer == NULL) {
    return 0;
  }

  if (needed_capacity <= buffer->capacity) {
    return 1;
  }

  new_capacity = buffer->capacity == 0 ? 1024 : buffer->capacity;
  while (new_capacity < needed_capacity) {
    size_t next_capacity = new_capacity * 2;
    if (next_capacity <= new_capacity) {
      new_capacity = needed_capacity;
      break;
    }
    new_capacity = next_capacity;
  }

  new_data = (char *)realloc(buffer->data, new_capacity);
  if (new_data == NULL) {
    return 0;
  }

  buffer->data = new_data;
  buffer->capacity = new_capacity;
  return 1;
}

static int img2bin_dynamic_buffer_appendf(img2bin_dynamic_buffer_t *buffer, const char *format, ...)
{
  va_list args;
  va_list args_copy;
  int needed = 0;

  if (buffer == NULL || format == NULL) {
    return 0;
  }

  va_start(args, format);
  va_copy(args_copy, args);
  needed = vsnprintf(NULL, 0, format, args_copy);
  va_end(args_copy);
  if (needed < 0) {
    va_end(args);
    return 0;
  }

  if (!img2bin_dynamic_buffer_reserve(buffer, buffer->length + (size_t)needed + 1)) {
    va_end(args);
    return 0;
  }

  if (vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format, args) != needed) {
    va_end(args);
    return 0;
  }
  va_end(args);

  buffer->length += (size_t)needed;
  return 1;
}

static int img2bin_dynamic_buffer_append_json_string(img2bin_dynamic_buffer_t *buffer, const char *value)
{
  char *escaped = NULL;
  int ok = 0;

  escaped = img2bin_json_escape_alloc(value);
  if (escaped == NULL) {
    return 0;
  }

  ok = img2bin_dynamic_buffer_appendf(buffer, "\"%s\"", escaped);
  free(escaped);
  return ok;
}

static int img2bin_manifest_write_file(
  const img2bin_tool_descriptor_t *tool,
  const img2bin_manifest_t *manifest,
  char *error_buffer,
  size_t error_buffer_size)
{
  img2bin_dynamic_buffer_t buffer;
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  size_t index = 0;
  int written = 0;

  if (!img2bin_tool_is_valid(tool) || manifest == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Manifest is not initialized.");
    return 0;
  }

  if (!img2bin_path_join(manifest->output_directory, tool->manifest_file_name, manifest_path, sizeof(manifest_path))) {
    img2bin_set_error(error_buffer, error_buffer_size, "Manifest output path is too long.");
    return 0;
  }

  img2bin_dynamic_buffer_init(&buffer);

  if (!img2bin_dynamic_buffer_appendf(
        &buffer,
        "{\n  \"tool\": {\n    \"id\": \"%s\",\n    \"version\": \"" IMG2BIN_VERSION_TEXT "\"\n  },\n  \"run\": {\n    \"output_directory\": ",
        tool->tool_id)) {
    goto oom;
  }
  if (!img2bin_dynamic_buffer_append_json_string(&buffer, manifest->output_directory)) {
    goto oom;
  }
  if (!img2bin_dynamic_buffer_appendf(
        &buffer,
        ",\n    \"endianness\": \"%s\",\n    \"requested_formats\": [",
        manifest->endianness == IMG2BIN_ENDIAN_BIG ? "big" : "little")) {
    goto oom;
  }

  for (index = 0; index < manifest->requested_format_count; ++index) {
    const img2bin_format_info_t *format = img2bin_get_format_info(manifest->requested_formats[index]);
    if (format == NULL) {
      img2bin_set_error(error_buffer, error_buffer_size, "Manifest could not resolve requested format metadata.");
      img2bin_dynamic_buffer_free(&buffer);
      return 0;
    }

    if (index > 0 && !img2bin_dynamic_buffer_appendf(&buffer, ", ")) {
      goto oom;
    }
    if (!img2bin_dynamic_buffer_append_json_string(&buffer, format->name)) {
      goto oom;
    }
  }

  if (!img2bin_dynamic_buffer_appendf(
        &buffer,
        "]\n  },\n  \"summary\": {\n    \"source_images_total\": %zu,\n    \"source_images_succeeded\": %zu,\n    \"source_images_failed\": %zu,\n    \"generated_bin_files_total\": %zu\n  },\n  \"items\": [\n",
        manifest->source_images_total,
        manifest->source_images_succeeded,
        manifest->source_images_failed,
        manifest->generated_bin_files_total)) {
    goto oom;
  }

  for (index = 0; index < manifest->item_count; ++index) {
    const img2bin_manifest_item_t *item = &manifest->items[index];
    size_t output_index = 0;

    if (!img2bin_dynamic_buffer_appendf(&buffer, "    {\n      \"source_path\": ")) {
      goto oom;
    }
    if (!img2bin_dynamic_buffer_append_json_string(&buffer, item->source_path)) {
      goto oom;
    }

    if (item->success) {
      if (!img2bin_dynamic_buffer_appendf(
            &buffer,
            ",\n      \"status\": \"success\",\n      \"width\": %d,\n      \"height\": %d,\n      \"outputs\": [\n",
            item->width,
            item->height)) {
        goto oom;
      }

      for (output_index = 0; output_index < item->output_count; ++output_index) {
        if (!img2bin_dynamic_buffer_appendf(&buffer, "        {\n          \"format\": ")) {
          goto oom;
        }
        if (!img2bin_dynamic_buffer_append_json_string(&buffer, item->outputs[output_index].format)) {
          goto oom;
        }
        if (!img2bin_dynamic_buffer_appendf(&buffer, ",\n          \"path\": ")) {
          goto oom;
        }
        if (!img2bin_dynamic_buffer_append_json_string(&buffer, item->outputs[output_index].path)) {
          goto oom;
        }
        if (!img2bin_dynamic_buffer_appendf(
              &buffer,
              ",\n          \"bytes\": %zu,\n          \"payload_bytes\": %zu,\n          \"raw_payload_bytes\": %zu,\n          \"compression_percent\": %.1f\n        }%s\n",
              item->outputs[output_index].bytes,
              item->outputs[output_index].payload_bytes,
              item->outputs[output_index].raw_payload_bytes,
              item->outputs[output_index].raw_payload_bytes > 0u
                ? (double)item->outputs[output_index].payload_bytes * 100.0 / (double)item->outputs[output_index].raw_payload_bytes
                : 0.0,
              output_index + 1 == item->output_count ? "" : ",")) {
          goto oom;
        }
      }

      if (!img2bin_dynamic_buffer_appendf(&buffer, "      ]\n")) {
        goto oom;
      }
    } else {
      if (!img2bin_dynamic_buffer_appendf(&buffer, ",\n      \"status\": \"error\",\n      \"error\": {\n        \"code\": ")) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_append_json_string(&buffer, item->error.code)) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_appendf(&buffer, ",\n        \"stage\": ")) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_append_json_string(&buffer, item->error.stage)) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_appendf(&buffer, ",\n        \"exit_code\": %d,\n        \"message\": {\n          \"zh_cn\": ", item->error.exit_code)) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_append_json_string(&buffer, item->error.message_zh_cn)) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_appendf(&buffer, ",\n          \"en\": ")) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_append_json_string(&buffer, item->error.message_en)) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_appendf(&buffer, "\n        },\n        \"detail\": ")) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_append_json_string(&buffer, item->error.detail)) {
        goto oom;
      }
      if (!img2bin_dynamic_buffer_appendf(&buffer, "\n      }\n")) {
        goto oom;
      }
    }

    if (!img2bin_dynamic_buffer_appendf(&buffer, "    }%s\n", index + 1 == manifest->item_count ? "" : ",")) {
      goto oom;
    }
  }

  if (!img2bin_dynamic_buffer_appendf(&buffer, "  ]\n}\n")) {
    goto oom;
  }

  written = img2bin_write_file(manifest_path, (const unsigned char *)buffer.data, buffer.length, error_buffer, error_buffer_size);
  img2bin_dynamic_buffer_free(&buffer);
  return written;

oom:
  img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while building manifest.");
  img2bin_dynamic_buffer_free(&buffer);
  return 0;
}

static void img2bin_run_stats_note_success(img2bin_run_stats_t *stats)
{
  if (stats == NULL) {
    return;
  }

  ++stats->source_image_success_count;
}

static void img2bin_run_stats_note_error(img2bin_run_stats_t *stats, int exit_code, int count_as_source_image)
{
  if (stats == NULL) {
    return;
  }

  if (stats->first_error_exit_code == 0) {
    stats->first_error_exit_code = exit_code;
  }

  if (count_as_source_image) {
    ++stats->source_image_failure_count;
  } else {
    ++stats->nonimage_error_count;
  }
}

static int img2bin_appendf(char *buffer, size_t buffer_size, size_t *current, const char *format, ...)
{
  va_list args;
  int written = 0;

  if (buffer == NULL || current == NULL || format == NULL || *current >= buffer_size) {
    return 0;
  }

  va_start(args, format);
  written = vsnprintf(buffer + *current, buffer_size - *current, format, args);
  va_end(args);

  if (written < 0 || (size_t)written >= buffer_size - *current) {
    return 0;
  }

  *current += (size_t)written;
  return 1;
}

static int img2bin_emit_error_json(const img2bin_runtime_error_t *error)
{
  char *code = NULL;
  char *stage = NULL;
  char *message_zh_cn = NULL;
  char *message_en = NULL;
  char *detail = NULL;
  char *file_path = NULL;
  int ok = 0;

  if (error == NULL) {
    return 0;
  }

  code = img2bin_json_escape_alloc(error->code);
  stage = img2bin_json_escape_alloc(error->stage);
  message_zh_cn = img2bin_json_escape_alloc(error->message_zh_cn);
  message_en = img2bin_json_escape_alloc(error->message_en);
  detail = img2bin_json_escape_alloc(error->detail);
  if (error->has_file) {
    file_path = img2bin_json_escape_alloc(error->file_path);
  }

  if (code == NULL || stage == NULL || message_zh_cn == NULL || message_en == NULL || detail == NULL ||
      (error->has_file && file_path == NULL)) {
    fprintf(
      stderr,
      "{\"error\":{\"code\":\"internal_error\",\"exit_code\":%d,\"message\":{\"zh_cn\":\"输出错误 JSON 失败。\",\"en\":\"Failed to emit error JSON.\"},\"stage\":\"internal\"}}\n",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR);
    goto cleanup;
  }

  fprintf(
    stderr,
    "{\"error\":{\"code\":\"%s\",\"exit_code\":%d,\"message\":{\"zh_cn\":\"%s\",\"en\":\"%s\"}",
    code,
    error->exit_code,
    message_zh_cn,
    message_en);

  if (error->has_file) {
    fprintf(stderr, ",\"file\":\"%s\"", file_path);
  }
  if (error->detail[0] != '\0') {
    fprintf(stderr, ",\"detail\":\"%s\"", detail);
  }

  fprintf(stderr, ",\"stage\":\"%s\"}}\n", stage);
  ok = 1;

cleanup:
  free(code);
  free(stage);
  free(message_zh_cn);
  free(message_en);
  free(detail);
  free(file_path);
  return ok;
}

static int img2bin_append_argument_json(
  char *buffer,
  size_t buffer_size,
  size_t *current,
  const char *id,
  const char *flag,
  const char *value_type,
  int takes_value,
  int required,
  const char *default_json,
  const char *conflicts_with_json,
  const char *display_name_zh_cn,
  const char *display_name_en,
  const char *accepts_json,
  const char *special_values_json,
  const char *element_type_json,
  const char *value_delimiter_json,
  int is_last)
{
  return img2bin_appendf(
    buffer,
    buffer_size,
    current,
    "      {\n"
    "        \"id\": \"%s\",\n"
    "        \"flag\": \"%s\",\n"
    "        \"value_type\": \"%s\",\n"
    "        \"takes_value\": %s,\n"
    "        \"required\": %s,\n"
    "        \"conflicts_with\": %s,\n"
    "        \"default\": %s,\n"
    "        \"display_name\": {\n"
    "          \"zh_cn\": \"%s\",\n"
    "          \"en\": \"%s\"\n"
    "        },\n"
    "        \"accepts\": %s,\n"
    "        \"special_values\": %s,\n"
    "        \"element_type\": %s,\n"
    "        \"value_delimiter\": %s\n"
    "      }%s\n",
    id,
    flag,
    value_type,
    takes_value ? "true" : "false",
    required ? "true" : "false",
    conflicts_with_json,
    default_json,
    display_name_zh_cn,
    display_name_en,
    accepts_json,
    special_values_json,
    element_type_json,
    value_delimiter_json,
    is_last ? "" : ",");
}

static void img2bin_print_help_for_tool(const img2bin_tool_descriptor_t *tool)
{
  if (!img2bin_tool_is_valid(tool)) {
    return;
  }

  printf("%s %s - %s\n", tool->exe_name, IMG2BIN_VERSION_TEXT, tool->help_summary_en);
  printf("Usage:\n");
  printf("  %s [options] [path ...]\n\n", tool->exe_name);
  printf("Options:\n");
  printf("  --input <file-or-dir>      Input image file or directory.\n");
  printf("  --output <dir>             Output directory.\n");
  printf("  --format <name>            Output a single pixel format.\n");
  printf("  --formats <list|all>       Output multiple formats, comma separated or all.\n");
  printf("  --little-endian            Output little-endian pixels.\n");
  printf("  --bg-color <RRGGBB>        Background color for non-alpha target formats.\n");
  if (tool->supports_index_interval) {
    printf("  --index-interval <count>   Pixel interval for index points. Default is image width.\n");
  }
  printf("  --manifest                 Write %s into the output directory (off by default).\n", tool->manifest_file_name);
  printf("  --info                     Print machine-readable tool metadata as JSON.\n");
  printf("  --list-formats             Print supported pixel formats.\n");
  printf("  --help                     Print this help text.\n\n");
  printf("Positional inputs:\n");
  printf("  One or more file/directory paths may be passed directly.\n");
  printf("  On Windows, dragging files or folders onto the exe uses this mode.\n\n");
  printf("Default behavior with no arguments:\n");
  printf("  Input directory  : <exe_dir>/input\n");
  printf("  Output directory : <exe_dir>/output\n");
  printf("  Format           : rgb565\n");
  printf("  Endianness       : big-endian\n");
  printf("  Missing input/output folders will be created automatically.\n");
}

int img2bin_tool_get_info_json(const img2bin_tool_descriptor_t *tool, char *buffer, size_t buffer_size)
{
  const img2bin_format_info_t *formats = NULL;
  size_t format_count = 0;
  size_t index = 0;
  size_t current = 0;

  if (!img2bin_tool_is_valid(tool) || buffer == NULL || buffer_size == 0) {
    return 0;
  }

  formats = img2bin_get_format_infos(&format_count);

  if (!img2bin_appendf(buffer, buffer_size, &current, "{\n")) {
    return 0;
  }
  if (!img2bin_appendf(buffer, buffer_size, &current, "  \"schema_version\": \"%s\",\n", IMG2BIN_INFO_SCHEMA_VERSION)) {
    return 0;
  }
  if (!img2bin_appendf(
        buffer,
        buffer_size,
        &current,
        "  \"tool\": {\n"
        "    \"id\": \"%s\",\n"
        "    \"kind\": \"image_converter\",\n"
        "    \"version\": \"" IMG2BIN_VERSION_TEXT "\",\n"
        "    \"version_semver\": \"" IMG2BIN_VERSION_SEMVER "\"\n"
        "  },\n"
        "  \"gui\": {\n"
        "    \"display_name\": {\n"
        "      \"zh_cn\": \"%s\",\n"
        "      \"en\": \"%s\"\n"
        "    },\n"
        "    \"description\": {\n"
        "      \"zh_cn\": \"%s\",\n"
        "      \"en\": \"%s\"\n"
        "    },\n"
        "    \"gui_category\": {\n"
        "      \"zh_cn\": \"%s\",\n"
        "      \"en\": \"%s\"\n"
        "    },\n"
        "    \"priority\": %d\n"
        "  },\n"
        "  \"algorithm\": {\n"
        "    \"id\": \"%s\",\n"
        "    \"algorithm_code\": \"%s\",\n"
        "    \"compression\": \"%s\",\n"
        "    \"supports_multi_format\": true\n"
        "  },\n"
        "  \"defaults\": {\n"
        "    \"format\": \"rgb565\",\n"
        "    \"endianness\": \"big\",\n"
        "    \"input_dir\": \"exe_dir/input\",\n"
        "    \"output_dir\": \"exe_dir/output\",\n"
        "    \"background_color\": \"000000\"%s\n"
        "  },\n"
        "  \"capabilities\": {\n"
        "    \"input_formats\": [\"png\", \"bmp\", \"jpg\", \"jpeg\"],\n"
        "    \"output_extension\": \"bin\",\n"
        "    \"supports_batch\": true,\n"
        "    \"supports_single_format\": true,\n"
        "    \"supports_multiple_formats\": true,\n"
        "    \"supports_endianness_switch\": true,\n"
        "    \"supports_bg_color\": true,\n"
        "    \"supports_index_interval\": %s,\n"
        "    \"supports_no_arg_batch\": true,\n"
        "    \"supports_directory_input\": true,\n"
        "    \"supports_file_input\": true,\n"
        "    \"query_flag\": \"--info\"\n"
        "  },\n"
        "  \"invocation\": {\n"
        "    \"style\": \"flag_cli\",\n"
        "    \"info_flag\": \"--info\",\n"
        "    \"help_flag\": \"--help\",\n"
        "    \"arguments\": [\n",
        tool->tool_id,
        tool->display_name_zh_cn,
        tool->display_name_en,
        tool->description_zh_cn,
        tool->description_en,
        tool->gui_category_zh_cn,
        tool->gui_category_en,
        tool->priority,
        tool->algorithm_id,
        tool->algorithm_code,
        tool->compression,
        tool->supports_index_interval ? ",\n    \"index_interval\": \"image_width\"" : "",
        tool->supports_index_interval ? "true" : "false")) {
    return 0;
  }

  if (!img2bin_append_argument_json(
        buffer,
        buffer_size,
        &current,
        "input",
        "--input",
        "path",
        1,
        0,
        "\"exe_dir/input\"",
        "[]",
        "输入路径",
        "Input Path",
        "[\"file\", \"directory\"]",
        "[]",
        "null",
        "null",
        0)) {
    return 0;
  }
  if (!img2bin_append_argument_json(
        buffer,
        buffer_size,
        &current,
        "output",
        "--output",
        "path",
        1,
        0,
        "\"exe_dir/output\"",
        "[]",
        "输出目录",
        "Output Directory",
        "[\"directory\"]",
        "[]",
        "null",
        "null",
        0)) {
    return 0;
  }
  if (!img2bin_append_argument_json(
        buffer,
        buffer_size,
        &current,
        "format",
        "--format",
        "pixel_format_name",
        1,
        0,
        "\"rgb565\"",
        "[\"formats\"]",
        "单一格式",
        "Single Format",
        "[]",
        "[]",
        "null",
        "null",
        0)) {
    return 0;
  }
  if (!img2bin_append_argument_json(
        buffer,
        buffer_size,
        &current,
        "formats",
        "--formats",
        "csv_or_keyword",
        1,
        0,
        "null",
        "[\"format\"]",
        "多格式",
        "Multiple Formats",
        "[]",
        "[\"all\"]",
        "\"pixel_format_name\"",
        "\",\"",
        0)) {
    return 0;
  }
  if (!img2bin_append_argument_json(
        buffer,
        buffer_size,
        &current,
        "little_endian",
        "--little-endian",
        "boolean_flag",
        0,
        0,
        "false",
        "[]",
        "小端输出",
        "Little Endian",
        "[]",
        "[]",
        "null",
        "null",
        0)) {
    return 0;
  }
  if (!img2bin_append_argument_json(
        buffer,
        buffer_size,
        &current,
        "bg_color",
        "--bg-color",
        "hex_rgb",
        1,
        0,
        "\"000000\"",
        "[]",
        "背景色",
        "Background Color",
        "[]",
        "[]",
        "null",
        "null",
        0)) {
    return 0;
  }
  if (tool->supports_index_interval) {
    if (!img2bin_append_argument_json(
          buffer,
          buffer_size,
          &current,
          "index_interval",
          "--index-interval",
          "positive_integer",
          1,
          0,
          "\"image_width\"",
          "[]",
          "索引间隔",
          "Index Interval",
          "[]",
          "[]",
          "null",
          "null",
          0)) {
      return 0;
    }
  }
  if (!img2bin_append_argument_json(
        buffer,
        buffer_size,
        &current,
        "manifest",
        "--manifest",
        "boolean_flag",
        0,
        0,
        "false",
        "[]",
        "写出批处理清单",
        "Write Manifest",
        "[]",
        "[]",
        "null",
        "null",
        0)) {
    return 0;
  }
  if (!img2bin_append_argument_json(
        buffer,
        buffer_size,
        &current,
        "list_formats",
        "--list-formats",
        "boolean_flag",
        0,
        0,
        "false",
        "[]",
        "列出格式",
        "List Formats",
        "[]",
        "[]",
        "null",
        "null",
        1)) {
    return 0;
  }

  if (!img2bin_appendf(
        buffer,
        buffer_size,
        &current,
        "    ]\n"
        "  },\n"
        "  \"output\": {\n"
        "    \"extension\": \"bin\",\n"
        "    \"filename_pattern\": \"{source_stem}_{format_name}_%s_{endianness_token}_{width}x{height}.bin\",\n"
        "    \"endianness_tokens\": {\n"
        "      \"big\": \"be\",\n"
        "      \"little\": \"le\"\n"
        "    },\n"
        "    \"resource_header\": {\n"
        "      \"size\": 6,\n"
        "      \"resource_type\": 0,\n"
        "      \"algorithm_nibble\": %u,\n"
        "      \"layout\": \"type:1,algo_format:1,width_be:2,height_be:2\"\n"
        "    }\n"
        "  },\n"
        "  \"exit_codes\": {\n"
        "    \"success\": 0,\n"
        "    \"cli_error\": 1,\n"
        "    \"input_error\": 2,\n"
        "    \"encode_error\": 3,\n"
        "    \"write_error\": 4,\n"
        "    \"internal_error\": 5,\n"
        "    \"batch_partial_failure\": 6\n"
        "  },\n"
        "  \"pixel_formats\": [\n",
        tool->output_token,
        tool->header_algorithm_nibble)) {
    return 0;
  }

  {
    size_t supported_count = 0;
    size_t emitted = 0;

    for (index = 0; index < format_count; ++index) {
      if (formats[index].is_alpha_only && !tool->supports_alpha_only_formats) {
        continue;
      }
      ++supported_count;
    }

    for (index = 0; index < format_count; ++index) {
      if (formats[index].is_alpha_only && !tool->supports_alpha_only_formats) {
        continue;
      }
      ++emitted;
      if (!img2bin_appendf(
            buffer,
            buffer_size,
            &current,
            "    {\n"
            "      \"name\": \"%s\",\n"
            "      \"display_name\": {\n"
            "        \"zh_cn\": \"%s\",\n"
            "        \"en\": \"%s\"\n"
            "      },\n"
            "      \"bytes_per_pixel\": %u,\n"
            "      \"bits_per_pixel\": %u,\n"
            "      \"is_alpha_only\": %s,\n"
            "      \"stores_alpha\": %s,\n"
            "      \"uses_background_color\": %s,\n"
            "      \"endianness_affects_output\": %s,\n"
            "      \"header_nibble\": %d\n"
            "    }%s\n",
            formats[index].name,
            formats[index].display_name_zh_cn,
            formats[index].display_name_en,
            (unsigned int)formats[index].bytes_per_pixel,
            (unsigned int)formats[index].bits_per_pixel,
            formats[index].is_alpha_only ? "true" : "false",
            formats[index].stores_alpha ? "true" : "false",
            formats[index].uses_background_color ? "true" : "false",
            formats[index].endianness_affects_output ? "true" : "false",
            img2bin_get_format_header_nibble(formats[index].id),
            emitted == supported_count ? "" : ",")) {
        return 0;
      }
    }
  }

  return img2bin_appendf(buffer, buffer_size, &current, "  ]\n}\n");
}

static int img2bin_process_single_image(
  const img2bin_tool_descriptor_t *tool,
  const char *image_path,
  const char *output_directory,
  const img2bin_cli_options_t *options,
  img2bin_processed_image_t *processed,
  img2bin_runtime_error_t *runtime_error)
{
  img2bin_image_t image;
  char image_error[512];
  char stem[IMG2BIN_PATH_CAPACITY];
  size_t format_index = 0;

  if (!img2bin_tool_is_valid(tool)) {
    img2bin_runtime_error_set(
      runtime_error,
      "tool_descriptor_invalid",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      image_path,
      "工具描述无效。",
      "Tool descriptor is invalid.",
      NULL);
    return 0;
  }

  memset(&image, 0, sizeof(image));
  img2bin_processed_image_init(processed);
  if (!img2bin_load_image(image_path, &image, image_error, sizeof(image_error))) {
    img2bin_runtime_error_set(
      runtime_error,
      "image_load_failed",
      IMG2BIN_APP_EXIT_INPUT_ERROR,
      "load",
      image_path,
      "加载图片失败。",
      "Failed to load image.",
      image_error);
    return 0;
  }

  if (!img2bin_path_basename_stem(image_path, stem, sizeof(stem))) {
    img2bin_runtime_error_set(
      runtime_error,
      "output_name_failed",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      image_path,
      "生成输出文件名失败。",
      "Failed to derive output file name.",
      image_path);
    img2bin_free_image(&image);
    return 0;
  }

  if (processed != NULL) {
    processed->width = image.width;
    processed->height = image.height;
  }

  if ((unsigned int)image.width > 0xFFFFu || (unsigned int)image.height > 0xFFFFu) {
    img2bin_runtime_error_set(
      runtime_error,
      "image_too_large",
      IMG2BIN_APP_EXIT_ENCODE_ERROR,
      "encode",
      image_path,
      "图片宽高超过 65535，无法写入资源头。",
      "Image dimensions exceed 65535 and do not fit the resource header.",
      NULL);
    img2bin_free_image(&image);
    return 0;
  }

  for (format_index = 0; format_index < options->format_count; ++format_index) {
    const img2bin_format_info_t *format = img2bin_get_format_info(options->formats[format_index]);
    unsigned char *encoded = NULL;
    size_t encoded_size = 0;
    size_t payload_size = 0;
    size_t raw_payload_size = 0;
    char encode_error[512];
    char file_name[IMG2BIN_PATH_CAPACITY];
    char output_path[IMG2BIN_PATH_CAPACITY];
    int written = 0;
    int name_length = 0;

    if (format == NULL) {
      img2bin_runtime_error_set(
        runtime_error,
        "format_lookup_failed",
        IMG2BIN_APP_EXIT_INTERNAL_ERROR,
        "internal",
        image_path,
        "查找像素格式失败。",
        "Failed to resolve pixel format metadata.",
        "The requested pixel format id is not registered.");
      img2bin_free_image(&image);
      return 0;
    }

    if (!tool->encode_image(
          format->id,
          options->endianness,
          options->background,
          &image,
          options,
          &encoded,
          &encoded_size,
          encode_error,
          sizeof(encode_error))) {
      img2bin_runtime_error_set(
        runtime_error,
        "encode_failed",
        IMG2BIN_APP_EXIT_ENCODE_ERROR,
        "encode",
        image_path,
        "取模编码失败。",
        "Failed to encode image.",
        encode_error);
      img2bin_free_image(&image);
      return 0;
    }

    name_length = snprintf(
      file_name,
      sizeof(file_name),
      "%s_%s_%s_%s_%dx%d.bin",
      stem,
      format->name,
      tool->output_token,
      options->endianness == IMG2BIN_ENDIAN_BIG ? "be" : "le",
      image.width,
      image.height);
    if (name_length < 0 || (size_t)name_length >= sizeof(file_name)) {
      free(encoded);
      img2bin_runtime_error_set(
        runtime_error,
        "output_name_too_long",
        IMG2BIN_APP_EXIT_WRITE_ERROR,
        "write",
        image_path,
        "输出文件名过长。",
        "Output file name is too long.",
        stem);
      img2bin_free_image(&image);
      return 0;
    }

    if (!img2bin_path_join(output_directory, file_name, output_path, sizeof(output_path))) {
      free(encoded);
      img2bin_runtime_error_set(
        runtime_error,
        "output_path_too_long",
        IMG2BIN_APP_EXIT_WRITE_ERROR,
        "write",
        image_path,
        "输出路径过长。",
        "Output path is too long.",
        file_name);
      img2bin_free_image(&image);
      return 0;
    }

    {
      unsigned char resource_header[IMG2BIN_RESOURCE_HEADER_SIZE];
      unsigned char *file_data = NULL;
      size_t file_size = 0;

      payload_size = encoded_size;

      if (!img2bin_build_resource_header(
            tool->header_algorithm_nibble,
            format->id,
            (unsigned int)image.width,
            (unsigned int)image.height,
            resource_header)) {
        free(encoded);
        img2bin_runtime_error_set(
          runtime_error,
          "resource_header_failed",
          IMG2BIN_APP_EXIT_INTERNAL_ERROR,
          "internal",
          image_path,
          "生成资源头失败。",
          "Failed to build the resource header.",
          NULL);
        img2bin_free_image(&image);
        return 0;
      }

      file_size = encoded_size + IMG2BIN_RESOURCE_HEADER_SIZE;
      file_data = (unsigned char *)malloc(file_size);
      if (file_data == NULL) {
        free(encoded);
        img2bin_runtime_error_set(
          runtime_error,
          "out_of_memory",
          IMG2BIN_APP_EXIT_INTERNAL_ERROR,
          "internal",
          image_path,
          "内存不足。",
          "Out of memory while assembling the output file.",
          NULL);
        img2bin_free_image(&image);
        return 0;
      }

      memcpy(file_data, resource_header, IMG2BIN_RESOURCE_HEADER_SIZE);
      memcpy(file_data + IMG2BIN_RESOURCE_HEADER_SIZE, encoded, encoded_size);
      free(encoded);

      written = img2bin_write_file(output_path, file_data, file_size, encode_error, sizeof(encode_error));
      free(file_data);
      encoded_size = file_size;
    }

    if (!written) {
      img2bin_runtime_error_set(
        runtime_error,
        "write_failed",
        IMG2BIN_APP_EXIT_WRITE_ERROR,
        "write",
        image_path,
        "写入输出文件失败。",
        "Failed to write output file.",
        encode_error);
      img2bin_free_image(&image);
      return 0;
    }

    raw_payload_size = img2bin_format_payload_size(format->id, (unsigned int)image.width, (unsigned int)image.height);

    if (processed != NULL && processed->output_count < IMG2BIN_FMT_COUNT) {
      img2bin_set_error(
        processed->outputs[processed->output_count].format,
        sizeof(processed->outputs[processed->output_count].format),
        "%s",
        format->name);
      img2bin_set_error(
        processed->outputs[processed->output_count].path,
        sizeof(processed->outputs[processed->output_count].path),
        "%s",
        output_path);
      processed->outputs[processed->output_count].bytes = encoded_size;
      processed->outputs[processed->output_count].payload_bytes = payload_size;
      processed->outputs[processed->output_count].raw_payload_bytes = raw_payload_size;
      ++processed->output_count;
    }

    /* 体积率 = 算法 payload / 同格式 RAW payload（通用头两边恒定 6 字节，不计入） */
    if (raw_payload_size > 0u) {
      printf(
        "Wrote %s (%zu bytes, payload %zu / raw %zu = %.1f%%)\n",
        output_path,
        encoded_size,
        payload_size,
        raw_payload_size,
        (double)payload_size * 100.0 / (double)raw_payload_size);
    } else {
      printf("Wrote %s (%zu bytes)\n", output_path, encoded_size);
    }
  }

  img2bin_free_image(&image);
  return 1;
}

static int img2bin_record_manifest_error(
  img2bin_manifest_t *manifest,
  const char *source_path,
  const img2bin_runtime_error_t *runtime_error,
  int count_as_source_image,
  size_t generated_outputs_before_failure,
  img2bin_runtime_error_t *out_internal_error)
{
  if (manifest == NULL) {
    return 1;
  }

  if (!img2bin_manifest_record_error(
        manifest,
        source_path,
        runtime_error,
        count_as_source_image,
        generated_outputs_before_failure)) {
    img2bin_runtime_error_set(
      out_internal_error,
      "manifest_record_failed",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      source_path,
      "记录批处理结果失败。",
      "Failed to record batch manifest entry.",
      "Out of memory while storing manifest data.");
    return 0;
  }

  return 1;
}

static int img2bin_record_manifest_success(
  img2bin_manifest_t *manifest,
  const char *source_path,
  const img2bin_processed_image_t *processed,
  img2bin_runtime_error_t *out_internal_error)
{
  if (manifest == NULL) {
    return 1;
  }

  if (!img2bin_manifest_record_success(manifest, source_path, processed)) {
    img2bin_runtime_error_set(
      out_internal_error,
      "manifest_record_failed",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      source_path,
      "记录批处理结果失败。",
      "Failed to record batch manifest entry.",
      "Out of memory while storing manifest data.");
    return 0;
  }

  return 1;
}

static int img2bin_process_image_input(
  const img2bin_tool_descriptor_t *tool,
  const char *image_path,
  const char *output_directory,
  const img2bin_cli_options_t *options,
  img2bin_manifest_t *manifest,
  img2bin_run_stats_t *stats,
  img2bin_runtime_error_t *out_fatal_error)
{
  img2bin_runtime_error_t runtime_error;
  img2bin_processed_image_t processed;

  if (!img2bin_is_supported_image_path(image_path)) {
    img2bin_runtime_error_set(
      &runtime_error,
      "input_extension_unsupported",
      IMG2BIN_APP_EXIT_INPUT_ERROR,
      "scan",
      image_path,
      "输入文件格式不受支持。",
      "Unsupported input file format.",
      image_path);
    img2bin_emit_error_json(&runtime_error);
    img2bin_run_stats_note_error(stats, runtime_error.exit_code, 0);
    return img2bin_record_manifest_error(manifest, image_path, &runtime_error, 0, 0, out_fatal_error);
  }

  img2bin_runtime_error_init(&runtime_error);
  img2bin_processed_image_init(&processed);
  if (!img2bin_process_single_image(tool, image_path, output_directory, options, &processed, &runtime_error)) {
    img2bin_emit_error_json(&runtime_error);
    img2bin_run_stats_note_error(stats, runtime_error.exit_code, 1);
    return img2bin_record_manifest_error(manifest, image_path, &runtime_error, 1, processed.output_count, out_fatal_error);
  }

  img2bin_run_stats_note_success(stats);
  return img2bin_record_manifest_success(manifest, image_path, &processed, out_fatal_error);
}

static int img2bin_process_directory_input(
  const img2bin_tool_descriptor_t *tool,
  const char *input_path,
  const char *output_directory,
  const img2bin_cli_options_t *options,
  img2bin_manifest_t *manifest,
  img2bin_run_stats_t *stats,
  img2bin_runtime_error_t *out_fatal_error)
{
  img2bin_string_list_t images;
  size_t index = 0;
  char scan_error[512];

  memset(&images, 0, sizeof(images));
  if (!img2bin_collect_supported_images(input_path, &images, scan_error, sizeof(scan_error))) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "input_scan_failed",
      IMG2BIN_APP_EXIT_INPUT_ERROR,
      "scan",
      input_path,
      "扫描输入目录失败。",
      "Failed to scan input directory.",
      scan_error);
    img2bin_emit_error_json(&runtime_error);
    img2bin_run_stats_note_error(stats, runtime_error.exit_code, 0);
    return img2bin_record_manifest_error(manifest, input_path, &runtime_error, 0, 0, out_fatal_error);
  }

  if (images.count == 0) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "no_supported_images",
      IMG2BIN_APP_EXIT_INPUT_ERROR,
      "scan",
      input_path,
      "输入目录中没有找到受支持的图片。",
      "No supported images were found in the input directory.",
      input_path);
    img2bin_emit_error_json(&runtime_error);
    img2bin_run_stats_note_error(stats, runtime_error.exit_code, 0);
    img2bin_string_list_free(&images);
    return img2bin_record_manifest_error(manifest, input_path, &runtime_error, 0, 0, out_fatal_error);
  }

  for (index = 0; index < images.count; ++index) {
    if (!img2bin_process_image_input(tool, images.items[index], output_directory, options, manifest, stats, out_fatal_error)) {
      img2bin_string_list_free(&images);
      return 0;
    }
  }

  img2bin_string_list_free(&images);
  return 1;
}

static int img2bin_process_root_input(
  const img2bin_tool_descriptor_t *tool,
  const char *input_path,
  const char *output_directory,
  const img2bin_cli_options_t *options,
  img2bin_manifest_t *manifest,
  img2bin_run_stats_t *stats,
  img2bin_runtime_error_t *out_fatal_error)
{
  if (img2bin_is_regular_file(input_path)) {
    return img2bin_process_image_input(tool, input_path, output_directory, options, manifest, stats, out_fatal_error);
  }

  if (img2bin_is_directory(input_path)) {
    return img2bin_process_directory_input(tool, input_path, output_directory, options, manifest, stats, out_fatal_error);
  }

  {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "input_path_invalid",
      IMG2BIN_APP_EXIT_INPUT_ERROR,
      "scan",
      input_path,
      "输入路径不存在或不受支持。",
      "Input path does not exist or is unsupported.",
      input_path);
    img2bin_emit_error_json(&runtime_error);
    img2bin_run_stats_note_error(stats, runtime_error.exit_code, 0);
    return img2bin_record_manifest_error(manifest, input_path, &runtime_error, 0, 0, out_fatal_error);
  }
}

/* manifest 日志默认关闭：只有显式传 --manifest 才写（任何运行形态均生效）。 */
static int img2bin_should_write_manifest(const img2bin_cli_options_t *options)
{
  return options != NULL && options->write_manifest;
}

static int img2bin_process_run_inputs(
  const img2bin_tool_descriptor_t *tool,
  const img2bin_cli_options_t *options,
  const char *default_input_path,
  const char *output_directory,
  int write_manifest)
{
  img2bin_manifest_t manifest;
  img2bin_run_stats_t stats;
  img2bin_runtime_error_t fatal_error;
  const char *single_input_path = NULL;
  size_t root_input_count = 0;
  size_t index = 0;
  int using_default_input = 0;

  memset(&stats, 0, sizeof(stats));
  img2bin_runtime_error_init(&fatal_error);
  img2bin_manifest_init(&manifest, output_directory, options);

  if (options->input_path != NULL) {
    single_input_path = options->input_path;
    root_input_count = 1;
  } else if (options->positional_input_count > 0) {
    root_input_count = options->positional_input_count;
  } else {
    single_input_path = default_input_path;
    root_input_count = 1;
    using_default_input = 1;
  }

  if (options->positional_input_count > 0) {
    for (index = 0; index < options->positional_input_count; ++index) {
      if (!img2bin_process_root_input(
            tool,
            options->positional_inputs[index],
            output_directory,
            options,
            write_manifest ? &manifest : NULL,
            &stats,
            &fatal_error)) {
        img2bin_emit_error_json(&fatal_error);
        img2bin_manifest_free(&manifest);
        return fatal_error.exit_code;
      }
    }
  } else {
    if (!img2bin_process_root_input(
          tool,
          single_input_path,
          output_directory,
          options,
          write_manifest ? &manifest : NULL,
          &stats,
          &fatal_error)) {
      img2bin_emit_error_json(&fatal_error);
      img2bin_manifest_free(&manifest);
      return fatal_error.exit_code;
    }
  }

  if (write_manifest) {
    char manifest_error[512];

    if (!img2bin_manifest_write_file(tool, &manifest, manifest_error, sizeof(manifest_error))) {
      img2bin_runtime_error_t runtime_error;

      img2bin_runtime_error_set(
        &runtime_error,
        "manifest_write_failed",
        IMG2BIN_APP_EXIT_WRITE_ERROR,
        "write",
        output_directory,
        "写入批处理结果文件失败。",
        "Failed to write batch manifest.",
        manifest_error);
      img2bin_emit_error_json(&runtime_error);
      img2bin_manifest_free(&manifest);
      return runtime_error.exit_code;
    }
  }

  img2bin_manifest_free(&manifest);

  if (root_input_count > 1) {
    return (stats.source_image_failure_count + stats.nonimage_error_count) > 0
             ? IMG2BIN_APP_EXIT_BATCH_PARTIAL_FAILURE
             : IMG2BIN_APP_EXIT_SUCCESS;
  }

  if (stats.source_image_failure_count > 0) {
    return IMG2BIN_APP_EXIT_BATCH_PARTIAL_FAILURE;
  }

  if (stats.nonimage_error_count > 0) {
    return stats.first_error_exit_code != 0 ? stats.first_error_exit_code : IMG2BIN_APP_EXIT_INPUT_ERROR;
  }

  /* 默认 input 目录存在但没有任何成功产出：维持既有的输入错误退出码
     （历史上该判断挂在 manifest 开关上；manifest 改为默认关闭后独立保留）。 */
  if (using_default_input && stats.source_image_success_count == 0) {
    return stats.first_error_exit_code != 0 ? stats.first_error_exit_code : IMG2BIN_APP_EXIT_INPUT_ERROR;
  }

  return IMG2BIN_APP_EXIT_SUCCESS;
}

static int img2bin_resolve_default_path(
  const char *executable_directory,
  const char *leaf_name,
  char *buffer,
  size_t buffer_size)
{
  if (!img2bin_path_join(executable_directory, leaf_name, buffer, buffer_size)) {
    return 0;
  }
  return 1;
}

static int img2bin_prepare_default_directories(
  const char *input_path,
  const char *output_path,
  img2bin_runtime_error_t *runtime_error)
{
  char make_error[512];
  int created_input = 0;
  int created_output = 0;

  if (input_path == NULL || output_path == NULL) {
    img2bin_runtime_error_set(
      runtime_error,
      "default_directory_prepare_failed",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      NULL,
      "准备默认目录失败。",
      "Failed to prepare default directories.",
      "The default input/output paths are not initialized.");
    return -1;
  }

  if (!img2bin_is_directory(input_path)) {
    if (img2bin_is_regular_file(input_path)) {
      img2bin_runtime_error_set(
        runtime_error,
        "input_path_not_directory",
        IMG2BIN_APP_EXIT_INPUT_ERROR,
        "scan",
        input_path,
        "默认输入路径不是文件夹。",
        "Default input path is not a directory.",
        input_path);
      return -1;
    }

    if (!img2bin_make_dirs(input_path, make_error, sizeof(make_error))) {
      img2bin_runtime_error_set(
        runtime_error,
        "input_directory_create_failed",
        IMG2BIN_APP_EXIT_WRITE_ERROR,
        "write",
        input_path,
        "创建默认输入目录失败。",
        "Failed to create default input directory.",
        make_error);
      return -1;
    }

    created_input = 1;
  }

  if (!img2bin_is_directory(output_path)) {
    if (img2bin_is_regular_file(output_path)) {
      img2bin_runtime_error_set(
        runtime_error,
        "output_path_not_directory",
        IMG2BIN_APP_EXIT_WRITE_ERROR,
        "write",
        output_path,
        "默认输出路径不是文件夹。",
        "Default output path is not a directory.",
        output_path);
      return -1;
    }

    if (!img2bin_make_dirs(output_path, make_error, sizeof(make_error))) {
      img2bin_runtime_error_set(
        runtime_error,
        "output_directory_create_failed",
        IMG2BIN_APP_EXIT_WRITE_ERROR,
        "write",
        output_path,
        "创建默认输出目录失败。",
        "Failed to create default output directory.",
        make_error);
      return -1;
    }

    created_output = 1;
  }

  if (created_input) {
    if (created_output) {
      printf("Created input directory: %s\n", input_path);
      printf("Created output directory: %s\n", output_path);
    } else {
      printf("Created input directory: %s\n", input_path);
      printf("Output directory is ready: %s\n", output_path);
    }
    printf("Put image files into the input directory and run again.\n");
    return 1;
  }

  return 0;
}

int img2bin_tool_run_with_executable_path(
  const img2bin_tool_descriptor_t *tool,
  int argc,
  const char *const *argv,
  const char *executable_path_override)
{
  img2bin_cli_options_t options;
  char cli_error[512];
  char executable_path[IMG2BIN_PATH_CAPACITY];
  char executable_directory[IMG2BIN_PATH_CAPACITY];
  char input_path[IMG2BIN_PATH_CAPACITY];
  char output_path[IMG2BIN_PATH_CAPACITY];
  int using_default_input = 0;
  int write_manifest = 0;

  if (!img2bin_tool_is_valid(tool)) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "tool_descriptor_invalid",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      NULL,
      "工具描述无效。",
      "Tool descriptor is invalid.",
      NULL);
    img2bin_emit_error_json(&runtime_error);
    return runtime_error.exit_code;
  }

  if (!img2bin_parse_cli(argc, argv, &options, cli_error, sizeof(cli_error))) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "cli_parse_failed",
      IMG2BIN_APP_EXIT_CLI_ERROR,
      "cli",
      NULL,
      "命令行参数无效。",
      "Invalid command-line arguments.",
      cli_error);
    img2bin_emit_error_json(&runtime_error);
    return runtime_error.exit_code;
  }

  if (options.show_help) {
    img2bin_print_help_for_tool(tool);
    return IMG2BIN_APP_EXIT_SUCCESS;
  }

  if (options.show_info) {
    char info_json[16384];

    if (!img2bin_tool_get_info_json(tool, info_json, sizeof(info_json))) {
      img2bin_runtime_error_t runtime_error;

      img2bin_runtime_error_set(
        &runtime_error,
        "metadata_build_failed",
        IMG2BIN_APP_EXIT_INTERNAL_ERROR,
        "internal",
        NULL,
        "生成工具元数据失败。",
        "Failed to build tool metadata.",
        "The --info response exceeded the metadata buffer.");
      img2bin_emit_error_json(&runtime_error);
      return runtime_error.exit_code;
    }

    printf("%s", info_json);
    return IMG2BIN_APP_EXIT_SUCCESS;
  }

  if (options.list_formats) {
    size_t format_count = 0;
    const img2bin_format_info_t *format_infos = img2bin_get_format_infos(&format_count);
    size_t format_index;

    printf("Supported formats:\n");
    for (format_index = 0; format_index < format_count; ++format_index) {
      if (format_infos[format_index].is_alpha_only && !tool->supports_alpha_only_formats) {
        continue;
      }
      printf("  %s\n", format_infos[format_index].name);
    }
    return IMG2BIN_APP_EXIT_SUCCESS;
  }

  if (options.index_interval_specified && !tool->supports_index_interval) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "cli_parse_failed",
      IMG2BIN_APP_EXIT_CLI_ERROR,
      "cli",
      NULL,
      "当前工具不支持自定义索引间隔。",
      "This tool does not support a custom index interval.",
      "--index-interval is only available for indexed QOI tools.");
    img2bin_emit_error_json(&runtime_error);
    return runtime_error.exit_code;
  }

  /* 按工具过滤格式：显式点名不支持的格式报 CLI 错误；--formats all 静默滤除。 */
  {
    size_t read_index = 0;
    size_t write_index = 0;

    for (read_index = 0; read_index < options.format_count; ++read_index) {
      const img2bin_format_info_t *format_info = img2bin_get_format_info(options.formats[read_index]);

      if (format_info != NULL && (!format_info->is_alpha_only || tool->supports_alpha_only_formats)) {
        options.formats[write_index] = options.formats[read_index];
        ++write_index;
        continue;
      }

      if (!options.formats_all) {
        img2bin_runtime_error_t runtime_error;

        img2bin_runtime_error_set(
          &runtime_error,
          "cli_parse_failed",
          IMG2BIN_APP_EXIT_CLI_ERROR,
          "cli",
          NULL,
          "当前工具不支持 Alpha 蒙版格式（a8/a4/a2/a1 仅限 raw 工具）。",
          "This tool does not support alpha mask formats (a8/a4/a2/a1 are raw-only).",
          format_info != NULL ? format_info->name : NULL);
        img2bin_emit_error_json(&runtime_error);
        return runtime_error.exit_code;
      }
    }
    options.format_count = write_index;
  }

  if (executable_path_override != NULL) {
    if (strlen(executable_path_override) + 1 > sizeof(executable_path)) {
      img2bin_runtime_error_t runtime_error;

      img2bin_runtime_error_set(
        &runtime_error,
        "executable_path_too_long",
        IMG2BIN_APP_EXIT_INTERNAL_ERROR,
        "internal",
        NULL,
        "可执行文件路径过长。",
        "Executable path override is too long.",
        executable_path_override);
      img2bin_emit_error_json(&runtime_error);
      return runtime_error.exit_code;
    }
    strcpy(executable_path, executable_path_override);
  } else if (!img2bin_get_executable_path(executable_path, sizeof(executable_path))) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "executable_path_failed",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      NULL,
      "解析可执行文件路径失败。",
      "Failed to resolve executable path.",
      NULL);
    img2bin_emit_error_json(&runtime_error);
    return runtime_error.exit_code;
  }

  if (!img2bin_dirname(executable_path, executable_directory, sizeof(executable_directory))) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "executable_directory_failed",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      executable_path,
      "解析可执行文件目录失败。",
      "Failed to resolve executable directory.",
      executable_path);
    img2bin_emit_error_json(&runtime_error);
    return runtime_error.exit_code;
  }

  if (options.input_path != NULL) {
    if (strlen(options.input_path) + 1 > sizeof(input_path)) {
      img2bin_runtime_error_t runtime_error;

      img2bin_runtime_error_set(
        &runtime_error,
        "input_path_too_long",
        IMG2BIN_APP_EXIT_INPUT_ERROR,
        "scan",
        options.input_path,
        "输入路径过长。",
        "Input path is too long.",
        options.input_path);
      img2bin_emit_error_json(&runtime_error);
      return runtime_error.exit_code;
    }
    strcpy(input_path, options.input_path);
  } else if (!img2bin_resolve_default_path(executable_directory, "input", input_path, sizeof(input_path))) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "default_input_path_failed",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      executable_directory,
      "生成默认输入路径失败。",
      "Failed to resolve default input path.",
      executable_directory);
    img2bin_emit_error_json(&runtime_error);
    return runtime_error.exit_code;
  }

  using_default_input = options.input_path == NULL && options.positional_input_count == 0;
  write_manifest = img2bin_should_write_manifest(&options);

  if (options.output_path != NULL) {
    if (strlen(options.output_path) + 1 > sizeof(output_path)) {
      img2bin_runtime_error_t runtime_error;

      img2bin_runtime_error_set(
        &runtime_error,
        "output_path_too_long",
        IMG2BIN_APP_EXIT_WRITE_ERROR,
        "write",
        options.output_path,
        "输出路径过长。",
        "Output path is too long.",
        options.output_path);
      img2bin_emit_error_json(&runtime_error);
      return runtime_error.exit_code;
    }
    strcpy(output_path, options.output_path);
  } else if (!img2bin_resolve_default_path(executable_directory, "output", output_path, sizeof(output_path))) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "default_output_path_failed",
      IMG2BIN_APP_EXIT_INTERNAL_ERROR,
      "internal",
      executable_directory,
      "生成默认输出路径失败。",
      "Failed to resolve default output path.",
      executable_directory);
    img2bin_emit_error_json(&runtime_error);
    return runtime_error.exit_code;
  }

  if (using_default_input) {
    img2bin_runtime_error_t runtime_error;
    int prepare_result = img2bin_prepare_default_directories(input_path, output_path, &runtime_error);

    if (prepare_result < 0) {
      img2bin_emit_error_json(&runtime_error);
      return runtime_error.exit_code;
    }
    if (prepare_result > 0) {
      return IMG2BIN_APP_EXIT_SUCCESS;
    }
  } else if (!img2bin_make_dirs(output_path, cli_error, sizeof(cli_error))) {
    img2bin_runtime_error_t runtime_error;

    img2bin_runtime_error_set(
      &runtime_error,
      "output_directory_create_failed",
      IMG2BIN_APP_EXIT_WRITE_ERROR,
      "write",
      output_path,
      "创建输出目录失败。",
      "Failed to create output directory.",
      cli_error);
    img2bin_emit_error_json(&runtime_error);
    return runtime_error.exit_code;
  }

  return img2bin_process_run_inputs(tool, &options, input_path, output_path, write_manifest);
}

int img2bin_tool_run(const img2bin_tool_descriptor_t *tool, int argc, const char *const *argv)
{
  return img2bin_tool_run_with_executable_path(tool, argc, argv, NULL);
}
