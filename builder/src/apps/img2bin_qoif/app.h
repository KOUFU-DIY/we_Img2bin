#ifndef IMG2BIN_QOIF_APP_H
#define IMG2BIN_QOIF_APP_H

#include <stddef.h>

int img2bin_qoif_run(int argc, const char *const *argv);
int img2bin_qoif_run_with_executable_path(int argc, const char *const *argv, const char *executable_path_override);
int img2bin_qoif_get_info_json(char *buffer, size_t buffer_size);

#endif
