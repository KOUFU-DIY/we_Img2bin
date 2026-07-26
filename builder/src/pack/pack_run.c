#include "pack_run.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "format.h"
#include "pack_codegen.h"
#include "pack_config.h"
#include "pack_discovery.h"
#include "pack_json.h"
#include "pack_process.h"
#include "pack_util.h"
#include "version.h"

#define IMG2BIN_PACK_MAX_JOBS 128
#define IMG2BIN_PACK_EXIT_SUCCESS 0
#define IMG2BIN_PACK_EXIT_CLI_ERROR 1
#define IMG2BIN_PACK_EXIT_INPUT_ERROR 2
#define IMG2BIN_PACK_EXIT_INTERNAL_ERROR 5
#define IMG2BIN_PACK_EXIT_PARTIAL_FAILURE 6

typedef struct img2bin_pack_cli_s {
  const char *root;
  const char *config_path;
  const char *output;
  const char *tools_dir;
  const char *formats;
  int little_endian;
  const char *bg_color;
  long index_interval;
  int split;
  const char *base_name;
  int no_codegen;
  int show_help;
  int show_info;
} img2bin_pack_cli_t;

typedef struct img2bin_pack_job_s {
  char folder_name[256];
  char input_directory[IMG2BIN_PATH_CAPACITY];
  char output_directory[IMG2BIN_PATH_CAPACITY];
  const img2bin_pack_tool_t *tool;
  char requested_tool[64];
  size_t image_count;
  int executed;
  int exit_code;
  const char *status;
  char detail[512];
  img2bin_string_list_t outputs;
} img2bin_pack_job_t;

typedef struct img2bin_pack_context_s {
  img2bin_pack_config_t *config;
  img2bin_pack_tool_list_t *tools;
  img2bin_pack_job_t *jobs;
  size_t job_count;
  char exe_dir[IMG2BIN_PATH_CAPACITY];
  char root[IMG2BIN_PATH_CAPACITY];
  char output[IMG2BIN_PATH_CAPACITY];
  char tools_dir[IMG2BIN_PATH_CAPACITY];
  int codegen_enabled;
  int codegen_split;
  const char *codegen_base_name;
  img2bin_string_list_t generated_files;
} img2bin_pack_context_t;

static void img2bin_pack_emit_error(const char *code, int exit_code, const char *zh_cn, const char *en, const char *detail)
{
  char *escaped_detail = NULL;

  escaped_detail = img2bin_json_escape_alloc(detail != NULL ? detail : "");
  printf(
    "{\"error\":{\"code\":\"%s\",\"exit_code\":%d,\"message\":{\"zh_cn\":\"%s\",\"en\":\"%s\"},\"detail\":\"%s\"}}\n",
    code,
    exit_code,
    zh_cn,
    en,
    escaped_detail != NULL ? escaped_detail : "");
  free(escaped_detail);
}

static void img2bin_pack_print_help(void)
{
  printf("img2bin_pack %s - batch orchestrator for the img2bin tools\n", IMG2BIN_VERSION_TEXT);
  printf("\n");
  printf("Scans a workspace for input2<algorithm> folders, runs the matching\n");
  printf("img2bin_<algorithm> tool on each folder, then generates C sources from\n");
  printf("the resulting .bin files.\n");
  printf("\n");
  printf("Usage:\n");
  printf("  img2bin_pack [options]\n");
  printf("\n");
  printf("Options:\n");
  printf("  --root <dir>            Workspace root to scan (default: exe directory)\n");
  printf("  --config <file>         Config file (default: <root>/img2bin_pack.json,\n");
  printf("                          then <exe_dir>/img2bin_pack.json)\n");
  printf("  --output <dir>          Output directory (default: <root>/output)\n");
  printf("  --tools <dir>           Tools directory (default: <exe_dir>/tools, then <exe_dir>)\n");
  printf("  --format <name>         Default pixel format for all folders (default: rgb565)\n");
  printf("  --formats <list|all>    Default pixel format list for all folders\n");
  printf("  --little-endian         Default to little-endian output\n");
  printf("  --bg-color <RRGGBB>     Default background color (default: 000000)\n");
  printf("  --index-interval <n>    Default index interval for index-capable tools\n");
  printf("  --combined              Generate one .c/.h pair for all resources (default)\n");
  printf("  --split                 Generate one .c/.h pair per .bin file\n");
  printf("  --name <base>           Base name for combined output (default: img_resources)\n");
  printf("  --no-codegen            Only run the tools, do not generate .c/.h\n");
  printf("  --help                  Show this help\n");
  printf("  --info                  Print machine-readable tool metadata as JSON\n");
  printf("\n");
  printf("Folder convention:\n");
  printf("  <root>/input2raw       -> img2bin_raw\n");
  printf("  <root>/input2rle       -> img2bin_rle\n");
  printf("  <root>/input2indexqoi  -> img2bin_indexqoi (and so on)\n");
  printf("  Any folder name can be mapped to a tool in img2bin_pack.json.\n");
}

