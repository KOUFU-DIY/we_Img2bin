#ifndef IMG2BIN_FILESYSTEM_H
#define IMG2BIN_FILESYSTEM_H

#include <stddef.h>

#include "util.h"

#define IMG2BIN_PATH_CAPACITY 4096

int img2bin_get_executable_path(char *buffer, size_t buffer_size);
int img2bin_dirname(const char *path, char *buffer, size_t buffer_size);
int img2bin_path_join(const char *lhs, const char *rhs, char *buffer, size_t buffer_size);
int img2bin_path_basename_stem(const char *path, char *buffer, size_t buffer_size);
int img2bin_is_directory(const char *path);
int img2bin_is_regular_file(const char *path);
int img2bin_make_dirs(const char *path, char *error_buffer, size_t error_buffer_size);
int img2bin_read_file(const char *path, unsigned char **buffer, size_t *buffer_size, char *error_buffer, size_t error_buffer_size);
int img2bin_write_file(const char *path, const unsigned char *buffer, size_t buffer_size, char *error_buffer, size_t error_buffer_size);
int img2bin_collect_supported_images(const char *directory, img2bin_string_list_t *paths, char *error_buffer, size_t error_buffer_size);
int img2bin_is_supported_image_path(const char *path);

#ifdef _WIN32
#include <wchar.h>
char *img2bin_wide_to_utf8_alloc(const wchar_t *value);
int img2bin_utf8_to_wide_alloc(const char *value, wchar_t **out_value);
#endif

#endif
