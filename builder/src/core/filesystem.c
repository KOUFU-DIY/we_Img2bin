#include "filesystem.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

static char img2bin_path_separator(void)
{
#ifdef _WIN32
  return '\\';
#else
  return '/';
#endif
}

static int img2bin_is_separator(char value)
{
  return value == '/' || value == '\\';
}

#ifdef _WIN32
char *img2bin_wide_to_utf8_alloc(const wchar_t *value)
{
  int size = 0;
  char *buffer = NULL;

  if (value == NULL) {
    return NULL;
  }

  size = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
  if (size <= 0) {
    return NULL;
  }

  buffer = (char *)malloc((size_t)size);
  if (buffer == NULL) {
    return NULL;
  }

  if (WideCharToMultiByte(CP_UTF8, 0, value, -1, buffer, size, NULL, NULL) <= 0) {
    free(buffer);
    return NULL;
  }

  return buffer;
}

int img2bin_utf8_to_wide_alloc(const char *value, wchar_t **out_value)
{
  int size = 0;

  if (value == NULL || out_value == NULL) {
    return 0;
  }

  *out_value = NULL;
  size = MultiByteToWideChar(CP_UTF8, 0, value, -1, NULL, 0);
  if (size <= 0) {
    return 0;
  }

  *out_value = (wchar_t *)malloc((size_t)size * sizeof(wchar_t));
  if (*out_value == NULL) {
    return 0;
  }

  if (MultiByteToWideChar(CP_UTF8, 0, value, -1, *out_value, size) <= 0) {
    free(*out_value);
    *out_value = NULL;
    return 0;
  }

  return 1;
}
#endif

int img2bin_get_executable_path(char *buffer, size_t buffer_size)
{
#ifdef _WIN32
  wchar_t wide_buffer[IMG2BIN_PATH_CAPACITY];
  char *utf8_path = NULL;
  DWORD length = 0;

  if (buffer == NULL || buffer_size == 0) {
    return 0;
  }

  length = GetModuleFileNameW(NULL, wide_buffer, (DWORD)IMG2BIN_PATH_CAPACITY);
  if (length == 0 || length >= IMG2BIN_PATH_CAPACITY) {
    return 0;
  }

  utf8_path = img2bin_wide_to_utf8_alloc(wide_buffer);
  if (utf8_path == NULL) {
    return 0;
  }

  if (strlen(utf8_path) + 1 > buffer_size) {
    free(utf8_path);
    return 0;
  }

  strcpy(buffer, utf8_path);
  free(utf8_path);
  return 1;
#else
  char path_buffer[IMG2BIN_PATH_CAPACITY];
  ssize_t length = 0;

  if (buffer == NULL || buffer_size == 0) {
    return 0;
  }

#ifdef __APPLE__
  {
    uint32_t size = (uint32_t)sizeof(path_buffer);
    if (_NSGetExecutablePath(path_buffer, &size) != 0) {
      return 0;
    }
  }
  length = (ssize_t)strlen(path_buffer);
#else
  length = readlink("/proc/self/exe", path_buffer, sizeof(path_buffer) - 1);
  if (length < 0) {
    return 0;
  }
  path_buffer[length] = '\0';
#endif

  if ((size_t)length + 1 > buffer_size) {
    return 0;
  }

  strcpy(buffer, path_buffer);
  return 1;
#endif
}

int img2bin_dirname(const char *path, char *buffer, size_t buffer_size)
{
  const char *last_separator = NULL;
  const char *cursor = NULL;
  size_t length = 0;

  if (path == NULL || buffer == NULL || buffer_size == 0) {
    return 0;
  }

  for (cursor = path; *cursor != '\0'; ++cursor) {
    if (img2bin_is_separator(*cursor)) {
      last_separator = cursor;
    }
  }

  if (last_separator == NULL) {
    if (buffer_size < 2) {
      return 0;
    }
    strcpy(buffer, ".");
    return 1;
  }

  if (last_separator == path) {
    if (buffer_size < 2) {
      return 0;
    }
    strcpy(buffer, "/");
    return 1;
  }

  length = (size_t)(last_separator - path);
  if (length == 2 && path[1] == ':') {
    ++length;
  }

  if (length + 1 > buffer_size) {
    return 0;
  }

  memcpy(buffer, path, length);
  buffer[length] = '\0';
  return 1;
}

int img2bin_path_join(const char *lhs, const char *rhs, char *buffer, size_t buffer_size)
{
  size_t left_length = 0;
  char separator = img2bin_path_separator();

  if (lhs == NULL || rhs == NULL || buffer == NULL || buffer_size == 0) {
    return 0;
  }

  left_length = strlen(lhs);
  if (left_length == 0) {
    if (strlen(rhs) + 1 > buffer_size) {
      return 0;
    }
    strcpy(buffer, rhs);
    return 1;
  }

  if (left_length + strlen(rhs) + 2 > buffer_size) {
    return 0;
  }

  strcpy(buffer, lhs);
  if (!img2bin_is_separator(buffer[left_length - 1])) {
    buffer[left_length] = separator;
    buffer[left_length + 1] = '\0';
  }
  strcat(buffer, rhs);
  return 1;
}

