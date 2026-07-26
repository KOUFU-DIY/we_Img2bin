#include "app.h"

#include <stdlib.h>

#ifdef _WIN32
#include <wchar.h>

#include "filesystem.h"

int wmain(int argc, wchar_t **wargv)
{
  char **argv = NULL;
  int index = 0;
  int exit_code = 1;

  argv = (char **)calloc((size_t)argc, sizeof(char *));
  if (argv == NULL) {
    return 1;
  }

  for (index = 0; index < argc; ++index) {
    argv[index] = img2bin_wide_to_utf8_alloc(wargv[index]);
    if (argv[index] == NULL) {
      goto cleanup;
    }
  }

  exit_code = img2bin_raw_run(argc, (const char *const *)argv);

cleanup:
  for (index = 0; index < argc; ++index) {
    free(argv[index]);
  }
  free(argv);
  return exit_code;
}
#else
int main(int argc, char **argv)
{
  return img2bin_raw_run(argc, (const char *const *)argv);
}
#endif