int img2bin_pack_get_info_json(char *buffer, size_t buffer_size)
{
  int written = snprintf(
    buffer,
    buffer_size,
    "{\n"
    "  \"schema_version\": \"%s\",\n"
    "  \"tool\": {\n"
    "    \"id\": \"img2bin_pack\",\n"
    "    \"kind\": \"batch_orchestrator\",\n"
    "    \"version\": \"%s\",\n"
    "    \"version_semver\": \"%s\"\n"
    "  },\n"
    "  \"gui\": {\n"
    "    \"display_name\": {\n"
    "      \"zh_cn\": \"\\u7edf\\u7b79\\u7ba1\\u7406\\u5668\",\n"
    "      \"en\": \"Batch Orchestrator\"\n"
    "    },\n"
    "    \"description\": {\n"
    "      \"zh_cn\": \"\\u626b\\u63cf input2<algo> \\u6587\\u4ef6\\u5939\\u5e76\\u8c03\\u7528\\u5bf9\\u5e94\\u5de5\\u5177\\uff0c\\u53ef\\u751f\\u6210 .c/.h\\u3002\",\n"
    "      \"en\": \"Scans input2<algo> folders, runs matching tools, and can emit .c/.h sources.\"\n"
    "    },\n"
    "    \"gui_category\": {\n"
    "      \"zh_cn\": \"\\u6279\\u5904\\u7406\",\n"
    "      \"en\": \"Batch\"\n"
    "    },\n"
    "    \"priority\": 5\n"
    "  },\n"
    "  \"capabilities\": {\n"
    "    \"folder_convention\": \"input2<algorithm_code>\",\n"
    "    \"config_file\": \"img2bin_pack.json\",\n"
    "    \"discovers_tools_via\": \"--info\",\n"
    "    \"supports_custom_folder_mapping\": true,\n"
    "    \"supports_custom_output\": true,\n"
    "    \"codegen_modes\": [\"combined\", \"split\"],\n"
    "    \"codegen_outputs\": [\"c\", \"h\"],\n"
    "    \"manifest_file\": \"img2bin_pack-manifest.json\",\n"
    "    \"query_flag\": \"--info\"\n"
    "  },\n"
    "  \"invocation\": {\n"
    "    \"style\": \"flag_cli\",\n"
    "    \"info_flag\": \"--info\",\n"
    "    \"help_flag\": \"--help\",\n"
    "    \"arguments\": [\n"
    "      { \"name\": \"root\", \"flag\": \"--root\", \"type\": \"path\" },\n"
    "      { \"name\": \"config\", \"flag\": \"--config\", \"type\": \"path\" },\n"
    "      { \"name\": \"output\", \"flag\": \"--output\", \"type\": \"path\" },\n"
    "      { \"name\": \"tools\", \"flag\": \"--tools\", \"type\": \"path\" },\n"
    "      { \"name\": \"format\", \"flag\": \"--format\", \"type\": \"pixel_format_name\" },\n"
    "      { \"name\": \"formats\", \"flag\": \"--formats\", \"type\": \"csv_or_keyword\" },\n"
    "      { \"name\": \"little_endian\", \"flag\": \"--little-endian\", \"type\": \"boolean_flag\" },\n"
    "      { \"name\": \"bg_color\", \"flag\": \"--bg-color\", \"type\": \"hex_rgb\" },\n"
    "      { \"name\": \"index_interval\", \"flag\": \"--index-interval\", \"type\": \"positive_integer\" },\n"
    "      { \"name\": \"combined\", \"flag\": \"--combined\", \"type\": \"boolean_flag\" },\n"
    "      { \"name\": \"split\", \"flag\": \"--split\", \"type\": \"boolean_flag\" },\n"
    "      { \"name\": \"name\", \"flag\": \"--name\", \"type\": \"identifier\" },\n"
    "      { \"name\": \"no_codegen\", \"flag\": \"--no-codegen\", \"type\": \"boolean_flag\" }\n"
    "    ]\n"
    "  },\n"
    "  \"exit_codes\": {\n"
    "    \"success\": 0,\n"
    "    \"cli_error\": 1,\n"
    "    \"input_error\": 2,\n"
    "    \"internal_error\": 5,\n"
    "    \"batch_partial_failure\": 6\n"
    "  }\n"
    "}\n",
    IMG2BIN_INFO_SCHEMA_VERSION,
    IMG2BIN_VERSION_TEXT,
    IMG2BIN_VERSION_SEMVER);

  return written > 0 && (size_t)written < buffer_size;
}

static int img2bin_pack_parse_cli(
  int argc,
  const char *const *argv,
  img2bin_pack_cli_t *cli,
  char *error_buffer,
  size_t error_buffer_size)
{
  int index = 0;
  const char *arg = NULL;
  char *number_end = NULL;
  long parsed = 0;

  memset(cli, 0, sizeof(*cli));
  cli->index_interval = -1;
  cli->split = -1;

  for (index = 1; index < argc; ++index) {
    arg = argv[index];

    if (strcmp(arg, "--help") == 0) {
      cli->show_help = 1;
      continue;
    }
    if (strcmp(arg, "--info") == 0) {
      cli->show_info = 1;
      continue;
    }
    if (strcmp(arg, "--little-endian") == 0) {
      cli->little_endian = 1;
      continue;
    }
    if (strcmp(arg, "--split") == 0) {
      cli->split = 1;
      continue;
    }
    if (strcmp(arg, "--combined") == 0) {
      cli->split = 0;
      continue;
    }
    if (strcmp(arg, "--no-codegen") == 0) {
      cli->no_codegen = 1;
      continue;
    }

    if (strcmp(arg, "--root") == 0 || strcmp(arg, "--config") == 0 || strcmp(arg, "--output") == 0 ||
        strcmp(arg, "--tools") == 0 || strcmp(arg, "--format") == 0 || strcmp(arg, "--formats") == 0 ||
        strcmp(arg, "--bg-color") == 0 || strcmp(arg, "--index-interval") == 0 || strcmp(arg, "--name") == 0) {
      if (index + 1 >= argc) {
        img2bin_set_error(error_buffer, error_buffer_size, "Missing value for %s.", arg);
        return 0;
      }
      ++index;

      if (strcmp(arg, "--root") == 0) {
        cli->root = argv[index];
      } else if (strcmp(arg, "--config") == 0) {
        cli->config_path = argv[index];
      } else if (strcmp(arg, "--output") == 0) {
        cli->output = argv[index];
      } else if (strcmp(arg, "--tools") == 0) {
        cli->tools_dir = argv[index];
      } else if (strcmp(arg, "--format") == 0 || strcmp(arg, "--formats") == 0) {
        cli->formats = argv[index];
      } else if (strcmp(arg, "--bg-color") == 0) {
        if (strlen(argv[index]) != 6) {
          img2bin_set_error(error_buffer, error_buffer_size, "--bg-color expects RRGGBB, got: %s", argv[index]);
          return 0;
        }
        cli->bg_color = argv[index];
      } else if (strcmp(arg, "--index-interval") == 0) {
        parsed = strtol(argv[index], &number_end, 10);
        if (number_end == argv[index] || *number_end != '\0' || parsed <= 0 || parsed > 1000000) {
          img2bin_set_error(error_buffer, error_buffer_size, "--index-interval expects a positive integer, got: %s", argv[index]);
          return 0;
        }
        cli->index_interval = parsed;
      } else {
        cli->base_name = argv[index];
      }
      continue;
    }

    img2bin_set_error(error_buffer, error_buffer_size, "Unknown argument: %s", arg);
    return 0;
  }

  return 1;
}

