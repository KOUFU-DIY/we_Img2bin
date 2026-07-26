#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void img2bin_set_error(char *buffer, size_t buffer_size, const char *format, ...)
{
  va_list args;

  if (buffer == NULL || buffer_size == 0) {
    return;
  }

  va_start(args, format);
  vsnprintf(buffer, buffer_size, format, args);
  va_end(args);
}

char *img2bin_strdup(const char *value)
{
  size_t length;
  char *copy;

  if (value == NULL) {
    return NULL;
  }

  length = strlen(value);
  copy = (char *)malloc(length + 1);
  if (copy == NULL) {
    return NULL;
  }

  memcpy(copy, value, length + 1);
  return copy;
}

char *img2bin_json_escape_alloc(const char *value)
{
  static const char hex_digits[] = "0123456789ABCDEF";
  size_t input_length = 0;
  size_t output_length = 0;
  size_t input_index = 0;
  size_t output_index = 0;
  char *escaped = NULL;

  if (value == NULL) {
    return img2bin_strdup("");
  }

  input_length = strlen(value);
  for (input_index = 0; input_index < input_length; ++input_index) {
    unsigned char ch = (unsigned char)value[input_index];

    switch (ch) {
      case '\\':
      case '"':
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
        output_length += 2;
        break;
      default:
        output_length += (ch < 0x20u) ? 6 : 1;
        break;
    }
  }

  escaped = (char *)malloc(output_length + 1);
  if (escaped == NULL) {
    return NULL;
  }

  for (input_index = 0; input_index < input_length; ++input_index) {
    unsigned char ch = (unsigned char)value[input_index];

    switch (ch) {
      case '\\':
        escaped[output_index++] = '\\';
        escaped[output_index++] = '\\';
        break;
      case '"':
        escaped[output_index++] = '\\';
        escaped[output_index++] = '"';
        break;
      case '\b':
        escaped[output_index++] = '\\';
        escaped[output_index++] = 'b';
        break;
      case '\f':
        escaped[output_index++] = '\\';
        escaped[output_index++] = 'f';
        break;
      case '\n':
        escaped[output_index++] = '\\';
        escaped[output_index++] = 'n';
        break;
      case '\r':
        escaped[output_index++] = '\\';
        escaped[output_index++] = 'r';
        break;
      case '\t':
        escaped[output_index++] = '\\';
        escaped[output_index++] = 't';
        break;
      default:
        if (ch < 0x20u) {
          escaped[output_index++] = '\\';
          escaped[output_index++] = 'u';
          escaped[output_index++] = '0';
          escaped[output_index++] = '0';
          escaped[output_index++] = hex_digits[(ch >> 4) & 0x0Fu];
          escaped[output_index++] = hex_digits[ch & 0x0Fu];
        } else {
          escaped[output_index++] = (char)ch;
        }
        break;
    }
  }

  escaped[output_index] = '\0';
  return escaped;
}

int img2bin_stricmp(const char *lhs, const char *rhs)
{
  unsigned char left;
  unsigned char right;

  if (lhs == NULL && rhs == NULL) {
    return 0;
  }
  if (lhs == NULL) {
    return -1;
  }
  if (rhs == NULL) {
    return 1;
  }

  while (*lhs != '\0' || *rhs != '\0') {
    left = (unsigned char)*lhs;
    right = (unsigned char)*rhs;

    if (left >= 'A' && left <= 'Z') {
      left = (unsigned char)(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = (unsigned char)(right - 'A' + 'a');
    }

    if (left != right) {
      return (left < right) ? -1 : 1;
    }

    if (*lhs != '\0') {
      ++lhs;
    }
    if (*rhs != '\0') {
      ++rhs;
    }
  }

  return 0;
}

int img2bin_string_list_append(img2bin_string_list_t *list, const char *value)
{
  char **new_items;

  if (list == NULL || value == NULL) {
    return 0;
  }

  if (list->count == list->capacity) {
    size_t new_capacity = (list->capacity == 0) ? 8 : list->capacity * 2;
    new_items = (char **)realloc(list->items, new_capacity * sizeof(char *));
    if (new_items == NULL) {
      return 0;
    }
    list->items = new_items;
    list->capacity = new_capacity;
  }

  list->items[list->count] = img2bin_strdup(value);
  if (list->items[list->count] == NULL) {
    return 0;
  }

  ++list->count;
  return 1;
}

static int img2bin_qsort_compare_strings(const void *lhs, const void *rhs)
{
  const char *const *left = (const char *const *)lhs;
  const char *const *right = (const char *const *)rhs;
  return img2bin_stricmp(*left, *right);
}

void img2bin_string_list_sort(img2bin_string_list_t *list)
{
  if (list == NULL || list->count < 2) {
    return;
  }

  qsort(list->items, list->count, sizeof(char *), img2bin_qsort_compare_strings);
}

void img2bin_string_list_free(img2bin_string_list_t *list)
{
  size_t index;

  if (list == NULL) {
    return;
  }

  for (index = 0; index < list->count; ++index) {
    free(list->items[index]);
  }

  free(list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}
