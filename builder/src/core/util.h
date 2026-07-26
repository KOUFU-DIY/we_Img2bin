#ifndef IMG2BIN_UTIL_H
#define IMG2BIN_UTIL_H

#include <stddef.h>

typedef struct img2bin_string_list_s {
  char **items;
  size_t count;
  size_t capacity;
} img2bin_string_list_t;

void img2bin_set_error(char *buffer, size_t buffer_size, const char *format, ...);
char *img2bin_strdup(const char *value);
char *img2bin_json_escape_alloc(const char *value);
int img2bin_stricmp(const char *lhs, const char *rhs);
int img2bin_string_list_append(img2bin_string_list_t *list, const char *value);
void img2bin_string_list_sort(img2bin_string_list_t *list);
void img2bin_string_list_free(img2bin_string_list_t *list);

#endif