static img2bin_pack_job_t *img2bin_pack_add_job(img2bin_pack_context_t *context, const char *folder_name)
{
  img2bin_pack_job_t *job = NULL;

  if (context->job_count >= IMG2BIN_PACK_MAX_JOBS) {
    return NULL;
  }

  job = &context->jobs[context->job_count];
  memset(job, 0, sizeof(*job));
  img2bin_pack_copy_string(job->folder_name, sizeof(job->folder_name), folder_name);
  job->exit_code = -1;
  job->status = "pending";
  ++context->job_count;
  return job;
}

static img2bin_pack_job_t *img2bin_pack_find_job(img2bin_pack_context_t *context, const char *folder_name)
{
  size_t index = 0;

  for (index = 0; index < context->job_count; ++index) {
    if (img2bin_stricmp(context->jobs[index].folder_name, folder_name) == 0) {
      return &context->jobs[index];
    }
  }
  return NULL;
}

static void img2bin_pack_prepare_job(img2bin_pack_context_t *context, img2bin_pack_job_t *job)
{
  const img2bin_pack_folder_rule_t *rule = img2bin_pack_config_find_rule(context->config, job->folder_name);
  img2bin_string_list_t images;
  char error[512];

  memset(&images, 0, sizeof(images));

  if (!img2bin_path_join(context->root, job->folder_name, job->input_directory, sizeof(job->input_directory))) {
    job->status = "error";
    img2bin_pack_copy_string(job->detail, sizeof(job->detail), "Input path is too long.");
    return;
  }

  if (rule != NULL && rule->output[0] != '\0') {
    if (!img2bin_pack_resolve_path(context->root, rule->output, job->output_directory, sizeof(job->output_directory))) {
      job->status = "error";
      img2bin_pack_copy_string(job->detail, sizeof(job->detail), "Output path is too long.");
      return;
    }
  } else {
    img2bin_pack_copy_string(job->output_directory, sizeof(job->output_directory), context->output);
  }

  if (rule != NULL && rule->tool[0] != '\0') {
    img2bin_pack_copy_string(job->requested_tool, sizeof(job->requested_tool), rule->tool);
  } else if (img2bin_pack_starts_with_ci(job->folder_name, "input2") && job->folder_name[6] != '\0') {
    img2bin_pack_copy_string(job->requested_tool, sizeof(job->requested_tool), job->folder_name + 6);
    img2bin_pack_lower_string(job->requested_tool);
  }

  if (job->requested_tool[0] == '\0') {
    job->status = "no_tool";
    img2bin_pack_copy_string(job->detail, sizeof(job->detail), "Folder name does not identify an algorithm; map it with \"tool\" in img2bin_pack.json.");
    return;
  }

  job->tool = img2bin_pack_find_tool(context->tools, job->requested_tool);
  if (job->tool == NULL) {
    job->status = "no_tool";
    snprintf(job->detail, sizeof(job->detail), "No discovered tool provides \"%s\".", job->requested_tool);
    return;
  }

  if (!img2bin_is_directory(job->input_directory)) {
    job->status = "error";
    img2bin_pack_copy_string(job->detail, sizeof(job->detail), "Configured folder does not exist.");
    return;
  }

  if (!img2bin_collect_supported_images(job->input_directory, &images, error, sizeof(error))) {
    job->status = "error";
    img2bin_pack_copy_string(job->detail, sizeof(job->detail), error);
    img2bin_string_list_free(&images);
    return;
  }

  job->image_count = images.count;
  img2bin_string_list_free(&images);

  if (job->image_count == 0) {
    job->status = "skipped_empty";
    return;
  }

  job->status = "ready";
}

static void img2bin_pack_collect_job_outputs(img2bin_pack_job_t *job)
{
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char manifest_name[128];
  unsigned char *raw_text = NULL;
  size_t raw_size = 0;
  char *text = NULL;
  char error[256];
  img2bin_pack_json_value_t *manifest = NULL;
  const img2bin_pack_json_value_t *items = NULL;
  const img2bin_pack_json_value_t *item = NULL;
  const img2bin_pack_json_value_t *outputs = NULL;
  const img2bin_pack_json_value_t *output_entry = NULL;
  const char *status = NULL;
  const char *path = NULL;
  size_t item_index = 0;
  size_t output_index = 0;

  if (snprintf(manifest_name, sizeof(manifest_name), "%s-manifest.json", job->tool->tool_id) < 0) {
    return;
  }
  if (!img2bin_path_join(job->output_directory, manifest_name, manifest_path, sizeof(manifest_path))) {
    return;
  }
  if (!img2bin_read_file(manifest_path, &raw_text, &raw_size, error, sizeof(error))) {
    return;
  }

  text = (char *)malloc(raw_size + 1);
  if (text == NULL) {
    free(raw_text);
    return;
  }
  if (raw_size > 0) {
    memcpy(text, raw_text, raw_size);
  }
  text[raw_size] = '\0';
  free(raw_text);

  manifest = img2bin_pack_json_parse(text, error, sizeof(error));
  free(text);
  if (manifest == NULL) {
    return;
  }

  items = img2bin_pack_json_object_get(manifest, "items");
  if (items != NULL && items->type == IMG2BIN_PACK_JSON_ARRAY) {
    for (item_index = 0; item_index < items->member_count; ++item_index) {
      item = items->member_values[item_index];
      status = img2bin_pack_json_string_at(item, "status", "");
      if (strcmp(status, "success") != 0) {
        continue;
      }
      outputs = img2bin_pack_json_object_get(item, "outputs");
      if (outputs == NULL || outputs->type != IMG2BIN_PACK_JSON_ARRAY) {
        continue;
      }
      for (output_index = 0; output_index < outputs->member_count; ++output_index) {
        output_entry = outputs->member_values[output_index];
        path = img2bin_pack_json_string_at(output_entry, "path", NULL);
        if (path != NULL) {
          img2bin_string_list_append(&job->outputs, path);
        }
      }
    }
  }

  img2bin_pack_json_free(manifest);
}

