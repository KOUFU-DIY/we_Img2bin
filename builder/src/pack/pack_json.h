#ifndef IMG2BIN_PACK_JSON_H
#define IMG2BIN_PACK_JSON_H

#include <stddef.h>

typedef enum img2bin_pack_json_type_e {
  IMG2BIN_PACK_JSON_NULL = 0,
  IMG2BIN_PACK_JSON_BOOL,
  IMG2BIN_PACK_JSON_NUMBER,
  IMG2BIN_PACK_JSON_STRING,
  IMG2BIN_PACK_JSON_ARRAY,
  IMG2BIN_PACK_JSON_OBJECT
} img2bin_pack_json_type_t;

typedef struct img2bin_pack_json_value_s img2bin_pack_json_value_t;

struct img2bin_pack_json_value_s {
  img2bin_pack_json_type_t type;
  int bool_value;
  double number_value;
  char *string_value;
  char **member_keys;
  img2bin_pack_json_value_t **member_values;
  size_t member_count;
  size_t member_capacity;
};

img2bin_pack_json_value_t *img2bin_pack_json_parse(const char *text, char *error_buffer, size_t error_buffer_size);
void img2bin_pack_json_free(img2bin_pack_json_value_t *value);

const img2bin_pack_json_value_t *img2bin_pack_json_object_get(const img2bin_pack_json_value_t *object, const char *key);
const img2bin_pack_json_value_t *img2bin_pack_json_path_get(const img2bin_pack_json_value_t *root, const char *dotted_path);
const char *img2bin_pack_json_string_at(const img2bin_pack_json_value_t *root, const char *dotted_path, const char *fallback);
int img2bin_pack_json_bool_at(const img2bin_pack_json_value_t *root, const char *dotted_path, int fallback);
double img2bin_pack_json_number_at(const img2bin_pack_json_value_t *root, const char *dotted_path, double fallback);

#endif
