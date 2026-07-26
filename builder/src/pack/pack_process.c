#include "pack_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "filesystem.h"
#include "pack_util.h"

#ifdef _WIN32

#include <windows.h>

static int img2bin_pack_append_quoted_argument(img2bin_pack_buffer_t *buffer, const char *argument)
{
  size_t index = 0;
  size_t backslash_count = 0;
  size_t emit_index = 0;
  int needs_quotes = 0;

  if (argument[0] == '\0') {
    needs_quotes = 1;
  }
  for (index = 0; argument[index] != '\0'; ++index) {
    if (argument[index] == ' ' || argument[index] == '\t' || argument[index] == '"') {
      needs_quotes = 1;
      break;
    }
  }

  if (!needs_quotes) {
    return img2bin_pack_buffer_append(buffer, argument, strlen(argument));
  }

  if (!img2bin_pack_buffer_append(buffer, "\"", 1)) {
    return 0;
  }

  for (index = 0; argument[index] != '\0'; ++index) {
    backslash_count = 0;
    while (argument[index] == '\\') {
      ++backslash_count;
      ++index;
    }

    if (argument[index] == '\0') {
      for (emit_index = 0; emit_index < backslash_count * 2; ++emit_index) {
        if (!img2bin_pack_buffer_append(buffer, "\\", 1)) {
          return 0;
        }
      }
      break;
    }

    if (argument[index] == '"') {
      for (emit_index = 0; emit_index < backslash_count * 2 + 1; ++emit_index) {
        if (!img2bin_pack_buffer_append(buffer, "\\", 1)) {
          return 0;
        }
      }
      if (!img2bin_pack_buffer_append(buffer, "\"", 1)) {
        return 0;
      }
      continue;
    }

    for (emit_index = 0; emit_index < backslash_count; ++emit_index) {
      if (!img2bin_pack_buffer_append(buffer, "\\", 1)) {
        return 0;
      }
    }
    if (!img2bin_pack_buffer_append(buffer, &argument[index], 1)) {
      return 0;
    }
  }

  return img2bin_pack_buffer_append(buffer, "\"", 1);
}

int img2bin_pack_run_command(
  const char *const *arguments,
  size_t argument_count,
  char **out_captured_output,
  int *out_exit_code,
  char *error_buffer,
  size_t error_buffer_size)
{
  img2bin_pack_buffer_t command_line;
  img2bin_pack_buffer_t captured;
  wchar_t *wide_command_line = NULL;
  SECURITY_ATTRIBUTES security_attributes;
  HANDLE pipe_read = NULL;
  HANDLE pipe_write = NULL;
  STARTUPINFOW startup_info;
  PROCESS_INFORMATION process_info;
  char chunk[4096];
  DWORD bytes_read = 0;
  DWORD wait_result = 0;
  DWORD exit_code = 0;
  size_t index = 0;
  int ok = 0;

  if (arguments == NULL || argument_count == 0 || out_exit_code == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Command invocation request is invalid.");
    return 0;
  }

  img2bin_pack_buffer_init(&command_line);
  img2bin_pack_buffer_init(&captured);

  for (index = 0; index < argument_count; ++index) {
    if (index > 0 && !img2bin_pack_buffer_append(&command_line, " ", 1)) {
      goto oom;
    }
    if (!img2bin_pack_append_quoted_argument(&command_line, arguments[index])) {
      goto oom;
    }
  }

  if (!img2bin_utf8_to_wide_alloc(command_line.data, &wide_command_line)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to convert command line to wide characters.");
    goto cleanup;
  }

  memset(&security_attributes, 0, sizeof(security_attributes));
  security_attributes.nLength = sizeof(security_attributes);
  security_attributes.bInheritHandle = TRUE;

  if (!CreatePipe(&pipe_read, &pipe_write, &security_attributes, 0)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to create output pipe for child process.");
    goto cleanup;
  }
  if (!SetHandleInformation(pipe_read, HANDLE_FLAG_INHERIT, 0)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to configure output pipe for child process.");
    goto cleanup;
  }

  memset(&startup_info, 0, sizeof(startup_info));
  startup_info.cb = sizeof(startup_info);
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdOutput = pipe_write;
  startup_info.hStdError = pipe_write;
  startup_info.hStdInput = INVALID_HANDLE_VALUE;

  memset(&process_info, 0, sizeof(process_info));

  if (!CreateProcessW(NULL, wide_command_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup_info, &process_info)) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to launch: %s", arguments[0]);
    goto cleanup;
  }

  CloseHandle(pipe_write);
  pipe_write = NULL;

  for (;;) {
    if (!ReadFile(pipe_read, chunk, sizeof(chunk), &bytes_read, NULL) || bytes_read == 0) {
      break;
    }
    if (!img2bin_pack_buffer_append(&captured, chunk, bytes_read)) {
      CloseHandle(process_info.hProcess);
      CloseHandle(process_info.hThread);
      goto oom;
    }
  }

  wait_result = WaitForSingleObject(process_info.hProcess, INFINITE);
  if (wait_result != WAIT_OBJECT_0 || !GetExitCodeProcess(process_info.hProcess, &exit_code)) {
    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to read exit code from: %s", arguments[0]);
    goto cleanup;
  }

  CloseHandle(process_info.hProcess);
  CloseHandle(process_info.hThread);

  *out_exit_code = (int)exit_code;
  if (out_captured_output != NULL) {
    *out_captured_output = captured.data != NULL ? captured.data : img2bin_strdup("");
    captured.data = NULL;
  }
  ok = 1;
  goto cleanup;