static void img2bin_pack_execute_job(img2bin_pack_context_t *context, img2bin_pack_job_t *job)
{
  const img2bin_pack_folder_rule_t *rule = img2bin_pack_config_find_rule(context->config, job->folder_name);
  const char *arguments[16];
  size_t argument_count = 0;
  const char *formats = NULL;
  const char *bg_color = NULL;
  long index_interval = 0;
  int endianness = 0;
  char interval_text[32];
  char error[512];
  char *captured_output = NULL;
  size_t captured_length = 0;
  const char *captured_tail = NULL;
  int exit_code = -1;
  char make_dirs_error[512];

  if (!img2bin_make_dirs(job->output_directory, make_dirs_error, sizeof(make_dirs_error))) {
    job->status = "error";
    img2bin_pack_copy_string(job->detail, sizeof(job->detail), make_dirs_error);
    return;
  }

  formats = (rule != NULL && rule->formats[0] != '\0') ? rule->formats : context->config->formats;
  bg_color = (rule != NULL && rule->bg_color[0] != '\0') ? rule->bg_color : context->config->bg_color;
  endianness = (rule != NULL && rule->endianness >= 0) ? rule->endianness : context->config->endianness;
  index_interval = (rule != NULL && rule->index_interval > 0) ? rule->index_interval : context->config->index_interval;

  arguments[argument_count++] = job->tool->exe_path;
  arguments[argument_count++] = "--input";
  arguments[argument_count++] = job->input_directory;
  arguments[argument_count++] = "--output";
  arguments[argument_count++] = job->output_directory;
  if (img2bin_stricmp(formats, "all") == 0 || strchr(formats, ',') != NULL) {
    arguments[argument_count++] = "--formats";
  } else {
    arguments[argument_count++] = "--format";
  }
  arguments[argument_count++] = formats;
  arguments[argument_count++] = "--bg-color";
  arguments[argument_count++] = bg_color;
  if (endianness == IMG2BIN_ENDIAN_LITTLE) {
    arguments[argument_count++] = "--little-endian";
  }
  if (index_interval > 0 && job->tool->supports_index_interval) {
    snprintf(interval_text, sizeof(interval_text), "%ld", index_interval);
    arguments[argument_count++] = "--index-interval";
    arguments[argument_count++] = interval_text;
  }

  if (!img2bin_pack_run_command(arguments, argument_count, &captured_output, &exit_code, error, sizeof(error))) {
    job->status = "error";
    img2bin_pack_copy_string(job->detail, sizeof(job->detail), error);
    free(captured_output);
    return;
  }

  job->executed = 1;
  job->exit_code = exit_code;

  if (exit_code == 0) {
    job->status = "success";
  } else if (exit_code == 6) {
    job->status = "partial";
  } else {
    job->status = "error";
  }

  if (exit_code != 0 && captured_output != NULL) {
    captured_length = strlen(captured_output);
    captured_tail = captured_length > 400 ? captured_output + captured_length - 400 : captured_output;
    img2bin_pack_copy_string(job->detail, sizeof(job->detail), captured_tail);
  }
  free(captured_output);

  if (job->status[0] == 's' || job->status[0] == 'p') {
    img2bin_pack_collect_job_outputs(job);
  }
}

static int img2bin_pack_bootstrap_folders(img2bin_pack_context_t *context)
{
  char folder_path[IMG2BIN_PATH_CAPACITY];
  char folder_name[64];
  char error[512];
  size_t index = 0;

  for (index = 0; index < context->tools->count; ++index) {
    if (snprintf(folder_name, sizeof(folder_name), "input2%s", context->tools->items[index].algorithm_code) < 0) {
      return 0;
    }
    if (!img2bin_path_join(context->root, folder_name, folder_path, sizeof(folder_path))) {
      return 0;
    }
    if (!img2bin_make_dirs(folder_path, error, sizeof(error))) {
      printf("Could not create %s: %s\n", folder_path, error);
      return 0;
    }
    printf("Created input directory: %s\n", folder_path);
  }

  if (!img2bin_make_dirs(context->output, error, sizeof(error))) {
    printf("Could not create %s: %s\n", context->output, error);
    return 0;
  }
  printf("Created output directory: %s\n", context->output);
  printf("Put image files into the input2<algorithm> directories and run again.\n");
  return 1;
}

