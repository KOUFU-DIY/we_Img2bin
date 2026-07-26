#ifndef IMG2BIN_PACK_UTIL_H
#define IMG2BIN_PACK_UTIL_H

#include <stddef.h>

#include "util.h"

typedef struct img2bin_pack_buffer_s {
  char *data;
  size_t length;
  size_t capacity;
} img2bin_pack_buffer_t;

void img2bin_pack_buffer_init(img2bin_pack_buffer_t *buffer);
void img2bin_pack_buffer_free(img2bin_pack_buffer_t *buffer);
int img2bin_pack_buffer_append(img2bin_pack_buffer_t *buffer, const char *data, size_t length);
int img2bin_pack_buffer_appendf(img2bin_pack_buffer_t *buffer, const char *format, ...);
int img2bin_pack_buffer_append_json_string(img2bin_pack_buffer_t *buffer, const char *value);

int img2bin_pack_list_directory(
  const char *directory,
  int include_files,
  int include_directories,
  img2bin_string_list_t *names,
  char *error_buffer,
  size_t error_buffer_size);

int img2bin_pack_starts_with_ci(const char *value, const char *prefix);
int img2bin_pack_ends_with_ci(const char *value, const char *suffix);
void img2bin_pack_copy_string(char *destination, size_t destination_size, const char *source);
void img2bin_pack_lower_string(char *value);
int img2bin_pack_is_absolute_path(const char *path);
int img2bin_pack_resolve_path(const char *base_directory, const char *path, char *buffer, size_t buffer_size);
int img2bin_pack_absolute_path(const char *path, char *buffer, size_t buffer_size);

#endif
