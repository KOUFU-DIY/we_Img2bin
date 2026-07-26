#include "pack_config.h"

#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "pack_json.h"
#include "pack_util.h"

void img2bin_pack_config_init(img2bin_pack_config_t *config)
{
  if (config == NULL) {
    return;
  }

  memset(config, 0, sizeof(*config));
  img2bin_pack_copy_string(config->formats, sizeof(config->formats), "rgb565");
  config->endianness = IMG2BIN_ENDIAN_BIG;
  img2bin_pack_copy_string(config->bg_color, sizeof(config->bg_color), "000000");
  config->index_interval = -1;
  config->codegen_enabled = 1;
  config->codegen_split = 0;
  img2bin_pack_copy_string(config->codegen_base_name, sizeof(config->codegen_base_name), "img_resources");
}

static int img2bin_pack_parse_endianness(const char *value, int *out_endianness)
{
  if (img2bin_stricmp(value, "big") == 0 || img2bin_stricmp(value, "be") == 0) {
    *out_endianness = IMG2BIN_ENDIAN_BIG;
    return 1;
  }
  if (img2bin_stricmp(value, "little") == 0 || img2bin_stricmp(value, "le") == 0) {
    *out_endianness = IMG2BIN_ENDIAN_LITTLE;
    return 1;
  }
  return 0;
}

static int img2bin_pack_read_formats_value(
  const img2bin_pack_json_value_t *value,
  char *buffer,
  size_t buffer_size,
  char *error_buffer,
  size_t error_buffer_size)
{
  size_t index = 0;
  size_t length = 0;
  const char *item_text = NULL;

  if (value == NULL) {
    return 1;
  }

  if (value->type == IMG2BIN_PACK_JSON_STRING) {
    if (value->string_value == NULL || value->string_value[0] == '\0') {
      img2bin_set_error(error_buffer, error_buffer_size, "Config formats value must not be empty.");
      return 0;
    }
    img2bin_pack_copy_string(buffer, buffer_size, value->string_value);
    return 1;
  }

  if (value->type != IMG2BIN_PACK_JSON_ARRAY || value->member_count == 0) {
    img2bin_set_error(error_buffer, error_buffer_size, "Config formats value must be a string or a non-empty array.");
    return 0;
  }

  buffer[0] = '\0';
  for (index = 0; index < value->member_count; ++index) {
    const img2bin_pack_json_value_t *item = value->member_values[index];
    if (item == NULL || item->type != IMG2BIN_PACK_JSON_STRING || item->string_value == NULL || item->string_value[0] == '\0') {
      img2bin_set_error(error_buffer, error_buffer_size, "Config formats array must contain strings.");
      return 0;
    }
    item_text = item->string_value;
    length = strlen(buffer);
    if (length + strlen(item_text) + 2 >= buffer_size) {
      img2bin_set_error(error_buffer, error_buffer_size, "Config formats list is too long.");
      return 0;
    }
    if (length > 0) {
      buffer[length] = ',';
      buffer[length + 1] = '\0';
    }
    strcat(buffer, item_text);
  }
  return 1;
}

static int img2bin_pack_read_index_interval(
  const img2bin_pack_json_value_t *object,
  const char *key,
  long *out_value,
  char *error_buffer,
  size_t error_buffer_size)
{
  const img2bin_pack_json_value_t *value = img2bin_pack_json_object_get(object, key);
  double number = 0.0;

  if (value == NULL) {
    return 1;
  }
  if (value->type != IMG2BIN_PACK_JSON_NUMBER) {
    img2bin_set_error(error_buffer, error_buffer_size, "Config index_interval must be a number.");
    return 0;
  }
  number = value->number_value;
  if (number < 1.0 || number > 1000000.0 || number != (double)(long)number) {
    img2bin_set_error(error_buffer, error_buffer_size, "Config index_interval must be a positive integer.");
    return 0;
  }
  *out_value = (long)number;
  return 1;
}

