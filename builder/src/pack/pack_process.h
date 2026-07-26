#ifndef IMG2BIN_PACK_PROCESS_H
#define IMG2BIN_PACK_PROCESS_H

#include <stddef.h>

int img2bin_pack_run_command(
  const char *const *arguments,
  size_t argument_count,
  char **out_captured_output,
  int *out_exit_code,
  char *error_buffer,
  size_t error_buffer_size);

#endif