int img2bin_path_basename_stem(const char *path, char *buffer, size_t buffer_size)
{
  const char *name_start = path;
  const char *last_dot = NULL;
  const char *cursor = NULL;
  size_t length = 0;

  if (path == NULL || buffer == NULL || buffer_size == 0) {
    return 0;
  }

  for (cursor = path; *cursor != '\0'; ++cursor) {
    if (img2bin_is_separator(*cursor)) {
      name_start = cursor + 1;
      last_dot = NULL;
    } else if (*cursor == '.') {
      last_dot = cursor;
    }
  }

  if (last_dot == NULL || last_dot == name_start) {
    length = strlen(name_start);
  } else {
    length = (size_t)(last_dot - name_start);
  }

  if (length + 1 > buffer_size) {
    return 0;
  }

  memcpy(buffer, name_start, length);
  buffer[length] = '\0';
  return 1;
}

int img2bin_is_directory(const char *path)
{
#ifdef _WIN32
  DWORD attributes = 0;
  wchar_t *wide_path = NULL;
  int result = 0;

  if (!img2bin_utf8_to_wide_alloc(path, &wide_path)) {
    return 0;
  }

  attributes = GetFileAttributesW(wide_path);
  if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    result = 1;
  }

  free(wide_path);
  return result;
#else
  struct stat file_stat;
  if (stat(path, &file_stat) != 0) {
    return 0;
  }
  return S_ISDIR(file_stat.st_mode) ? 1 : 0;
#endif
}

int img2bin_is_regular_file(const char *path)
{
#ifdef _WIN32
  DWORD attributes = 0;
  wchar_t *wide_path = NULL;
  int result = 0;

  if (!img2bin_utf8_to_wide_alloc(path, &wide_path)) {
    return 0;
  }

  attributes = GetFileAttributesW(wide_path);
  if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    result = 1;
  }

  free(wide_path);
  return result;
#else
  struct stat file_stat;
  if (stat(path, &file_stat) != 0) {
    return 0;
  }
  return S_ISREG(file_stat.st_mode) ? 1 : 0;
#endif
}

static int img2bin_create_directory_single(const char *path)
{
#ifdef _WIN32
  wchar_t *wide_path = NULL;
  int result = 0;

  if (!img2bin_utf8_to_wide_alloc(path, &wide_path)) {
    return 0;
  }

  result = CreateDirectoryW(wide_path, NULL) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
  free(wide_path);
  return result;
#else
  if (mkdir(path, 0777) == 0 || errno == EEXIST) {
    return 1;
  }
  return 0;
#endif
}

int img2bin_make_dirs(const char *path, char *error_buffer, size_t error_buffer_size)
{
  char partial[IMG2BIN_PATH_CAPACITY];
  size_t index = 0;
  size_t length = 0;

  if (path == NULL || *path == '\0') {
    img2bin_set_error(error_buffer, error_buffer_size, "Cannot create an empty directory path.");
    return 0;
  }

  length = strlen(path);
  if (length + 1 > sizeof(partial)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Directory path is too long: %s", path);
    return 0;
  }

  memset(partial, 0, sizeof(partial));

  if (length >= 2 && path[1] == ':') {
    partial[0] = path[0];
    partial[1] = path[1];
    index = 2;
    if (img2bin_is_separator(path[2])) {
      partial[2] = path[2];
      index = 3;
    }
  } else if (img2bin_is_separator(path[0])) {
    partial[0] = path[0];
    index = 1;
  }

  for (; index < length; ++index) {
    partial[index] = path[index];
    partial[index + 1] = '\0';

    if (img2bin_is_separator(path[index]) && index > 0) {
      if (!img2bin_create_directory_single(partial) && !img2bin_is_directory(partial)) {
        img2bin_set_error(error_buffer, error_buffer_size, "Failed to create directory: %s", partial);
        return 0;
      }
    }
  }

  if (!img2bin_create_directory_single(partial) && !img2bin_is_directory(partial)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to create directory: %s", partial);
    return 0;
  }

  return 1;
}

