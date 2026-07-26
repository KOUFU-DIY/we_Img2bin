#include "pack_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

void img2bin_pack_buffer_init(img2bin_pack_buffer_t *buffer)
{
  if (buffer == NULL) {
    return;
  }
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
}

void img2bin_pack_buffer_free(img2bin_pack_buffer_t *buffer)
{
  if (buffer == NULL) {
    return;
  }
  free(buffer->data);
  buffer->data = NULL;
  buffer->length = 0;
  buffer->capacity = 0;
}

static int img2bin_pack_buffer_reserve(img2bin_pack_buffer_t *buffer, size_t needed_capacity)
{
  char *grown = NULL;
  size_t new_capacity = 0;

  if (buffer == NULL) {
    return 0;
  }
  if (needed_capacity <= buffer->capacity) {
    return 1;
  }

  new_capacity = buffer->capacity == 0 ? 256 : buffer->capacity;
  while (new_capacity < needed_capacity) {
    new_capacity *= 2;
  }

  grown = (char *)realloc(buffer->data, new_capacity);
  if (grown == NULL) {
    return 0;
  }

  buffer->data = grown;
  buffer->capacity = new_capacity;
  return 1;
}

int img2bin_pack_buffer_append(img2bin_pack_buffer_t *buffer, const char *data, size_t length)
{
  if (buffer == NULL || data == NULL) {
    return 0;
  }
  if (!img2bin_pack_buffer_reserve(buffer, buffer->length + length + 1)) {
    return 0;
  }
  if (length > 0) {
    memcpy(buffer->data + buffer->length, data, length);
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

int img2bin_pack_buffer_appendf(img2bin_pack_buffer_t *buffer, const char *format, ...)
{
  va_list args;
  int needed = 0;

  if (buffer == NULL || format == NULL) {
    return 0;
  }

  va_start(args, format);
  needed = vsnprintf(NULL, 0, format, args);
  va_end(args);
  if (needed < 0) {
    return 0;
  }

  if (!img2bin_pack_buffer_reserve(buffer, buffer->length + (size_t)needed + 1)) {
    return 0;
  }

  va_start(args, format);
  vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format, args);
  va_end(args);

  buffer->length += (size_t)needed;
  buffer->data[buffer->length] = '\0';
  return 1;
}

int img2bin_pack_buffer_append_json_string(img2bin_pack_buffer_t *buffer, const char *value)
{
  char *escaped = NULL;
  int ok = 0;

  if (buffer == NULL || value == NULL) {
    return 0;
  }

  escaped = img2bin_json_escape_alloc(value);
  if (escaped == NULL) {
    return 0;
  }

  ok = img2bin_pack_buffer_appendf(buffer, "\"%s\"", escaped);
  free(escaped);
  return ok;
}

#ifdef _WIN32
int img2bin_pack_list_directory(
  const char *directory,
  int include_files,
  int include_directories,
  img2bin_string_list_t *names,
  char *error_buffer,
  size_t error_buffer_size)
{
  wchar_t *wide_directory = NULL;
  wchar_t search_pattern[IMG2BIN_PATH_CAPACITY];
  WIN32_FIND_DATAW find_data;
  HANDLE find_handle = INVALID_HANDLE_VALUE;
  char *entry_name = NULL;
  int is_directory = 0;

  if (directory == NULL || names == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Directory listing request is invalid.");
    return 0;
  }

  if (!img2bin_utf8_to_wide_alloc(directory, &wide_directory)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to convert directory path: %s", directory);
    return 0;
  }

  if (wcslen(wide_directory) + 3 >= IMG2BIN_PATH_CAPACITY) {
    free(wide_directory);
    img2bin_set_error(error_buffer, error_buffer_size, "Directory path is too long: %s", directory);
    return 0;
  }

  swprintf(search_pattern, IMG2BIN_PATH_CAPACITY, L"%ls\\*", wide_directory);
  find_handle = FindFirstFileW(search_pattern, &find_data);
  if (find_handle == INVALID_HANDLE_VALUE) {
    free(wide_directory);
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to enumerate directory: %s", directory);
    return 0;
  }

  do {
    is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (is_directory && wcscmp(find_data.cFileName, L".") == 0) {
      continue;
    }
    if (is_directory && wcscmp(find_data.cFileName, L"..") == 0) {
      continue;
    }
    if (is_directory && !include_directories) {
      continue;
    }
    if (!is_directory && !include_files) {
      continue;
    }

    entry_name = img2bin_wide_to_utf8_alloc(find_data.cFileName);
    if (entry_name == NULL) {
      FindClose(find_handle);
      free(wide_directory);
      img2bin_set_error(error_buffer, error_buffer_size, "Failed to convert entry name to UTF-8.");
      return 0;
    }

    if (!img2bin_string_list_append(names, entry_name)) {
      free(entry_name);
      FindClose(find_handle);
      free(wide_directory);
      img2bin_set_error(error_buffer, error_buffer_size, "Failed to store entry name while listing: %s", directory);
      return 0;
    }

    free(entry_name);
  } while (FindNextFileW(find_handle, &find_data) != 0);

  FindClose(find_handle);
  free(wide_directory);
  img2bin_string_list_sort(names);
  return 1;
}
#else
int img2bin_pack_list_directory(
  const char *directory,
  int include_files,
  int include_directories,
  img2bin_string_list_t *names,
  char *error_buffer,
  size_t error_buffer_size)
{
  DIR *handle = NULL;
  struct dirent *entry = NULL;
  char joined_path[IMG2BIN_PATH_CAPACITY];
  int is_directory = 0;

  if (directory == NULL || names == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Directory listing request is invalid.");
    return 0;
  }

  handle = opendir(directory);
  if (handle == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to enumerate directory: %s", directory);
    return 0;
  }

  while ((entry = readdir(handle)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    if (!img2bin_path_join(directory, entry->d_name, joined_path, sizeof(joined_path))) {
      closedir(handle);
      img2bin_set_error(error_buffer, error_buffer_size, "Entry path is too long while listing: %s", directory);
      return 0;
    }

    is_directory = img2bin_is_directory(joined_path);
    if (is_directory && !include_directories) {
      continue;
    }
    if (!is_directory && !include_files) {
      continue;
    }

    if (!img2bin_string_list_append(names, entry->d_name)) {
      closedir(handle);
      img2bin_set_error(error_buffer, error_buffer_size, "Failed to store entry name while listing: %s", directory);
      return 0;
    }
  }

  closedir(handle);
  img2bin_string_list_sort(names);
  return 1;
}
#endif

int img2bin_pack_starts_with_ci(const char *value, const char *prefix)
{
  if (value == NULL || prefix == NULL) {
    return 0;
  }
  while (*prefix != '\0') {
    char left = *value;
    char right = *prefix;
    if (left >= 'A' && left <= 'Z') {
      left = (char)(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = (char)(right - 'A' + 'a');
    }
    if (left != right) {
      return 0;
    }
    ++value;
    ++prefix;
  }
  return 1;
}

int img2bin_pack_ends_with_ci(const char *value, const char *suffix)
{
  size_t value_length = 0;
  size_t suffix_length = 0;

  if (value == NULL || suffix == NULL) {
    return 0;
  }

  value_length = strlen(value);
  suffix_length = strlen(suffix);
  if (suffix_length > value_length) {
    return 0;
  }
  return img2bin_pack_starts_with_ci(value + (value_length - suffix_length), suffix) &&
         strlen(value + (value_length - suffix_length)) == suffix_length;
}

void img2bin_pack_copy_string(char *destination, size_t destination_size, const char *source)
{
  size_t length = 0;

  if (destination == NULL || destination_size == 0) {
    return;
  }
  if (source == NULL) {
    destination[0] = '\0';
    return;
  }

  length = strlen(source);
  if (length >= destination_size) {
    length = destination_size - 1;
  }
  if (length > 0) {
    memcpy(destination, source, length);
  }
  destination[length] = '\0';
}

void img2bin_pack_lower_string(char *value)
{
  if (value == NULL) {
    return;
  }
  while (*value != '\0') {
    if (*value >= 'A' && *value <= 'Z') {
      *value = (char)(*value - 'A' + 'a');
    }
    ++value;
  }
}

int img2bin_pack_is_absolute_path(const char *path)
{
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
#ifdef _WIN32
  if ((path[0] == '\\' && path[1] == '\\') || path[0] == '/') {
    return 1;
  }
  if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':') {
    return 1;
  }
  return path[0] == '\\';
#else
  return path[0] == '/';
#endif
}

int img2bin_pack_resolve_path(const char *base_directory, const char *path, char *buffer, size_t buffer_size)
{
  if (path == NULL || buffer == NULL || buffer_size == 0) {
    return 0;
  }
  if (img2bin_pack_is_absolute_path(path) || base_directory == NULL || base_directory[0] == '\0') {
    img2bin_pack_copy_string(buffer, buffer_size, path);
    return buffer[0] != '\0';
  }
  return img2bin_path_join(base_directory, path, buffer, buffer_size);
}

#ifdef _WIN32
int img2bin_pack_absolute_path(const char *path, char *buffer, size_t buffer_size)
{
  wchar_t *wide_path = NULL;
  wchar_t *wide_full = NULL;
  char *utf8_full = NULL;

  if (path == NULL || buffer == NULL || buffer_size == 0) {
    return 0;
  }

  if (!img2bin_utf8_to_wide_alloc(path, &wide_path)) {
    return 0;
  }

  wide_full = _wfullpath(NULL, wide_path, 0);
  free(wide_path);
  if (wide_full == NULL) {
    img2bin_pack_copy_string(buffer, buffer_size, path);
    return buffer[0] != '\0';
  }

  utf8_full = img2bin_wide_to_utf8_alloc(wide_full);
  free(wide_full);
  if (utf8_full == NULL) {
    return 0;
  }

  img2bin_pack_copy_string(buffer, buffer_size, utf8_full);
  free(utf8_full);
  return buffer[0] != '\0';
}
#else
int img2bin_pack_absolute_path(const char *path, char *buffer, size_t buffer_size)
{
  char resolved[IMG2BIN_PATH_CAPACITY];

  if (path == NULL || buffer == NULL || buffer_size == 0) {
    return 0;
  }

  if (realpath(path, resolved) != NULL) {
    img2bin_pack_copy_string(buffer, buffer_size, resolved);
  } else {
    img2bin_pack_copy_string(buffer, buffer_size, path);
  }
  return buffer[0] != '\0';
}
#endif