static int img2bin_pack_write_manifest(img2bin_pack_context_t *context, char *error_buffer, size_t error_buffer_size)
{
  img2bin_pack_buffer_t buffer;
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  size_t index = 0;
  size_t output_index = 0;
  size_t succeeded = 0;
  size_t partial = 0;
  size_t failed = 0;
  size_t skipped = 0;
  size_t bins_total = 0;
  const img2bin_pack_job_t *job = NULL;
  int written = 0;

  for (index = 0; index < context->job_count; ++index) {
    job = &context->jobs[index];
    if (strcmp(job->status, "success") == 0) {
      ++succeeded;
    } else if (strcmp(job->status, "partial") == 0) {
      ++partial;
    } else if (strcmp(job->status, "skipped_empty") == 0) {
      ++skipped;
    } else {
      ++failed;
    }
    bins_total += job->outputs.count;
  }

  img2bin_pack_buffer_init(&buffer);

  if (!img2bin_pack_buffer_appendf(
        &buffer,
        "{\n  \"tool\": {\n    \"id\": \"img2bin_pack\",\n    \"version\": \"%s\"\n  },\n  \"run\": {\n    \"root\": ",
        IMG2BIN_VERSION_TEXT) ||
      !img2bin_pack_buffer_append_json_string(&buffer, context->root) ||
      !img2bin_pack_buffer_appendf(&buffer, ",\n    \"output_directory\": ") ||
      !img2bin_pack_buffer_append_json_string(&buffer, context->output) ||
      !img2bin_pack_buffer_appendf(&buffer, ",\n    \"tools_directory\": ") ||
      !img2bin_pack_buffer_append_json_string(&buffer, context->tools_dir) ||
      !img2bin_pack_buffer_appendf(&buffer, ",\n    \"discovered_tools\": [\n")) {
    goto oom;
  }

  for (index = 0; index < context->tools->count; ++index) {
    if (!img2bin_pack_buffer_appendf(
          &buffer,
          "      { \"tool_id\": \"%s\", \"algorithm_code\": \"%s\", \"supports_index_interval\": %s }%s\n",
          context->tools->items[index].tool_id,
          context->tools->items[index].algorithm_code,
          context->tools->items[index].supports_index_interval ? "true" : "false",
          index + 1 == context->tools->count ? "" : ",")) {
      goto oom;
    }
  }

  if (!img2bin_pack_buffer_appendf(
        &buffer,
        "    ]\n  },\n  \"summary\": {\n    \"folders_total\": %zu,\n    \"folders_succeeded\": %zu,\n    \"folders_partial\": %zu,\n    \"folders_failed\": %zu,\n    \"folders_skipped\": %zu,\n    \"collected_bin_files_total\": %zu\n  },\n  \"folders\": [\n",
        context->job_count,
        succeeded,
        partial,
        failed,
        skipped,
        bins_total)) {
    goto oom;
  }

  for (index = 0; index < context->job_count; ++index) {
    job = &context->jobs[index];

    if (!img2bin_pack_buffer_appendf(&buffer, "    {\n      \"folder\": ") ||
        !img2bin_pack_buffer_append_json_string(&buffer, job->folder_name) ||
        !img2bin_pack_buffer_appendf(&buffer, ",\n      \"input_directory\": ") ||
        !img2bin_pack_buffer_append_json_string(&buffer, job->input_directory) ||
        !img2bin_pack_buffer_appendf(&buffer, ",\n      \"output_directory\": ") ||
        !img2bin_pack_buffer_append_json_string(&buffer, job->output_directory) ||
        !img2bin_pack_buffer_appendf(
          &buffer,
          ",\n      \"tool_id\": \"%s\",\n      \"algorithm_code\": \"%s\",\n      \"status\": \"%s\",\n      \"exit_code\": %d,\n      \"images_found\": %zu,\n      \"outputs\": [\n",
          job->tool != NULL ? job->tool->tool_id : "",
          job->tool != NULL ? job->tool->algorithm_code : job->requested_tool,
          job->status,
          job->exit_code,
          job->image_count)) {
      goto oom;
    }

    for (output_index = 0; output_index < job->outputs.count; ++output_index) {
      if (!img2bin_pack_buffer_appendf(&buffer, "        ") ||
          !img2bin_pack_buffer_append_json_string(&buffer, job->outputs.items[output_index]) ||
          !img2bin_pack_buffer_appendf(&buffer, "%s\n", output_index + 1 == job->outputs.count ? "" : ",")) {
        goto oom;
      }
    }

    if (!img2bin_pack_buffer_appendf(&buffer, "      ],\n      \"detail\": ") ||
        !img2bin_pack_buffer_append_json_string(&buffer, job->detail) ||
        !img2bin_pack_buffer_appendf(&buffer, "\n    }%s\n", index + 1 == context->job_count ? "" : ",")) {
      goto oom;
    }
  }

  if (!img2bin_pack_buffer_appendf(
        &buffer,
        "  ],\n  \"codegen\": {\n    \"enabled\": %s,\n    \"mode\": \"%s\",\n    \"generated_files\": [\n",
        context->codegen_enabled ? "true" : "false",
        context->codegen_split ? "split" : "combined")) {
    goto oom;
  }

  for (index = 0; index < context->generated_files.count; ++index) {
    if (!img2bin_pack_buffer_appendf(&buffer, "      ") ||
        !img2bin_pack_buffer_append_json_string(&buffer, context->generated_files.items[index]) ||
        !img2bin_pack_buffer_appendf(&buffer, "%s\n", index + 1 == context->generated_files.count ? "" : ",")) {
      goto oom;
    }
  }

  if (!img2bin_pack_buffer_appendf(&buffer, "    ]\n  }\n}\n")) {
    goto oom;
  }

  if (!img2bin_path_join(context->output, "img2bin_pack-manifest.json", manifest_path, sizeof(manifest_path))) {
    img2bin_set_error(error_buffer, error_buffer_size, "Manifest output path is too long.");
    img2bin_pack_buffer_free(&buffer);
    return 0;
  }

  written = img2bin_write_file(manifest_path, (const unsigned char *)buffer.data, buffer.length, error_buffer, error_buffer_size);
  img2bin_pack_buffer_free(&buffer);
  if (written) {
    printf("Wrote %s\n", manifest_path);
  }
  return written;

oom:
  img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while building manifest.");
  img2bin_pack_buffer_free(&buffer);
  return 0;
}