oom:
  img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while launching child process.");

cleanup:
  if (pipe_read != NULL) {
    CloseHandle(pipe_read);
  }
  if (pipe_write != NULL) {
    CloseHandle(pipe_write);
  }
  free(wide_command_line);
  img2bin_pack_buffer_free(&command_line);
  img2bin_pack_buffer_free(&captured);
  return ok;
}

#else

int img2bin_pack_run_command(
  const char *const *arguments,
  size_t argument_count,
  char **out_captured_output,
  int *out_exit_code,
  char *error_buffer,
  size_t error_buffer_size)
{
  img2bin_pack_buffer_t command_line;
  img2bin_pack_buffer_t captured;
  FILE *stream = NULL;
  char chunk[4096];
  size_t bytes_read = 0;
  size_t index = 0;
  const char *character = NULL;
  int status = 0;
  int ok = 0;

  if (arguments == NULL || argument_count == 0 || out_exit_code == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Command invocation request is invalid.");
    return 0;
  }

  img2bin_pack_buffer_init(&command_line);
  img2bin_pack_buffer_init(&captured);

  for (index = 0; index < argument_count; ++index) {
    if (index > 0 && !img2bin_pack_buffer_append(&command_line, " ", 1)) {
      goto oom;
    }
    if (!img2bin_pack_buffer_append(&command_line, "'", 1)) {
      goto oom;
    }
    for (character = arguments[index]; *character != '\0'; ++character) {
      if (*character == '\'') {
        if (!img2bin_pack_buffer_append(&command_line, "'\\''", 4)) {
          goto oom;
        }
      } else if (!img2bin_pack_buffer_append(&command_line, character, 1)) {
        goto oom;
      }
    }
    if (!img2bin_pack_buffer_append(&command_line, "'", 1)) {
      goto oom;
    }
  }

  if (!img2bin_pack_buffer_append(&command_line, " 2>&1", 5)) {
    goto oom;
  }

  stream = popen(command_line.data, "r");
  if (stream == NULL) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to launch: %s", arguments[0]);
    goto cleanup;
  }

  while ((bytes_read = fread(chunk, 1, sizeof(chunk), stream)) > 0) {
    if (!img2bin_pack_buffer_append(&captured, chunk, bytes_read)) {
      pclose(stream);
      goto oom;
    }
  }

  status = pclose(stream);
  if (status == -1) {
    img2bin_set_error(error_buffer, error_buffer_size, "Failed to read exit code from: %s", arguments[0]);
    goto cleanup;
  }

  *out_exit_code = (status >> 8) & 0xFF;
  if (out_captured_output != NULL) {
    *out_captured_output = captured.data != NULL ? captured.data : img2bin_strdup("");
    captured.data = NULL;
  }
  ok = 1;
  goto cleanup;

oom:
  img2bin_set_error(error_buffer, error_buffer_size, "Out of memory while launching child process.");

cleanup:
  img2bin_pack_buffer_free(&command_line);
  img2bin_pack_buffer_free(&captured);
  return ok;
}

#endif
