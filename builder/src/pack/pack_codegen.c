#include "pack_codegen.h"

#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "format.h"
#include "pack_util.h"
#include "version.h"

static const char *img2bin_pack_basename(const char *path)
{
  const char *basename = path;
  const char *cursor = path;

  while (*cursor != '\0') {
    if (*cursor == '/' || *cursor == '\\') {
      basename = cursor + 1;
    }
    ++cursor;
  }
  return basename;
}

static int img2bin_pack_parse_dimensions(const char *token, unsigned int *out_width, unsigned int *out_height)
{
  unsigned long width = 0;
  unsigned long height = 0;
  const char *cursor = token;

  if (*cursor < '0' || *cursor > '9') {
    return 0;
  }
  while (*cursor >= '0' && *cursor <= '9') {
    width = width * 10 + (unsigned long)(*cursor - '0');
    if (width > 0xFFFFFFul) {
      return 0;
    }
    ++cursor;
  }
  if (*cursor != 'x') {
    return 0;
  }
  ++cursor;
  if (*cursor < '0' || *cursor > '9') {
    return 0;
  }
  while (*cursor >= '0' && *cursor <= '9') {
    height = height * 10 + (unsigned long)(*cursor - '0');
    if (height > 0xFFFFFFul) {
      return 0;
    }
    ++cursor;
  }
  if (*cursor != '\0') {
    return 0;
  }

  *out_width = (unsigned int)width;
  *out_height = (unsigned int)height;
  return 1;
}

int img2bin_pack_parse_bin_name(const char *file_name, img2bin_pack_bin_info_t *out_info)
{
  char core[512];
  char *dimensions_token = NULL;
  char *endianness_token = NULL;
  char *algorithm_token = NULL;
  char *format_token = NULL;
  const char *basename = NULL;
  size_t core_length = 0;
  img2bin_pixel_format_t parsed_format = IMG2BIN_FMT_ARGB8888;

  if (file_name == NULL || out_info == NULL) {
    return 0;
  }

  basename = img2bin_pack_basename(file_name);
  if (!img2bin_pack_ends_with_ci(basename, ".bin")) {
    return 0;
  }

  core_length = strlen(basename) - 4;
  if (core_length == 0 || core_length >= sizeof(core)) {
    return 0;
  }
  memcpy(core, basename, core_length);
  core[core_length] = '\0';

  dimensions_token = strrchr(core, '_');
  if (dimensions_token == NULL) {
    return 0;
  }
  *dimensions_token = '\0';
  ++dimensions_token;
  if (!img2bin_pack_parse_dimensions(dimensions_token, &out_info->width, &out_info->height)) {
    return 0;
  }

  endianness_token = strrchr(core, '_');
  if (endianness_token == NULL) {
    return 0;
  }
  *endianness_token = '\0';
  ++endianness_token;
  img2bin_pack_lower_string(endianness_token);
  if (strcmp(endianness_token, "be") != 0 && strcmp(endianness_token, "le") != 0) {
    return 0;
  }

  algorithm_token = strrchr(core, '_');
  if (algorithm_token == NULL) {
    return 0;
  }
  *algorithm_token = '\0';
  ++algorithm_token;
  if (algorithm_token[0] == '\0') {
    return 0;
  }

  format_token = strrchr(core, '_');
  if (format_token == NULL) {
    return 0;
  }
  *format_token = '\0';
  ++format_token;
  if (!img2bin_parse_format_name(format_token, &parsed_format)) {
    return 0;
  }

  if (core[0] == '\0') {
    return 0;
  }

  img2bin_pack_copy_string(out_info->stem, sizeof(out_info->stem), core);
  img2bin_pack_copy_string(out_info->format_name, sizeof(out_info->format_name), format_token);
  img2bin_pack_lower_string(out_info->format_name);
  img2bin_pack_copy_string(out_info->algorithm, sizeof(out_info->algorithm), algorithm_token);
  img2bin_pack_lower_string(out_info->algorithm);
  img2bin_pack_copy_string(out_info->endianness, sizeof(out_info->endianness), endianness_token);
  return 1;
}