static int img2bin_pack_run_codegen(img2bin_pack_context_t *context, char *error_buffer, size_t error_buffer_size)
{
  img2bin_pack_codegen_options_t options;
  img2bin_string_list_t bin_paths;
  img2bin_string_list_t names;
  char joined_path[IMG2BIN_PATH_CAPACITY];
  size_t job_index = 0;
  size_t other_index = 0;
  size_t name_index = 0;
  const img2bin_pack_job_t *job = NULL;
  int seen_before = 0;
  size_t generated_before = 0;

  img2bin_pack_codegen_options_init(&options);
  options.split = context->codegen_split;
  options.base_name = context->codegen_base_name;

  for (job_index = 0; job_index < context->job_count; ++job_index) {
    job = &context->jobs[job_index];
    if (strcmp(job->status, "success") != 0 && strcmp(job->status, "partial") != 0) {
      continue;
    }

    seen_before = 0;
    for (other_index = 0; other_index < job_index; ++other_index) {
      if ((strcmp(context->jobs[other_index].status, "success") == 0 || strcmp(context->jobs[other_index].status, "partial") == 0) &&
          img2bin_stricmp(context->jobs[other_index].output_directory, job->output_directory) == 0) {
        seen_before = 1;
        break;
      }
    }
    if (seen_before) {
      continue;
    }

    memset(&bin_paths, 0, sizeof(bin_paths));
    memset(&names, 0, sizeof(names));

    if (!img2bin_pack_list_directory(job->output_directory, 1, 0, &names, error_buffer, error_buffer_size)) {
      img2bin_string_list_free(&names);
      return 0;
    }

    for (name_index = 0; name_index < names.count; ++name_index) {
      img2bin_pack_bin_info_t info;
      if (!img2bin_pack_ends_with_ci(names.items[name_index], ".bin")) {
        continue;
      }
      if (!img2bin_pack_parse_bin_name(names.items[name_index], &info)) {
        continue;
      }
      if (!img2bin_path_join(job->output_directory, names.items[name_index], joined_path, sizeof(joined_path)) ||
          !img2bin_string_list_append(&bin_paths, joined_path)) {
        img2bin_string_list_free(&names);
        img2bin_string_list_free(&bin_paths);
        img2bin_set_error(error_buffer, error_buffer_size, "Failed to collect .bin files for code generation.");
        return 0;
      }
    }
    img2bin_string_list_free(&names);

    if (bin_paths.count == 0) {
      img2bin_string_list_free(&bin_paths);
      continue;
    }

    generated_before = context->generated_files.count;
    if (!img2bin_pack_generate_sources(&bin_paths, job->output_directory, &options, &context->generated_files, error_buffer, error_buffer_size)) {
      img2bin_string_list_free(&bin_paths);
      return 0;
    }
    img2bin_string_list_free(&bin_paths);

    while (generated_before < context->generated_files.count) {
      printf("Generated %s\n", context->generated_files.items[generated_before]);
      ++generated_before;
    }
  }

  return 1;
}

