#include "pack_json.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

#define IMG2BIN_PACK_JSON_MAX_DEPTH 64

typedef struct img2bin_pack_json_parser_s {
  const char *cursor;
  int depth;
  char *error_buffer;
  size_t error_buffer_size;
} img2bin_pack_json_parser_t;

static img2bin_pack_json_value_t *img2bin_pack_json_parse_value(img2bin_pack_json_parser_t *parser);

static void img2bin_pack_json_set_error(img2bin_pack_json_parser_t *parser, const char *message)
{
  img2bin_set_error(parser->error_buffer, parser->error_buffer_size, "JSON parse error: %s", message);
}

static void img2bin_pack_json_skip_whitespace(img2bin_pack_json_parser_t *parser)
{
  while (*parser->cursor == ' ' || *parser->cursor == '\t' || *parser->cursor == '\r' || *parser->cursor == '\n') {
    ++parser->cursor;
  }
}

static img2bin_pack_json_value_t *img2bin_pack_json_new_value(img2bin_pack_json_type_t type)
{
  img2bin_pack_json_value_t *value = (img2bin_pack_json_value_t *)calloc(1, sizeof(img2bin_pack_json_value_t));
  if (value != NULL) {
    value->type = type;
  }
  return value;
}

void img2bin_pack_json_free(img2bin_pack_json_value_t *value)
{
  size_t index = 0;

  if (value == NULL) {
    return;
  }

  free(value->string_value);
  for (index = 0; index < value->member_count; ++index) {
    if (value->member_keys != NULL) {
      free(value->member_keys[index]);
    }
    img2bin_pack_json_free(value->member_values[index]);
  }
  free(value->member_keys);
  free(value->member_values);
  free(value);
}

static int img2bin_pack_json_append_member(img2bin_pack_json_value_t *container, char *key, img2bin_pack_json_value_t *member)
{
  size_t new_capacity = 0;
  char **grown_keys = NULL;
  img2bin_pack_json_value_t **grown_values = NULL;

  if (container->member_count == container->member_capacity) {
    new_capacity = container->member_capacity == 0 ? 8 : container->member_capacity * 2;
    grown_values = (img2bin_pack_json_value_t **)realloc(container->member_values, new_capacity * sizeof(*grown_values));
    if (grown_values == NULL) {
      return 0;
    }
    container->member_values = grown_values;

    if (container->type == IMG2BIN_PACK_JSON_OBJECT) {
      grown_keys = (char **)realloc(container->member_keys, new_capacity * sizeof(*grown_keys));
      if (grown_keys == NULL) {
        return 0;
      }
      container->member_keys = grown_keys;
    }
    container->member_capacity = new_capacity;
  }

  if (container->type == IMG2BIN_PACK_JSON_OBJECT) {
    container->member_keys[container->member_count] = key;
  }
  container->member_values[container->member_count] = member;
  ++container->member_count;
  return 1;
}

static int img2bin_pack_json_hex_digit(char digit)
{
  if (digit >= '0' && digit <= '9') {
    return digit - '0';
  }
  if (digit >= 'a' && digit <= 'f') {
    return digit - 'a' + 10;
  }
  if (digit >= 'A' && digit <= 'F') {
    return digit - 'A' + 10;
  }
  return -1;
}

static int img2bin_pack_json_parse_hex4(img2bin_pack_json_parser_t *parser, unsigned int *out_value)
{
  unsigned int value = 0;
  int index = 0;
  int digit = 0;

  for (index = 0; index < 4; ++index) {
    digit = img2bin_pack_json_hex_digit(parser->cursor[index]);
    if (digit < 0) {
      return 0;
    }
    value = (value << 4) | (unsigned int)digit;
  }

  parser->cursor += 4;
  *out_value = value;
  return 1;
}