int img2bin_read_file(const char *path, unsigned char **buffer, size_t *buffer_size, char *error_buffer, size_t error_buffer_size)
{
  FILE *file = NULL;
  long size = 0;
  unsigned char *data = NULL;

  if (path == NULL || buffer == NULL || buffer_size == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Invalid file read request.");
    return 0;
  }

#ifdef _WIN32
  {
    wchar_t *wide_path = NULL;
    if (!img2bin_utf8_to_wide_alloc(path, &wide_path)) {
      img2bin_set_error(error_buffer, error_buffer_size, "Failed to convert path to UTF-16: %s", path);
      return 0;
    }
    file = _wfopen(wide_path, L"rb");
    free(wide_path);
  }
#else
  file = fopen(path, "rb");
#endif

  if (file == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to open file: %s", path);
    return 0;
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to seek file: %s", path);
    return 0;
  }

  size = ftell(file);
  if (size < 0) {
    fclose(file);
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to determine file size: %s", path);
    return 0;
  }

  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to rewind file: %s", path);
    return 0;
  }

  data = (unsigned char *)malloc((size_t)size);
  if (data == NULL && size > 0) {
    fclose(file);
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while reading file: %s", path);
    return 0;
  }

  if (size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to read file: %s", path);
    return 0;
  }

  fclose(file);

  *buffer = data;
  *buffer_size = (size_t)size;
  return 1;
}

int img2bin_write_file(const char *path, const unsigned char *buffer, size_t buffer_size, char *error_buffer, size_t error_buffer_size)
{
  FILE *file = NULL;

  if (path == NULL || (buffer == NULL && buffer_size > 0)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Invalid file write request.");
    return 0;
  }

#ifdef _WIN32
  {
    wchar_t *wide_path = NULL;
    if (!img2bin_utf8_to_wide_alloc(path, &wide_path)) {
      img2bin_set_error(error_buffer, error_buffer_size, "Failed to convert path to UTF-16: %s", path);
      return 0;
    }
    file = _wfopen(wide_path, L"wb");
    free(wide_path);
  }
#else
  file = fopen(path, "wb");
#endif

  if (file == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to open output file: %s", path);
    return 0;
  }

  if (buffer_size > 0 && fwrite(buffer, 1, buffer_size, file) != buffer_size) {
    fclose(file);
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to write output file: %s", path);
    return 0;
  }

  fclose(file);
  return 1;
}

int img2bin_is_supported_image_path(const char *path)
{
  const char *dot = NULL;
  const char *cursor = NULL;

  if (path == NULL) {
    return 0;
  }

  for (cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '.') {
      dot = cursor;
    }
  }

  if (dot == NULL) {
    return 0;
  }

  return img2bin_stricmp(dot, ".png") == 0 ||
         img2bin_stricmp(dot, ".bmp") == 0 ||
         img2bin_stricmp(dot, ".jpg") == 0 ||
         img2bin_stricmp(dot, ".jpeg") == 0;
}

int img2bin_collect_supported_images(const char *directory, img2bin_string_list_t *paths, char *error_buffer, size_t error_buffer_size)
{
#ifdef _WIN32
  wchar_t *wide_directory = NULL;
  wchar_t search_pattern[IMG2BIN_PATH_CAPACITY];
  WIN32_FIND_DATAW find_data;
  HANDLE find_handle = INVALID_HANDLE_VALUE;
  char *entry_name = NULL;
  char joined_path[IMG2BIN_PATH_CAPACITY];

  if (directory == NULL || paths == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Directory scan request is invalid.");
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
    if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      continue;
    }

    entry_name = img2bin_wide_to_utf8_alloc(find_data.cFileName);
    if (entry_name == NULL) {
      FindClose(find_handle);
      free(wide_directory);
      img2bin_set_error(error_buffer, error_buffer_size, "Failed to convert entry name to UTF-8.");
      return 0;
    }

    if (img2bin_is_supported_image_path(entry_name)) {
      if (!img2bin_path_join(directory, entry_name, joined_path, sizeof(joined_path)) ||
          !img2bin_string_list_append(paths, joined_path)) {
        free(entry_name);
        FindClose(find_handle);
        free(wide_directory);
        img2bin_set_error(error_buffer, error_buffer_size, "Failed to store image path while scanning: %s", directory);
        return 0;
      }
    }

    free(entry_name);
  } while (FindNextFileW(find_handle, &find_data) != 0);

  FindClose(find_handle);
  free(wide_directory);
  img2bin_string_list_sort(paths);
  return 1;
#else
  DIR *handle = NULL;
  struct dirent *entry = NULL;
  char joined_path[IMG2BIN_PATH_CAPACITY];

  if (directory == NULL || paths == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Directory scan request is invalid.");
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
      img2bin_set_error(error_buffer, error_buffer_size, "Scanned image path is too long in: %s", directory);
      return 0;
    }

    if (img2bin_is_regular_file(joined_path) && img2bin_is_supported_image_path(entry->d_name)) {
      if (!img2bin_string_list_append(paths, joined_path)) {
        closedir(handle);
        img2bin_set_error(error_buffer, error_buffer_size, "Failed to store image path while scanning: %s", directory);
        return 0;
      }
    }
  }

  closedir(handle);
  img2bin_string_list_sort(paths);
  return 1;
#endif
}