static int img2bin_pack_read_common_options(
  const img2bin_pack_json_value_t *object,
  char *formats,
  size_t formats_size,
  int *endianness,
  char *bg_color,
  size_t bg_color_size,
  long *index_interval,
  char *error_buffer,
  size_t error_buffer_size)
{
  const char *text = NULL;
  int parsed_endianness = 0;

  if (object == NULL) {
    return 1;
  }

  if (!img2bin_pack_read_formats_value(img2bin_pack_json_object_get(object, "formats"), formats, formats_size, error_buffer, error_buffer_size)) {
    return 0;
  }
  text = img2bin_pack_json_string_at(object, "format", NULL);
  if (text != NULL) {
    img2bin_pack_copy_string(formats, formats_size, text);
  }

  text = img2bin_pack_json_string_at(object, "endianness", NULL);
  if (text != NULL) {
    if (!img2bin_pack_parse_endianness(text, &parsed_endianness)) {
      img2bin_set_error(error_buffer, error_buffer_size, "Config endianness must be \"big\" or \"little\": %s", text);
      return 0;
    }
    *endianness = parsed_endianness;
  }

  text = img2bin_pack_json_string_at(object, "bg_color", NULL);
  if (text != NULL) {
    if (strlen(text) != 6) {
      img2bin_set_error(error_buffer, error_buffer_size, "Config bg_color must be RRGGBB: %s", text);
      return 0;
    }
    img2bin_pack_copy_string(bg_color, bg_color_size, text);
  }

  return img2bin_pack_read_index_interval(object, "index_interval", index_interval, error_buffer, error_buffer_size);
}

