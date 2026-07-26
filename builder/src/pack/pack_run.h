#ifndef IMG2BIN_PACK_RUN_H
#define IMG2BIN_PACK_RUN_H

#include <stddef.h>

int img2bin_pack_get_info_json(char *buffer, size_t buffer_size);
int img2bin_pack_run_with_executable_path(int argc, const char *const *argv, const char *executable_path_override);
int img2bin_pack_run(int argc, const char *const *argv);

#endif