int img2bin_pack_sanitize_symbol(const char *name, char *buffer, size_t buffer_size)
{
  size_t length = 0;
  const char *cursor = NULL;
  char current = '\0';

  if (name == NULL || buffer == NULL || buffer_size < 8) {
    return 0;
  }

  if (name[0] >= '0' && name[0] <= '9') {
    img2bin_pack_copy_string(buffer, buffer_size, "img_");
    length = 4;
  }

  for (cursor = name; *cursor != '\0'; ++cursor) {
    if (length + 2 > buffer_size) {
      break;
    }
    current = *cursor;
    if ((current >= 'a' && current <= 'z') || (current >= 'A' && current <= 'Z') || (current >= '0' && current <= '9')) {
      buffer[length++] = current;
    } else {
      buffer[length++] = '_';
    }
  }

  if (length == 0) {
    img2bin_pack_copy_string(buffer, buffer_size, "img");
    return 1;
  }

  buffer[length] = '\0';
  return 1;
}

void img2bin_pack_codegen_options_init(img2bin_pack_codegen_options_t *options)
{
  if (options == NULL) {
    return;
  }
  options->split = 0;
  options->base_name = "img_resources";
  options->bytes_per_line = 12;
}

static void img2bin_pack_symbol_to_upper(const char *symbol, char *buffer, size_t buffer_size)
{
  size_t index = 0;

  while (symbol[index] != '\0' && index + 1 < buffer_size) {
    if (symbol[index] >= 'a' && symbol[index] <= 'z') {
      buffer[index] = (char)(symbol[index] - 'a' + 'A');
    } else {
      buffer[index] = symbol[index];
    }
    ++index;
  }
  buffer[index] = '\0';
}

static int img2bin_pack_symbol_is_used(const img2bin_string_list_t *used_symbols, const char *symbol)
{
  size_t index = 0;

  for (index = 0; index < used_symbols->count; ++index) {
    if (strcmp(used_symbols->items[index], symbol) == 0) {
      return 1;
    }
  }
  return 0;
}

static int img2bin_pack_make_unique_symbol(
  img2bin_string_list_t *used_symbols,
  const char *file_name,
  char *buffer,
  size_t buffer_size)
{
  char base_symbol[512];
  char stem[512];
  size_t suffix = 2;
  size_t stem_length = 0;
  const char *basename = img2bin_pack_basename(file_name);

  img2bin_pack_copy_string(stem, sizeof(stem), basename);
  stem_length = strlen(stem);
  if (stem_length > 4 && img2bin_pack_ends_with_ci(stem, ".bin")) {
    stem[stem_length - 4] = '\0';
  }

  if (!img2bin_pack_sanitize_symbol(stem, base_symbol, sizeof(base_symbol))) {
    return 0;
  }

  img2bin_pack_copy_string(buffer, buffer_size, base_symbol);
  while (img2bin_pack_symbol_is_used(used_symbols, buffer)) {
    if (snprintf(buffer, buffer_size, "%s_%u", base_symbol, (unsigned int)suffix) < 0) {
      return 0;
    }
    ++suffix;
  }

  return img2bin_string_list_append(used_symbols, buffer);
}

static int img2bin_pack_append_bytes(
  img2bin_pack_buffer_t *source_buffer,
  const unsigned char *bytes,
  size_t byte_count,
  size_t bytes_per_line)
{
  size_t index = 0;
  int line_open = 0;

  for (index = 0; index < byte_count; ++index) {
    if (!line_open) {
      if (!img2bin_pack_buffer_appendf(source_buffer, "  ")) {
        return 0;
      }
      line_open = 1;
    }

    if (!img2bin_pack_buffer_appendf(source_buffer, "0x%02X", bytes[index])) {
      return 0;
    }

    if (index + 1 < byte_count) {
      if ((index + 1) % bytes_per_line == 0) {
        if (!img2bin_pack_buffer_appendf(source_buffer, ",\n")) {
          return 0;
        }
        line_open = 0;
      } else if (!img2bin_pack_buffer_appendf(source_buffer, ", ")) {
        return 0;
      }
    }
  }

  if (line_open && !img2bin_pack_buffer_appendf(source_buffer, "\n")) {
    return 0;
  }
  return 1;
}