static int img2bin_pack_execute(const img2bin_pack_cli_t *cli, const char *exe_dir)
{
  img2bin_pack_context_t context;
  img2bin_pack_config_t *config = NULL;
  img2bin_pack_tool_list_t *tools = NULL;
  img2bin_pack_job_t *jobs = NULL;
  img2bin_string_list_t folder_names;
  char config_path[IMG2BIN_PATH_CAPACITY];
  char config_dir[IMG2BIN_PATH_CAPACITY];
  char resolved[IMG2BIN_PATH_CAPACITY];
  char error[1024];
  size_t index = 0;
  size_t scanned_input_folders = 0;
  size_t failed = 0;
  size_t succeeded = 0;
  size_t partial = 0;
  size_t skipped = 0;
  const char *config_used = NULL;
  img2bin_pack_job_t *job = NULL;
  int exit_code = IMG2BIN_PACK_EXIT_INTERNAL_ERROR;

  memset(&context, 0, sizeof(context));
  memset(&folder_names, 0, sizeof(folder_names));
  config_path[0] = '\0';
  config_dir[0] = '\0';

  config = (img2bin_pack_config_t *)calloc(1, sizeof(*config));
  tools = (img2bin_pack_tool_list_t *)calloc(1, sizeof(*tools));
  jobs = (img2bin_pack_job_t *)calloc(IMG2BIN_PACK_MAX_JOBS, sizeof(*jobs));
  if (config == NULL || tools == NULL || jobs == NULL) {
    img2bin_pack_emit_error("out_of_memory", IMG2BIN_PACK_EXIT_INTERNAL_ERROR, "内存不足。", "Out of memory.", "");
    goto cleanup;
  }

  img2bin_pack_config_init(config);
  context.config = config;
  context.tools = tools;
  context.jobs = jobs;
  img2bin_pack_copy_string(context.exe_dir, sizeof(context.exe_dir), exe_dir);

  if (cli->config_path != NULL) {
    img2bin_pack_copy_string(config_path, sizeof(config_path), cli->config_path);
    if (!img2bin_is_regular_file(config_path)) {
      img2bin_pack_emit_error("config_not_found", IMG2BIN_PACK_EXIT_CLI_ERROR, "配置文件不存在。", "Config file does not exist.", config_path);
      exit_code = IMG2BIN_PACK_EXIT_CLI_ERROR;
      goto cleanup;
    }
  } else {
    if (cli->root != NULL) {
      if (img2bin_path_join(cli->root, "img2bin_pack.json", resolved, sizeof(resolved)) && img2bin_is_regular_file(resolved)) {
        img2bin_pack_copy_string(config_path, sizeof(config_path), resolved);
      }
    }
    if (config_path[0] == '\0') {
      if (img2bin_path_join(exe_dir, "img2bin_pack.json", resolved, sizeof(resolved)) && img2bin_is_regular_file(resolved)) {
        img2bin_pack_copy_string(config_path, sizeof(config_path), resolved);
      }
    }
  }

  if (config_path[0] != '\0') {
    if (!img2bin_pack_config_load_file(config, config_path, error, sizeof(error))) {
      img2bin_pack_emit_error("config_invalid", IMG2BIN_PACK_EXIT_CLI_ERROR, "配置文件无效。", "Config file is invalid.", error);
      exit_code = IMG2BIN_PACK_EXIT_CLI_ERROR;
      goto cleanup;
    }
    if (!img2bin_dirname(config_path, config_dir, sizeof(config_dir))) {
      config_dir[0] = '\0';
    }
    config_used = config_path;
    printf("Using config: %s\n", config_path);
  }

  if (cli->formats != NULL) {
    img2bin_pack_copy_string(config->formats, sizeof(config->formats), cli->formats);
  }
  if (cli->little_endian) {
    config->endianness = IMG2BIN_ENDIAN_LITTLE;
  }
  if (cli->bg_color != NULL) {
    img2bin_pack_copy_string(config->bg_color, sizeof(config->bg_color), cli->bg_color);
  }
  if (cli->index_interval > 0) {
    config->index_interval = cli->index_interval;
  }
  if (cli->split >= 0) {
    config->codegen_split = cli->split;
  }
  if (cli->base_name != NULL) {
    img2bin_pack_copy_string(config->codegen_base_name, sizeof(config->codegen_base_name), cli->base_name);
  }
  if (cli->no_codegen) {
    config->codegen_enabled = 0;
  }

  if (cli->root != NULL) {
    img2bin_pack_copy_string(resolved, sizeof(resolved), cli->root);
  } else if (config->root[0] != '\0') {
    if (!img2bin_pack_resolve_path(config_dir, config->root, resolved, sizeof(resolved))) {
      img2bin_pack_emit_error("config_invalid", IMG2BIN_PACK_EXIT_CLI_ERROR, "配置文件无效。", "Config file is invalid.", "root path is too long");
      exit_code = IMG2BIN_PACK_EXIT_CLI_ERROR;
      goto cleanup;
    }
  } else {
    img2bin_pack_copy_string(resolved, sizeof(resolved), exe_dir);
  }
  if (!img2bin_pack_absolute_path(resolved, context.root, sizeof(context.root))) {
    img2bin_pack_copy_string(context.root, sizeof(context.root), resolved);
  }

  if (!img2bin_is_directory(context.root)) {
    img2bin_pack_emit_error("root_not_found", IMG2BIN_PACK_EXIT_INPUT_ERROR, "工作目录不存在。", "Workspace root does not exist.", context.root);
    exit_code = IMG2BIN_PACK_EXIT_INPUT_ERROR;
    goto cleanup;
  }

  if (cli->output != NULL) {
    img2bin_pack_copy_string(resolved, sizeof(resolved), cli->output);
  } else if (config->output[0] != '\0') {
    if (!img2bin_pack_resolve_path(context.root, config->output, resolved, sizeof(resolved))) {
      img2bin_pack_emit_error("config_invalid", IMG2BIN_PACK_EXIT_CLI_ERROR, "配置文件无效。", "Config file is invalid.", "output path is too long");
      exit_code = IMG2BIN_PACK_EXIT_CLI_ERROR;
      goto cleanup;
    }
  } else if (!img2bin_path_join(context.root, "output", resolved, sizeof(resolved))) {
    img2bin_pack_emit_error("internal_error", IMG2BIN_PACK_EXIT_INTERNAL_ERROR, "内部错误。", "Internal error.", "output path is too long");
    goto cleanup;
  }
  if (!img2bin_pack_absolute_path(resolved, context.output, sizeof(context.output))) {
    img2bin_pack_copy_string(context.output, sizeof(context.output), resolved);
  }

  if (cli->tools_dir != NULL) {
    img2bin_pack_copy_string(resolved, sizeof(resolved), cli->tools_dir);
  } else if (config->tools_dir[0] != '\0') {
    if (!img2bin_pack_resolve_path(config_dir[0] != '\0' ? config_dir : exe_dir, config->tools_dir, resolved, sizeof(resolved))) {
      img2bin_pack_emit_error("config_invalid", IMG2BIN_PACK_EXIT_CLI_ERROR, "配置文件无效。", "Config file is invalid.", "tools_dir path is too long");
      exit_code = IMG2BIN_PACK_EXIT_CLI_ERROR;
      goto cleanup;
    }
  } else {
    if (img2bin_path_join(exe_dir, "tools", resolved, sizeof(resolved)) && img2bin_is_directory(resolved)) {
      /* keep <exe_dir>/tools */
    } else {
      img2bin_pack_copy_string(resolved, sizeof(resolved), exe_dir);
    }
  }
  if (!img2bin_pack_absolute_path(resolved, context.tools_dir, sizeof(context.tools_dir))) {
    img2bin_pack_copy_string(context.tools_dir, sizeof(context.tools_dir), resolved);
  }

  context.codegen_enabled = config->codegen_enabled;
  context.codegen_split = config->codegen_split;
  context.codegen_base_name = config->codegen_base_name;

  if (!img2bin_pack_discover_tools(context.tools_dir, tools, error, sizeof(error))) {
    img2bin_pack_emit_error("tools_dir_invalid", IMG2BIN_PACK_EXIT_INPUT_ERROR, "工具目录无效。", "Tools directory is invalid.", error);
    exit_code = IMG2BIN_PACK_EXIT_INPUT_ERROR;
    goto cleanup;
  }
  if (tools->count == 0) {
    img2bin_pack_emit_error(
      "no_tools_found",
      IMG2BIN_PACK_EXIT_INPUT_ERROR,
      "未在工具目录中找到任何 img2bin 工具。",
      "No img2bin tools were found in the tools directory.",
      context.tools_dir);
    exit_code = IMG2BIN_PACK_EXIT_INPUT_ERROR;
    goto cleanup;
  }

  printf("Tools directory: %s\n", context.tools_dir);
  for (index = 0; index < tools->count; ++index) {
    printf("Discovered tool: %s (%s)\n", tools->items[index].tool_id, tools->items[index].algorithm_code);
  }
  printf("Workspace root: %s\n", context.root);

  if (!img2bin_pack_list_directory(context.root, 0, 1, &folder_names, error, sizeof(error))) {
    img2bin_pack_emit_error("root_scan_failed", IMG2BIN_PACK_EXIT_INPUT_ERROR, "扫描工作目录失败。", "Failed to scan workspace root.", error);
    exit_code = IMG2BIN_PACK_EXIT_INPUT_ERROR;
    goto cleanup;
  }

  for (index = 0; index < folder_names.count; ++index) {
    if (!img2bin_pack_starts_with_ci(folder_names.items[index], "input2") || folder_names.items[index][6] == '\0') {
      continue;
    }
    ++scanned_input_folders;
    if (img2bin_pack_add_job(&context, folder_names.items[index]) == NULL) {
      printf("Too many input folders; only the first %d are processed.\n", IMG2BIN_PACK_MAX_JOBS);
      break;
    }
  }

  for (index = 0; index < config->folder_rule_count; ++index) {
    if (img2bin_pack_find_job(&context, config->folder_rules[index].folder_name) != NULL) {
      continue;
    }
    if (img2bin_pack_add_job(&context, config->folder_rules[index].folder_name) == NULL) {
      printf("Too many input folders; only the first %d are processed.\n", IMG2BIN_PACK_MAX_JOBS);
      break;
    }
  }

  if (scanned_input_folders == 0 && context.job_count == 0) {
    if (!img2bin_pack_bootstrap_folders(&context)) {
      exit_code = IMG2BIN_PACK_EXIT_INPUT_ERROR;
      goto cleanup;
    }
    exit_code = IMG2BIN_PACK_EXIT_SUCCESS;
    goto cleanup;
  }

  if (!img2bin_make_dirs(context.output, error, sizeof(error))) {
    img2bin_pack_emit_error("output_invalid", IMG2BIN_PACK_EXIT_INPUT_ERROR, "无法创建输出目录。", "Could not create the output directory.", error);
    exit_code = IMG2BIN_PACK_EXIT_INPUT_ERROR;
    goto cleanup;
  }

  for (index = 0; index < context.job_count; ++index) {
    job = &context.jobs[index];
    img2bin_pack_prepare_job(&context, job);
    if (strcmp(job->status, "ready") == 0) {
      img2bin_pack_execute_job(&context, job);
    }

    if (strcmp(job->status, "success") == 0) {
      ++succeeded;
      printf("[%s] %s: success (%zu images, %zu bins)\n", job->folder_name, job->tool->tool_id, job->image_count, job->outputs.count);
    } else if (strcmp(job->status, "partial") == 0) {
      ++partial;
      printf("[%s] %s: partial failure (exit code %d)\n", job->folder_name, job->tool->tool_id, job->exit_code);
    } else if (strcmp(job->status, "skipped_empty") == 0) {
      ++skipped;
      printf("[%s] skipped: no images\n", job->folder_name);
    } else {
      ++failed;
      printf("[%s] failed: %s\n", job->folder_name, job->detail);
    }
  }

  if (context.codegen_enabled) {
    if (!img2bin_pack_run_codegen(&context, error, sizeof(error))) {
      img2bin_pack_emit_error("codegen_failed", IMG2BIN_PACK_EXIT_INTERNAL_ERROR, "生成 .c/.h 失败。", "Failed to generate .c/.h sources.", error);
      goto cleanup;
    }
  }

  if (!img2bin_pack_write_manifest(&context, error, sizeof(error))) {
    img2bin_pack_emit_error("manifest_write_failed", IMG2BIN_PACK_EXIT_INTERNAL_ERROR, "写入 manifest 失败。", "Failed to write the manifest.", error);
    goto cleanup;
  }

  printf(
    "Done. %zu folder(s): %zu succeeded, %zu partial, %zu failed, %zu skipped.\n",
    context.job_count,
    succeeded,
    partial,
    failed,
    skipped);

  exit_code = (failed > 0 || partial > 0) ? IMG2BIN_PACK_EXIT_PARTIAL_FAILURE : IMG2BIN_PACK_EXIT_SUCCESS;

cleanup:
  if (jobs != NULL) {
    for (index = 0; index < context.job_count; ++index) {
      img2bin_string_list_free(&jobs[index].outputs);
    }
  }
  img2bin_string_list_free(&folder_names);
  img2bin_string_list_free(&context.generated_files);
  free(jobs);
  free(tools);
  free(config);
  (void)config_used;
  return exit_code;
}