static int img2bin_pack_json_append_utf8(char **buffer, size_t *length, size_t *capacity, unsigned long code_point)
{
  char bytes[4];
  size_t byte_count = 0;
  char *grown = NULL;

  if (code_point < 0x80) {
    bytes[0] = (char)code_point;
    byte_count = 1;
  } else if (code_point < 0x800) {
    bytes[0] = (char)(0xC0 | (code_point >> 6));
    bytes[1] = (char)(0x80 | (code_point & 0x3F));
    byte_count = 2;
  } else if (code_point < 0x10000) {
    bytes[0] = (char)(0xE0 | (code_point >> 12));
    bytes[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
    bytes[2] = (char)(0x80 | (code_point & 0x3F));
    byte_count = 3;
  } else {
    bytes[0] = (char)(0xF0 | (code_point >> 18));
    bytes[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
    bytes[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
    bytes[3] = (char)(0x80 | (code_point & 0x3F));
    byte_count = 4;
  }

  if (*length + byte_count + 1 > *capacity) {
    size_t new_capacity = *capacity == 0 ? 32 : *capacity * 2;
    while (new_capacity < *length + byte_count + 1) {
      new_capacity *= 2;
    }
    grown = (char *)realloc(*buffer, new_capacity);
    if (grown == NULL) {
      return 0;
    }
    *buffer = grown;
    *capacity = new_capacity;
  }

  memcpy(*buffer + *length, bytes, byte_count);
  *length += byte_count;
  return 1;
}

static int img2bin_pack_json_append_byte(char **buffer, size_t *length, size_t *capacity, unsigned char byte)
{
  char *grown = NULL;

  if (*length + 2 > *capacity) {
    size_t new_capacity = *capacity == 0 ? 32 : *capacity * 2;
    while (new_capacity < *length + 2) {
      new_capacity *= 2;
    }
    grown = (char *)realloc(*buffer, new_capacity);
    if (grown == NULL) {
      return 0;
    }
    *buffer = grown;
    *capacity = new_capacity;
  }

  (*buffer)[(*length)++] = (char)byte;
  return 1;
}

static char *img2bin_pack_json_parse_string_raw(img2bin_pack_json_parser_t *parser)
{
  char *buffer = NULL;
  size_t length = 0;
  size_t capacity = 0;
  unsigned int code_unit = 0;
  unsigned int low_unit = 0;
  unsigned long code_point = 0;

  if (*parser->cursor != '"') {
    img2bin_pack_json_set_error(parser, "expected string");
    return NULL;
  }
  ++parser->cursor;

  for (;;) {
    unsigned char current = (unsigned char)*parser->cursor;

    if (current == '\0') {
      img2bin_pack_json_set_error(parser, "unterminated string");
      free(buffer);
      return NULL;
    }
    if (current == '"') {
      ++parser->cursor;
      break;
    }
    if (current == '\\') {
      ++parser->cursor;
      switch (*parser->cursor) {
        case '"': code_point = '"'; ++parser->cursor; break;
        case '\\': code_point = '\\'; ++parser->cursor; break;
        case '/': code_point = '/'; ++parser->cursor; break;
        case 'b': code_point = '\b'; ++parser->cursor; break;
        case 'f': code_point = '\f'; ++parser->cursor; break;
        case 'n': code_point = '\n'; ++parser->cursor; break;
        case 'r': code_point = '\r'; ++parser->cursor; break;
        case 't': code_point = '\t'; ++parser->cursor; break;
        case 'u':
          ++parser->cursor;
          if (!img2bin_pack_json_parse_hex4(parser, &code_unit)) {
            img2bin_pack_json_set_error(parser, "invalid \\u escape");
            free(buffer);
            return NULL;
          }
          code_point = code_unit;
          if (code_unit >= 0xD800 && code_unit <= 0xDBFF && parser->cursor[0] == '\\' && parser->cursor[1] == 'u') {
            parser->cursor += 2;
            if (!img2bin_pack_json_parse_hex4(parser, &low_unit) || low_unit < 0xDC00 || low_unit > 0xDFFF) {
              img2bin_pack_json_set_error(parser, "invalid surrogate pair");
              free(buffer);
              return NULL;
            }
            code_point = 0x10000ul + (((unsigned long)code_unit - 0xD800ul) << 10) + ((unsigned long)low_unit - 0xDC00ul);
          }
          break;
        default:
          img2bin_pack_json_set_error(parser, "invalid escape character");
          free(buffer);
          return NULL;
      }
      if (!img2bin_pack_json_append_utf8(&buffer, &length, &capacity, code_point)) {
        img2bin_pack_json_set_error(parser, "out of memory");
        free(buffer);
        return NULL;
      }
      continue;
    }

    if (!img2bin_pack_json_append_byte(&buffer, &length, &capacity, current)) {
      img2bin_pack_json_set_error(parser, "out of memory");
      free(buffer);
      return NULL;
    }
    ++parser->cursor;
  }

  if (buffer == NULL) {
    buffer = (char *)malloc(1);
    if (buffer == NULL) {
      img2bin_pack_json_set_error(parser, "out of memory");
      return NULL;
    }
    length = 0;
  }
  buffer[length] = '\0';
  return buffer;
}

static img2bin_pack_json_value_t *img2bin_pack_json_parse_object(img2bin_pack_json_parser_t *parser)
{
  img2bin_pack_json_value_t *object = img2bin_pack_json_new_value(IMG2BIN_PACK_JSON_OBJECT);
  char *key = NULL;
  img2bin_pack_json_value_t *member = NULL;

  if (object == NULL) {
    img2bin_pack_json_set_error(parser, "out of memory");
    return NULL;
  }

  ++parser->cursor;
  img2bin_pack_json_skip_whitespace(parser);
  if (*parser->cursor == '}') {
    ++parser->cursor;
    return object;
  }

  for (;;) {
    img2bin_pack_json_skip_whitespace(parser);
    key = img2bin_pack_json_parse_string_raw(parser);
    if (key == NULL) {
      img2bin_pack_json_free(object);
      return NULL;
    }

    img2bin_pack_json_skip_whitespace(parser);
    if (*parser->cursor != ':') {
      img2bin_pack_json_set_error(parser, "expected ':' in object");
      free(key);
      img2bin_pack_json_free(object);
      return NULL;
    }
    ++parser->cursor;

    member = img2bin_pack_json_parse_value(parser);
    if (member == NULL) {
      free(key);
      img2bin_pack_json_free(object);
      return NULL;
    }

    if (!img2bin_pack_json_append_member(object, key, member)) {
      img2bin_pack_json_set_error(parser, "out of memory");
      free(key);
      img2bin_pack_json_free(member);
      img2bin_pack_json_free(object);
      return NULL;
    }

    img2bin_pack_json_skip_whitespace(parser);
    if (*parser->cursor == ',') {
      ++parser->cursor;
      continue;
    }
    if (*parser->cursor == '}') {
      ++parser->cursor;
      return object;
    }

    img2bin_pack_json_set_error(parser, "expected ',' or '}' in object");
    img2bin_pack_json_free(object);
    return NULL;
  }
}

static img2bin_pack_json_value_t *img2bin_pack_json_parse_array(img2bin_pack_json_parser_t *parser)
{
  img2bin_pack_json_value_t *array = img2bin_pack_json_new_value(IMG2BIN_PACK_JSON_ARRAY);
  img2bin_pack_json_value_t *item = NULL;

  if (array == NULL) {
    img2bin_pack_json_set_error(parser, "out of memory");
    return NULL;
  }

  ++parser->cursor;
  img2bin_pack_json_skip_whitespace(parser);
  if (*parser->cursor == ']') {
    ++parser->cursor;
    return array;
  }

  for (;;) {
    item = img2bin_pack_json_parse_value(parser);
    if (item == NULL) {
      img2bin_pack_json_free(array);
      return NULL;
    }

    if (!img2bin_pack_json_append_member(array, NULL, item)) {
      img2bin_pack_json_set_error(parser, "out of memory");
      img2bin_pack_json_free(item);
      img2bin_pack_json_free(array);
      return NULL;
    }

    img2bin_pack_json_skip_whitespace(parser);
    if (*parser->cursor == ',') {
      ++parser->cursor;
      continue;
    }
    if (*parser->cursor == ']') {
      ++parser->cursor;
      return array;
    }

    img2bin_pack_json_set_error(parser, "expected ',' or ']' in array");
    img2bin_pack_json_free(array);
    return NULL;
  }
}

static img2bin_pack_json_value_t *img2bin_pack_json_parse_value(img2bin_pack_json_parser_t *parser)
{
  img2bin_pack_json_value_t *value = NULL;
  char *string_value = NULL;
  char *number_end = NULL;
  double number_value = 0.0;

  if (parser->depth >= IMG2BIN_PACK_JSON_MAX_DEPTH) {
    img2bin_pack_json_set_error(parser, "maximum nesting depth exceeded");
    return NULL;
  }

  img2bin_pack_json_skip_whitespace(parser);

  if (*parser->cursor == '{') {
    ++parser->depth;
    value = img2bin_pack_json_parse_object(parser);
    --parser->depth;
    return value;
  }
  if (*parser->cursor == '[') {
    ++parser->depth;
    value = img2bin_pack_json_parse_array(parser);
    --parser->depth;
    return value;
  }
  if (*parser->cursor == '"') {
    string_value = img2bin_pack_json_parse_string_raw(parser);
    if (string_value == NULL) {
      return NULL;
    }
    value = img2bin_pack_json_new_value(IMG2BIN_PACK_JSON_STRING);
    if (value == NULL) {
      img2bin_pack_json_set_error(parser, "out of memory");
      free(string_value);
      return NULL;
    }
    value->string_value = string_value;
    return value;
  }
  if (strncmp(parser->cursor, "true", 4) == 0) {
    parser->cursor += 4;
    value = img2bin_pack_json_new_value(IMG2BIN_PACK_JSON_BOOL);
    if (value == NULL) {
      img2bin_pack_json_set_error(parser, "out of memory");
      return NULL;
    }
    value->bool_value = 1;
    return value;
  }
  if (strncmp(parser->cursor, "false", 5) == 0) {
    parser->cursor += 5;
    value = img2bin_pack_json_new_value(IMG2BIN_PACK_JSON_BOOL);
    if (value == NULL) {
      img2bin_pack_json_set_error(parser, "out of memory");
      return NULL;
    }
    return value;
  }
  if (strncmp(parser->cursor, "null", 4) == 0) {
    parser->cursor += 4;
    value = img2bin_pack_json_new_value(IMG2BIN_PACK_JSON_NULL);
    if (value == NULL) {
      img2bin_pack_json_set_error(parser, "out of memory");
    }
    return value;
  }

  number_value = strtod(parser->cursor, &number_end);
  if (number_end == parser->cursor) {
    img2bin_pack_json_set_error(parser, "unexpected token");
    return NULL;
  }

  parser->cursor = number_end;
  value = img2bin_pack_json_new_value(IMG2BIN_PACK_JSON_NUMBER);
  if (value == NULL) {
    img2bin_pack_json_set_error(parser, "out of memory");
    return NULL;
  }
  value->number_value = number_value;
  return value;
}

img2bin_pack_json_value_t *img2bin_pack_json_parse(const char *text, char *error_buffer, size_t error_buffer_size)
{
  img2bin_pack_json_parser_t parser;
  img2bin_pack_json_value_t *value = NULL;

  if (text == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "JSON parse error: input is null");
    return NULL;
  }

  parser.cursor = text;
  parser.depth = 0;
  parser.error_buffer = error_buffer;
  parser.error_buffer_size = error_buffer_size;

  value = img2bin_pack_json_parse_value(&parser);
  if (value == NULL) {
    return NULL;
  }

  img2bin_pack_json_skip_whitespace(&parser);
  if (*parser.cursor != '\0') {
    img2bin_set_error(error_buffer, error_buffer_size, "JSON parse error: trailing characters");
    img2bin_pack_json_free(value);
    return NULL;
  }

  return value;
}

const img2bin_pack_json_value_t *img2bin_pack_json_object_get(const img2bin_pack_json_value_t *object, const char *key)
{
  size_t index = 0;

  if (object == NULL || object->type != IMG2BIN_PACK_JSON_OBJECT || key == NULL) {
    return NULL;
  }
  for (index = 0; index < object->member_count; ++index) {
    if (object->member_keys[index] != NULL && strcmp(object->member_keys[index], key) == 0) {
      return object->member_values[index];
    }
  }
  return NULL;
}

const img2bin_pack_json_value_t *img2bin_pack_json_path_get(const img2bin_pack_json_value_t *root, const char *dotted_path)
{
  const img2bin_pack_json_value_t *current = root;
  char segment[128];
  size_t segment_length = 0;

  if (root == NULL || dotted_path == NULL) {
    return NULL;
  }

  while (*dotted_path != '\0' && current != NULL) {
    segment_length = 0;
    while (*dotted_path != '\0' && *dotted_path != '.' && segment_length + 1 < sizeof(segment)) {
      segment[segment_length++] = *dotted_path++;
    }
    segment[segment_length] = '\0';
    if (*dotted_path == '.') {
      ++dotted_path;
    }
    current = img2bin_pack_json_object_get(current, segment);
  }

  return current;
}

const char *img2bin_pack_json_string_at(const img2bin_pack_json_value_t *root, const char *dotted_path, const char *fallback)
{
  const img2bin_pack_json_value_t *value = img2bin_pack_json_path_get(root, dotted_path);
  if (value == NULL || value->type != IMG2BIN_PACK_JSON_STRING || value->string_value == NULL) {
    return fallback;
  }
  return value->string_value;
}

int img2bin_pack_json_bool_at(const img2bin_pack_json_value_t *root, const char *dotted_path, int fallback)
{
  const img2bin_pack_json_value_t *value = img2bin_pack_json_path_get(root, dotted_path);
  if (value == NULL || value->type != IMG2BIN_PACK_JSON_BOOL) {
    return fallback;
  }
  return value->bool_value;
}

double img2bin_pack_json_number_at(const img2bin_pack_json_value_t *root, const char *dotted_path, double fallback)
{
  const img2bin_pack_json_value_t *value = img2bin_pack_json_path_get(root, dotted_path);
  if (value == NULL || value->type != IMG2BIN_PACK_JSON_NUMBER) {
    return fallback;
  }
  return value->number_value;
}
