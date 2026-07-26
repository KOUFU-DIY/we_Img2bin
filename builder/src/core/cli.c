#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "version.h"

static int img2bin_parse_hex_color(const char *value, img2bin_rgb_t *out_color)
{
  char *end = NULL;
  unsigned long parsed = 0;

  if (value == NULL || out_color == NULL || strlen(value) != 6) {
    return 0;
  }

  parsed = strtoul(value, &end, 16);
  if (end == NULL || *end != '\0') {
    return 0;
  }

  out_color->r = (unsigned char)((parsed >> 16) & 0xFFu);
  out_color->g = (unsigned char)((parsed >> 8) & 0xFFu);
  out_color->b = (unsigned char)(parsed & 0xFFu);
  return 1;
}

static int img2bin_parse_u32_value(const char *value, unsigned int *out_value)
{
  char *end = NULL;
  unsigned long parsed = 0;

  if (value == NULL || out_value == NULL || value[0] == '\0') {
    return 0;
  }

  parsed = strtoul(value, &end, 10);
  if (end == NULL || *end != '\0' || parsed == 0ul || parsed > 65535ul) {
    return 0;
  }

  *out_value = (unsigned int)parsed;
  return 1;
}

static int img2bin_add_format(img2bin_cli_options_t *options, img2bin_pixel_format_t format)
{
  size_t index;

  if (options == NULL) {
    return 0;
  }

  for (index = 0; index < options->format_count; ++index) {
    if (options->formats[index] == format) {
      return 1;
    }
  }

  if (options->format_count >= IMG2BIN_FMT_COUNT) {
    return 0;
  }

  options->formats[options->format_count] = format;
  ++options->format_count;
  return 1;
}

static void img2bin_set_all_formats(img2bin_cli_options_t *options)
{
  size_t index;

  options->format_count = 0;
  for (index = 0; index < IMG2BIN_FMT_COUNT; ++index) {
    options->formats[options->format_count] = (img2bin_pixel_format_t)index;
    ++options->format_count;
  }
}

static int img2bin_parse_formats_csv(const char *value, img2bin_cli_options_t *options, char *error_buffer, size_t error_buffer_size)
{
  char *copy = NULL;
  char *cursor = NULL;
  char *token = NULL;

  if (value == NULL || options == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Missing format list.");
    return 0;
  }

  if (img2bin_stricmp(value, "all") == 0) {
    img2bin_set_all_formats(options);
    return 1;
  }

  copy = img2bin_strdup(value);
  if (copy == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while parsing formats.");
    return 0;
  }

  options->format_count = 0;
  cursor = copy;
  while ((token = strtok(cursor, ",")) != NULL) {
    img2bin_pixel_format_t format;
    size_t start = 0;
    size_t end = strlen(token);

    cursor = NULL;
    while (token[start] == ' ' || token[start] == '\t') {
      ++start;
    }
    while (end > start && (token[end - 1] == ' ' || token[end - 1] == '\t')) {
      token[end - 1] = '\0';
      --end;
    }

    if (!img2bin_parse_format_name(token + start, &format)) {
      img2bin_set_error(error_buffer, error_buffer_size, "Unsupported format: %s", token + start);
      free(copy);
      return 0;
    }

    if (!img2bin_add_format(options, format)) {
      img2bin_set_error(error_buffer, error_buffer_size, "Too many formats requested.");
      free(copy);
      return 0;
    }
  }

  free(copy);

  if (options->format_count == 0) {
    img2bin_set_error(error_buffer, error_buffer_size, "No formats were specified.");
    return 0;
  }

  return 1;
}

void img2bin_cli_init(img2bin_cli_options_t *options)
{
  if (options == NULL) {
    return;
  }

  memset(options, 0, sizeof(*options));
  options->endianness = IMG2BIN_ENDIAN_BIG;
  options->background.r = 0;
  options->background.g = 0;
  options->background.b = 0;
  options->formats[0] = IMG2BIN_FMT_RGB565;
  options->format_count = 1;
}