static int img2bin_pack_write_text_file(
  const char *directory,
  const char *file_name,
  const img2bin_pack_buffer_t *buffer,
  img2bin_string_list_t *out_generated_files,
  char *error_buffer,
  size_t error_buffer_size)
{
  char path[IMG2BIN_PATH_CAPACITY];

  if (!img2bin_path_join(directory, file_name, path, sizeof(path))) {
    img2bin_set_error(error_buffer, error_buffer_size, "Generated file path is too long: %s", file_name);
    return 0;
  }
  if (!img2bin_write_file(path, (const unsigned char *)buffer->data, buffer->length, error_buffer, error_buffer_size)) {
    return 0;
  }
  if (out_generated_files != NULL && !img2bin_string_list_append(out_generated_files, path)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to record generated file: %s", file_name);
    return 0;
  }
  return 1;
}

static int img2bin_pack_emit_resource(
  img2bin_pack_buffer_t *header_buffer,
  img2bin_pack_buffer_t *source_buffer,
  const char *file_name,
  const char *symbol,
  const img2bin_pack_bin_info_t *info,
  const unsigned char *bytes,
  size_t byte_count,
  size_t bytes_per_line)
{
  char symbol_upper[512];

  img2bin_pack_symbol_to_upper(symbol, symbol_upper, sizeof(symbol_upper));

  if (!img2bin_pack_buffer_appendf(
        header_buffer,
        "/* %s */\n"
        "#define %s_WIDTH %uu\n"
        "#define %s_HEIGHT %uu\n"
        "#define %s_SIZE %uu\n"
        "extern const unsigned char %s[%u];\n\n",
        file_name,
        symbol_upper,
        info->width,
        symbol_upper,
        info->height,
        symbol_upper,
        (unsigned int)byte_count,
        symbol,
        (unsigned int)byte_count)) {
    return 0;
  }

  if (!img2bin_pack_buffer_appendf(
        source_buffer,
        "/* %s: format=%s algorithm=%s endianness=%s width=%u height=%u bytes=%u */\n"
        "const unsigned char %s[%u] = {\n",
        file_name,
        info->format_name,
        info->algorithm,
        info->endianness,
        info->width,
        info->height,
        (unsigned int)byte_count,
        symbol,
        (unsigned int)byte_count)) {
    return 0;
  }

  if (!img2bin_pack_append_bytes(source_buffer, bytes, byte_count, bytes_per_line)) {
    return 0;
  }

  return img2bin_pack_buffer_appendf(source_buffer, "};\n\n");
}