int img2bin_pack_config_load_file(
  img2bin_pack_config_t *config,
  const char *path,
  char *error_buffer,
  size_t error_buffer_size)
{
  unsigned char *raw_text = NULL;
  size_t raw_size = 0;
  char *text = NULL;
  img2bin_pack_json_value_t *root = NULL;
  const img2bin_pack_json_value_t *section = NULL;
  const img2bin_pack_json_value_t *folders = NULL;
  const img2bin_pack_json_value_t *rule_value = NULL;
  img2bin_pack_folder_rule_t *rule = NULL;
  const char *text_value = NULL;
  size_t index = 0;
  int ok = 0;

  if (config == NULL || path == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Config load request is invalid.");
    return 0;
  }

  if (!img2bin_read_file(path, &raw_text, &raw_size, error_buffer, error_buffer_size)) {
    return 0;
  }

  text = (char *)malloc(raw_size + 1);
  if (text == NULL) {
    free(raw_text);
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while reading config.");
    return 0;
  }
  if (raw_size > 0) {
    memcpy(text, raw_text, raw_size);
  }
  text[raw_size] = '\0';
  free(raw_text);

  if (raw_size >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
    memmove(text, text + 3, raw_size - 3 + 1);
  }

  root = img2bin_pack_json_parse(text, error_buffer, error_buffer_size);
  free(text);
  if (root == NULL) {
    return 0;
  }
  if (root->type != IMG2BIN_PACK_JSON_OBJECT) {
    img2bin_set_error(error_buffer, error_buffer_size, "Config root must be a JSON object: %s", path);
    goto cleanup;
  }

  text_value = img2bin_pack_json_string_at(root, "root", NULL);
  if (text_value != NULL) {
    img2bin_pack_copy_string(config->root, sizeof(config->root), text_value);
  }
  text_value = img2bin_pack_json_string_at(root, "output", NULL);
  if (text_value != NULL) {
    img2bin_pack_copy_string(config->output, sizeof(config->output), text_value);
  }
  text_value = img2bin_pack_json_string_at(root, "tools_dir", NULL);
  if (text_value != NULL) {
    img2bin_pack_copy_string(config->tools_dir, sizeof(config->tools_dir), text_value);
  }

  section = img2bin_pack_json_object_get(root, "defaults");
  if (section != NULL && !img2bin_pack_read_common_options(
        section,
        config->formats,
        sizeof(config->formats),
        &config->endianness,
        config->bg_color,
        sizeof(config->bg_color),
        &config->index_interval,
        error_buffer,
        error_buffer_size)) {
    goto cleanup;
  }

  section = img2bin_pack_json_object_get(root, "codegen");
  if (section != NULL) {
    config->codegen_enabled = img2bin_pack_json_bool_at(section, "enabled", config->codegen_enabled);
    text_value = img2bin_pack_json_string_at(section, "mode", NULL);
    if (text_value != NULL) {
      if (img2bin_stricmp(text_value, "combined") == 0) {
        config->codegen_split = 0;
      } else if (img2bin_stricmp(text_value, "split") == 0) {
        config->codegen_split = 1;
      } else {
        img2bin_set_error(error_buffer, error_buffer_size, "Config codegen.mode must be \"combined\" or \"split\": %s", text_value);
        goto cleanup;
      }
    }
    text_value = img2bin_pack_json_string_at(section, "base_name", NULL);
    if (text_value != NULL && text_value[0] != '\0') {
      img2bin_pack_copy_string(config->codegen_base_name, sizeof(config->codegen_base_name), text_value);
    }
  }

  folders = img2bin_pack_json_object_get(root, "folders");
  if (folders != NULL) {
    if (folders->type != IMG2BIN_PACK_JSON_OBJECT) {
      img2bin_set_error(error_buffer, error_buffer_size, "Config folders must be a JSON object.");
      goto cleanup;
    }

    for (index = 0; index < folders->member_count; ++index) {
      if (folders->member_keys[index] == NULL || folders->member_keys[index][0] == '\0') {
        continue;
      }

      rule = img2bin_pack_config_find_rule(config, folders->member_keys[index]);
      if (rule == NULL) {
        if (config->folder_rule_count >= IMG2BIN_PACK_MAX_FOLDER_RULES) {
          img2bin_set_error(error_buffer, error_buffer_size, "Config declares too many folders (max %d).", IMG2BIN_PACK_MAX_FOLDER_RULES);
          goto cleanup;
        }
        rule = &config->folder_rules[config->folder_rule_count];
        memset(rule, 0, sizeof(*rule));
        rule->endianness = -1;
        rule->index_interval = -1;
        img2bin_pack_copy_string(rule->folder_name, sizeof(rule->folder_name), folders->member_keys[index]);
        ++config->folder_rule_count;
      }

      rule_value = folders->member_values[index];
      if (rule_value == NULL || rule_value->type != IMG2BIN_PACK_JSON_OBJECT) {
        img2bin_set_error(error_buffer, error_buffer_size, "Config folder entry must be a JSON object: %s", rule->folder_name);
        goto cleanup;
      }

      text_value = img2bin_pack_json_string_at(rule_value, "tool", NULL);
      if (text_value != NULL) {
        img2bin_pack_copy_string(rule->tool, sizeof(rule->tool), text_value);
      }
      text_value = img2bin_pack_json_string_at(rule_value, "output", NULL);
      if (text_value != NULL) {
        img2bin_pack_copy_string(rule->output, sizeof(rule->output), text_value);
      }

      if (!img2bin_pack_read_common_options(
            rule_value,
            rule->formats,
            sizeof(rule->formats),
            &rule->endianness,
            rule->bg_color,
            sizeof(rule->bg_color),
            &rule->index_interval,
            error_buffer,
            error_buffer_size)) {
        goto cleanup;
      }
    }
  }

  ok = 1;

cleanup:
  img2bin_pack_json_free(root);
  return ok;
}

img2bin_pack_folder_rule_t *img2bin_pack_config_find_rule(img2bin_pack_config_t *config, const char *folder_name)
{
  size_t index = 0;

  if (config == NULL || folder_name == NULL) {
    return NULL;
  }
  for (index = 0; index < config->folder_rule_count; ++index) {
    if (img2bin_stricmp(config->folder_rules[index].folder_name, folder_name) == 0) {
      return &config->folder_rules[index];
    }
  }
  return NULL;
}