int img2bin_pack_run_with_executable_path(int argc, const char *const *argv, const char *executable_path_override)
{
  img2bin_pack_cli_t cli;
  char error[512];
  char executable_path[IMG2BIN_PATH_CAPACITY];
  char exe_dir[IMG2BIN_PATH_CAPACITY];
  char info_json[16384];

  if (!img2bin_pack_parse_cli(argc, argv, &cli, error, sizeof(error))) {
    img2bin_pack_emit_error("cli_invalid", IMG2BIN_PACK_EXIT_CLI_ERROR, "命令行参数无效。", "Invalid command line arguments.", error);
    return IMG2BIN_PACK_EXIT_CLI_ERROR;
  }

  if (cli.show_help) {
    img2bin_pack_print_help();
    return IMG2BIN_PACK_EXIT_SUCCESS;
  }
  if (cli.show_info) {
    if (!img2bin_pack_get_info_json(info_json, sizeof(info_json))) {
      img2bin_pack_emit_error("internal_error", IMG2BIN_PACK_EXIT_INTERNAL_ERROR, "内部错误。", "Internal error.", "info buffer too small");
      return IMG2BIN_PACK_EXIT_INTERNAL_ERROR;
    }
    printf("%s", info_json);
    return IMG2BIN_PACK_EXIT_SUCCESS;
  }

  if (executable_path_override != NULL) {
    img2bin_pack_copy_string(executable_path, sizeof(executable_path), executable_path_override);
  } else if (!img2bin_get_executable_path(executable_path, sizeof(executable_path))) {
    img2bin_pack_emit_error("internal_error", IMG2BIN_PACK_EXIT_INTERNAL_ERROR, "无法定位程序路径。", "Could not resolve the executable path.", "");
    return IMG2BIN_PACK_EXIT_INTERNAL_ERROR;
  }

  if (!img2bin_dirname(executable_path, exe_dir, sizeof(exe_dir))) {
    img2bin_pack_emit_error("internal_error", IMG2BIN_PACK_EXIT_INTERNAL_ERROR, "无法定位程序目录。", "Could not resolve the executable directory.", "");
    return IMG2BIN_PACK_EXIT_INTERNAL_ERROR;
  }

  return img2bin_pack_execute(&cli, exe_dir);
}

int img2bin_pack_run(int argc, const char *const *argv)
{
  return img2bin_pack_run_with_executable_path(argc, argv, NULL);
}