int img2bin_pack_generate_sources(
  const img2bin_string_list_t *bin_paths,
  const char *output_directory,
  const img2bin_pack_codegen_options_t *options,
  img2bin_string_list_t *out_generated_files,
  char *error_buffer,
  size_t error_buffer_size)
{
  img2bin_pack_codegen_options_t default_options;
  img2bin_pack_buffer_t header_buffer;
  img2bin_pack_buffer_t source_buffer;
  img2bin_string_list_t used_symbols;
  img2bin_pack_bin_info_t info;
  char symbol[512];
  char guard[600];
  char guard_symbol[512];
  char file_name[600];
  unsigned char *bytes = NULL;
  size_t byte_count = 0;
  size_t index = 0;
  size_t bytes_per_line = 0;
  const char *base_name = NULL;
  const char *bin_name = NULL;
  int ok = 0;

  if (bin_paths == NULL || output_directory == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Code generation request is invalid.");
    return 0;
  }

  img2bin_pack_codegen_options_init(&default_options);
  if (options == NULL) {
    options = &default_options;
  }
  base_name = options->base_name != NULL && options->base_name[0] != '\0' ? options->base_name : "img_resources";
  bytes_per_line = options->bytes_per_line > 0 ? options->bytes_per_line : 12;

  img2bin_pack_buffer_init(&header_buffer);
  img2bin_pack_buffer_init(&source_buffer);
  memset(&used_symbols, 0, sizeof(used_symbols));

  if (!options->split) {
    if (!img2bin_pack_sanitize_symbol(base_name, guard_symbol, sizeof(guard_symbol))) {
      img2bin_set_error(error_buffer, error_buffer_size, "Could not derive header guard from base name.");
      goto cleanup;
    }
    img2bin_pack_symbol_to_upper(guard_symbol, guard, sizeof(guard) - 3);
    strcat(guard, "_H");

    if (!img2bin_pack_buffer_appendf(
          &header_buffer,
          "/* Generated by img2bin_pack %s. Do not edit. */\n#ifndef IMG2BIN_PACK_%s\n#define IMG2BIN_PACK_%s\n\n",
          IMG2BIN_VERSION_TEXT,
          guard,
          guard) ||
        !img2bin_pack_buffer_appendf(
          &source_buffer,
          "/* Generated by img2bin_pack %s. Do not edit. */\n#include \"%s.h\"\n\n",
          IMG2BIN_VERSION_TEXT,
          base_name)) {
      img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while generating sources.");
      goto cleanup;
    }
  }

  for (index = 0; index < bin_paths->count; ++index) {
    bin_name = img2bin_pack_basename(bin_paths->items[index]);

    if (!img2bin_pack_parse_bin_name(bin_name, &info)) {
      continue;
    }

    if (!img2bin_read_file(bin_paths->items[index], &bytes, &byte_count, error_buffer, error_buffer_size)) {
      goto cleanup;
    }
    if (byte_count == 0) {
      free(bytes);
      bytes = NULL;
      continue;
    }

    if (!img2bin_pack_make_unique_symbol(&used_symbols, bin_name, symbol, sizeof(symbol))) {
      img2bin_set_error(error_buffer, error_buffer_size, "Could not derive symbol for: %s", bin_name);
      goto cleanup;
    }

    if (options->split) {
      img2bin_pack_buffer_free(&header_buffer);
      img2bin_pack_buffer_free(&source_buffer);
      img2bin_pack_buffer_init(&header_buffer);
      img2bin_pack_buffer_init(&source_buffer);

      img2bin_pack_symbol_to_upper(symbol, guard, sizeof(guard) - 3);
      strcat(guard, "_H");

      if (!img2bin_pack_buffer_appendf(
            &header_buffer,
            "/* Generated by img2bin_pack %s. Do not edit. */\n#ifndef IMG2BIN_PACK_%s\n#define IMG2BIN_PACK_%s\n\n",
            IMG2BIN_VERSION_TEXT,
            guard,
            guard) ||
          !img2bin_pack_buffer_appendf(
            &source_buffer,
            "/* Generated by img2bin_pack %s. Do not edit. */\n#include \"%s.h\"\n\n",
            IMG2BIN_VERSION_TEXT,
            symbol)) {
        img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while generating sources.");
        goto cleanup;
      }
    }

    if (!img2bin_pack_emit_resource(&header_buffer, &source_buffer, bin_name, symbol, &info, bytes, byte_count, bytes_per_line)) {
      img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while generating sources.");
      goto cleanup;
    }

    free(bytes);
    bytes = NULL;

    if (options->split) {
      if (!img2bin_pack_buffer_appendf(&header_buffer, "#endif\n")) {
        img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while generating sources.");
        goto cleanup;
      }

      if (snprintf(file_name, sizeof(file_name), "%s.h", symbol) < 0 ||
          !img2bin_pack_write_text_file(output_directory, file_name, &header_buffer, out_generated_files, error_buffer, error_buffer_size)) {
        goto cleanup;
      }
      if (snprintf(file_name, sizeof(file_name), "%s.c", symbol) < 0 ||
          !img2bin_pack_write_text_file(output_directory, file_name, &source_buffer, out_generated_files, error_buffer, error_buffer_size)) {
        goto cleanup;
      }
    }
  }

  if (!options->split) {
    if (!img2bin_pack_buffer_appendf(&header_buffer, "#endif\n")) {
      img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while generating sources.");
      goto cleanup;
    }

    if (snprintf(file_name, sizeof(file_name), "%s.h", base_name) < 0 ||
        !img2bin_pack_write_text_file(output_directory, file_name, &header_buffer, out_generated_files, error_buffer, error_buffer_size)) {
      goto cleanup;
    }
    if (snprintf(file_name, sizeof(file_name), "%s.c", base_name) < 0 ||
        !img2bin_pack_write_text_file(output_directory, file_name, &source_buffer, out_generated_files, error_buffer, error_buffer_size)) {
      goto cleanup;
    }
  }

  ok = 1;

cleanup:
  free(bytes);
  img2bin_pack_buffer_free(&header_buffer);
  img2bin_pack_buffer_free(&source_buffer);
  img2bin_string_list_free(&used_symbols);
  return ok;
}