int img2bin_parse_cli(int argc, const char *const *argv, img2bin_cli_options_t *options, char *error_buffer, size_t error_buffer_size)
{
  int index;
  int has_explicit_single = 0;
  int has_explicit_list = 0;

  if (options == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "CLI options are not initialized.");
    return 0;
  }

  img2bin_cli_init(options);

  for (index = 1; index < argc; ++index) {
    const char *arg = argv[index];

    if (strcmp(arg, "--help") == 0) {
      options->show_help = 1;
      return 1;
    }
    if (strcmp(arg, "--info") == 0) {
      options->show_info = 1;
      return 1;
    }
    if (strcmp(arg, "--list-formats") == 0) {
      options->list_formats = 1;
      return 1;
    }
    if (strcmp(arg, "--little-endian") == 0) {
      options->endianness = IMG2BIN_ENDIAN_LITTLE;
      continue;
    }
    if (strcmp(arg, "--input") == 0) {
      if (index + 1 >= argc) {
        img2bin_set_error(error_buffer, error_buffer_size, "--input requires a file or directory path.");
        return 0;
      }
      options->input_path = argv[++index];
      continue;
    }
    if (strcmp(arg, "--output") == 0) {
      if (index + 1 >= argc) {
        img2bin_set_error(error_buffer, error_buffer_size, "--output requires a directory path.");
        return 0;
      }
      options->output_path = argv[++index];
      continue;
    }
    if (strcmp(arg, "--bg-color") == 0) {
      if (index + 1 >= argc) {
        img2bin_set_error(error_buffer, error_buffer_size, "--bg-color requires an RRGGBB value.");
        return 0;
      }
      if (!img2bin_parse_hex_color(argv[++index], &options->background)) {
        img2bin_set_error(error_buffer, error_buffer_size, "Invalid --bg-color value. Expected RRGGBB.");
        return 0;
      }
      continue;
    }
    if (strcmp(arg, "--index-interval") == 0) {
      if (index + 1 >= argc) {
        img2bin_set_error(error_buffer, error_buffer_size, "--index-interval requires a positive integer value.");
        return 0;
      }
      if (!img2bin_parse_u32_value(argv[++index], &options->index_interval)) {
        img2bin_set_error(error_buffer, error_buffer_size, "Invalid --index-interval value. Expected 1..65535.");
        return 0;
      }
      options->index_interval_specified = 1;
      continue;
    }
    if (strcmp(arg, "--format") == 0) {
      img2bin_pixel_format_t format;

      if (index + 1 >= argc) {
        img2bin_set_error(error_buffer, error_buffer_size, "--format requires a value.");
        return 0;
      }
      if (has_explicit_list) {
        img2bin_set_error(error_buffer, error_buffer_size, "--format cannot be used together with --formats.");
        return 0;
      }
      if (!img2bin_parse_format_name(argv[++index], &format)) {
        img2bin_set_error(error_buffer, error_buffer_size, "Unsupported format: %s", argv[index]);
        return 0;
      }
      options->format_count = 0;
      options->formats[0] = format;
      options->format_count = 1;
      has_explicit_single = 1;
      continue;
    }
    if (strcmp(arg, "--formats") == 0) {
      if (index + 1 >= argc) {
        img2bin_set_error(error_buffer, error_buffer_size, "--formats requires a value.");
        return 0;
      }
      if (has_explicit_single) {
        img2bin_set_error(error_buffer, error_buffer_size, "--formats cannot be used together with --format.");
        return 0;
      }
      if (!img2bin_parse_formats_csv(argv[++index], options, error_buffer, error_buffer_size)) {
        return 0;
      }
      has_explicit_list = 1;
      continue;
    }
    if (arg[0] != '-') {
      if (options->positional_input_count >= IMG2BIN_CLI_MAX_POSITIONAL_INPUTS) {
        img2bin_set_error(error_buffer, error_buffer_size, "Too many positional input paths.");
        return 0;
      }
      options->positional_inputs[options->positional_input_count] = arg;
      ++options->positional_input_count;
      continue;
    }

    img2bin_set_error(error_buffer, error_buffer_size, "Unknown argument: %s", arg);
    return 0;
  }

  if (options->input_path != NULL && options->positional_input_count > 0) {
    img2bin_set_error(error_buffer, error_buffer_size, "--input cannot be used together with positional input paths.");
    return 0;
  }

  return 1;
}

void img2bin_print_help(void)
{
  printf("img2bin_raw %s - raw pixel format image converter\n", IMG2BIN_RAW_VERSION_TEXT);
  printf("Usage:\n");
  printf("  img2bin_raw [options] [path ...]\n\n");
  printf("Options:\n");
  printf("  --input <file-or-dir>      Input image file or directory.\n");
  printf("  --output <dir>             Output directory.\n");
  printf("  --format <name>            Output a single pixel format.\n");
  printf("  --formats <list|all>       Output multiple formats, comma separated or all.\n");
  printf("  --little-endian            Output little-endian pixels.\n");
  printf("  --bg-color <RRGGBB>        Background color for non-alpha target formats.\n");
  printf("  --info                     Print machine-readable tool metadata as JSON.\n");
  printf("  --list-formats             Print supported pixel formats.\n");
  printf("  --help                     Print this help text.\n\n");
  printf("Positional inputs:\n");
  printf("  One or more file/directory paths may be passed directly.\n");
  printf("  On Windows, dragging files or folders onto the exe uses this mode.\n");
  printf("  Batch runs write img2bin_raw-manifest.json into the output directory.\n\n");
  printf("Default behavior with no arguments:\n");
  printf("  Input directory  : <exe_dir>/input\n");
  printf("  Output directory : <exe_dir>/output\n");
  printf("  Format           : rgb565\n");
  printf("  Endianness       : big-endian\n");
  printf("  Missing input/output folders will be created automatically.\n");
}

void img2bin_print_formats(void)
{
  size_t count = 0;
  const img2bin_format_info_t *formats = img2bin_get_format_infos(&count);
  size_t index;

  printf("Supported formats:\n");
  for (index = 0; index < count; ++index) {
    printf("  %s\n", formats[index].name);
  }
}
