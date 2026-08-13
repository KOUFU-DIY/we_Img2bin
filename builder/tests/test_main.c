#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "img2bin_imprle/app.h"
#include "img2bin_indexqoi/app.h"
#include "img2bin_indexqoimask/app.h"
#include "img2bin_qoi/app.h"
#include "img2bin_qoif/app.h"
#include "img2bin_raw/app.h"
#include "img2bin_rle/app.h"
#include "filesystem.h"
#include "format.h"
#include "image_io.h"
#include "img2bin_decode.h"
#include "imprle_encoder.h"
#include "indexqoimask_encoder.h"
#include "qoi_encoder.h"
#include "raw_encoder.h"
#include "rle_encoder.h"
#include "version.h"

static int g_test_failures = 0;

#define TEST_ASSERT(condition, message)                                         \
  do {                                                                          \
    if (!(condition)) {                                                         \
      fprintf(stderr, "TEST FAILURE: %s (%s:%d)\n", message, __FILE__, __LINE__); \
      ++g_test_failures;                                                        \
      return;                                                                   \
    }                                                                           \
  } while (0)

static void test_expect_bytes(const unsigned char *actual, const unsigned char *expected, size_t size, const char *label)
{
  if (memcmp(actual, expected, size) != 0) {
    size_t index;
    fprintf(stderr, "TEST FAILURE: %s bytes differ.\n", label);
    fprintf(stderr, "  actual  :");
    for (index = 0; index < size; ++index) {
      fprintf(stderr, " %02X", actual[index]);
    }
    fprintf(stderr, "\n  expected:");
    for (index = 0; index < size; ++index) {
      fprintf(stderr, " %02X", expected[index]);
    }
    fprintf(stderr, "\n");
    ++g_test_failures;
  }
}

static void test_expect_channel_tolerance(
  const unsigned char *actual,
  const unsigned char *expected,
  size_t size,
  unsigned int tolerance,
  const char *label)
{
  size_t index;

  for (index = 0; index < size; ++index) {
    unsigned int left = actual[index];
    unsigned int right = expected[index];
    unsigned int delta = (left > right) ? (left - right) : (right - left);
    if (delta > tolerance) {
      fprintf(stderr, "TEST FAILURE: %s differs at byte %zu (actual=%u expected=%u tolerance=%u).\n", label, index, left, right, tolerance);
      ++g_test_failures;
      return;
    }
  }
}

static void test_get_binary_directory(char *buffer, size_t buffer_size)
{
  char executable_path[IMG2BIN_PATH_CAPACITY];

  TEST_ASSERT(img2bin_get_executable_path(executable_path, sizeof(executable_path)), "Could not resolve test executable path.");
  TEST_ASSERT(img2bin_dirname(executable_path, buffer, buffer_size), "Could not resolve test executable directory.");
}

static void test_get_root_directory(char *buffer, size_t buffer_size)
{
  char binary_dir[IMG2BIN_PATH_CAPACITY];
  char error[512];

  test_get_binary_directory(binary_dir, sizeof(binary_dir));
  TEST_ASSERT(img2bin_path_join(binary_dir, "test_artifacts", buffer, buffer_size), "Could not compose test root directory.");
  TEST_ASSERT(img2bin_make_dirs(buffer, error, sizeof(error)), "Could not create test root directory.");
}

static void test_make_stage_directory(const char *leaf, char *buffer, size_t buffer_size)
{
  char root[IMG2BIN_PATH_CAPACITY];
  char error[512];

  test_get_root_directory(root, sizeof(root));
  TEST_ASSERT(img2bin_path_join(root, leaf, buffer, buffer_size), "Could not compose stage directory.");
  TEST_ASSERT(img2bin_make_dirs(buffer, error, sizeof(error)), "Could not create stage directory.");
}

static void test_write_rgba_fixture(const char *path, int width, int height, const unsigned char *rgba_pixels)
{
  int ok = 0;
  const char *extension = strrchr(path, '.');

  TEST_ASSERT(extension != NULL, "Fixture path must include an extension.");

  if (img2bin_stricmp(extension, ".png") == 0) {
    ok = stbi_write_png(path, width, height, 4, rgba_pixels, width * 4);
  } else if (img2bin_stricmp(extension, ".bmp") == 0) {
    ok = stbi_write_bmp(path, width, height, 4, rgba_pixels);
  } else if (img2bin_stricmp(extension, ".jpg") == 0 || img2bin_stricmp(extension, ".jpeg") == 0) {
    unsigned char *rgb = (unsigned char *)malloc((size_t)width * (size_t)height * 3u);
    int pixel_index;
    TEST_ASSERT(rgb != NULL, "Could not allocate RGB fixture buffer.");
    for (pixel_index = 0; pixel_index < width * height; ++pixel_index) {
      rgb[pixel_index * 3 + 0] = rgba_pixels[pixel_index * 4 + 0];
      rgb[pixel_index * 3 + 1] = rgba_pixels[pixel_index * 4 + 1];
      rgb[pixel_index * 3 + 2] = rgba_pixels[pixel_index * 4 + 2];
    }
    ok = stbi_write_jpg(path, width, height, 3, rgb, 100);
    free(rgb);
  }

  TEST_ASSERT(ok != 0, "Failed to write image fixture.");
}

static void test_copy_file(const char *source, const char *destination)
{
  unsigned char *buffer = NULL;
  size_t size = 0;
  char error[512];

  TEST_ASSERT(img2bin_read_file(source, &buffer, &size, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_write_file(destination, buffer, size, error, sizeof(error)), error);
  free(buffer);
}

static char *test_read_text_file(const char *path)
{
  unsigned char *buffer = NULL;
  size_t size = 0;
  char error[512];
  char *text = NULL;

  if (!img2bin_read_file(path, &buffer, &size, error, sizeof(error))) {
    return NULL;
  }

  text = (char *)malloc(size + 1);
  if (text == NULL) {
    free(buffer);
    return NULL;
  }

  if (size > 0) {
    memcpy(text, buffer, size);
  }
  text[size] = '\0';
  free(buffer);
  return text;
}

static void test_get_source_directory(char *buffer, size_t buffer_size)
{
  TEST_ASSERT(buffer != NULL && buffer_size > 0, "Source-directory buffer is not initialized.");
  TEST_ASSERT(strlen(IMG2BIN_SOURCE_DIR) + 1 <= buffer_size, "Source-directory buffer is too small.");
  strcpy(buffer, IMG2BIN_SOURCE_DIR);
}

static void test_join_source_path(const char *relative_path, char *buffer, size_t buffer_size)
{
  char source_dir[IMG2BIN_PATH_CAPACITY];

  test_get_source_directory(source_dir, sizeof(source_dir));
  TEST_ASSERT(img2bin_path_join(source_dir, relative_path, buffer, buffer_size), "Could not compose source-relative path.");
}

static int test_hex_digit_value(char ch)
{
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  return -1;
}

static int test_extract_escaped_hex_bytes(const char *text, unsigned char **out_bytes, size_t *out_size)
{
  size_t count = 0;
  size_t index = 0;
  unsigned char *bytes = NULL;

  if (text == NULL || out_bytes == NULL || out_size == NULL) {
    return 0;
  }

  while (text[index] != '\0') {
    if (text[index] == '\\' && text[index + 1] == 'x' &&
        test_hex_digit_value(text[index + 2]) >= 0 &&
        test_hex_digit_value(text[index + 3]) >= 0) {
      ++count;
      index += 4;
      continue;
    }
    ++index;
  }

  bytes = (unsigned char *)malloc(count > 0 ? count : 1u);
  if (bytes == NULL) {
    return 0;
  }

  count = 0;
  index = 0;
  while (text[index] != '\0') {
    int high = -1;
    int low = -1;

    if (text[index] == '\\' && text[index + 1] == 'x') {
      high = test_hex_digit_value(text[index + 2]);
      low = test_hex_digit_value(text[index + 3]);
      if (high >= 0 && low >= 0) {
        bytes[count++] = (unsigned char)((high << 4) | low);
        index += 4;
        continue;
      }
    }
    ++index;
  }

  *out_bytes = bytes;
  *out_size = count;
  return 1;
}

static int test_redirect_stderr_begin(const char *path, int *saved_fd)
{
  int target_fd = -1;

  if (path == NULL || saved_fd == NULL) {
    return 0;
  }

  fflush(stderr);

#ifdef _WIN32
  *saved_fd = _dup(_fileno(stderr));
  if (*saved_fd < 0) {
    return 0;
  }
  target_fd = _open(path, _O_CREAT | _O_TRUNC | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
  if (target_fd < 0) {
    _close(*saved_fd);
    *saved_fd = -1;
    return 0;
  }
  if (_dup2(target_fd, _fileno(stderr)) < 0) {
    _close(target_fd);
    _close(*saved_fd);
    *saved_fd = -1;
    return 0;
  }
  _close(target_fd);
#else
  *saved_fd = dup(fileno(stderr));
  if (*saved_fd < 0) {
    return 0;
  }
  target_fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0666);
  if (target_fd < 0) {
    close(*saved_fd);
    *saved_fd = -1;
    return 0;
  }
  if (dup2(target_fd, fileno(stderr)) < 0) {
    close(target_fd);
    close(*saved_fd);
    *saved_fd = -1;
    return 0;
  }
  close(target_fd);
#endif

  clearerr(stderr);
  return 1;
}

static int test_redirect_stderr_end(int saved_fd)
{
  if (saved_fd < 0) {
    return 0;
  }

  fflush(stderr);

#ifdef _WIN32
  if (_dup2(saved_fd, _fileno(stderr)) < 0) {
    _close(saved_fd);
    return 0;
  }
  _close(saved_fd);
#else
  if (dup2(saved_fd, fileno(stderr)) < 0) {
    close(saved_fd);
    return 0;
  }
  close(saved_fd);
#endif

  clearerr(stderr);
  return 1;
}

static int test_count_nonempty_lines(const char *text)
{
  int count = 0;
  int has_content = 0;

  if (text == NULL) {
    return 0;
  }

  while (*text != '\0') {
    if (*text == '\n') {
      if (has_content) {
        ++count;
        has_content = 0;
      }
    } else if (*text != '\r') {
      has_content = 1;
    }
    ++text;
  }

  if (has_content) {
    ++count;
  }

  return count;
}

static void test_raw_encoder_golden_values(void)
{
  unsigned char alpha_pixels[4] = { 0x12, 0x34, 0x56, 0x78 };
  unsigned char opaque_pixels[4] = { 0x12, 0x34, 0x56, 0xFF };
  img2bin_image_t alpha_image;
  img2bin_image_t opaque_image;
  img2bin_rgb_t background = { 0, 0, 0 };
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];

  alpha_image.width = 1;
  alpha_image.height = 1;
  alpha_image.pixels = alpha_pixels;
  opaque_image.width = 1;
  opaque_image.height = 1;
  opaque_image.pixels = opaque_pixels;

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &alpha_image, &encoded, &encoded_size, error, sizeof(error)), error);
  TEST_ASSERT(encoded_size == 4, "ARGB8888 size mismatch.");
  test_expect_bytes(encoded, (const unsigned char[]){ 0x78, 0x12, 0x34, 0x56 }, 4, "ARGB8888 big-endian");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_LITTLE, background, &alpha_image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x56, 0x34, 0x12, 0x78 }, 4, "ARGB8888 little-endian");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_ARGB6666, IMG2BIN_ENDIAN_BIG, background, &alpha_image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x78, 0x43, 0x55 }, 3, "ARGB6666 big-endian");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_ARGB4444, IMG2BIN_ENDIAN_BIG, background, &alpha_image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x71, 0x35 }, 2, "ARGB4444 big-endian");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_ARGB2222, IMG2BIN_ENDIAN_BIG, background, &alpha_image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x45 }, 1, "ARGB2222");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_ARGB8565, IMG2BIN_ENDIAN_BIG, background, &alpha_image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x78, 0x11, 0xAA }, 3, "ARGB8565 big-endian");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_RGB888, IMG2BIN_ENDIAN_BIG, background, &opaque_image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x12, 0x34, 0x56 }, 3, "RGB888 big-endian");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_RGB565, IMG2BIN_ENDIAN_LITTLE, background, &opaque_image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0xAA, 0x11 }, 2, "RGB565 little-endian");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_RGB332, IMG2BIN_ENDIAN_BIG, background, &opaque_image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x05 }, 1, "RGB332");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_RAGB5155, IMG2BIN_ENDIAN_BIG, background, &alpha_image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x10, 0xCA }, 2, "RAGB5155 big-endian");
  free(encoded);
}

static void test_alpha_edges_and_background_blend(void)
{
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 255 };
  unsigned char pixels[6 * 4];
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];
  int index;

  memset(pixels, 0, sizeof(pixels));
  for (index = 0; index < 6; ++index) {
    pixels[index * 4 + 3] = (unsigned char)(index == 0 ? 0 : index == 1 ? 1 : index == 2 ? 127 : index == 3 ? 128 : index == 4 ? 254 : 255);
  }

  image.width = 6;
  image.height = 1;
  image.pixels = pixels;

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_ARGB2222, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x00, 0x00, 0x40, 0x80, 0xC0, 0xC0 }, 6, "ARGB2222 alpha edges");
  free(encoded);

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_RAGB5155, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04, 0x00 }, 12, "RAGB5155 alpha threshold");
  free(encoded);

  pixels[0] = 255;
  pixels[1] = 0;
  pixels[2] = 0;
  pixels[3] = 128;
  image.width = 1;

  TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_RGB888, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
  test_expect_bytes(encoded, (const unsigned char[]){ 0x80, 0x00, 0x7F }, 3, "RGB888 background blend");
  free(encoded);
}

/* Alpha 蒙版黄金字节：只取 Alpha 通道（RGB 填充干扰值证明被忽略）、
   MSB-first 行打包、行字节对齐、行尾补 0、无字节序差异。奇数宽度覆盖补位。 */
static void test_raw_alpha_golden_values(void)
{
  img2bin_image_t image;
  img2bin_rgb_t background = { 255, 255, 255 };
  unsigned char pixels[18 * 4];
  unsigned char *encoded = NULL;
  unsigned char *encoded_le = NULL;
  size_t encoded_size = 0;
  size_t encoded_le_size = 0;
  char error[256];
  size_t index;

  /* a8：3x2，逐字节存储原始 Alpha。 */
  {
    const unsigned char alphas[6] = { 0x00, 0x7F, 0xFF, 0x12, 0x80, 0xFE };

    memset(pixels, 0xDE, sizeof(pixels));
    for (index = 0; index < 6; ++index) {
      pixels[index * 4 + 3] = alphas[index];
    }
    image.width = 3;
    image.height = 2;
    image.pixels = pixels;

    TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_A8, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
    TEST_ASSERT(encoded_size == 6, "A8 payload size mismatch.");
    test_expect_bytes(encoded, alphas, 6, "A8 identity bytes");
    free(encoded);
    encoded = NULL;
  }

  /* a4：5x3 奇数宽度，行 stride 3，末 nibble 补 0；量化 (a*15+127)/255。 */
  {
    const unsigned char alphas[15] = {
      0, 17, 34, 255, 128,
      51, 68, 85, 102, 119,
      136, 153, 170, 187, 204
    };
    const unsigned char expected[9] = { 0x01, 0x2F, 0x80, 0x34, 0x56, 0x70, 0x89, 0xAB, 0xC0 };

    memset(pixels, 0xAD, sizeof(pixels));
    for (index = 0; index < 15; ++index) {
      pixels[index * 4 + 3] = alphas[index];
    }
    image.width = 5;
    image.height = 3;
    image.pixels = pixels;

    TEST_ASSERT(img2bin_format_row_stride(IMG2BIN_FMT_A4, 5u) == 3u, "A4 row stride mismatch.");
    TEST_ASSERT(img2bin_format_payload_size(IMG2BIN_FMT_A4, 5u, 3u) == 9u, "A4 payload size helper mismatch.");
    TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_A4, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
    TEST_ASSERT(encoded_size == 9, "A4 payload size mismatch.");
    test_expect_bytes(encoded, expected, 9, "A4 odd-width packed bytes");

    /* 字节序对 Alpha 蒙版无影响：le 输出必须与 be 逐字节一致。 */
    TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_A4, IMG2BIN_ENDIAN_LITTLE, background, &image, &encoded_le, &encoded_le_size, error, sizeof(error)), error);
    TEST_ASSERT(encoded_le_size == encoded_size, "A4 le/be size mismatch.");
    test_expect_bytes(encoded_le, encoded, encoded_size, "A4 endianness-neutral bytes");
    free(encoded_le);
    encoded_le = NULL;
    free(encoded);
    encoded = NULL;
  }

  /* a2：5x1，行 stride 2；量化 (a*3+127)/255。 */
  {
    const unsigned char alphas[5] = { 0, 85, 170, 255, 255 };
    const unsigned char expected[2] = { 0x1B, 0xC0 };

    memset(pixels, 0xBE, sizeof(pixels));
    for (index = 0; index < 5; ++index) {
      pixels[index * 4 + 3] = alphas[index];
    }
    image.width = 5;
    image.height = 1;
    image.pixels = pixels;

    TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_A2, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
    TEST_ASSERT(encoded_size == 2, "A2 payload size mismatch.");
    test_expect_bytes(encoded, expected, 2, "A2 packed bytes");
    free(encoded);
    encoded = NULL;
  }

  /* a1：9x2 奇数宽度跨字节，行 stride 2；阈值 (a*1+127)/255 → 127→0、128→1。 */
  {
    const unsigned char alphas[18] = {
      255, 0, 255, 0, 255, 0, 255, 0, 255,
      127, 128, 0, 0, 0, 0, 0, 0, 127
    };
    const unsigned char expected[4] = { 0xAA, 0x80, 0x40, 0x00 };

    memset(pixels, 0xEF, sizeof(pixels));
    image.width = 9;
    image.height = 2;
    image.pixels = pixels;
    for (index = 0; index < 18; ++index) {
      pixels[index * 4 + 3] = alphas[index];
    }

    TEST_ASSERT(img2bin_format_row_stride(IMG2BIN_FMT_A1, 9u) == 2u, "A1 row stride mismatch.");
    TEST_ASSERT(img2bin_encode_raw_image(IMG2BIN_FMT_A1, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
    TEST_ASSERT(encoded_size == 4, "A1 payload size mismatch.");
    test_expect_bytes(encoded, expected, 4, "A1 threshold and padding bytes");
    free(encoded);
    encoded = NULL;
  }
}

static void test_imprle_segments_for_group_sizes(void)
{
  const img2bin_pixel_format_t formats[] = {
    IMG2BIN_FMT_ARGB2222,
    IMG2BIN_FMT_RAGB5155,
    IMG2BIN_FMT_RGB888,
    IMG2BIN_FMT_ARGB8888
  };
  const char *labels[] = {
    "ARGB2222 improved RLE",
    "RAGB5155 improved RLE",
    "RGB888 improved RLE",
    "ARGB8888 improved RLE"
  };
  unsigned char pixels[] = {
    0x10, 0x20, 0x30, 0x40,
    0xA0, 0xB0, 0xC0, 0xD0,
    0xA0, 0xB0, 0xC0, 0xD0,
    0x22, 0x44, 0x66, 0x88
  };
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  size_t format_index = 0;

  image.width = 4;
  image.height = 1;
  image.pixels = pixels;

  for (format_index = 0; format_index < sizeof(formats) / sizeof(formats[0]); ++format_index) {
    const img2bin_format_info_t *info = img2bin_get_format_info(formats[format_index]);
    unsigned char *raw_bytes = NULL;
    unsigned char *encoded = NULL;
    unsigned char expected[32];
    size_t raw_size = 0;
    size_t encoded_size = 0;
    size_t expected_size = 0;
    char error[256];

    TEST_ASSERT(info != NULL, "Improved-RLE test could not resolve format info.");
    TEST_ASSERT(
      img2bin_encode_raw_image(formats[format_index], IMG2BIN_ENDIAN_BIG, background, &image, &raw_bytes, &raw_size, error, sizeof(error)),
      error);
    TEST_ASSERT(
      img2bin_encode_imprle_image(formats[format_index], IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)),
      error);

    expected[expected_size++] = 0x01u;
    memcpy(expected + expected_size, raw_bytes, info->bytes_per_pixel);
    expected_size += info->bytes_per_pixel;
    expected[expected_size++] = 0x82u;
    memcpy(expected + expected_size, raw_bytes + info->bytes_per_pixel, info->bytes_per_pixel);
    expected_size += info->bytes_per_pixel;
    expected[expected_size++] = 0x01u;
    memcpy(expected + expected_size, raw_bytes + (info->bytes_per_pixel * 3u), info->bytes_per_pixel);
    expected_size += info->bytes_per_pixel;
    expected[expected_size++] = 0x00u;

    TEST_ASSERT(encoded_size == expected_size, "Improved-RLE size mismatch for grouped segment test.");
    test_expect_bytes(encoded, expected, expected_size, labels[format_index]);
    free(raw_bytes);
    free(encoded);
  }
}

static void test_imprle_split_boundaries(void)
{
  unsigned char repeat_pixels[130 * 4];
  unsigned char literal_pixels[128 * 4];
  img2bin_image_t repeat_image;
  img2bin_image_t literal_image;
  img2bin_rgb_t background = { 0, 0, 0 };
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];
  size_t index = 0;

  for (index = 0; index < 130u; ++index) {
    repeat_pixels[index * 4 + 0] = 0x11;
    repeat_pixels[index * 4 + 1] = 0x22;
    repeat_pixels[index * 4 + 2] = 0x33;
    repeat_pixels[index * 4 + 3] = 0xFF;
  }

  for (index = 0; index < 128u; ++index) {
    unsigned char code = (unsigned char)index;
    literal_pixels[index * 4 + 0] = (unsigned char)(((code >> 5) & 0x07u) * 255u / 7u);
    literal_pixels[index * 4 + 1] = (unsigned char)(((code >> 2) & 0x07u) * 255u / 7u);
    literal_pixels[index * 4 + 2] = (unsigned char)((code & 0x03u) * 255u / 3u);
    literal_pixels[index * 4 + 3] = 0xFF;
  }

  repeat_image.width = 130;
  repeat_image.height = 1;
  repeat_image.pixels = repeat_pixels;
  literal_image.width = 128;
  literal_image.height = 1;
  literal_image.pixels = literal_pixels;

  TEST_ASSERT(
    img2bin_encode_imprle_image(IMG2BIN_FMT_RGB332, IMG2BIN_ENDIAN_BIG, background, &repeat_image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == 5, "Improved-RLE repeat split should produce 5 bytes.");
  test_expect_bytes(encoded, (const unsigned char[]){ 0xFF, 0x05, 0x83, 0x05, 0x00 }, 5, "Improved-RLE repeat split");
  free(encoded);

  TEST_ASSERT(
    img2bin_encode_imprle_image(IMG2BIN_FMT_RGB332, IMG2BIN_ENDIAN_BIG, background, &literal_image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == 131, "Improved-RLE literal split should produce 131 bytes.");
  TEST_ASSERT(encoded[0] == 0x7F, "Improved-RLE literal split missing first chunk length.");
  for (index = 0; index < 127u; ++index) {
    TEST_ASSERT(encoded[1 + index] == (unsigned char)index, "Improved-RLE literal split first chunk payload mismatch.");
  }
  TEST_ASSERT(encoded[128] == 0x01, "Improved-RLE literal split missing second chunk length.");
  TEST_ASSERT(encoded[129] == 0x7F, "Improved-RLE literal split second chunk payload mismatch.");
  TEST_ASSERT(encoded[130] == 0x00, "Improved-RLE literal split missing terminator.");
  free(encoded);
}

/* 参考样例目录 参考/取模例子/ 属于可选素材，不随仓库分发（云端 CI 的干净检出上也没有）。
   缺失时相关测试跳过而不是失败，保证 ctest 在任意干净检出上都能全绿。 */
static int test_reference_sample_ready(const char *sample_path, const char *test_name)
{
  if (img2bin_is_regular_file(sample_path)) {
    return 1;
  }
  printf("TEST SKIP: %s (optional reference sample is absent: %s)\n", test_name, sample_path);
  return 0;
}

static void test_imprle_reference_sample(void)
{
  char image_path[IMG2BIN_PATH_CAPACITY];
  char text_path[IMG2BIN_PATH_CAPACITY];
  char error[256];
  char *text = NULL;
  unsigned char *expected = NULL;
  unsigned char *encoded = NULL;
  size_t expected_size = 0;
  size_t encoded_size = 0;
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };

  memset(&image, 0, sizeof(image));
  test_join_source_path("参考/取模例子/取模图片.png", image_path, sizeof(image_path));
  test_join_source_path("参考/取模例子/ARGB8888-改进RLE-数组.txt", text_path, sizeof(text_path));

  if (!test_reference_sample_ready(image_path, "imprle reference sample")) {
    return;
  }

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  text = test_read_text_file(text_path);
  TEST_ASSERT(text != NULL, "Could not read improved-RLE reference text.");
  TEST_ASSERT(test_extract_escaped_hex_bytes(text, &expected, &expected_size), "Could not extract improved-RLE reference bytes.");
  TEST_ASSERT(
    img2bin_encode_imprle_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == expected_size, "Improved-RLE reference output size mismatch.");
  test_expect_bytes(encoded, expected, expected_size, "Improved-RLE ARGB8888 reference sample");

  img2bin_free_image(&image);
  free(text);
  free(expected);
  free(encoded);
}

static void test_rle_segments_for_group_sizes(void)
{
  const img2bin_pixel_format_t formats[] = {
    IMG2BIN_FMT_ARGB2222,
    IMG2BIN_FMT_RAGB5155,
    IMG2BIN_FMT_RGB888,
    IMG2BIN_FMT_ARGB8888
  };
  const char *labels[] = {
    "ARGB2222 original RLE",
    "RAGB5155 original RLE",
    "RGB888 original RLE",
    "ARGB8888 original RLE"
  };
  unsigned char pixels[] = {
    0x10, 0x20, 0x30, 0x40,
    0xA0, 0xB0, 0xC0, 0xD0,
    0xA0, 0xB0, 0xC0, 0xD0,
    0x22, 0x44, 0x66, 0x88
  };
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  size_t format_index = 0;

  image.width = 4;
  image.height = 1;
  image.pixels = pixels;

  for (format_index = 0; format_index < sizeof(formats) / sizeof(formats[0]); ++format_index) {
    const img2bin_format_info_t *info = img2bin_get_format_info(formats[format_index]);
    unsigned char *raw_bytes = NULL;
    unsigned char *encoded = NULL;
    unsigned char expected[32];
    size_t raw_size = 0;
    size_t encoded_size = 0;
    size_t expected_size = 0;
    char error[256];

    TEST_ASSERT(info != NULL, "Original-RLE test could not resolve format info.");
    TEST_ASSERT(
      img2bin_encode_raw_image(formats[format_index], IMG2BIN_ENDIAN_BIG, background, &image, &raw_bytes, &raw_size, error, sizeof(error)),
      error);
    TEST_ASSERT(
      img2bin_encode_rle_image(formats[format_index], IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)),
      error);

    expected[expected_size++] = 0x01u;
    memcpy(expected + expected_size, raw_bytes, info->bytes_per_pixel);
    expected_size += info->bytes_per_pixel;
    expected[expected_size++] = 0x02u;
    memcpy(expected + expected_size, raw_bytes + info->bytes_per_pixel, info->bytes_per_pixel);
    expected_size += info->bytes_per_pixel;
    expected[expected_size++] = 0x01u;
    memcpy(expected + expected_size, raw_bytes + (info->bytes_per_pixel * 3u), info->bytes_per_pixel);
    expected_size += info->bytes_per_pixel;
    expected[expected_size++] = 0x00u;

    TEST_ASSERT(encoded_size == expected_size, "Original-RLE size mismatch for grouped segment test.");
    test_expect_bytes(encoded, expected, expected_size, labels[format_index]);
    free(raw_bytes);
    free(encoded);
  }
}

static void test_rle_split_boundaries(void)
{
  unsigned char repeat_pixels[260 * 4];
  img2bin_image_t repeat_image;
  img2bin_rgb_t background = { 0, 0, 0 };
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];
  size_t index = 0;

  for (index = 0; index < 260u; ++index) {
    repeat_pixels[index * 4 + 0] = 0x11;
    repeat_pixels[index * 4 + 1] = 0x22;
    repeat_pixels[index * 4 + 2] = 0x33;
    repeat_pixels[index * 4 + 3] = 0xFF;
  }

  repeat_image.width = 260;
  repeat_image.height = 1;
  repeat_image.pixels = repeat_pixels;

  TEST_ASSERT(
    img2bin_encode_rle_image(IMG2BIN_FMT_RGB332, IMG2BIN_ENDIAN_BIG, background, &repeat_image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == 5, "Original-RLE split should produce 5 bytes.");
  test_expect_bytes(encoded, (const unsigned char[]){ 0xFF, 0x05, 0x05, 0x05, 0x00 }, 5, "Original-RLE repeat split");
  free(encoded);
}

static void test_rle_reference_sample(void)
{
  char image_path[IMG2BIN_PATH_CAPACITY];
  char text_path[IMG2BIN_PATH_CAPACITY];
  char error[256];
  char *text = NULL;
  unsigned char *expected = NULL;
  unsigned char *encoded = NULL;
  size_t expected_size = 0;
  size_t encoded_size = 0;
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };

  memset(&image, 0, sizeof(image));
  test_join_source_path("参考/取模例子/取模图片.png", image_path, sizeof(image_path));
  test_join_source_path("参考/取模例子/ARGB8888-原始RLE-数组.txt", text_path, sizeof(text_path));

  if (!test_reference_sample_ready(image_path, "rle reference sample")) {
    return;
  }

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  text = test_read_text_file(text_path);
  TEST_ASSERT(text != NULL, "Could not read original-RLE reference text.");
  TEST_ASSERT(test_extract_escaped_hex_bytes(text, &expected, &expected_size), "Could not extract original-RLE reference bytes.");
  TEST_ASSERT(
    img2bin_encode_rle_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == expected_size, "Original-RLE reference output size mismatch.");
  test_expect_bytes(encoded, expected, expected_size, "Original-RLE ARGB8888 reference sample");

  img2bin_free_image(&image);
  free(text);
  free(expected);
  free(encoded);
}

static void test_qoi_argb_name_order(void)
{
  unsigned char pixels[] = { 0x12, 0x34, 0x56, 0x78 };
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const unsigned char expected[] = { 0xFF, 0x78, 0x12, 0x34, 0x56 };

  memset(&image, 0, sizeof(image));
  image.width = 1;
  image.height = 1;
  image.pixels = pixels;

  TEST_ASSERT(
    img2bin_encode_qoi_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == sizeof(expected), "Original-QOI ARGB raw chunk size mismatch.");
  test_expect_bytes(encoded, expected, sizeof(expected), "Original-QOI ARGB name-order chunk");
  free(encoded);
}

static void test_qoi_run_split_boundaries(void)
{
  unsigned char pixels[64 * 4];
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  size_t index = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const unsigned char expected[] = { 0xA7, 0x11, 0xFD, 0xC0 };

  for (index = 0; index < 64u; ++index) {
    pixels[index * 4 + 0] = 0x00;
    pixels[index * 4 + 1] = 0xFF;
    pixels[index * 4 + 2] = 0x00;
    pixels[index * 4 + 3] = 0xFF;
  }

  memset(&image, 0, sizeof(image));
  image.width = 64;
  image.height = 1;
  image.pixels = pixels;

  TEST_ASSERT(
    img2bin_encode_qoi_image(IMG2BIN_FMT_RGB332, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == sizeof(expected), "Original-QOI run split output size mismatch.");
  test_expect_bytes(encoded, expected, sizeof(expected), "Original-QOI run split");
  free(encoded);
}

static void test_qoi_reference_sample(void)
{
  char image_path[IMG2BIN_PATH_CAPACITY];
  char text_path[IMG2BIN_PATH_CAPACITY];
  char error[256];
  char *text = NULL;
  unsigned char *expected = NULL;
  unsigned char *encoded = NULL;
  size_t expected_size = 0;
  size_t encoded_size = 0;
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };

  memset(&image, 0, sizeof(image));
  test_join_source_path("参考/取模例子/取模图片.png", image_path, sizeof(image_path));
  test_join_source_path("参考/取模例子/ARGB8888-原始QOI-数组.txt", text_path, sizeof(text_path));

  if (!test_reference_sample_ready(image_path, "qoi reference sample")) {
    return;
  }

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  text = test_read_text_file(text_path);
  TEST_ASSERT(text != NULL, "Could not read original-QOI reference text.");
  TEST_ASSERT(test_extract_escaped_hex_bytes(text, &expected, &expected_size), "Could not extract original-QOI reference bytes.");
  TEST_ASSERT(
    img2bin_encode_qoi_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == expected_size, "Original-QOI reference output size mismatch.");
  test_expect_bytes(encoded, expected, expected_size, "Original-QOI ARGB8888 reference sample");

  img2bin_free_image(&image);
  free(text);
  free(expected);
  free(encoded);
}

static void test_qoif_omits_index_chunks(void)
{
  unsigned char pixels[] = {
    0xFF, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0xFF
  };
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const unsigned char expected[] = {
    0xFE, 0xFF, 0x00, 0x00,
    0xFE, 0x00, 0x00, 0xFF,
    0xFE, 0xFF, 0x00, 0x00
  };

  memset(&image, 0, sizeof(image));
  image.width = 3;
  image.height = 1;
  image.pixels = pixels;

  TEST_ASSERT(
    img2bin_encode_qoif_image(IMG2BIN_FMT_RGB888, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == sizeof(expected), "Original-QOIF no-index output size mismatch.");
  test_expect_bytes(encoded, expected, sizeof(expected), "Original-QOIF omits index chunks");
  free(encoded);
}

static void test_qoif_reference_sample(void)
{
  char image_path[IMG2BIN_PATH_CAPACITY];
  char text_path[IMG2BIN_PATH_CAPACITY];
  char error[256];
  char *text = NULL;
  unsigned char *expected = NULL;
  unsigned char *encoded = NULL;
  size_t expected_size = 0;
  size_t encoded_size = 0;
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };

  memset(&image, 0, sizeof(image));
  test_join_source_path("参考/取模例子/取模图片.png", image_path, sizeof(image_path));
  test_join_source_path("参考/取模例子/ARGB8888-原始QOI(无字典)-数组.txt", text_path, sizeof(text_path));

  if (!test_reference_sample_ready(image_path, "qoif reference sample")) {
    return;
  }

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  text = test_read_text_file(text_path);
  TEST_ASSERT(text != NULL, "Could not read original-QOIF reference text.");
  TEST_ASSERT(test_extract_escaped_hex_bytes(text, &expected, &expected_size), "Could not extract original-QOIF reference bytes.");
  if (expected_size >= 2u && expected[expected_size - 2u] == 0xA0u && expected[expected_size - 1u] == 0x88u) {
    expected_size -= 2u;
  }
  TEST_ASSERT(
    img2bin_encode_qoif_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == expected_size, "Original-QOIF reference output size mismatch.");
  test_expect_bytes(encoded, expected, expected_size, "Original-QOIF ARGB8888 reference sample");

  img2bin_free_image(&image);
  free(text);
  free(expected);
  free(encoded);
}

static void test_indexqoi_header_and_offsets(void)
{
  /* V2：4 色各出现一次，收益都是 pix_size(3)，不满足 “净收益 > pix_size”，
     因此调色盘为空（条目数 0），段首/全量一律 0xFF，无尾部结束码。 */
  unsigned char pixels[] = {
    0xFF, 0x00, 0x00, 0xFF,
    0x00, 0xFF, 0x00, 0xFF,
    0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
  };
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const unsigned char expected[] = {
    0x0E, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x08,
    0xFF, 0xFF, 0x00, 0x00,
    0xFF, 0x00, 0xFF, 0x00,
    0xFF, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
  };

  memset(&image, 0, sizeof(image));
  image.width = 4;
  image.height = 1;
  image.pixels = pixels;

  TEST_ASSERT(
    img2bin_encode_indexqoi_image(IMG2BIN_FMT_RGB888, IMG2BIN_ENDIAN_BIG, background, &image, 2u, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == sizeof(expected), "IndexQOI header test size mismatch.");
  test_expect_bytes(encoded, expected, sizeof(expected), "IndexQOI V2 header and offsets");
  free(encoded);
}

static void test_indexqoi_v2_palette_golden_rgb565(void)
{
  /* 两遍法选盘黄金样例：红出现 3 次(省6)、蓝 2 次(省4)进盘（阈值 >2），
     (1,1,1) 只省 2 不进盘；覆盖 调色盘op/0xFF全量/RUN/DIFF/LUMA/结尾标志。 */
  unsigned char pixels[] = {
    255, 0, 0, 255,
    0, 0, 255, 255,
    255, 0, 0, 255,
    0, 0, 255, 255,
    255, 0, 0, 255,
    8, 4, 8, 255,
    8, 4, 8, 255,
    16, 8, 16, 255,
    41, 12, 24, 255
  };
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const unsigned char expected[] = {
    /* 14 字节索引头：0x0E, 宽9, 高1, 间隔9, u16=2, u24=0, u32=0, 调色盘2项 */
    0x0E, 0x00, 0x09, 0x00, 0x01, 0x00, 0x09, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02,
    /* u16 索引：段首偏移 0 */
    0x00, 0x00,
    /* 调色盘：红 0xF800、蓝 0x001F（RGB565 大端） */
    0xF8, 0x00, 0x00, 0x1F,
    /* 数据流：盘0 盘1 盘0 盘1 盘0 FF(1,1,1) RUN1 DIFF(+1,+1,+1) LUMA(dg=1,dr-dg=2,db-dg=0) */
    0x00, 0x01, 0x00, 0x01, 0x00, 0xFF, 0x08, 0x21, 0xC0, 0x7F, 0xA1, 0xA8
  };

  memset(&image, 0, sizeof(image));
  image.width = 9;
  image.height = 1;
  image.pixels = pixels;

  TEST_ASSERT(
    img2bin_encode_indexqoi_image(IMG2BIN_FMT_RGB565, IMG2BIN_ENDIAN_BIG, background, &image, 0u, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == sizeof(expected), "IndexQOI V2 RGB565 palette golden size mismatch.");
  test_expect_bytes(encoded, expected, sizeof(expected), "IndexQOI V2 RGB565 palette golden");
  free(encoded);
}

static void test_indexqoi_v2_palette_golden_argb8888(void)
{
  /* 覆盖：透明度变化像素命中调色盘出单字节 op、段首盘命中、0xFE 剥透明度
     全量（仅 ARGB8888/8565）、按收益降序选盘（12/8/6）。 */
  unsigned char pixels[] = {
    10, 20, 30, 255,
    10, 20, 30, 128,
    10, 20, 30, 255,
    200, 100, 50, 255,
    10, 20, 30, 128,
    10, 20, 30, 255,
    100, 200, 50, 255,
    200, 100, 50, 255
  };
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const unsigned char expected[] = {
    /* 14 字节索引头：0x0E, 宽4, 高2, 间隔4, u16=4, u24=0, u32=0, 调色盘3项 */
    0x0E, 0x00, 0x04, 0x00, 0x02, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x03,
    /* u16 索引：两个段首偏移 0 与 4 */
    0x00, 0x00, 0x00, 0x04,
    /* 调色盘（ARGB8888 大端 A,R,G,B）：省12 的 (10,20,30,255)、省8 的
       (10,20,30,128)、省6 的 (200,100,50,255) */
    0xFF, 0x0A, 0x14, 0x1E,
    0x80, 0x0A, 0x14, 0x1E,
    0xFF, 0xC8, 0x64, 0x32,
    /* 数据流：盘0 盘1(透明度变化也命中) 盘0 盘2 | 段首盘1 盘0 FE(100,200,50) 盘2 */
    0x00, 0x01, 0x00, 0x02, 0x01, 0x00, 0xFE, 0x64, 0xC8, 0x32, 0x02
  };

  memset(&image, 0, sizeof(image));
  image.width = 4;
  image.height = 2;
  image.pixels = pixels;

  TEST_ASSERT(
    img2bin_encode_indexqoi_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, 0u, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == sizeof(expected), "IndexQOI V2 ARGB8888 palette golden size mismatch.");
  test_expect_bytes(encoded, expected, sizeof(expected), "IndexQOI V2 ARGB8888 palette golden");
  free(encoded);
}

static void test_indexqoi_default_interval_uses_image_width(void)
{
  char image_path[IMG2BIN_PATH_CAPACITY];
  char error[256];
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  size_t payload_position = 0u;
  size_t u16_bytes = 0u;
  size_t u24_bytes = 0u;
  size_t u32_bytes = 0u;
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };

  memset(&image, 0, sizeof(image));
  test_join_source_path("参考/取模例子/取模图片.png", image_path, sizeof(image_path));

  if (!test_reference_sample_ready(image_path, "indexqoi default-interval sample")) {
    return;
  }

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(
    img2bin_encode_indexqoi_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, 0u, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size >= 16u, "IndexQOI default-interval output is missing the header or index bytes.");
  TEST_ASSERT(encoded[0] == 0x0E, "IndexQOI default-interval header length mismatch.");
  TEST_ASSERT(encoded[1] == 0x00 && encoded[2] == 0x24, "IndexQOI default width mismatch.");
  TEST_ASSERT(encoded[3] == 0x00 && encoded[4] == 0x2D, "IndexQOI default height mismatch.");
  TEST_ASSERT(encoded[5] == 0x00 && encoded[6] == 0x24, "IndexQOI default interval should match image width.");
  TEST_ASSERT(encoded[7] == 0x00 && encoded[8] > 0x00, "IndexQOI should emit at least one u16 index entry.");
  TEST_ASSERT(encoded[13] <= 64u, "IndexQOI palette count must stay within 0..64.");
  TEST_ASSERT(encoded[14] == 0x00 && encoded[15] == 0x00, "IndexQOI first index should point at stream offset 0.");
  u16_bytes = ((size_t)encoded[7] << 8) | encoded[8];
  u24_bytes = ((size_t)encoded[9] << 8) | encoded[10];
  u32_bytes = ((size_t)encoded[11] << 8) | encoded[12];
  payload_position = 14u + u16_bytes + u24_bytes + u32_bytes + (size_t)encoded[13] * 4u;
  TEST_ASSERT(payload_position < encoded_size, "IndexQOI stream position exceeds output size.");
  TEST_ASSERT(encoded[payload_position] < encoded[13] || encoded[payload_position] == 0xFF,
    "IndexQOI stream should start with a palette op or a raw 0xFF chunk at the first index position.");

  img2bin_free_image(&image);
  free(encoded);
}

/* 索引QOI_MASK 黄金字节（q=8 无损）：覆盖 行首原始字节、RUN（含计数 0）、
   DIFF、INDEX、ALPHA、两遍法字典（频次 ≥2 进典、同频次按值降序）、
   行去重共享偏移、u16 行索引表；RGB 填干扰值证明只取 Alpha 通道。 */
static void test_indexqoimask_golden_values(void)
{
  /* 3 行 8 列；行1 与 行0 完全相同（去重后共享偏移 0）。
     行0: 10,10,10,10,12,11,60,200 -> 首字节10, RUN×3, DIFF(+2,-1), 60/200 落字典
     行2: 200,60,60,200,200,200,200,200 -> 首字节200, INDEX, RUN×1, INDEX, RUN×4
     ALPHA 兜底频次: 60×2、200×2 -> 字典 [200, 60]（同频次值降序） */
  const unsigned char alphas[24] = {
    10, 10, 10, 10, 12, 11, 60, 200,
    10, 10, 10, 10, 12, 11, 60, 200,
    200, 60, 60, 200, 200, 200, 200, 200
  };
  const unsigned char expected[] = {
    0x00,                               /* 标志位: q=8 (b1:b0=00) */
    0x00, 0x03,                         /* u16 行索引项数量 m=3 */
    0x00, 0x00,                         /* u32 行索引项数量 = 高-m = 0 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x05, /* u16 行索引: 0, 0（去重）, 5 */
    0x02, 0xC8, 0x3C,                   /* 字典 2 项: 200, 60 */
    0x0A, 0xC2, 0x73, 0x01, 0x00,      /* 行0: 10, RUN c=2, DIFF(6,3), INDEX1, INDEX0 */
    0xC8, 0x01, 0xC0, 0x00, 0xC3       /* 行2: 200, INDEX1, RUN c=0, INDEX0, RUN c=3 */
  };
  unsigned char pixels[24 * 4];
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 255, 255, 255 };
  size_t index = 0;

  memset(pixels, 0xDE, sizeof(pixels));
  for (index = 0; index < 24; ++index) {
    pixels[index * 4 + 3] = alphas[index];
  }
  image.width = 8;
  image.height = 3;
  image.pixels = pixels;

  TEST_ASSERT(
    img2bin_encode_indexqoimask_image(IMG2BIN_FMT_A8, IMG2BIN_ENDIAN_BIG, background, &image, 8u, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == sizeof(expected), "IndexQOI mask q=8 golden size mismatch.");
  test_expect_bytes(encoded, expected, sizeof(expected), "IndexQOI mask q=8 golden payload");
  free(encoded);
  encoded = NULL;

  /* 字节序对本算法无影响：le 输出必须与 be 逐字节一致。 */
  TEST_ASSERT(
    img2bin_encode_indexqoimask_image(IMG2BIN_FMT_A8, IMG2BIN_ENDIAN_LITTLE, background, &image, 8u, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == sizeof(expected), "IndexQOI mask le/be size mismatch.");
  test_expect_bytes(encoded, expected, sizeof(expected), "IndexQOI mask endianness-neutral payload");
  free(encoded);
}

/* 量化档位黄金：quantize_bits 传 0 时默认 6bit（v = a>>2、流内全为 6 位域值、
   DELTA 差分不回绕）；同时验证解码端按高位复制扩展的 8bit 输出。 */
static void test_indexqoimask_quantize_golden_and_default_bits(void)
{
  const unsigned char alphas[4] = { 255, 247, 128, 0 };
  const unsigned char expected[] = {
    0x02,                   /* 标志位: q=6 (b1:b0=10) */
    0x00, 0x01, 0x00, 0x00, /* m=1, u32 项数 0 */
    0x00, 0x00,             /* 行0 偏移 0 */
    0x00,                   /* 字典 0 项 */
    0x3F, 0x9E, 0x83, 0x80  /* 首字节63, DELTA-2, DELTA-29, DELTA-32 */
  };
  const unsigned char expected_alpha[4] = { 255, 247, 130, 0 }; /* expand(63/61/32/0) */
  unsigned char pixels[4 * 4];
  unsigned char decoded[4];
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  size_t decoded_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;
  size_t index = 0;

  memset(pixels, 0xAD, sizeof(pixels));
  for (index = 0; index < 4; ++index) {
    pixels[index * 4 + 3] = alphas[index];
  }
  image.width = 4;
  image.height = 1;
  image.pixels = pixels;

  TEST_ASSERT(
    img2bin_encode_indexqoimask_image(IMG2BIN_FMT_A8, IMG2BIN_ENDIAN_BIG, background, &image, 0u, &encoded, &encoded_size, error, sizeof(error)),
    error);
  TEST_ASSERT(encoded_size == sizeof(expected), "IndexQOI mask default-bits golden size mismatch.");
  test_expect_bytes(encoded, expected, sizeof(expected), "IndexQOI mask default 6-bit golden payload");

  status = img2bin_decode_indexqoimask(encoded, encoded_size, 4u, 1u, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_OK, "IndexQOI mask 6-bit decode failed.");
  TEST_ASSERT(decoded_size == 4u, "IndexQOI mask 6-bit decode size mismatch.");
  test_expect_bytes(decoded, expected_alpha, 4, "IndexQOI mask 6-bit high-bit replication expansion");
  free(encoded);
}

static void test_image_loading_for_png_bmp_jpg(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char png_path[IMG2BIN_PATH_CAPACITY];
  char bmp_path[IMG2BIN_PATH_CAPACITY];
  char jpg_path[IMG2BIN_PATH_CAPACITY];
  unsigned char rgba_pixels[4 * 4 * 4];
  img2bin_image_t image;
  char error[256];
  int pixel_index;

  for (pixel_index = 0; pixel_index < 16; ++pixel_index) {
    rgba_pixels[pixel_index * 4 + 0] = 17;
    rgba_pixels[pixel_index * 4 + 1] = 34;
    rgba_pixels[pixel_index * 4 + 2] = 51;
    rgba_pixels[pixel_index * 4 + 3] = 255;
  }

  test_make_stage_directory("loaders", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "solid.png", png_path, sizeof(png_path)), "Could not compose PNG path.");
  TEST_ASSERT(img2bin_path_join(stage, "solid.bmp", bmp_path, sizeof(bmp_path)), "Could not compose BMP path.");
  TEST_ASSERT(img2bin_path_join(stage, "solid.jpg", jpg_path, sizeof(jpg_path)), "Could not compose JPG path.");

  test_write_rgba_fixture(png_path, 4, 4, rgba_pixels);
  test_write_rgba_fixture(bmp_path, 4, 4, rgba_pixels);
  test_write_rgba_fixture(jpg_path, 4, 4, rgba_pixels);

  TEST_ASSERT(img2bin_load_image(png_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(image.width == 4 && image.height == 4, "PNG dimensions mismatch.");
  TEST_ASSERT(image.pixels[0] == 17 && image.pixels[1] == 34 && image.pixels[2] == 51 && image.pixels[3] == 255, "PNG pixels mismatch.");
  img2bin_free_image(&image);

  TEST_ASSERT(img2bin_load_image(bmp_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(image.width == 4 && image.height == 4, "BMP dimensions mismatch.");
  TEST_ASSERT(image.pixels[0] == 17 && image.pixels[1] == 34 && image.pixels[2] == 51 && image.pixels[3] == 255, "BMP pixels mismatch.");
  img2bin_free_image(&image);

  TEST_ASSERT(img2bin_load_image(jpg_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(image.width == 4 && image.height == 4, "JPG dimensions mismatch.");
  test_expect_channel_tolerance(image.pixels, rgba_pixels, 4 * 4 * 4, 2u, "JPG pixel tolerance");
  img2bin_free_image(&image);
}

static void test_info_json(void)
{
  char json[16384];

  TEST_ASSERT(img2bin_raw_get_info_json(json, sizeof(json)), "Could not build info JSON.");
  TEST_ASSERT(strstr(json, "\"schema_version\": \"" IMG2BIN_INFO_SCHEMA_VERSION "\"") != NULL, "Info JSON missing schema version.");
  TEST_ASSERT(strstr(json, "\"id\": \"img2bin_raw\"") != NULL, "Info JSON missing tool id.");
  TEST_ASSERT(strstr(json, "\"id\": \"raw\"") != NULL, "Info JSON missing algorithm id.");
  TEST_ASSERT(strstr(json, "\"version\": \"" IMG2BIN_RAW_VERSION_TEXT "\"") != NULL, "Info JSON missing display version.");
  TEST_ASSERT(strstr(json, "\"version_semver\": \"" IMG2BIN_RAW_VERSION_SEMVER "\"") != NULL, "Info JSON missing semver.");
  TEST_ASSERT(strstr(json, "\"display_name\": {") != NULL, "Info JSON missing display name container.");
  TEST_ASSERT(strstr(json, "\"description\": {") != NULL, "Info JSON missing description container.");
  TEST_ASSERT(strstr(json, "\"zh_cn\": \"无压缩取模\"") != NULL, "Info JSON missing Chinese display name.");
  TEST_ASSERT(strstr(json, "\"en\": \"Raw Image Converter\"") != NULL, "Info JSON missing English display name.");
  TEST_ASSERT(strstr(json, "\"gui_category\": {") != NULL, "Info JSON missing GUI category container.");
  TEST_ASSERT(strstr(json, "\"priority\": 10") != NULL, "Info JSON missing GUI priority.");
  TEST_ASSERT(strstr(json, "\"algorithm_code\": \"raw\"") != NULL, "Info JSON missing algorithm code.");
  TEST_ASSERT(strstr(json, "\"style\": \"flag_cli\"") != NULL, "Info JSON missing invocation style.");
  TEST_ASSERT(strstr(json, "\"help_flag\": \"--help\"") != NULL, "Info JSON missing help flag.");
  TEST_ASSERT(strstr(json, "\"id\": \"little_endian\"") != NULL, "Info JSON missing little-endian argument metadata.");
  TEST_ASSERT(strstr(json, "\"flag\": \"--manifest\"") != NULL, "Info JSON missing manifest argument metadata.");
  TEST_ASSERT(strstr(json, "\"value_type\": \"hex_rgb\"") != NULL, "Info JSON missing bg_color value type.");
  TEST_ASSERT(strstr(json, "\"default\": \"exe_dir/input\"") != NULL, "Info JSON missing default input path.");
  TEST_ASSERT(strstr(json, "\"default\": \"exe_dir/output\"") != NULL, "Info JSON missing default output path.");
  TEST_ASSERT(strstr(json, "\"filename_pattern\": \"{source_stem}_{format_name}_raw_{endianness_token}_{width}x{height}.bin\"") != NULL, "Info JSON missing output filename pattern.");
  TEST_ASSERT(strstr(json, "\"batch_partial_failure\": 6") != NULL, "Info JSON missing batch failure exit code.");
  TEST_ASSERT(strstr(json, "\"query_flag\": \"--info\"") != NULL, "Info JSON missing query flag.");
  TEST_ASSERT(strstr(json, "\"display_name\": {\n        \"zh_cn\": \"ARGB8888\"") != NULL, "Info JSON missing ARGB8888 display name.");
  TEST_ASSERT(strstr(json, "\"uses_background_color\": true") != NULL, "Info JSON missing background-color capability.");
  TEST_ASSERT(strstr(json, "\"endianness_affects_output\": false") != NULL, "Info JSON missing one-byte endianness metadata.");
  TEST_ASSERT(strstr(json, "\"name\": \"a8\"") != NULL, "Raw info JSON missing a8 alpha mask format.");
  TEST_ASSERT(strstr(json, "\"name\": \"a4\"") != NULL, "Raw info JSON missing a4 alpha mask format.");
  TEST_ASSERT(strstr(json, "\"name\": \"a2\"") != NULL, "Raw info JSON missing a2 alpha mask format.");
  TEST_ASSERT(strstr(json, "\"name\": \"a1\"") != NULL, "Raw info JSON missing a1 alpha mask format.");
  TEST_ASSERT(strstr(json, "\"bits_per_pixel\": 4") != NULL, "Raw info JSON missing sub-byte bits-per-pixel metadata.");
  TEST_ASSERT(strstr(json, "\"is_alpha_only\": true") != NULL, "Raw info JSON missing alpha-only capability.");
  TEST_ASSERT(strstr(json, "\"format\": \"rgb565\"") != NULL, "Raw info JSON missing rgb565 default format.");
  TEST_ASSERT(strstr(json, "\"supports_quantize_bits\": false") != NULL, "Raw info JSON missing quantize-bits capability flag.");
  TEST_ASSERT(strstr(json, "\"flag\": \"--quantize-bits\"") == NULL, "Raw info JSON must not list the quantize-bits argument.");
  TEST_ASSERT(strstr(json, "\"supports_multi_format\": true") != NULL, "Raw info JSON missing multi-format capability.");
}

static void test_imprle_info_json(void)
{
  char json[16384];

  TEST_ASSERT(img2bin_imprle_get_info_json(json, sizeof(json)), "Could not build improved-RLE info JSON.");
  TEST_ASSERT(strstr(json, "\"schema_version\": \"" IMG2BIN_INFO_SCHEMA_VERSION "\"") != NULL, "Improved-RLE info JSON missing schema version.");
  TEST_ASSERT(strstr(json, "\"id\": \"img2bin_imprle\"") != NULL, "Improved-RLE info JSON missing tool id.");
  TEST_ASSERT(strstr(json, "\"id\": \"imprle\"") != NULL, "Improved-RLE info JSON missing algorithm id.");
  TEST_ASSERT(strstr(json, "\"algorithm_code\": \"imprle\"") != NULL, "Improved-RLE info JSON missing algorithm code.");
  TEST_ASSERT(strstr(json, "\"compression\": \"improved_rle\"") != NULL, "Improved-RLE info JSON missing compression type.");
  TEST_ASSERT(strstr(json, "\"zh_cn\": \"改进RLE取模\"") != NULL, "Improved-RLE info JSON missing Chinese display name.");
  TEST_ASSERT(strstr(json, "\"en\": \"Improved RLE Image Converter\"") != NULL, "Improved-RLE info JSON missing English display name.");
  TEST_ASSERT(strstr(json, "\"filename_pattern\": \"{source_stem}_{format_name}_imprle_{endianness_token}_{width}x{height}.bin\"") != NULL, "Improved-RLE info JSON missing filename pattern.");
  TEST_ASSERT(strstr(json, "\"name\": \"a8\"") == NULL, "Improved-RLE info JSON must not list alpha mask formats.");
  TEST_ASSERT(strstr(json, "\"is_alpha_only\": true") == NULL, "Improved-RLE info JSON must not flag alpha-only formats.");
}

static void test_rle_info_json(void)
{
  char json[16384];

  TEST_ASSERT(img2bin_rle_get_info_json(json, sizeof(json)), "Could not build original-RLE info JSON.");
  TEST_ASSERT(strstr(json, "\"schema_version\": \"" IMG2BIN_INFO_SCHEMA_VERSION "\"") != NULL, "Original-RLE info JSON missing schema version.");
  TEST_ASSERT(strstr(json, "\"id\": \"img2bin_rle\"") != NULL, "Original-RLE info JSON missing tool id.");
  TEST_ASSERT(strstr(json, "\"id\": \"rle\"") != NULL, "Original-RLE info JSON missing algorithm id.");
  TEST_ASSERT(strstr(json, "\"algorithm_code\": \"rle\"") != NULL, "Original-RLE info JSON missing algorithm code.");
  TEST_ASSERT(strstr(json, "\"compression\": \"rle\"") != NULL, "Original-RLE info JSON missing compression type.");
  TEST_ASSERT(strstr(json, "\"zh_cn\": \"原始RLE取模\"") != NULL, "Original-RLE info JSON missing Chinese display name.");
  TEST_ASSERT(strstr(json, "\"en\": \"Original RLE Image Converter\"") != NULL, "Original-RLE info JSON missing English display name.");
  TEST_ASSERT(strstr(json, "\"filename_pattern\": \"{source_stem}_{format_name}_rle_{endianness_token}_{width}x{height}.bin\"") != NULL, "Original-RLE info JSON missing filename pattern.");
  TEST_ASSERT(strstr(json, "\"name\": \"a8\"") == NULL, "Original-RLE info JSON must not list alpha mask formats.");
}

static void test_qoi_info_json(void)
{
  char json[16384];

  TEST_ASSERT(img2bin_qoi_get_info_json(json, sizeof(json)), "Could not build original-QOI info JSON.");
  TEST_ASSERT(strstr(json, "\"schema_version\": \"" IMG2BIN_INFO_SCHEMA_VERSION "\"") != NULL, "Original-QOI info JSON missing schema version.");
  TEST_ASSERT(strstr(json, "\"id\": \"img2bin_qoi\"") != NULL, "Original-QOI info JSON missing tool id.");
  TEST_ASSERT(strstr(json, "\"id\": \"qoi\"") != NULL, "Original-QOI info JSON missing algorithm id.");
  TEST_ASSERT(strstr(json, "\"algorithm_code\": \"qoi\"") != NULL, "Original-QOI info JSON missing algorithm code.");
  TEST_ASSERT(strstr(json, "\"compression\": \"qoi\"") != NULL, "Original-QOI info JSON missing compression type.");
  TEST_ASSERT(strstr(json, "\"zh_cn\": \"原始QOI取模\"") != NULL, "Original-QOI info JSON missing Chinese display name.");
  TEST_ASSERT(strstr(json, "\"en\": \"Original QOI Image Converter\"") != NULL, "Original-QOI info JSON missing English display name.");
  TEST_ASSERT(strstr(json, "\"filename_pattern\": \"{source_stem}_{format_name}_qoi_{endianness_token}_{width}x{height}.bin\"") != NULL, "Original-QOI info JSON missing filename pattern.");
  TEST_ASSERT(strstr(json, "\"name\": \"a8\"") == NULL, "Original-QOI info JSON must not list alpha mask formats.");
}

static void test_qoif_info_json(void)
{
  char json[16384];

  TEST_ASSERT(img2bin_qoif_get_info_json(json, sizeof(json)), "Could not build original-QOIF info JSON.");
  TEST_ASSERT(strstr(json, "\"schema_version\": \"" IMG2BIN_INFO_SCHEMA_VERSION "\"") != NULL, "Original-QOIF info JSON missing schema version.");
  TEST_ASSERT(strstr(json, "\"id\": \"img2bin_qoif\"") != NULL, "Original-QOIF info JSON missing tool id.");
  TEST_ASSERT(strstr(json, "\"id\": \"qoif\"") != NULL, "Original-QOIF info JSON missing algorithm id.");
  TEST_ASSERT(strstr(json, "\"algorithm_code\": \"qoif\"") != NULL, "Original-QOIF info JSON missing algorithm code.");
  TEST_ASSERT(strstr(json, "\"compression\": \"qoi_without_index\"") != NULL, "Original-QOIF info JSON missing compression type.");
  TEST_ASSERT(strstr(json, "\"zh_cn\": \"原始QOI(无字典)取模\"") != NULL, "Original-QOIF info JSON missing Chinese display name.");
  TEST_ASSERT(strstr(json, "\"en\": \"Original QOI Without Index Image Converter\"") != NULL, "Original-QOIF info JSON missing English display name.");
  TEST_ASSERT(strstr(json, "\"filename_pattern\": \"{source_stem}_{format_name}_qoif_{endianness_token}_{width}x{height}.bin\"") != NULL, "Original-QOIF info JSON missing filename pattern.");
  TEST_ASSERT(strstr(json, "\"name\": \"a8\"") == NULL, "Original-QOIF info JSON must not list alpha mask formats.");
}

static void test_indexqoi_info_json(void)
{
  char json[16384];

  TEST_ASSERT(img2bin_indexqoi_get_info_json(json, sizeof(json)), "Could not build IndexQOI info JSON.");
  TEST_ASSERT(strstr(json, "\"schema_version\": \"" IMG2BIN_INFO_SCHEMA_VERSION "\"") != NULL, "IndexQOI info JSON missing schema version.");
  TEST_ASSERT(strstr(json, "\"id\": \"img2bin_indexqoi\"") != NULL, "IndexQOI info JSON missing tool id.");
  TEST_ASSERT(strstr(json, "\"id\": \"indexqoi\"") != NULL, "IndexQOI info JSON missing algorithm id.");
  TEST_ASSERT(strstr(json, "\"algorithm_code\": \"indexqoi\"") != NULL, "IndexQOI info JSON missing algorithm code.");
  TEST_ASSERT(strstr(json, "\"compression\": \"indexed_qoi\"") != NULL, "IndexQOI info JSON missing compression type.");
  TEST_ASSERT(strstr(json, "\"zh_cn\": \"索引QOI取模\"") != NULL, "IndexQOI info JSON missing Chinese display name.");
  TEST_ASSERT(strstr(json, "\"en\": \"Indexed QOI Image Converter\"") != NULL, "IndexQOI info JSON missing English display name.");
  TEST_ASSERT(strstr(json, "\"filename_pattern\": \"{source_stem}_{format_name}_indexqoi_{endianness_token}_{width}x{height}.bin\"") != NULL, "IndexQOI info JSON missing filename pattern.");
  TEST_ASSERT(strstr(json, "\"supports_index_interval\": true") != NULL, "IndexQOI info JSON missing index-interval capability.");
  TEST_ASSERT(strstr(json, "\"index_interval\": \"image_width\"") != NULL, "IndexQOI info JSON missing default index interval.");
  TEST_ASSERT(strstr(json, "\"flag\": \"--index-interval\"") != NULL, "IndexQOI info JSON missing invocation flag for index interval.");
  TEST_ASSERT(strstr(json, "\"name\": \"a8\"") == NULL, "IndexQOI info JSON must not list alpha mask formats.");
}

static void test_indexqoimask_info_json(void)
{
  char json[16384];

  TEST_ASSERT(img2bin_indexqoimask_get_info_json(json, sizeof(json)), "Could not build IndexQOI mask info JSON.");
  TEST_ASSERT(strstr(json, "\"schema_version\": \"" IMG2BIN_INFO_SCHEMA_VERSION "\"") != NULL, "IndexQOI mask info JSON missing schema version.");
  TEST_ASSERT(strstr(json, "\"id\": \"img2bin_indexqoimask\"") != NULL, "IndexQOI mask info JSON missing tool id.");
  TEST_ASSERT(strstr(json, "\"id\": \"indexqoimask\"") != NULL, "IndexQOI mask info JSON missing algorithm id.");
  TEST_ASSERT(strstr(json, "\"algorithm_code\": \"indexqoimask\"") != NULL, "IndexQOI mask info JSON missing algorithm code.");
  TEST_ASSERT(strstr(json, "\"compression\": \"indexed_qoi_mask\"") != NULL, "IndexQOI mask info JSON missing compression type.");
  TEST_ASSERT(strstr(json, "\"zh_cn\": \"索引QOI蒙版取模\"") != NULL, "IndexQOI mask info JSON missing Chinese display name.");
  TEST_ASSERT(strstr(json, "\"en\": \"Indexed QOI Mask Converter\"") != NULL, "IndexQOI mask info JSON missing English display name.");
  TEST_ASSERT(strstr(json, "\"filename_pattern\": \"{source_stem}_{format_name}_indexqoimask_{endianness_token}_{width}x{height}.bin\"") != NULL, "IndexQOI mask info JSON missing filename pattern.");
  TEST_ASSERT(strstr(json, "\"algorithm_nibble\": 6") != NULL, "IndexQOI mask info JSON missing algorithm nibble.");
  TEST_ASSERT(strstr(json, "\"format\": \"a8\"") != NULL, "IndexQOI mask info JSON missing a8 default format.");
  TEST_ASSERT(strstr(json, "\"quantize_bits\": 6") != NULL, "IndexQOI mask info JSON missing default quantize bits.");
  TEST_ASSERT(strstr(json, "\"supports_quantize_bits\": true") != NULL, "IndexQOI mask info JSON missing quantize-bits capability.");
  TEST_ASSERT(strstr(json, "\"supports_index_interval\": false") != NULL, "IndexQOI mask info JSON must not claim index-interval support.");
  TEST_ASSERT(strstr(json, "\"supports_multi_format\": false") != NULL, "IndexQOI mask info JSON must not claim multi-format support.");
  TEST_ASSERT(strstr(json, "\"supports_multiple_formats\": false") != NULL, "IndexQOI mask info JSON must not claim multiple-format output.");
  TEST_ASSERT(strstr(json, "\"flag\": \"--quantize-bits\"") != NULL, "IndexQOI mask info JSON missing invocation flag for quantize bits.");
  TEST_ASSERT(strstr(json, "\"default\": \"a8\"") != NULL, "IndexQOI mask info JSON missing a8 default for the format argument.");
  TEST_ASSERT(strstr(json, "\"name\": \"a8\"") != NULL, "IndexQOI mask info JSON must list the a8 format.");
  TEST_ASSERT(strstr(json, "\"name\": \"a4\"") == NULL, "IndexQOI mask info JSON must not list a4.");
  TEST_ASSERT(strstr(json, "\"name\": \"a2\"") == NULL, "IndexQOI mask info JSON must not list a2.");
  TEST_ASSERT(strstr(json, "\"name\": \"a1\"") == NULL, "IndexQOI mask info JSON must not list a1.");
  TEST_ASSERT(strstr(json, "\"name\": \"rgb565\"") == NULL, "IndexQOI mask info JSON must not list color formats.");
  TEST_ASSERT(strstr(json, "\"name\": \"argb8888\"") == NULL, "IndexQOI mask info JSON must not list argb8888.");
  TEST_ASSERT(strstr(json, "\"is_alpha_only\": true") != NULL, "IndexQOI mask info JSON missing alpha-only metadata.");
}

static void test_cli_default_mode_and_unicode_paths(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char input_dir[IMG2BIN_PATH_CAPACITY];
  char fixture_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char default_output_dir[IMG2BIN_PATH_CAPACITY];
  char expected_output[IMG2BIN_PATH_CAPACITY];
  char default_manifest[IMG2BIN_PATH_CAPACITY];
  char absent_output_dir[IMG2BIN_PATH_CAPACITY];
  char absent_output[IMG2BIN_PATH_CAPACITY];
  char unicode_input[IMG2BIN_PATH_CAPACITY];
  char unicode_output_dir[IMG2BIN_PATH_CAPACITY];
  char unicode_manifest[IMG2BIN_PATH_CAPACITY];
  char copied_fixture[IMG2BIN_PATH_CAPACITY];
  char expected_all_outputs[IMG2BIN_FMT_COUNT][IMG2BIN_PATH_CAPACITY];
  const img2bin_format_info_t *formats = NULL;
  size_t format_count = 0;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0xFF };
  unsigned char *rgb565_bytes = NULL;
  size_t rgb565_size = 0;
  char error[256];
  const char *argv_default[] = { "img2bin_raw" };
  const char *argv_all[] = {
    "img2bin_raw",
    "--input",
    NULL,
    "--output",
    NULL,
    "--formats",
    "all",
    "--little-endian"
  };
  size_t format_index;

  test_make_stage_directory("cli", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "input", input_dir, sizeof(input_dir)), "Could not compose default input directory.");
  TEST_ASSERT(img2bin_make_dirs(input_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_path_join(input_dir, "sample.png", fixture_path, sizeof(fixture_path)), "Could not compose fixture path.");
  test_write_rgba_fixture(fixture_path, 1, 1, pixel);
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_raw.exe", exe_path, sizeof(exe_path)), "Could not compose executable path.");

  TEST_ASSERT(img2bin_raw_run_with_executable_path(1, argv_default, exe_path) == 0, "Default CLI run failed.");

  TEST_ASSERT(img2bin_path_join(stage, "output", default_output_dir, sizeof(default_output_dir)), "Could not compose output directory.");
  TEST_ASSERT(img2bin_path_join(default_output_dir, "sample_rgb565_raw_be_1x1.bin", expected_output, sizeof(expected_output)), "Could not compose expected RGB565 output.");
  TEST_ASSERT(img2bin_path_join(default_output_dir, "img2bin_raw-manifest.json", default_manifest, sizeof(default_manifest)), "Could not compose default manifest path.");
  TEST_ASSERT(img2bin_is_regular_file(expected_output), "Default RGB565 output file was not created.");
  TEST_ASSERT(!img2bin_is_regular_file(default_manifest), "Manifest logging is off by default and must not be written without --manifest.");

  TEST_ASSERT(img2bin_path_join(stage, "output", absent_output_dir, sizeof(absent_output_dir)), "Could not compose absent output directory.");
  TEST_ASSERT(img2bin_path_join(absent_output_dir, "sample_argb8888_raw_be_1x1.bin", absent_output, sizeof(absent_output)), "Could not compose absent output path.");
  TEST_ASSERT(!img2bin_is_regular_file(absent_output), "Default run should not emit non-default formats.");

  TEST_ASSERT(img2bin_encode_raw_image(
                IMG2BIN_FMT_RGB565,
                IMG2BIN_ENDIAN_BIG,
                (img2bin_rgb_t){ 0, 0, 0 },
                &(img2bin_image_t){ 1, 1, pixel },
                &rgb565_bytes,
                &rgb565_size,
                error,
                sizeof(error)),
              error);
  TEST_ASSERT(rgb565_size == 2, "RGB565 output size mismatch.");
  test_expect_bytes(rgb565_bytes, (const unsigned char[]){ 0x11, 0xAA }, 2, "Default RGB565 output bytes");
  free(rgb565_bytes);

  TEST_ASSERT(img2bin_path_join(stage, "fixture_source.png", copied_fixture, sizeof(copied_fixture)), "Could not compose unicode source fixture path.");
  test_write_rgba_fixture(copied_fixture, 1, 1, pixel);

  TEST_ASSERT(img2bin_path_join(stage, "out-\xE4\xB8\xAD\xE6\x96\x87 \xE7\xA9\xBA\xE6\xA0\xBC", unicode_output_dir, sizeof(unicode_output_dir)), "Could not compose unicode output directory.");
  TEST_ASSERT(img2bin_make_dirs(unicode_output_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_path_join(unicode_output_dir, "img2bin_raw-manifest.json", unicode_manifest, sizeof(unicode_manifest)), "Could not compose unicode manifest path.");
  TEST_ASSERT(img2bin_path_join(stage, "\xE5\x9B\xBE\xE7\x89\x87.png", unicode_input, sizeof(unicode_input)), "Could not compose unicode input path.");
  test_copy_file(copied_fixture, unicode_input);

  argv_all[2] = unicode_input;
  argv_all[4] = unicode_output_dir;
  TEST_ASSERT(img2bin_raw_run_with_executable_path(8, argv_all, exe_path) == 0, "CLI all-formats run failed.");

  formats = img2bin_get_format_infos(&format_count);
  for (format_index = 0; format_index < format_count; ++format_index) {
    char file_name[IMG2BIN_PATH_CAPACITY];
    snprintf(file_name, sizeof(file_name), "\xE5\x9B\xBE\xE7\x89\x87_%s_raw_le_1x1.bin", formats[format_index].name);
    TEST_ASSERT(img2bin_path_join(unicode_output_dir, file_name, expected_all_outputs[format_index], sizeof(expected_all_outputs[format_index])), "Could not compose all-format output path.");
    TEST_ASSERT(img2bin_is_regular_file(expected_all_outputs[format_index]), "Expected all-format output file is missing.");
  }
  TEST_ASSERT(!img2bin_is_regular_file(unicode_manifest), "Single-file CLI runs should not create a manifest file.");
}

static void test_default_mode_creates_missing_directories(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char input_dir[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  const char *argv_default[] = { "img2bin_raw" };

  test_make_stage_directory("default_create_dirs", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_raw.exe", exe_path, sizeof(exe_path)), "Could not compose executable path for default create-dirs test.");
  TEST_ASSERT(img2bin_path_join(stage, "input", input_dir, sizeof(input_dir)), "Could not compose input directory for default create-dirs test.");
  TEST_ASSERT(img2bin_path_join(stage, "output", output_dir, sizeof(output_dir)), "Could not compose output directory for default create-dirs test.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_raw-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose manifest path for default create-dirs test.");

  TEST_ASSERT(!img2bin_is_directory(input_dir), "Input directory should not exist before default create-dirs test.");
  TEST_ASSERT(!img2bin_is_directory(output_dir), "Output directory should not exist before default create-dirs test.");
  TEST_ASSERT(img2bin_raw_run_with_executable_path(1, argv_default, exe_path) == 0, "Default no-arg run should succeed when creating missing directories.");
  TEST_ASSERT(img2bin_is_directory(input_dir), "Default no-arg run did not create missing input directory.");
  TEST_ASSERT(img2bin_is_directory(output_dir), "Default no-arg run did not create missing output directory.");
  TEST_ASSERT(!img2bin_is_regular_file(manifest_path), "Default no-arg directory creation should not emit a manifest.");
}

static void test_error_json_for_invalid_cli(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char stderr_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char *stderr_text = NULL;
  int saved_fd = -1;
  int exit_code = 0;
  const char *argv_invalid[] = {
    "img2bin_raw",
    "--unknown-flag"
  };

  test_make_stage_directory("cli_errors", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "stderr-cli.jsonl", stderr_path, sizeof(stderr_path)), "Could not compose CLI stderr capture path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_raw.exe", exe_path, sizeof(exe_path)), "Could not compose executable override path.");
  TEST_ASSERT(test_redirect_stderr_begin(stderr_path, &saved_fd), "Could not redirect stderr for CLI error test.");

  exit_code = img2bin_raw_run_with_executable_path(2, argv_invalid, exe_path);

  TEST_ASSERT(test_redirect_stderr_end(saved_fd), "Could not restore stderr for CLI error test.");
  TEST_ASSERT(exit_code == 1, "Invalid CLI arguments should return exit code 1.");

  stderr_text = test_read_text_file(stderr_path);
  TEST_ASSERT(stderr_text != NULL, "Could not read captured CLI error output.");
  TEST_ASSERT(test_count_nonempty_lines(stderr_text) == 1, "CLI error output should contain exactly one JSON line.");
  TEST_ASSERT(strstr(stderr_text, "\"code\":\"cli_parse_failed\"") != NULL, "CLI error JSON missing code.");
  TEST_ASSERT(strstr(stderr_text, "\"exit_code\":1") != NULL, "CLI error JSON missing exit code.");
  TEST_ASSERT(strstr(stderr_text, "\"stage\":\"cli\"") != NULL, "CLI error JSON missing stage.");
  TEST_ASSERT(strstr(stderr_text, "\"zh_cn\":\"命令行参数无效。\"") != NULL, "CLI error JSON missing Chinese message.");
  TEST_ASSERT(strstr(stderr_text, "\"en\":\"Invalid command-line arguments.\"") != NULL, "CLI error JSON missing English message.");
  free(stderr_text);
}

/* Alpha 蒙版的工具门禁：非 raw 工具显式点名 A 格式报 CLI 错误、
   --formats all 静默滤除；raw 工具产出的 a4 文件含 0x0C 格式码头。 */
static void test_alpha_mask_cli_tool_gate(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char image_path[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char stderr_path[IMG2BIN_PATH_CAPACITY];
  char rle_exe[IMG2BIN_PATH_CAPACITY];
  char raw_exe[IMG2BIN_PATH_CAPACITY];
  char expected_a4[IMG2BIN_PATH_CAPACITY];
  char rejected_a8[IMG2BIN_PATH_CAPACITY];
  char rgb565_rle[IMG2BIN_PATH_CAPACITY];
  char error[256];
  char *stderr_text = NULL;
  unsigned char *actual = NULL;
  size_t actual_size = 0;
  int saved_fd = -1;
  int exit_code = 0;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0xFF };
  const unsigned char expected_a4_file[7] = { 0x00, 0x0C, 0x00, 0x01, 0x00, 0x01, 0xF0 };
  const char *argv_rle_a8[] = { "img2bin_rle", NULL, "--output", NULL, "--format", "a8" };
  const char *argv_rle_all[] = { "img2bin_rle", NULL, "--output", NULL, "--formats", "all" };
  const char *argv_raw_a4[] = { "img2bin_raw", NULL, "--output", NULL, "--format", "a4" };

  test_make_stage_directory("alpha_gate", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "mask.png", image_path, sizeof(image_path)), "Could not compose alpha gate fixture path.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose alpha gate output directory.");
  TEST_ASSERT(img2bin_path_join(stage, "stderr-alpha.jsonl", stderr_path, sizeof(stderr_path)), "Could not compose alpha gate stderr path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_rle.exe", rle_exe, sizeof(rle_exe)), "Could not compose rle executable override path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_raw.exe", raw_exe, sizeof(raw_exe)), "Could not compose raw executable override path.");
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  test_write_rgba_fixture(image_path, 1, 1, pixel);

  argv_rle_a8[1] = image_path;
  argv_rle_a8[3] = output_dir;
  TEST_ASSERT(test_redirect_stderr_begin(stderr_path, &saved_fd), "Could not redirect stderr for alpha gate test.");
  exit_code = img2bin_rle_run_with_executable_path(6, argv_rle_a8, rle_exe);
  TEST_ASSERT(test_redirect_stderr_end(saved_fd), "Could not restore stderr for alpha gate test.");
  TEST_ASSERT(exit_code == 1, "Explicit a8 on a non-raw tool must return exit code 1.");

  stderr_text = test_read_text_file(stderr_path);
  TEST_ASSERT(stderr_text != NULL, "Could not read alpha gate error output.");
  TEST_ASSERT(test_count_nonempty_lines(stderr_text) == 1, "Alpha gate error output should contain exactly one JSON line.");
  TEST_ASSERT(strstr(stderr_text, "\"code\":\"cli_parse_failed\"") != NULL, "Alpha gate error JSON missing code.");
  TEST_ASSERT(strstr(stderr_text, "\"stage\":\"cli\"") != NULL, "Alpha gate error JSON missing stage.");
  TEST_ASSERT(strstr(stderr_text, "a8") != NULL, "Alpha gate error JSON missing offending format name.");
  free(stderr_text);
  stderr_text = NULL;

  argv_rle_all[1] = image_path;
  argv_rle_all[3] = output_dir;
  TEST_ASSERT(img2bin_rle_run_with_executable_path(6, argv_rle_all, rle_exe) == 0, "--formats all on a non-raw tool must silently skip alpha masks.");
  TEST_ASSERT(img2bin_path_join(output_dir, "mask_rgb565_rle_be_1x1.bin", rgb565_rle, sizeof(rgb565_rle)), "Could not compose rgb565 rle output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "mask_a8_rle_be_1x1.bin", rejected_a8, sizeof(rejected_a8)), "Could not compose a8 rle output path.");
  TEST_ASSERT(img2bin_is_regular_file(rgb565_rle), "--formats all should still emit color formats on rle.");
  TEST_ASSERT(!img2bin_is_regular_file(rejected_a8), "--formats all must not emit alpha masks on rle.");

  argv_raw_a4[1] = image_path;
  argv_raw_a4[3] = output_dir;
  TEST_ASSERT(img2bin_raw_run_with_executable_path(6, argv_raw_a4, raw_exe) == 0, "raw --format a4 run failed.");
  TEST_ASSERT(img2bin_path_join(output_dir, "mask_a4_raw_be_1x1.bin", expected_a4, sizeof(expected_a4)), "Could not compose a4 raw output path.");
  TEST_ASSERT(img2bin_read_file(expected_a4, &actual, &actual_size, error, sizeof(error)), error);
  TEST_ASSERT(actual_size == sizeof(expected_a4_file), "a4 output file size mismatch.");
  test_expect_bytes(actual, expected_a4_file, sizeof(expected_a4_file), "a4 output file with resource header");
  free(actual);
}

static void test_positional_single_file_and_input_conflict(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char image_path[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char expected_output[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char stderr_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char error[256];
  char *stderr_text = NULL;
  int saved_fd = -1;
  int exit_code = 0;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0xFF };
  const char *argv_positional[] = {
    "img2bin_raw",
    NULL,
    "--output",
    NULL
  };
  const char *argv_conflict[] = {
    "img2bin_raw",
    "--input",
    NULL,
    NULL
  };

  test_make_stage_directory("positional_single", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "single.png", image_path, sizeof(image_path)), "Could not compose single positional image path.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose single positional output directory.");
  TEST_ASSERT(img2bin_path_join(stage, "stderr-conflict.jsonl", stderr_path, sizeof(stderr_path)), "Could not compose positional conflict stderr path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_raw.exe", exe_path, sizeof(exe_path)), "Could not compose executable override path.");
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  test_write_rgba_fixture(image_path, 1, 1, pixel);

  argv_positional[1] = image_path;
  argv_positional[3] = output_dir;
  TEST_ASSERT(img2bin_raw_run_with_executable_path(4, argv_positional, exe_path) == 0, "Single positional file run failed.");

  TEST_ASSERT(img2bin_path_join(output_dir, "single_rgb565_raw_be_1x1.bin", expected_output, sizeof(expected_output)), "Could not compose expected positional output.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_raw-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose positional manifest path.");
  TEST_ASSERT(img2bin_is_regular_file(expected_output), "Single positional file did not emit expected output.");
  TEST_ASSERT(!img2bin_is_regular_file(manifest_path), "Single positional file should not create a manifest.");

  argv_conflict[2] = image_path;
  argv_conflict[3] = image_path;
  TEST_ASSERT(test_redirect_stderr_begin(stderr_path, &saved_fd), "Could not redirect stderr for positional conflict test.");
  exit_code = img2bin_raw_run_with_executable_path(4, argv_conflict, exe_path);
  TEST_ASSERT(test_redirect_stderr_end(saved_fd), "Could not restore stderr for positional conflict test.");
  TEST_ASSERT(exit_code == 1, "Positional input mixed with --input should return exit code 1.");

  stderr_text = test_read_text_file(stderr_path);
  TEST_ASSERT(stderr_text != NULL, "Could not read positional conflict stderr output.");
  TEST_ASSERT(strstr(stderr_text, "\"code\":\"cli_parse_failed\"") != NULL, "Positional conflict JSON missing error code.");
  TEST_ASSERT(strstr(stderr_text, "--input cannot be used together with positional input paths.") != NULL, "Positional conflict JSON missing detail.");
  free(stderr_text);
}

static void test_positional_batch_manifest_and_order(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char file_path[IMG2BIN_PATH_CAPACITY];
  char directory_path[IMG2BIN_PATH_CAPACITY];
  char nested_directory[IMG2BIN_PATH_CAPACITY];
  char directory_image[IMG2BIN_PATH_CAPACITY];
  char nested_image[IMG2BIN_PATH_CAPACITY];
  char corrupt_image[IMG2BIN_PATH_CAPACITY];
  char missing_path[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char alpha_output[IMG2BIN_PATH_CAPACITY];
  char beta_output[IMG2BIN_PATH_CAPACITY];
  char nested_output[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char error[256];
  char *manifest_text = NULL;
  const char *alpha_pos = NULL;
  const char *beta_pos = NULL;
  const char *corrupt_pos = NULL;
  const char *missing_pos = NULL;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0xFF };
  const unsigned char invalid_bytes[] = "broken image payload";
  const char *argv_batch[] = {
    "img2bin_raw",
    "--manifest",
    "--output",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
  };

  test_make_stage_directory("positional_batch", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "alpha.png", file_path, sizeof(file_path)), "Could not compose positional alpha file.");
  TEST_ASSERT(img2bin_path_join(stage, "bundle", directory_path, sizeof(directory_path)), "Could not compose positional directory.");
  TEST_ASSERT(img2bin_path_join(directory_path, "nested", nested_directory, sizeof(nested_directory)), "Could not compose nested directory.");
  TEST_ASSERT(img2bin_path_join(directory_path, "beta.png", directory_image, sizeof(directory_image)), "Could not compose directory image path.");
  TEST_ASSERT(img2bin_path_join(nested_directory, "ignored.png", nested_image, sizeof(nested_image)), "Could not compose nested ignored image path.");
  TEST_ASSERT(img2bin_path_join(stage, "corrupt.png", corrupt_image, sizeof(corrupt_image)), "Could not compose corrupt image path.");
  TEST_ASSERT(img2bin_path_join(stage, "missing.png", missing_path, sizeof(missing_path)), "Could not compose missing path.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose positional batch output directory.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_raw-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose positional manifest path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_raw.exe", exe_path, sizeof(exe_path)), "Could not compose executable override path.");
  TEST_ASSERT(img2bin_make_dirs(directory_path, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_make_dirs(nested_directory, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);

  test_write_rgba_fixture(file_path, 1, 1, pixel);
  test_write_rgba_fixture(directory_image, 1, 1, pixel);
  test_write_rgba_fixture(nested_image, 1, 1, pixel);
  TEST_ASSERT(img2bin_write_file(corrupt_image, invalid_bytes, sizeof(invalid_bytes) - 1, error, sizeof(error)), error);

  argv_batch[3] = output_dir;
  argv_batch[4] = file_path;
  argv_batch[5] = directory_path;
  argv_batch[6] = corrupt_image;
  argv_batch[7] = missing_path;
  TEST_ASSERT(img2bin_raw_run_with_executable_path(8, argv_batch, exe_path) == 6, "Mixed positional batch should return exit code 6.");

  TEST_ASSERT(img2bin_path_join(output_dir, "alpha_rgb565_raw_be_1x1.bin", alpha_output, sizeof(alpha_output)), "Could not compose alpha output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "beta_rgb565_raw_be_1x1.bin", beta_output, sizeof(beta_output)), "Could not compose beta output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "ignored_rgb565_raw_be_1x1.bin", nested_output, sizeof(nested_output)), "Could not compose nested output path.");
  TEST_ASSERT(img2bin_is_regular_file(alpha_output), "Positional batch did not emit alpha output.");
  TEST_ASSERT(img2bin_is_regular_file(beta_output), "Positional batch did not emit directory image output.");
  TEST_ASSERT(!img2bin_is_regular_file(nested_output), "Positional directory scan should remain non-recursive.");
  TEST_ASSERT(img2bin_is_regular_file(manifest_path), "Positional batch should emit a manifest.");

  manifest_text = test_read_text_file(manifest_path);
  TEST_ASSERT(manifest_text != NULL, "Could not read positional batch manifest.");
  TEST_ASSERT(strstr(manifest_text, "\"source_images_total\": 3") != NULL, "Positional manifest missing source image total.");
  TEST_ASSERT(strstr(manifest_text, "\"source_images_succeeded\": 2") != NULL, "Positional manifest missing success count.");
  TEST_ASSERT(strstr(manifest_text, "\"source_images_failed\": 1") != NULL, "Positional manifest missing failure count.");
  TEST_ASSERT(strstr(manifest_text, "\"generated_bin_files_total\": 2") != NULL, "Positional manifest missing generated file count.");
  TEST_ASSERT(strstr(manifest_text, "\"payload_bytes\": 2") != NULL, "Positional manifest missing payload byte count.");
  TEST_ASSERT(strstr(manifest_text, "\"raw_payload_bytes\": 2") != NULL, "Positional manifest missing raw payload byte count.");
  TEST_ASSERT(strstr(manifest_text, "\"compression_percent\": 100.0") != NULL, "Positional manifest missing compression percentage.");
  TEST_ASSERT(strstr(manifest_text, "\"status\": \"success\"") != NULL, "Positional manifest missing success item.");
  TEST_ASSERT(strstr(manifest_text, "\"status\": \"error\"") != NULL, "Positional manifest missing error item.");
  TEST_ASSERT(strstr(manifest_text, "alpha.png") != NULL, "Positional manifest missing alpha file.");
  TEST_ASSERT(strstr(manifest_text, "beta.png") != NULL, "Positional manifest missing directory file.");
  TEST_ASSERT(strstr(manifest_text, "corrupt.png") != NULL, "Positional manifest missing corrupt file.");
  TEST_ASSERT(strstr(manifest_text, "missing.png") != NULL, "Positional manifest missing missing-path entry.");

  alpha_pos = strstr(manifest_text, "alpha.png");
  beta_pos = strstr(manifest_text, "beta.png");
  corrupt_pos = strstr(manifest_text, "corrupt.png");
  missing_pos = strstr(manifest_text, "missing.png");
  TEST_ASSERT(alpha_pos != NULL && beta_pos != NULL && corrupt_pos != NULL && missing_pos != NULL, "Could not resolve positional manifest ordering markers.");
  TEST_ASSERT(alpha_pos < beta_pos && beta_pos < corrupt_pos && corrupt_pos < missing_pos, "Positional manifest items are not in processing order.");
  free(manifest_text);
}

static void test_batch_error_json_ndjson(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char input_dir[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char stderr_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char valid_png[IMG2BIN_PATH_CAPACITY];
  char bad_png_1[IMG2BIN_PATH_CAPACITY];
  char bad_png_2[IMG2BIN_PATH_CAPACITY];
  char expected_output[IMG2BIN_PATH_CAPACITY];
  char error[256];
  char *stderr_text = NULL;
  char *manifest_text = NULL;
  int saved_fd = -1;
  int exit_code = 0;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0xFF };
  const unsigned char invalid_bytes[] = "not a valid image";
  const char *argv_batch[] = {
    "img2bin_raw",
    "--manifest",
    "--input",
    NULL,
    "--output",
    NULL
  };

  test_make_stage_directory("batch_errors", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "input", input_dir, sizeof(input_dir)), "Could not compose batch input directory.");
  TEST_ASSERT(img2bin_path_join(stage, "output", output_dir, sizeof(output_dir)), "Could not compose batch output directory.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_raw-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose batch manifest path.");
  TEST_ASSERT(img2bin_make_dirs(input_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_path_join(stage, "stderr-batch.jsonl", stderr_path, sizeof(stderr_path)), "Could not compose batch stderr capture path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_raw.exe", exe_path, sizeof(exe_path)), "Could not compose executable override path.");
  TEST_ASSERT(img2bin_path_join(input_dir, "good.png", valid_png, sizeof(valid_png)), "Could not compose valid PNG path.");
  TEST_ASSERT(img2bin_path_join(input_dir, "bad1.png", bad_png_1, sizeof(bad_png_1)), "Could not compose bad PNG path.");
  TEST_ASSERT(img2bin_path_join(input_dir, "bad2.png", bad_png_2, sizeof(bad_png_2)), "Could not compose second bad PNG path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "good_rgb565_raw_be_1x1.bin", expected_output, sizeof(expected_output)), "Could not compose expected batch output path.");

  test_write_rgba_fixture(valid_png, 1, 1, pixel);
  TEST_ASSERT(img2bin_write_file(bad_png_1, invalid_bytes, sizeof(invalid_bytes) - 1, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_write_file(bad_png_2, invalid_bytes, sizeof(invalid_bytes) - 1, error, sizeof(error)), error);

  argv_batch[3] = input_dir;
  argv_batch[5] = output_dir;
  TEST_ASSERT(test_redirect_stderr_begin(stderr_path, &saved_fd), "Could not redirect stderr for batch error test.");

  exit_code = img2bin_raw_run_with_executable_path(6, argv_batch, exe_path);

  TEST_ASSERT(test_redirect_stderr_end(saved_fd), "Could not restore stderr for batch error test.");
  TEST_ASSERT(exit_code == 6, "Batch processing with failures should return exit code 6.");
  TEST_ASSERT(img2bin_is_regular_file(expected_output), "Batch processing should still emit outputs for valid images.");

  stderr_text = test_read_text_file(stderr_path);
  TEST_ASSERT(stderr_text != NULL, "Could not read captured batch error output.");
  TEST_ASSERT(test_count_nonempty_lines(stderr_text) == 2, "Batch error output should contain one JSON line per failed image.");
  TEST_ASSERT(strstr(stderr_text, "\"stage\":\"load\"") != NULL, "Batch error JSON missing load stage.");
  TEST_ASSERT(strstr(stderr_text, "\"exit_code\":2") != NULL, "Batch error JSON missing per-file input error exit code.");
  TEST_ASSERT(strstr(stderr_text, "bad1.png") != NULL, "Batch error JSON missing first file name.");
  TEST_ASSERT(strstr(stderr_text, "bad2.png") != NULL, "Batch error JSON missing second file name.");
  free(stderr_text);

  TEST_ASSERT(img2bin_is_regular_file(manifest_path), "Directory batch with errors should still emit a manifest.");
  manifest_text = test_read_text_file(manifest_path);
  TEST_ASSERT(manifest_text != NULL, "Could not read batch manifest.");
  TEST_ASSERT(strstr(manifest_text, "\"source_images_total\": 3") != NULL, "Batch manifest missing source image total.");
  TEST_ASSERT(strstr(manifest_text, "\"source_images_succeeded\": 1") != NULL, "Batch manifest missing success count.");
  TEST_ASSERT(strstr(manifest_text, "\"source_images_failed\": 2") != NULL, "Batch manifest missing failure count.");
  TEST_ASSERT(strstr(manifest_text, "\"generated_bin_files_total\": 1") != NULL, "Batch manifest missing generated file count.");
  free(manifest_text);
}

static void test_expect_headered_file(
  const unsigned char *actual,
  size_t actual_size,
  const unsigned char *payload,
  size_t payload_size,
  unsigned int algorithm_nibble,
  img2bin_pixel_format_t format,
  int width,
  int height,
  const char *label)
{
  unsigned char header[IMG2BIN_RESOURCE_HEADER_SIZE];
  char header_label[160];

  TEST_ASSERT(
    img2bin_build_resource_header(algorithm_nibble, format, (unsigned int)width, (unsigned int)height, header),
    "Could not build the expected resource header.");

  if (actual_size != payload_size + IMG2BIN_RESOURCE_HEADER_SIZE) {
    fprintf(stderr, "TEST FAILURE: %s size mismatch (file=%zu payload=%zu).\n", label, actual_size, payload_size);
    ++g_test_failures;
    return;
  }

  snprintf(header_label, sizeof(header_label), "%s resource header", label);
  test_expect_bytes(actual, header, IMG2BIN_RESOURCE_HEADER_SIZE, header_label);
  test_expect_bytes(actual + IMG2BIN_RESOURCE_HEADER_SIZE, payload, payload_size, label);
}

static void test_resource_header_golden(void)
{
  unsigned char header[IMG2BIN_RESOURCE_HEADER_SIZE];
  const unsigned char expected_raw_rgb565[IMG2BIN_RESOURCE_HEADER_SIZE] = { 0x00, 0x00, 0x00, 0x02, 0x00, 0x03 };
  const unsigned char expected_qoif_ragb[IMG2BIN_RESOURCE_HEADER_SIZE] = { 0x00, 0x5A, 0x01, 0x00, 0x00, 0x40 };
  /* indexQOI_MASK + A8 的格式码恒为 0x6B（高 nibble 0x6 = 算法，低 nibble 0xB = A8）。 */
  const unsigned char expected_indexqoimask_a8[IMG2BIN_RESOURCE_HEADER_SIZE] = { 0x00, 0x6B, 0x00, 0x30, 0x00, 0x30 };
  const unsigned char bad_type[IMG2BIN_RESOURCE_HEADER_SIZE] = { 0x01, 0x00, 0x00, 0x01, 0x00, 0x01 };
  const unsigned char bad_format[IMG2BIN_RESOURCE_HEADER_SIZE] = { 0x00, 0x02, 0x00, 0x01, 0x00, 0x01 };
  const unsigned char bad_algo[IMG2BIN_RESOURCE_HEADER_SIZE] = { 0x00, 0x70, 0x00, 0x01, 0x00, 0x01 };
  img2bin_decode_header_t decoded_header;

  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_RGB565) == 0x0, "RGB565 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_RGB888) == 0x1, "RGB888 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_RGB332) == 0x4, "RGB332 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_ARGB8888) == 0x5, "ARGB8888 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_ARGB6666) == 0x6, "ARGB6666 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_ARGB4444) == 0x7, "ARGB4444 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_ARGB8565) == 0x8, "ARGB8565 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_ARGB2222) == 0x9, "ARGB2222 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_RAGB5155) == 0xA, "RAGB5155 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_A8) == 0xB, "A8 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_A4) == 0xC, "A4 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_A2) == 0xD, "A2 header nibble mismatch.");
  TEST_ASSERT(img2bin_get_format_header_nibble(IMG2BIN_FMT_A1) == 0xE, "A1 header nibble mismatch.");

  TEST_ASSERT(img2bin_build_resource_header(IMG2BIN_HEADER_ALGO_RAW, IMG2BIN_FMT_RGB565, 2u, 3u, header), "Could not build the raw/rgb565 header.");
  test_expect_bytes(header, expected_raw_rgb565, IMG2BIN_RESOURCE_HEADER_SIZE, "raw rgb565 resource header");

  TEST_ASSERT(img2bin_build_resource_header(IMG2BIN_HEADER_ALGO_QOIF, IMG2BIN_FMT_RAGB5155, 256u, 64u, header), "Could not build the qoif/ragb5155 header.");
  test_expect_bytes(header, expected_qoif_ragb, IMG2BIN_RESOURCE_HEADER_SIZE, "qoif ragb5155 resource header");

  TEST_ASSERT(img2bin_build_resource_header(IMG2BIN_HEADER_ALGO_INDEXQOIMASK, IMG2BIN_FMT_A8, 48u, 48u, header), "Could not build the indexqoimask/a8 header.");
  test_expect_bytes(header, expected_indexqoimask_a8, IMG2BIN_RESOURCE_HEADER_SIZE, "indexqoimask a8 resource header");

  TEST_ASSERT(!img2bin_build_resource_header(IMG2BIN_HEADER_ALGO_RAW, IMG2BIN_FMT_RGB565, 0u, 3u, header), "Zero width must be rejected.");
  TEST_ASSERT(!img2bin_build_resource_header(IMG2BIN_HEADER_ALGO_RAW, IMG2BIN_FMT_RGB565, 65536u, 3u, header), "Width above 65535 must be rejected.");

  TEST_ASSERT(img2bin_decode_header(expected_qoif_ragb, IMG2BIN_RESOURCE_HEADER_SIZE, &decoded_header) == IMG2BIN_DECODE_OK, "Decoder rejected a valid resource header.");
  TEST_ASSERT(decoded_header.algorithm_nibble == 0x5, "Decoded header algorithm mismatch.");
  TEST_ASSERT(decoded_header.format == IMG2BIN_DECODE_FMT_RAGB5155, "Decoded header format mismatch.");
  TEST_ASSERT(decoded_header.width == 256 && decoded_header.height == 64, "Decoded header dimensions mismatch.");

  TEST_ASSERT(img2bin_decode_header(expected_indexqoimask_a8, IMG2BIN_RESOURCE_HEADER_SIZE, &decoded_header) == IMG2BIN_DECODE_OK, "Decoder rejected a valid indexqoimask header.");
  TEST_ASSERT(decoded_header.algorithm_nibble == 0x6, "Decoded indexqoimask header algorithm mismatch.");
  TEST_ASSERT(decoded_header.format == IMG2BIN_DECODE_FMT_A8, "Decoded indexqoimask header format mismatch.");

  TEST_ASSERT(img2bin_decode_header(bad_type, IMG2BIN_RESOURCE_HEADER_SIZE, &decoded_header) == IMG2BIN_DECODE_ERR_CORRUPT, "Font resource type must be rejected by the image decoder.");
  TEST_ASSERT(img2bin_decode_header(bad_format, IMG2BIN_RESOURCE_HEADER_SIZE, &decoded_header) == IMG2BIN_DECODE_ERR_CORRUPT, "Legacy RGB555 nibble must be rejected.");
  TEST_ASSERT(img2bin_decode_header(bad_algo, IMG2BIN_RESOURCE_HEADER_SIZE, &decoded_header) == IMG2BIN_DECODE_ERR_CORRUPT, "Unknown algorithm nibble must be rejected.");
  TEST_ASSERT(img2bin_decode_header(expected_qoif_ragb, 5u, &decoded_header) == IMG2BIN_DECODE_ERR_TRUNCATED, "Short header must report truncation.");
}

static void test_imprle_cli_and_manifest(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char input_dir[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char image_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char output_path[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char *manifest_text = NULL;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0x78 };
  unsigned char *expected = NULL;
  unsigned char *actual = NULL;
  size_t expected_size = 0;
  size_t actual_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const char *argv_run[] = {
    "img2bin_imprle",
    "--input",
    NULL,
    "--output",
    NULL,
    "--format",
    "argb8888",
    "--manifest"
  };

  memset(&image, 0, sizeof(image));
  test_make_stage_directory("imprle_cli", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "input", input_dir, sizeof(input_dir)), "Could not compose improved-RLE input directory.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose improved-RLE output directory.");
  TEST_ASSERT(img2bin_path_join(stage, "sample.png", image_path, sizeof(image_path)), "Could not compose improved-RLE sample path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_imprle.exe", exe_path, sizeof(exe_path)), "Could not compose improved-RLE executable override path.");
  TEST_ASSERT(img2bin_make_dirs(input_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_path_join(input_dir, "sample.png", image_path, sizeof(image_path)), "Could not compose improved-RLE input fixture.");
  test_write_rgba_fixture(image_path, 1, 1, pixel);

  argv_run[2] = input_dir;
  argv_run[4] = output_dir;
  TEST_ASSERT(img2bin_imprle_run_with_executable_path(8, argv_run, exe_path) == 0, "Improved-RLE CLI directory run failed.");

  TEST_ASSERT(img2bin_path_join(output_dir, "sample_argb8888_imprle_be_1x1.bin", output_path, sizeof(output_path)), "Could not compose improved-RLE output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_imprle-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose improved-RLE manifest path.");
  TEST_ASSERT(img2bin_is_regular_file(output_path), "Improved-RLE CLI did not emit expected output file.");
  TEST_ASSERT(img2bin_is_regular_file(manifest_path), "Improved-RLE CLI did not emit expected manifest.");

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(
    img2bin_encode_imprle_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, &expected, &expected_size, error, sizeof(error)),
    error);
  TEST_ASSERT(img2bin_read_file(output_path, &actual, &actual_size, error, sizeof(error)), error);
  test_expect_headered_file(actual, actual_size, expected, expected_size, IMG2BIN_HEADER_ALGO_IMPRLE, IMG2BIN_FMT_ARGB8888, image.width, image.height, "Improved-RLE CLI output");

  manifest_text = test_read_text_file(manifest_path);
  TEST_ASSERT(manifest_text != NULL, "Could not read improved-RLE manifest.");
  TEST_ASSERT(strstr(manifest_text, "\"id\": \"img2bin_imprle\"") != NULL, "Improved-RLE manifest missing tool id.");
  TEST_ASSERT(strstr(manifest_text, "\"status\": \"success\"") != NULL, "Improved-RLE manifest missing success item.");
  TEST_ASSERT(strstr(manifest_text, "sample_argb8888_imprle_be_1x1.bin") != NULL, "Improved-RLE manifest missing output name.");

  img2bin_free_image(&image);
  free(expected);
  free(actual);
  free(manifest_text);
}

static void test_rle_cli_and_manifest(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char input_dir[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char image_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char output_path[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char *manifest_text = NULL;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0x78 };
  unsigned char *expected = NULL;
  unsigned char *actual = NULL;
  size_t expected_size = 0;
  size_t actual_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const char *argv_run[] = {
    "img2bin_rle",
    "--input",
    NULL,
    "--output",
    NULL,
    "--format",
    "argb8888",
    "--manifest"
  };

  memset(&image, 0, sizeof(image));
  test_make_stage_directory("rle_cli", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "input", input_dir, sizeof(input_dir)), "Could not compose original-RLE input directory.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose original-RLE output directory.");
  TEST_ASSERT(img2bin_path_join(stage, "sample.png", image_path, sizeof(image_path)), "Could not compose original-RLE sample path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_rle.exe", exe_path, sizeof(exe_path)), "Could not compose original-RLE executable override path.");
  TEST_ASSERT(img2bin_make_dirs(input_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_path_join(input_dir, "sample.png", image_path, sizeof(image_path)), "Could not compose original-RLE input fixture.");
  test_write_rgba_fixture(image_path, 1, 1, pixel);

  argv_run[2] = input_dir;
  argv_run[4] = output_dir;
  TEST_ASSERT(img2bin_rle_run_with_executable_path(8, argv_run, exe_path) == 0, "Original-RLE CLI directory run failed.");

  TEST_ASSERT(img2bin_path_join(output_dir, "sample_argb8888_rle_be_1x1.bin", output_path, sizeof(output_path)), "Could not compose original-RLE output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_rle-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose original-RLE manifest path.");
  TEST_ASSERT(img2bin_is_regular_file(output_path), "Original-RLE CLI did not emit expected output file.");
  TEST_ASSERT(img2bin_is_regular_file(manifest_path), "Original-RLE CLI did not emit expected manifest.");

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(
    img2bin_encode_rle_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, &expected, &expected_size, error, sizeof(error)),
    error);
  TEST_ASSERT(img2bin_read_file(output_path, &actual, &actual_size, error, sizeof(error)), error);
  test_expect_headered_file(actual, actual_size, expected, expected_size, IMG2BIN_HEADER_ALGO_RLE, IMG2BIN_FMT_ARGB8888, image.width, image.height, "Original-RLE CLI output");

  manifest_text = test_read_text_file(manifest_path);
  TEST_ASSERT(manifest_text != NULL, "Could not read original-RLE manifest.");
  TEST_ASSERT(strstr(manifest_text, "\"id\": \"img2bin_rle\"") != NULL, "Original-RLE manifest missing tool id.");
  TEST_ASSERT(strstr(manifest_text, "\"status\": \"success\"") != NULL, "Original-RLE manifest missing success item.");
  TEST_ASSERT(strstr(manifest_text, "sample_argb8888_rle_be_1x1.bin") != NULL, "Original-RLE manifest missing output name.");

  img2bin_free_image(&image);
  free(expected);
  free(actual);
  free(manifest_text);
}

static void test_qoi_cli_and_manifest(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char input_dir[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char image_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char output_path[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char *manifest_text = NULL;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0x78 };
  unsigned char *expected = NULL;
  unsigned char *actual = NULL;
  size_t expected_size = 0;
  size_t actual_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const char *argv_run[] = {
    "img2bin_qoi",
    "--input",
    NULL,
    "--output",
    NULL,
    "--format",
    "argb8888",
    "--manifest"
  };

  memset(&image, 0, sizeof(image));
  test_make_stage_directory("qoi_cli", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "input", input_dir, sizeof(input_dir)), "Could not compose original-QOI input directory.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose original-QOI output directory.");
  TEST_ASSERT(img2bin_path_join(stage, "sample.png", image_path, sizeof(image_path)), "Could not compose original-QOI sample path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_qoi.exe", exe_path, sizeof(exe_path)), "Could not compose original-QOI executable override path.");
  TEST_ASSERT(img2bin_make_dirs(input_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_path_join(input_dir, "sample.png", image_path, sizeof(image_path)), "Could not compose original-QOI input fixture.");
  test_write_rgba_fixture(image_path, 1, 1, pixel);

  argv_run[2] = input_dir;
  argv_run[4] = output_dir;
  TEST_ASSERT(img2bin_qoi_run_with_executable_path(8, argv_run, exe_path) == 0, "Original-QOI CLI directory run failed.");

  TEST_ASSERT(img2bin_path_join(output_dir, "sample_argb8888_qoi_be_1x1.bin", output_path, sizeof(output_path)), "Could not compose original-QOI output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_qoi-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose original-QOI manifest path.");
  TEST_ASSERT(img2bin_is_regular_file(output_path), "Original-QOI CLI did not emit expected output file.");
  TEST_ASSERT(img2bin_is_regular_file(manifest_path), "Original-QOI CLI did not emit expected manifest.");

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(
    img2bin_encode_qoi_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, &expected, &expected_size, error, sizeof(error)),
    error);
  TEST_ASSERT(img2bin_read_file(output_path, &actual, &actual_size, error, sizeof(error)), error);
  test_expect_headered_file(actual, actual_size, expected, expected_size, IMG2BIN_HEADER_ALGO_QOI, IMG2BIN_FMT_ARGB8888, image.width, image.height, "Original-QOI CLI output");

  manifest_text = test_read_text_file(manifest_path);
  TEST_ASSERT(manifest_text != NULL, "Could not read original-QOI manifest.");
  TEST_ASSERT(strstr(manifest_text, "\"id\": \"img2bin_qoi\"") != NULL, "Original-QOI manifest missing tool id.");
  TEST_ASSERT(strstr(manifest_text, "\"status\": \"success\"") != NULL, "Original-QOI manifest missing success item.");
  TEST_ASSERT(strstr(manifest_text, "sample_argb8888_qoi_be_1x1.bin") != NULL, "Original-QOI manifest missing output name.");

  img2bin_free_image(&image);
  free(expected);
  free(actual);
  free(manifest_text);
}

static void test_qoif_cli_and_manifest(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char input_dir[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char image_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char output_path[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char *manifest_text = NULL;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0x78 };
  unsigned char *expected = NULL;
  unsigned char *actual = NULL;
  size_t expected_size = 0;
  size_t actual_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const char *argv_run[] = {
    "img2bin_qoif",
    "--input",
    NULL,
    "--output",
    NULL,
    "--format",
    "argb8888",
    "--manifest"
  };

  memset(&image, 0, sizeof(image));
  test_make_stage_directory("qoif_cli", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "input", input_dir, sizeof(input_dir)), "Could not compose original-QOIF input directory.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose original-QOIF output directory.");
  TEST_ASSERT(img2bin_path_join(stage, "sample.png", image_path, sizeof(image_path)), "Could not compose original-QOIF sample path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_qoif.exe", exe_path, sizeof(exe_path)), "Could not compose original-QOIF executable override path.");
  TEST_ASSERT(img2bin_make_dirs(input_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_path_join(input_dir, "sample.png", image_path, sizeof(image_path)), "Could not compose original-QOIF input fixture.");
  test_write_rgba_fixture(image_path, 1, 1, pixel);

  argv_run[2] = input_dir;
  argv_run[4] = output_dir;
  TEST_ASSERT(img2bin_qoif_run_with_executable_path(8, argv_run, exe_path) == 0, "Original-QOIF CLI directory run failed.");

  TEST_ASSERT(img2bin_path_join(output_dir, "sample_argb8888_qoif_be_1x1.bin", output_path, sizeof(output_path)), "Could not compose original-QOIF output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_qoif-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose original-QOIF manifest path.");
  TEST_ASSERT(img2bin_is_regular_file(output_path), "Original-QOIF CLI did not emit expected output file.");
  TEST_ASSERT(img2bin_is_regular_file(manifest_path), "Original-QOIF CLI did not emit expected manifest.");

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(
    img2bin_encode_qoif_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, &expected, &expected_size, error, sizeof(error)),
    error);
  TEST_ASSERT(img2bin_read_file(output_path, &actual, &actual_size, error, sizeof(error)), error);
  test_expect_headered_file(actual, actual_size, expected, expected_size, IMG2BIN_HEADER_ALGO_QOIF, IMG2BIN_FMT_ARGB8888, image.width, image.height, "Original-QOIF CLI output");

  manifest_text = test_read_text_file(manifest_path);
  TEST_ASSERT(manifest_text != NULL, "Could not read original-QOIF manifest.");
  TEST_ASSERT(strstr(manifest_text, "\"id\": \"img2bin_qoif\"") != NULL, "Original-QOIF manifest missing tool id.");
  TEST_ASSERT(strstr(manifest_text, "\"status\": \"success\"") != NULL, "Original-QOIF manifest missing success item.");
  TEST_ASSERT(strstr(manifest_text, "sample_argb8888_qoif_be_1x1.bin") != NULL, "Original-QOIF manifest missing output name.");

  img2bin_free_image(&image);
  free(expected);
  free(actual);
  free(manifest_text);
}

static void test_indexqoi_cli_and_manifest(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char input_dir[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char image_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char output_path[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char *manifest_text = NULL;
  unsigned char pixels[] = {
    0x12, 0x34, 0x56, 0x78,
    0x90, 0xAB, 0xCD, 0xEF,
    0x24, 0x68, 0xAC, 0xFF
  };
  unsigned char *expected = NULL;
  unsigned char *actual = NULL;
  size_t expected_size = 0;
  size_t actual_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  const char *argv_run[] = {
    "img2bin_indexqoi",
    "--input",
    NULL,
    "--output",
    NULL,
    "--format",
    "argb8888",
    "--index-interval",
    "2",
    "--manifest"
  };

  memset(&image, 0, sizeof(image));
  test_make_stage_directory("indexqoi_cli", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "input", input_dir, sizeof(input_dir)), "Could not compose IndexQOI input directory.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose IndexQOI output directory.");
  TEST_ASSERT(img2bin_path_join(stage, "sample.png", image_path, sizeof(image_path)), "Could not compose IndexQOI sample path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_indexqoi.exe", exe_path, sizeof(exe_path)), "Could not compose IndexQOI executable override path.");
  TEST_ASSERT(img2bin_make_dirs(input_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_path_join(input_dir, "sample.png", image_path, sizeof(image_path)), "Could not compose IndexQOI input fixture.");
  test_write_rgba_fixture(image_path, 3, 1, pixels);

  argv_run[2] = input_dir;
  argv_run[4] = output_dir;
  TEST_ASSERT(img2bin_indexqoi_run_with_executable_path(10, argv_run, exe_path) == 0, "IndexQOI CLI directory run failed.");

  TEST_ASSERT(img2bin_path_join(output_dir, "sample_argb8888_indexqoi_be_3x1.bin", output_path, sizeof(output_path)), "Could not compose IndexQOI output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_indexqoi-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose IndexQOI manifest path.");
  TEST_ASSERT(img2bin_is_regular_file(output_path), "IndexQOI CLI did not emit expected output file.");
  TEST_ASSERT(img2bin_is_regular_file(manifest_path), "IndexQOI CLI did not emit expected manifest.");

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(
    img2bin_encode_indexqoi_image(IMG2BIN_FMT_ARGB8888, IMG2BIN_ENDIAN_BIG, background, &image, 2u, &expected, &expected_size, error, sizeof(error)),
    error);
  TEST_ASSERT(img2bin_read_file(output_path, &actual, &actual_size, error, sizeof(error)), error);
  TEST_ASSERT(actual_size >= IMG2BIN_RESOURCE_HEADER_SIZE + 13u, "IndexQOI CLI output is missing the index header.");
  TEST_ASSERT(actual[11] == 0x00 && actual[12] == 0x02, "IndexQOI CLI output did not keep the requested index interval.");
  test_expect_headered_file(actual, actual_size, expected, expected_size, IMG2BIN_HEADER_ALGO_INDEXQOI, IMG2BIN_FMT_ARGB8888, image.width, image.height, "IndexQOI CLI output");

  manifest_text = test_read_text_file(manifest_path);
  TEST_ASSERT(manifest_text != NULL, "Could not read IndexQOI manifest.");
  TEST_ASSERT(strstr(manifest_text, "\"id\": \"img2bin_indexqoi\"") != NULL, "IndexQOI manifest missing tool id.");
  TEST_ASSERT(strstr(manifest_text, "\"status\": \"success\"") != NULL, "IndexQOI manifest missing success item.");
  TEST_ASSERT(strstr(manifest_text, "sample_argb8888_indexqoi_be_3x1.bin") != NULL, "IndexQOI manifest missing output name.");

  img2bin_free_image(&image);
  free(expected);
  free(actual);
  free(manifest_text);
}

/* 索引QOI_MASK CLI：不传 --format 时默认格式为 a8；--quantize-bits 8 生效；
   输出文件 = 6 字节通用头（算法 0x6 + 格式 0xB）+ 编码 payload；manifest 正常。 */
static void test_indexqoimask_cli_and_manifest(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char input_dir[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char image_path[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  char output_path[IMG2BIN_PATH_CAPACITY];
  char manifest_path[IMG2BIN_PATH_CAPACITY];
  char *manifest_text = NULL;
  unsigned char pixels[6 * 4];
  const unsigned char alphas[6] = { 0, 128, 255, 10, 200, 60 };
  unsigned char *expected = NULL;
  unsigned char *actual = NULL;
  size_t expected_size = 0;
  size_t actual_size = 0;
  char error[256];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  size_t index = 0;
  const char *argv_run[] = {
    "img2bin_indexqoimask",
    "--input",
    NULL,
    "--output",
    NULL,
    "--quantize-bits",
    "8",
    "--manifest"
  };

  memset(&image, 0, sizeof(image));
  memset(pixels, 0x5A, sizeof(pixels));
  for (index = 0; index < 6; ++index) {
    pixels[index * 4 + 3] = alphas[index];
  }

  test_make_stage_directory("indexqoimask_cli", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "input", input_dir, sizeof(input_dir)), "Could not compose IndexQOI mask input directory.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose IndexQOI mask output directory.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_indexqoimask.exe", exe_path, sizeof(exe_path)), "Could not compose IndexQOI mask executable override path.");
  TEST_ASSERT(img2bin_make_dirs(input_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  TEST_ASSERT(img2bin_path_join(input_dir, "sample.png", image_path, sizeof(image_path)), "Could not compose IndexQOI mask input fixture.");
  test_write_rgba_fixture(image_path, 3, 2, pixels);

  argv_run[2] = input_dir;
  argv_run[4] = output_dir;
  TEST_ASSERT(img2bin_indexqoimask_run_with_executable_path(8, argv_run, exe_path) == 0, "IndexQOI mask CLI directory run failed.");

  TEST_ASSERT(img2bin_path_join(output_dir, "sample_a8_indexqoimask_be_3x2.bin", output_path, sizeof(output_path)), "Could not compose IndexQOI mask output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "img2bin_indexqoimask-manifest.json", manifest_path, sizeof(manifest_path)), "Could not compose IndexQOI mask manifest path.");
  TEST_ASSERT(img2bin_is_regular_file(output_path), "IndexQOI mask CLI did not emit the default a8 output file.");
  TEST_ASSERT(img2bin_is_regular_file(manifest_path), "IndexQOI mask CLI did not emit expected manifest.");

  TEST_ASSERT(img2bin_load_image(image_path, &image, error, sizeof(error)), error);
  TEST_ASSERT(
    img2bin_encode_indexqoimask_image(IMG2BIN_FMT_A8, IMG2BIN_ENDIAN_BIG, background, &image, 8u, &expected, &expected_size, error, sizeof(error)),
    error);
  TEST_ASSERT(img2bin_read_file(output_path, &actual, &actual_size, error, sizeof(error)), error);
  TEST_ASSERT(actual_size >= IMG2BIN_RESOURCE_HEADER_SIZE + 1u, "IndexQOI mask CLI output is missing the payload.");
  TEST_ASSERT(actual[IMG2BIN_RESOURCE_HEADER_SIZE] == 0x00, "IndexQOI mask CLI output did not keep the requested lossless depth.");
  test_expect_headered_file(actual, actual_size, expected, expected_size, IMG2BIN_HEADER_ALGO_INDEXQOIMASK, IMG2BIN_FMT_A8, image.width, image.height, "IndexQOI mask CLI output");

  manifest_text = test_read_text_file(manifest_path);
  TEST_ASSERT(manifest_text != NULL, "Could not read IndexQOI mask manifest.");
  TEST_ASSERT(strstr(manifest_text, "\"id\": \"img2bin_indexqoimask\"") != NULL, "IndexQOI mask manifest missing tool id.");
  TEST_ASSERT(strstr(manifest_text, "\"status\": \"success\"") != NULL, "IndexQOI mask manifest missing success item.");
  TEST_ASSERT(strstr(manifest_text, "sample_a8_indexqoimask_be_3x2.bin") != NULL, "IndexQOI mask manifest missing output name.");
  TEST_ASSERT(strstr(manifest_text, "\"requested_formats\": [\"a8\"]") != NULL, "IndexQOI mask manifest missing a8 default format.");

  img2bin_free_image(&image);
  free(expected);
  free(actual);
  free(manifest_text);
}

/* 索引QOI_MASK 的格式/选项门禁：显式点名任何非 a8 格式报 CLI 错误；
   --formats all 静默滤除到只剩 a8；--quantize-bits 在其他工具上报 CLI 错误；
   非法档位值在 CLI 解析层报错。 */
static void test_indexqoimask_format_and_option_gate(void)
{
  char stage[IMG2BIN_PATH_CAPACITY];
  char image_path[IMG2BIN_PATH_CAPACITY];
  char output_dir[IMG2BIN_PATH_CAPACITY];
  char stderr_path[IMG2BIN_PATH_CAPACITY];
  char mask_exe[IMG2BIN_PATH_CAPACITY];
  char raw_exe[IMG2BIN_PATH_CAPACITY];
  char expected_a8[IMG2BIN_PATH_CAPACITY];
  char rejected_rgb565[IMG2BIN_PATH_CAPACITY];
  char error[256];
  char *stderr_text = NULL;
  int saved_fd = -1;
  int exit_code = 0;
  unsigned char pixel[4] = { 0x12, 0x34, 0x56, 0x9A };
  const char *argv_rgb565[] = { "img2bin_indexqoimask", NULL, "--output", NULL, "--format", "rgb565" };
  const char *argv_a4[] = { "img2bin_indexqoimask", NULL, "--output", NULL, "--format", "a4" };
  const char *argv_all[] = { "img2bin_indexqoimask", NULL, "--output", NULL, "--formats", "all" };
  const char *argv_raw_quantize[] = { "img2bin_raw", NULL, "--output", NULL, "--quantize-bits", "6" };
  const char *argv_bad_bits[] = { "img2bin_indexqoimask", NULL, "--output", NULL, "--quantize-bits", "9" };

  test_make_stage_directory("indexqoimask_gate", stage, sizeof(stage));
  TEST_ASSERT(img2bin_path_join(stage, "mask.png", image_path, sizeof(image_path)), "Could not compose gate fixture path.");
  TEST_ASSERT(img2bin_path_join(stage, "out", output_dir, sizeof(output_dir)), "Could not compose gate output directory.");
  TEST_ASSERT(img2bin_path_join(stage, "stderr-mask-gate.jsonl", stderr_path, sizeof(stderr_path)), "Could not compose gate stderr path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_indexqoimask.exe", mask_exe, sizeof(mask_exe)), "Could not compose mask executable override path.");
  TEST_ASSERT(img2bin_path_join(stage, "img2bin_raw.exe", raw_exe, sizeof(raw_exe)), "Could not compose raw executable override path.");
  TEST_ASSERT(img2bin_make_dirs(output_dir, error, sizeof(error)), error);
  test_write_rgba_fixture(image_path, 1, 1, pixel);

  /* 显式点名彩色格式：报 CLI 错误。 */
  argv_rgb565[1] = image_path;
  argv_rgb565[3] = output_dir;
  TEST_ASSERT(test_redirect_stderr_begin(stderr_path, &saved_fd), "Could not redirect stderr for the rgb565 gate test.");
  exit_code = img2bin_indexqoimask_run_with_executable_path(6, argv_rgb565, mask_exe);
  TEST_ASSERT(test_redirect_stderr_end(saved_fd), "Could not restore stderr for the rgb565 gate test.");
  TEST_ASSERT(exit_code == 1, "Explicit rgb565 on the a8-only tool must return exit code 1.");
  stderr_text = test_read_text_file(stderr_path);
  TEST_ASSERT(stderr_text != NULL, "Could not read rgb565 gate error output.");
  TEST_ASSERT(test_count_nonempty_lines(stderr_text) == 1, "rgb565 gate error output should contain exactly one JSON line.");
  TEST_ASSERT(strstr(stderr_text, "\"code\":\"cli_parse_failed\"") != NULL, "rgb565 gate error JSON missing code.");
  TEST_ASSERT(strstr(stderr_text, "only supports the a8 alpha mask format") != NULL, "rgb565 gate error JSON missing a8-only message.");
  TEST_ASSERT(strstr(stderr_text, "rgb565") != NULL, "rgb565 gate error JSON missing offending format name.");
  free(stderr_text);
  stderr_text = NULL;

  /* 显式点名 a4：同样报 CLI 错误（本工具只认 a8）。 */
  argv_a4[1] = image_path;
  argv_a4[3] = output_dir;
  TEST_ASSERT(test_redirect_stderr_begin(stderr_path, &saved_fd), "Could not redirect stderr for the a4 gate test.");
  exit_code = img2bin_indexqoimask_run_with_executable_path(6, argv_a4, mask_exe);
  TEST_ASSERT(test_redirect_stderr_end(saved_fd), "Could not restore stderr for the a4 gate test.");
  TEST_ASSERT(exit_code == 1, "Explicit a4 on the a8-only tool must return exit code 1.");

  /* --formats all：静默滤除到只剩 a8。 */
  argv_all[1] = image_path;
  argv_all[3] = output_dir;
  TEST_ASSERT(img2bin_indexqoimask_run_with_executable_path(6, argv_all, mask_exe) == 0, "--formats all on the a8-only tool must silently keep a8 only.");
  TEST_ASSERT(img2bin_path_join(output_dir, "mask_a8_indexqoimask_be_1x1.bin", expected_a8, sizeof(expected_a8)), "Could not compose a8 gate output path.");
  TEST_ASSERT(img2bin_path_join(output_dir, "mask_rgb565_indexqoimask_be_1x1.bin", rejected_rgb565, sizeof(rejected_rgb565)), "Could not compose rgb565 gate output path.");
  TEST_ASSERT(img2bin_is_regular_file(expected_a8), "--formats all should still emit the a8 output.");
  TEST_ASSERT(!img2bin_is_regular_file(rejected_rgb565), "--formats all must not emit color formats on the a8-only tool.");

  /* --quantize-bits 在不支持的工具上：报 CLI 错误。 */
  argv_raw_quantize[1] = image_path;
  argv_raw_quantize[3] = output_dir;
  TEST_ASSERT(test_redirect_stderr_begin(stderr_path, &saved_fd), "Could not redirect stderr for the raw quantize gate test.");
  exit_code = img2bin_raw_run_with_executable_path(6, argv_raw_quantize, raw_exe);
  TEST_ASSERT(test_redirect_stderr_end(saved_fd), "Could not restore stderr for the raw quantize gate test.");
  TEST_ASSERT(exit_code == 1, "--quantize-bits on a non-mask tool must return exit code 1.");
  stderr_text = test_read_text_file(stderr_path);
  TEST_ASSERT(stderr_text != NULL, "Could not read raw quantize gate error output.");
  TEST_ASSERT(strstr(stderr_text, "\"code\":\"cli_parse_failed\"") != NULL, "raw quantize gate error JSON missing code.");
  TEST_ASSERT(strstr(stderr_text, "--quantize-bits") != NULL, "raw quantize gate error JSON missing flag detail.");
  free(stderr_text);
  stderr_text = NULL;

  /* 非法档位值（9）：CLI 解析层报错。 */
  argv_bad_bits[1] = image_path;
  argv_bad_bits[3] = output_dir;
  TEST_ASSERT(test_redirect_stderr_begin(stderr_path, &saved_fd), "Could not redirect stderr for the bad-bits gate test.");
  exit_code = img2bin_indexqoimask_run_with_executable_path(6, argv_bad_bits, mask_exe);
  TEST_ASSERT(test_redirect_stderr_end(saved_fd), "Could not restore stderr for the bad-bits gate test.");
  TEST_ASSERT(exit_code == 1, "--quantize-bits 9 must be rejected with exit code 1.");
  stderr_text = test_read_text_file(stderr_path);
  TEST_ASSERT(stderr_text != NULL, "Could not read bad-bits gate error output.");
  TEST_ASSERT(strstr(stderr_text, "Invalid --quantize-bits value") != NULL, "bad-bits gate error JSON missing detail.");
  free(stderr_text);
}

static void test_build_roundtrip_pixels(unsigned char *pixels, int width, int height)
{
  int x = 0;
  int y = 0;

  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      unsigned char *pixel = pixels + ((size_t)y * (size_t)width + (size_t)x) * 4u;

      if (y == 0) {
        pixel[0] = 200; pixel[1] = 100; pixel[2] = 50; pixel[3] = 255;
      } else if (y == 1) {
        pixel[0] = (unsigned char)(100 + x); pixel[1] = (unsigned char)(100 + x); pixel[2] = (unsigned char)(100 + x); pixel[3] = 255;
      } else if (y == 2) {
        pixel[0] = (unsigned char)(x * 16); pixel[1] = (unsigned char)(255 - x * 16); pixel[2] = 128; pixel[3] = 255;
      } else if (y == 3) {
        pixel[0] = 255; pixel[1] = 0; pixel[2] = 0; pixel[3] = (unsigned char)(x * 17);
      } else if (y == 4) {
        int alternate = x & 1;
        pixel[0] = alternate ? 10 : 240; pixel[1] = alternate ? 250 : 20; pixel[2] = alternate ? 60 : 200; pixel[3] = 255;
      } else if (y == 5) {
        pixel[0] = 0; pixel[1] = 0; pixel[2] = 0; pixel[3] = 0;
      } else {
        pixel[0] = (unsigned char)(x * 7 + y * 13);
        pixel[1] = (unsigned char)(x * 11 + y * 3);
        pixel[2] = (unsigned char)(x * 5 + y * 29);
        pixel[3] = (unsigned char)(((x + y) & 1) ? 255 : 128);
      }
    }
  }
}

static void test_decoder_expect_roundtrip(
  const char *algorithm_label,
  const img2bin_format_info_t *info,
  img2bin_endianness_t endianness,
  img2bin_decode_status_t status,
  const unsigned char *decoded,
  size_t decoded_size,
  const unsigned char *raw_reference,
  size_t raw_size)
{
  char label[192];

  snprintf(label, sizeof(label), "%s roundtrip (%s, %s)", algorithm_label, info->name, endianness == IMG2BIN_ENDIAN_BIG ? "be" : "le");

  if (status != IMG2BIN_DECODE_OK) {
    fprintf(stderr, "TEST FAILURE: %s decode failed with status %d.\n", label, (int)status);
    ++g_test_failures;
    return;
  }
  if (decoded_size != raw_size) {
    fprintf(stderr, "TEST FAILURE: %s size mismatch (decoded=%zu raw=%zu).\n", label, decoded_size, raw_size);
    ++g_test_failures;
    return;
  }
  test_expect_bytes(decoded, raw_reference, raw_size, label);
}

static void test_decoder_roundtrip_all(void)
{
  enum { ROUNDTRIP_W = 16, ROUNDTRIP_H = 9 };
  static unsigned char pixels[(size_t)ROUNDTRIP_W * ROUNDTRIP_H * 4u];
  unsigned char decoded[(size_t)ROUNDTRIP_W * ROUNDTRIP_H * 4u];
  img2bin_image_t image;
  img2bin_rgb_t background = { 16, 32, 48 };
  char error[256];
  char label[192];
  size_t format_index = 0;
  int endian_index = 0;
  size_t slot = 0;
  const size_t pixel_count = (size_t)ROUNDTRIP_W * (size_t)ROUNDTRIP_H;

  test_build_roundtrip_pixels(pixels, ROUNDTRIP_W, ROUNDTRIP_H);
  image.width = ROUNDTRIP_W;
  image.height = ROUNDTRIP_H;
  image.pixels = pixels;

  for (format_index = 0; format_index < IMG2BIN_FMT_COUNT; ++format_index) {
    for (endian_index = 0; endian_index < 2; ++endian_index) {
      img2bin_pixel_format_t format = (img2bin_pixel_format_t)format_index;
      img2bin_endianness_t endianness = endian_index == 0 ? IMG2BIN_ENDIAN_BIG : IMG2BIN_ENDIAN_LITTLE;
      img2bin_decode_format_t decode_format = (img2bin_decode_format_t)format_index;
      img2bin_decode_endianness_t decode_endianness = endian_index == 0 ? IMG2BIN_DECODE_BIG_ENDIAN : IMG2BIN_DECODE_LITTLE_ENDIAN;
      const img2bin_format_info_t *info = img2bin_get_format_info(format);
      img2bin_indexqoi_header_t header;
      unsigned char *raw_buffer = NULL;
      unsigned char *encoded = NULL;
      size_t raw_size = 0;
      size_t encoded_size = 0;
      size_t decoded_size = 0;
      img2bin_decode_status_t status = IMG2BIN_DECODE_OK;

      TEST_ASSERT(info != NULL, "Roundtrip format info is missing.");
      TEST_ASSERT(img2bin_decode_bytes_per_pixel(decode_format) == info->bytes_per_pixel, "Decoder bytes-per-pixel mismatch.");

      /* Alpha 蒙版家族：raw 单算法回环 + 文件级自动分发；其余编码器/解码器必须拒绝。 */
      if (info->is_alpha_only) {
        size_t row_stride = img2bin_format_row_stride(format, ROUNDTRIP_W);
        size_t expected_size = img2bin_format_payload_size(format, ROUNDTRIP_W, ROUNDTRIP_H);
        unsigned char headered[2048];
        unsigned char universal[IMG2BIN_RESOURCE_HEADER_SIZE];
        img2bin_decode_header_t file_header;
        size_t headered_size = 0;

        TEST_ASSERT(img2bin_decode_bits_per_pixel(decode_format) == info->bits_per_pixel, "Decoder bits-per-pixel mismatch.");
        TEST_ASSERT(img2bin_decode_row_stride(decode_format, ROUNDTRIP_W) == row_stride, "Decoder row stride mismatch.");
        TEST_ASSERT(img2bin_encode_raw_image(format, endianness, background, &image, &raw_buffer, &raw_size, error, sizeof(error)), error);
        TEST_ASSERT(raw_size == expected_size, "Alpha raw payload size mismatch.");

        status = img2bin_decode_raw_alpha(raw_buffer, raw_size, decode_format, ROUNDTRIP_W, ROUNDTRIP_H, decoded, sizeof(decoded), &decoded_size);
        test_decoder_expect_roundtrip("raw-alpha", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);

        status = img2bin_decode_raw_alpha(raw_buffer, raw_size - 1u, decode_format, ROUNDTRIP_W, ROUNDTRIP_H, decoded, sizeof(decoded), &decoded_size);
        TEST_ASSERT(status == IMG2BIN_DECODE_ERR_TRUNCATED, "Truncated alpha payload must be rejected.");

        status = img2bin_decode_raw(raw_buffer, raw_size, decode_format, pixel_count, decoded, sizeof(decoded), &decoded_size);
        TEST_ASSERT(status == IMG2BIN_DECODE_ERR_ARGUMENTS, "Pixel-count decode_raw must reject alpha mask formats.");
        status = img2bin_decode_rle(raw_buffer, raw_size, decode_format, pixel_count, decoded, sizeof(decoded), &decoded_size);
        TEST_ASSERT(status == IMG2BIN_DECODE_ERR_ARGUMENTS, "decode_rle must reject alpha mask formats.");
        status = img2bin_decode_qoi(raw_buffer, raw_size, decode_format, decode_endianness, pixel_count, decoded, sizeof(decoded), &decoded_size);
        TEST_ASSERT(status == IMG2BIN_DECODE_ERR_ARGUMENTS, "decode_qoi must reject alpha mask formats.");

        TEST_ASSERT(!img2bin_encode_rle_image(format, endianness, background, &image, &encoded, &encoded_size, error, sizeof(error)), "RLE encoder must reject alpha mask formats.");
        TEST_ASSERT(!img2bin_encode_imprle_image(format, endianness, background, &image, &encoded, &encoded_size, error, sizeof(error)), "Improved-RLE encoder must reject alpha mask formats.");
        TEST_ASSERT(!img2bin_encode_qoi_image(format, endianness, background, &image, &encoded, &encoded_size, error, sizeof(error)), "QOI encoder must reject alpha mask formats.");
        TEST_ASSERT(!img2bin_encode_qoif_image(format, endianness, background, &image, &encoded, &encoded_size, error, sizeof(error)), "QOIF encoder must reject alpha mask formats.");
        TEST_ASSERT(!img2bin_encode_indexqoi_image(format, endianness, background, &image, 0u, &encoded, &encoded_size, error, sizeof(error)), "IndexQOI encoder must reject alpha mask formats.");

        /* indexQOI_MASK 只接受 a8：a8 上 q=8 无损回环（输出与 raw a8 payload
           逐字节一致），a4/a2/a1 一律拒绝。 */
        if (format == IMG2BIN_FMT_A8) {
          unsigned char *mask_encoded = NULL;
          size_t mask_encoded_size = 0;

          TEST_ASSERT(
            img2bin_encode_indexqoimask_image(format, endianness, background, &image, 8u, &mask_encoded, &mask_encoded_size, error, sizeof(error)),
            error);
          status = img2bin_decode_indexqoimask(mask_encoded, mask_encoded_size, ROUNDTRIP_W, ROUNDTRIP_H, decoded, sizeof(decoded), &decoded_size);
          test_decoder_expect_roundtrip("indexqoimask-q8", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);
          free(mask_encoded);
        } else {
          TEST_ASSERT(!img2bin_encode_indexqoimask_image(format, endianness, background, &image, 8u, &encoded, &encoded_size, error, sizeof(error)), "IndexQOI mask encoder must reject a4/a2/a1.");
        }

        TEST_ASSERT(raw_size + IMG2BIN_RESOURCE_HEADER_SIZE <= sizeof(headered), "Headered alpha fixture is unexpectedly large.");
        TEST_ASSERT(img2bin_build_resource_header(IMG2BIN_HEADER_ALGO_RAW, format, ROUNDTRIP_W, ROUNDTRIP_H, universal), "Could not build the alpha raw resource header.");
        memcpy(headered, universal, IMG2BIN_RESOURCE_HEADER_SIZE);
        memcpy(headered + IMG2BIN_RESOURCE_HEADER_SIZE, raw_buffer, raw_size);
        headered_size = raw_size + IMG2BIN_RESOURCE_HEADER_SIZE;

        status = img2bin_decode_image(headered, headered_size, decode_endianness, &file_header, decoded, sizeof(decoded), &decoded_size);
        test_decoder_expect_roundtrip("file-level raw-alpha", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);
        TEST_ASSERT(file_header.format == decode_format, "decode_image reported the wrong alpha mask format.");
        TEST_ASSERT(file_header.width == ROUNDTRIP_W && file_header.height == ROUNDTRIP_H, "decode_image reported wrong alpha mask dimensions.");

        /* 头里 Alpha 蒙版 + 非 raw 算法 = 工具不可能产出的组合，按损坏流拒绝。 */
        headered[1] = (unsigned char)((IMG2BIN_HEADER_ALGO_RLE << 4) | (headered[1] & 0x0Fu));
        status = img2bin_decode_image(headered, headered_size, decode_endianness, NULL, decoded, sizeof(decoded), &decoded_size);
        TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "Alpha mask with a non-raw algorithm nibble must be rejected as corrupt.");

        free(raw_buffer);
        raw_buffer = NULL;
        continue;
      }

      TEST_ASSERT(img2bin_encode_raw_image(format, endianness, background, &image, &raw_buffer, &raw_size, error, sizeof(error)), error);

      /* 彩色格式对 indexQOI_MASK 编码器一律非法。 */
      TEST_ASSERT(!img2bin_encode_indexqoimask_image(format, endianness, background, &image, 8u, &encoded, &encoded_size, error, sizeof(error)), "IndexQOI mask encoder must reject color formats.");

      status = img2bin_decode_raw(raw_buffer, raw_size, decode_format, pixel_count, decoded, sizeof(decoded), &decoded_size);
      test_decoder_expect_roundtrip("raw", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);

      TEST_ASSERT(img2bin_encode_rle_image(format, endianness, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
      status = img2bin_decode_rle(encoded, encoded_size, decode_format, pixel_count, decoded, sizeof(decoded), &decoded_size);
      test_decoder_expect_roundtrip("rle", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);
      free(encoded);
      encoded = NULL;

      TEST_ASSERT(img2bin_encode_imprle_image(format, endianness, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
      status = img2bin_decode_imprle(encoded, encoded_size, decode_format, pixel_count, decoded, sizeof(decoded), &decoded_size);
      test_decoder_expect_roundtrip("imprle", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);
      free(encoded);
      encoded = NULL;

      TEST_ASSERT(img2bin_encode_qoi_image(format, endianness, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
      status = img2bin_decode_qoi(encoded, encoded_size, decode_format, decode_endianness, pixel_count, decoded, sizeof(decoded), &decoded_size);
      test_decoder_expect_roundtrip("qoi", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);
      free(encoded);
      encoded = NULL;

      TEST_ASSERT(img2bin_encode_qoif_image(format, endianness, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
      status = img2bin_decode_qoif(encoded, encoded_size, decode_format, decode_endianness, pixel_count, decoded, sizeof(decoded), &decoded_size);
      test_decoder_expect_roundtrip("qoif", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);
      free(encoded);
      encoded = NULL;

      TEST_ASSERT(img2bin_encode_indexqoi_image(format, endianness, background, &image, 0u, &encoded, &encoded_size, error, sizeof(error)), error);
      status = img2bin_decode_indexqoi_header(encoded, encoded_size, &header);
      TEST_ASSERT(status == IMG2BIN_DECODE_OK, "indexQOI header parse failed.");
      TEST_ASSERT(header.width == ROUNDTRIP_W && header.height == ROUNDTRIP_H, "indexQOI header dimensions mismatch.");
      TEST_ASSERT(header.index_interval == ROUNDTRIP_W, "indexQOI default interval should equal image width.");
      TEST_ASSERT(header.slot_count == (size_t)ROUNDTRIP_H, "indexQOI slot count mismatch.");

      status = img2bin_decode_indexqoi(encoded, encoded_size, decode_format, decode_endianness, decoded, sizeof(decoded), &decoded_size);
      test_decoder_expect_roundtrip("indexqoi", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);

      for (slot = 0; slot < header.slot_count; ++slot) {
        size_t base_pixel = slot * (size_t)header.index_interval;
        size_t tail_size = (pixel_count - base_pixel) * info->bytes_per_pixel;

        status = img2bin_decode_indexqoi_from_slot(encoded, encoded_size, decode_format, decode_endianness, slot, decoded, sizeof(decoded), &decoded_size);
        snprintf(label, sizeof(label), "indexqoi slot %u (%s, %s)", (unsigned int)slot, info->name, endian_index == 0 ? "be" : "le");
        if (status != IMG2BIN_DECODE_OK || decoded_size != tail_size) {
          fprintf(stderr, "TEST FAILURE: %s status=%d decoded=%zu expected=%zu.\n", label, (int)status, decoded_size, tail_size);
          ++g_test_failures;
        } else {
          test_expect_bytes(decoded, raw_buffer + base_pixel * info->bytes_per_pixel, tail_size, label);
        }
      }
      free(encoded);
      encoded = NULL;

      TEST_ASSERT(img2bin_encode_indexqoi_image(format, endianness, background, &image, 5u, &encoded, &encoded_size, error, sizeof(error)), error);
      status = img2bin_decode_indexqoi(encoded, encoded_size, decode_format, decode_endianness, decoded, sizeof(decoded), &decoded_size);
      test_decoder_expect_roundtrip("indexqoi-interval5", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);
      free(encoded);
      encoded = NULL;

      {
        unsigned char headered[2048];
        unsigned char universal[IMG2BIN_RESOURCE_HEADER_SIZE];
        img2bin_decode_header_t file_header;
        size_t headered_size = 0;
        size_t tail_size = 0;

        TEST_ASSERT(img2bin_encode_qoif_image(format, endianness, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
        TEST_ASSERT(encoded_size + IMG2BIN_RESOURCE_HEADER_SIZE <= sizeof(headered), "Headered QOIF fixture is unexpectedly large.");
        TEST_ASSERT(img2bin_build_resource_header(IMG2BIN_HEADER_ALGO_QOIF, format, ROUNDTRIP_W, ROUNDTRIP_H, universal), "Could not build the QOIF resource header.");
        memcpy(headered, universal, IMG2BIN_RESOURCE_HEADER_SIZE);
        memcpy(headered + IMG2BIN_RESOURCE_HEADER_SIZE, encoded, encoded_size);
        headered_size = encoded_size + IMG2BIN_RESOURCE_HEADER_SIZE;
        free(encoded);
        encoded = NULL;

        status = img2bin_decode_image(headered, headered_size, decode_endianness, &file_header, decoded, sizeof(decoded), &decoded_size);
        test_decoder_expect_roundtrip("file-level qoif", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);
        TEST_ASSERT(file_header.format == decode_format, "decode_image reported the wrong format.");
        TEST_ASSERT(file_header.width == ROUNDTRIP_W && file_header.height == ROUNDTRIP_H, "decode_image reported wrong dimensions.");

        TEST_ASSERT(img2bin_encode_indexqoi_image(format, endianness, background, &image, 0u, &encoded, &encoded_size, error, sizeof(error)), error);
        TEST_ASSERT(encoded_size + IMG2BIN_RESOURCE_HEADER_SIZE <= sizeof(headered), "Headered indexQOI fixture is unexpectedly large.");
        TEST_ASSERT(img2bin_build_resource_header(IMG2BIN_HEADER_ALGO_INDEXQOI, format, ROUNDTRIP_W, ROUNDTRIP_H, universal), "Could not build the indexQOI resource header.");
        memcpy(headered, universal, IMG2BIN_RESOURCE_HEADER_SIZE);
        memcpy(headered + IMG2BIN_RESOURCE_HEADER_SIZE, encoded, encoded_size);
        headered_size = encoded_size + IMG2BIN_RESOURCE_HEADER_SIZE;
        free(encoded);
        encoded = NULL;

        status = img2bin_decode_image(headered, headered_size, decode_endianness, NULL, decoded, sizeof(decoded), &decoded_size);
        test_decoder_expect_roundtrip("file-level indexqoi", info, endianness, status, decoded, decoded_size, raw_buffer, raw_size);

        tail_size = (pixel_count - (size_t)ROUNDTRIP_W) * info->bytes_per_pixel;
        status = img2bin_decode_image_from_slot(headered, headered_size, decode_endianness, 1u, decoded, sizeof(decoded), &decoded_size);
        if (status != IMG2BIN_DECODE_OK || decoded_size != tail_size) {
          fprintf(stderr, "TEST FAILURE: file-level indexqoi slot decode (%s) status=%d decoded=%zu expected=%zu.\n", info->name, (int)status, decoded_size, tail_size);
          ++g_test_failures;
        } else {
          test_expect_bytes(decoded, raw_buffer + (size_t)ROUNDTRIP_W * info->bytes_per_pixel, tail_size, "file-level indexqoi slot decode");
        }
      }

      free(raw_buffer);
      raw_buffer = NULL;
    }
  }
}

static void test_decoder_rejects_damage(void)
{
  unsigned char pixels[4 * 4];
  unsigned char decoded[64];
  unsigned char tampered[4096];
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  char error[256];
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  size_t decoded_size = 0;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;
  const unsigned char lone_index_op[1] = { 0x00 };
  const unsigned char underflow_diff_op[1] = { 0x40 };
  unsigned char v1_header[14] = { 0x0D, 0, 1, 0, 1, 0, 1, 0, 2, 0, 0, 0, 0, 0 };
  unsigned char overfull_palette_header[14] = { 0x0E, 0, 1, 0, 1, 0, 1, 0, 2, 0, 0, 0, 0, 65 };
  size_t index = 0;

  for (index = 0; index < 4; ++index) {
    pixels[index * 4 + 0] = (unsigned char)(index * 60);
    pixels[index * 4 + 1] = (unsigned char)(255 - index * 60);
    pixels[index * 4 + 2] = (unsigned char)(index * 30 + 10);
    pixels[index * 4 + 3] = 255;
  }
  image.width = 4;
  image.height = 1;
  image.pixels = pixels;

  TEST_ASSERT(img2bin_encode_qoif_image(IMG2BIN_FMT_RGB565, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
  TEST_ASSERT(encoded_size + 1 <= sizeof(tampered), "QOIF damage fixture is unexpectedly large.");

  status = img2bin_decode_qoif(encoded, encoded_size - 1, IMG2BIN_DECODE_FMT_RGB565, IMG2BIN_DECODE_BIG_ENDIAN, 4, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_TRUNCATED, "Truncated QOIF must be rejected.");

  memcpy(tampered, encoded, encoded_size);
  tampered[encoded_size] = 0x55;
  status = img2bin_decode_qoif(tampered, encoded_size + 1, IMG2BIN_DECODE_FMT_RGB565, IMG2BIN_DECODE_BIG_ENDIAN, 4, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_TRAILING_DATA, "Trailing bytes after QOIF must be rejected.");

  status = img2bin_decode_qoif(encoded, encoded_size, IMG2BIN_DECODE_FMT_RGB565, IMG2BIN_DECODE_BIG_ENDIAN, 3, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status != IMG2BIN_DECODE_OK, "Wrong pixel count must be rejected.");
  free(encoded);
  encoded = NULL;

  status = img2bin_decode_qoif(lone_index_op, sizeof(lone_index_op), IMG2BIN_DECODE_FMT_RGB565, IMG2BIN_DECODE_BIG_ENDIAN, 1, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "OP_INDEX inside QOIF must be rejected.");

  status = img2bin_decode_qoi(underflow_diff_op, sizeof(underflow_diff_op), IMG2BIN_DECODE_FMT_RGB565, IMG2BIN_DECODE_BIG_ENDIAN, 1, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "Channel underflow in OP_DIFF must be rejected.");

  TEST_ASSERT(img2bin_encode_rle_image(IMG2BIN_FMT_RGB332, IMG2BIN_ENDIAN_BIG, background, &image, &encoded, &encoded_size, error, sizeof(error)), error);
  TEST_ASSERT(encoded_size <= sizeof(tampered), "RLE damage fixture is unexpectedly large.");
  memcpy(tampered, encoded, encoded_size);
  tampered[encoded_size - 1] = 0x09;
  status = img2bin_decode_rle(tampered, encoded_size, IMG2BIN_DECODE_FMT_RGB332, 4, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status != IMG2BIN_DECODE_OK, "RLE without terminator must be rejected.");
  free(encoded);
  encoded = NULL;

  {
    img2bin_indexqoi_header_t header;

    status = img2bin_decode_indexqoi_header(v1_header, sizeof(v1_header), &header);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "indexQOI V1 (0x0D) header must be rejected as unsupported.");
    status = img2bin_decode_indexqoi_header(overfull_palette_header, sizeof(overfull_palette_header), &header);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "indexQOI palette count above 64 must be rejected.");

    TEST_ASSERT(img2bin_encode_indexqoi_image(IMG2BIN_FMT_RGB565, IMG2BIN_ENDIAN_BIG, background, &image, 0u, &encoded, &encoded_size, error, sizeof(error)), error);
    TEST_ASSERT(encoded_size + 1u <= sizeof(tampered), "indexQOI damage fixture is unexpectedly large.");
    status = img2bin_decode_indexqoi_header(encoded, encoded_size, &header);
    TEST_ASSERT(status == IMG2BIN_DECODE_OK, "indexQOI damage fixture header parse failed.");
    TEST_ASSERT(header.palette_count == 0u, "indexQOI damage fixture should have an empty palette.");

    memcpy(tampered, encoded, encoded_size);
    tampered[encoded_size] = 0x55; /* 数据流之后的多余字节 */
    status = img2bin_decode_indexqoi(tampered, encoded_size + 1u, IMG2BIN_DECODE_FMT_RGB565, IMG2BIN_DECODE_BIG_ENDIAN, decoded, sizeof(decoded), &decoded_size);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_TRAILING_DATA, "Trailing bytes after the indexQOI stream must be rejected.");

    status = img2bin_decode_indexqoi(encoded, encoded_size - 1u, IMG2BIN_DECODE_FMT_RGB565, IMG2BIN_DECODE_BIG_ENDIAN, decoded, sizeof(decoded), &decoded_size);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_TRUNCATED, "Truncated indexQOI stream must be rejected.");

    memcpy(tampered, encoded, encoded_size);
    tampered[header.palette_offset] = 0x00; /* 空调色盘却出现 0x00~0x3F 区 op */
    status = img2bin_decode_indexqoi(tampered, encoded_size, IMG2BIN_DECODE_FMT_RGB565, IMG2BIN_DECODE_BIG_ENDIAN, decoded, sizeof(decoded), &decoded_size);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "Palette op beyond the palette count must be rejected.");
    free(encoded);
    encoded = NULL;
  }
}

/* 测试侧独立实现的量化 + 高位复制扩展，对照解码器输出。 */
static unsigned char test_indexqoimask_expected_alpha(unsigned char alpha, unsigned int quantize_bits)
{
  unsigned char value = (unsigned char)(alpha >> (8u - quantize_bits));
  unsigned int shift = 8u - quantize_bits;

  if (shift == 0u) {
    return value;
  }
  return (unsigned char)(((unsigned int)value << shift) | ((unsigned int)value >> (quantize_bits - shift)));
}

/* 索引QOI_MASK 全档位回环：q=5/6/7/8 整图解码 == 逐像素 量化+扩展 的期望；
   单行随机访问与整图逐行一致；行偏移可查询；文件级自动分发（算法 0x6 + a8）；
   0x6 与 a4 的组合按损坏流拒绝。 */
static void test_indexqoimask_decoder_roundtrip(void)
{
  enum { MASK_W = 48, MASK_H = 21 };
  static unsigned char pixels[(size_t)MASK_W * MASK_H * 4u];
  static unsigned char expected[(size_t)MASK_W * MASK_H];
  static unsigned char decoded[(size_t)MASK_W * MASK_H];
  unsigned char row_decoded[MASK_W];
  const unsigned int depths[4] = { 5u, 6u, 7u, 8u };
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  char error[256];
  char label[160];
  size_t depth_index = 0;
  int x = 0;
  int y = 0;

  /* 合成蒙版：纯透明/纯实体行（RUN）、渐变（DIFF/DELTA）、噪声（INDEX/ALPHA）、
     重复行（去重）、台阶、抗锯齿边缘，覆盖全部 op 与行型。 */
  for (y = 0; y < MASK_H; ++y) {
    for (x = 0; x < MASK_W; ++x) {
      unsigned char alpha = 0;

      switch (y % 7) {
        case 0: alpha = 0; break;
        case 1: alpha = 255; break;
        case 2: alpha = (unsigned char)(x * 255 / (MASK_W - 1)); break;
        case 3: alpha = (unsigned char)((x * 37 + y * 11) & 0xFF); break;
        case 4: alpha = (unsigned char)((x * 37 + (y - 1) * 11) & 0xFF); break; /* 与上一行相同 */
        case 5: alpha = (unsigned char)((x / 8) * 32); break;
        default: alpha = (unsigned char)(x < 24 ? 255 : (x < 32 ? 255 - (x - 24) * 32 : 0)); break;
      }
      pixels[((size_t)y * MASK_W + x) * 4u + 0u] = 0xDE;
      pixels[((size_t)y * MASK_W + x) * 4u + 1u] = 0xAD;
      pixels[((size_t)y * MASK_W + x) * 4u + 2u] = 0xBE;
      pixels[((size_t)y * MASK_W + x) * 4u + 3u] = alpha;
    }
  }
  image.width = MASK_W;
  image.height = MASK_H;
  image.pixels = pixels;

  for (depth_index = 0; depth_index < 4; ++depth_index) {
    unsigned int quantize_bits = depths[depth_index];
    unsigned char *encoded = NULL;
    size_t encoded_size = 0;
    size_t decoded_size = 0;
    size_t row = 0;
    img2bin_indexqoimask_header_t header;
    img2bin_decode_status_t status = IMG2BIN_DECODE_OK;

    for (y = 0; y < MASK_H; ++y) {
      for (x = 0; x < MASK_W; ++x) {
        expected[(size_t)y * MASK_W + x] =
          test_indexqoimask_expected_alpha(pixels[((size_t)y * MASK_W + x) * 4u + 3u], quantize_bits);
      }
    }

    TEST_ASSERT(
      img2bin_encode_indexqoimask_image(IMG2BIN_FMT_A8, IMG2BIN_ENDIAN_BIG, background, &image, quantize_bits, &encoded, &encoded_size, error, sizeof(error)),
      error);

    status = img2bin_decode_indexqoimask_header(encoded, encoded_size, MASK_H, &header);
    TEST_ASSERT(status == IMG2BIN_DECODE_OK, "IndexQOI mask payload header parse failed.");
    TEST_ASSERT(header.quantize_bits == quantize_bits, "IndexQOI mask header quantize-bits mismatch.");
    TEST_ASSERT((size_t)header.u16_count + (size_t)header.u32_count == (size_t)MASK_H, "IndexQOI mask header row-count mismatch.");

    status = img2bin_decode_indexqoimask(encoded, encoded_size, MASK_W, MASK_H, decoded, sizeof(decoded), &decoded_size);
    snprintf(label, sizeof(label), "indexqoimask q=%u full decode", quantize_bits);
    if (status != IMG2BIN_DECODE_OK || decoded_size != sizeof(expected)) {
      fprintf(stderr, "TEST FAILURE: %s status=%d decoded=%zu.\n", label, (int)status, decoded_size);
      ++g_test_failures;
    } else {
      test_expect_bytes(decoded, expected, sizeof(expected), label);
    }

    /* 单行随机访问必须与整图逐行一致；重复行共享同一偏移。 */
    for (row = 0; row < (size_t)MASK_H; ++row) {
      uint32_t offset = 0;

      status = img2bin_decode_indexqoimask_row(encoded, encoded_size, MASK_W, MASK_H, row, row_decoded, sizeof(row_decoded), &decoded_size);
      snprintf(label, sizeof(label), "indexqoimask q=%u row %u", quantize_bits, (unsigned int)row);
      if (status != IMG2BIN_DECODE_OK || decoded_size != (size_t)MASK_W) {
        fprintf(stderr, "TEST FAILURE: %s status=%d decoded=%zu.\n", label, (int)status, decoded_size);
        ++g_test_failures;
      } else {
        test_expect_bytes(row_decoded, expected + row * MASK_W, MASK_W, label);
      }

      TEST_ASSERT(img2bin_decode_indexqoimask_row_offset(encoded, encoded_size, MASK_H, row, &offset) == IMG2BIN_DECODE_OK, "IndexQOI mask row-offset lookup failed.");
      TEST_ASSERT(header.stream_offset + (size_t)offset < encoded_size, "IndexQOI mask row offset points outside the payload.");
    }

    /* 行去重：case 4 行与 case 3 行内容相同，必须共享同一偏移。 */
    {
      uint32_t offset_row3 = 0;
      uint32_t offset_row4 = 0;

      TEST_ASSERT(img2bin_decode_indexqoimask_row_offset(encoded, encoded_size, MASK_H, 3u, &offset_row3) == IMG2BIN_DECODE_OK, "IndexQOI mask dedup offset lookup failed (row 3).");
      TEST_ASSERT(img2bin_decode_indexqoimask_row_offset(encoded, encoded_size, MASK_H, 4u, &offset_row4) == IMG2BIN_DECODE_OK, "IndexQOI mask dedup offset lookup failed (row 4).");
      TEST_ASSERT(offset_row3 == offset_row4, "Identical rows must share one stream offset.");
    }

    /* 文件级自动分发：6 字节通用头（0x6 + a8）+ payload。 */
    {
      unsigned char universal[IMG2BIN_RESOURCE_HEADER_SIZE];
      unsigned char *headered = NULL;
      img2bin_decode_header_t file_header;
      size_t headered_size = encoded_size + IMG2BIN_RESOURCE_HEADER_SIZE;

      headered = (unsigned char *)malloc(headered_size);
      TEST_ASSERT(headered != NULL, "Could not allocate the headered indexqoimask fixture.");
      TEST_ASSERT(img2bin_build_resource_header(IMG2BIN_HEADER_ALGO_INDEXQOIMASK, IMG2BIN_FMT_A8, MASK_W, MASK_H, universal), "Could not build the indexqoimask resource header.");
      memcpy(headered, universal, IMG2BIN_RESOURCE_HEADER_SIZE);
      memcpy(headered + IMG2BIN_RESOURCE_HEADER_SIZE, encoded, encoded_size);

      status = img2bin_decode_image(headered, headered_size, IMG2BIN_DECODE_BIG_ENDIAN, &file_header, decoded, sizeof(decoded), &decoded_size);
      snprintf(label, sizeof(label), "file-level indexqoimask q=%u", quantize_bits);
      if (status != IMG2BIN_DECODE_OK || decoded_size != sizeof(expected)) {
        fprintf(stderr, "TEST FAILURE: %s status=%d decoded=%zu.\n", label, (int)status, decoded_size);
        ++g_test_failures;
      } else {
        test_expect_bytes(decoded, expected, sizeof(expected), label);
        TEST_ASSERT(file_header.algorithm_nibble == 0x6, "decode_image reported the wrong indexqoimask algorithm nibble.");
        TEST_ASSERT(file_header.format == IMG2BIN_DECODE_FMT_A8, "decode_image reported the wrong indexqoimask format.");
      }

      /* 0x6 + a4 是工具不可能产出的组合，按损坏流拒绝。 */
      headered[1] = (unsigned char)((IMG2BIN_HEADER_ALGO_INDEXQOIMASK << 4) | 0x0Cu);
      status = img2bin_decode_image(headered, headered_size, IMG2BIN_DECODE_BIG_ENDIAN, NULL, decoded, sizeof(decoded), &decoded_size);
      TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "indexQOI_MASK with an a4 format nibble must be rejected as corrupt.");
      free(headered);
    }

    free(encoded);
  }
}

/* u16/u32 行索引分表按行序（不按偏移值）：像素流超过 64 KB 后，即使某行因
   去重指回小偏移，只要它排在首个大偏移行之后就必须进 u32 表。 */
static void test_indexqoimask_u32_index_and_row_dedup(void)
{
  enum { BIG_W = 200, BIG_H = 250 };
  unsigned char *pixels = NULL;
  unsigned char *decoded = NULL;
  unsigned char *encoded = NULL;
  size_t encoded_size = 0;
  size_t decoded_size = 0;
  img2bin_image_t image;
  img2bin_rgb_t background = { 0, 0, 0 };
  img2bin_indexqoimask_header_t header;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;
  char error[256];
  uint32_t offset_first = 0;
  uint32_t offset_last = 0;
  uint32_t offset_split = 0;
  int x = 0;
  int y = 0;

  pixels = (unsigned char *)malloc((size_t)BIG_W * BIG_H * 4u);
  decoded = (unsigned char *)malloc((size_t)BIG_W * BIG_H);
  TEST_ASSERT(pixels != NULL && decoded != NULL, "Could not allocate the u32-index fixture.");

  /* 相邻差恒大的噪声，逼出大量 ALPHA/INDEX（约 2 字节/像素），拉长像素流；
     最后一行复制第 0 行，制造"排在 u32 区、偏移却很小"的去重行。 */
  for (y = 0; y < BIG_H; ++y) {
    for (x = 0; x < BIG_W; ++x) {
      int source_y = (y == BIG_H - 1) ? 0 : y;
      unsigned char alpha = (unsigned char)((x * 97 + source_y * 53) & 0xFF);

      pixels[((size_t)y * BIG_W + x) * 4u + 0u] = 0x11;
      pixels[((size_t)y * BIG_W + x) * 4u + 1u] = 0x22;
      pixels[((size_t)y * BIG_W + x) * 4u + 2u] = 0x33;
      pixels[((size_t)y * BIG_W + x) * 4u + 3u] = alpha;
    }
  }
  image.width = BIG_W;
  image.height = BIG_H;
  image.pixels = pixels;

  TEST_ASSERT(
    img2bin_encode_indexqoimask_image(IMG2BIN_FMT_A8, IMG2BIN_ENDIAN_BIG, background, &image, 8u, &encoded, &encoded_size, error, sizeof(error)),
    error);

  status = img2bin_decode_indexqoimask_header(encoded, encoded_size, BIG_H, &header);
  TEST_ASSERT(status == IMG2BIN_DECODE_OK, "u32-index fixture header parse failed.");
  TEST_ASSERT(header.u16_count > 0u, "u32-index fixture should keep a u16 prefix.");
  TEST_ASSERT(header.u16_count < (uint16_t)BIG_H, "u32-index fixture did not overflow into the u32 table.");
  TEST_ASSERT((size_t)header.u16_count + (size_t)header.u32_count == (size_t)BIG_H, "u32-index fixture table split mismatch.");

  /* m = 偏移 ≤65535 的最长行前缀：分界行偏移必大于 65535。 */
  TEST_ASSERT(img2bin_decode_indexqoimask_row_offset(encoded, encoded_size, BIG_H, header.u16_count, &offset_split) == IMG2BIN_DECODE_OK, "u32-index split-row offset lookup failed.");
  TEST_ASSERT(offset_split > 0xFFFFu, "The first u32-table row must have an offset above 65535.");

  /* 最后一行去重指回第 0 行：偏移相等且 ≤65535，却仍位于 u32 表（按行序分表）。 */
  TEST_ASSERT(img2bin_decode_indexqoimask_row_offset(encoded, encoded_size, BIG_H, 0u, &offset_first) == IMG2BIN_DECODE_OK, "u32-index first-row offset lookup failed.");
  TEST_ASSERT(img2bin_decode_indexqoimask_row_offset(encoded, encoded_size, BIG_H, (size_t)BIG_H - 1u, &offset_last) == IMG2BIN_DECODE_OK, "u32-index last-row offset lookup failed.");
  TEST_ASSERT(offset_first == offset_last, "Deduplicated last row must reuse the first row's offset.");
  TEST_ASSERT(offset_last <= 0xFFFFu, "Deduplicated last row keeps a small offset inside the u32 table.");

  /* 整图回环（q=8 无损：期望值即源 Alpha）。 */
  status = img2bin_decode_indexqoimask(encoded, encoded_size, BIG_W, BIG_H, decoded, (size_t)BIG_W * BIG_H, &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_OK, "u32-index fixture full decode failed.");
  TEST_ASSERT(decoded_size == (size_t)BIG_W * BIG_H, "u32-index fixture decode size mismatch.");
  for (y = 0; y < BIG_H && g_test_failures == 0; ++y) {
    for (x = 0; x < BIG_W; ++x) {
      if (decoded[(size_t)y * BIG_W + x] != pixels[((size_t)y * BIG_W + x) * 4u + 3u]) {
        fprintf(stderr, "TEST FAILURE: u32-index roundtrip differs at (%d,%d).\n", x, y);
        ++g_test_failures;
        break;
      }
    }
  }

  free(pixels);
  free(decoded);
  free(encoded);
}

/* 索引QOI_MASK 损坏流拒绝：保留位、行数不符、字典越界、INDEX 越界、
   DIFF 行尾/越界、RUN 超行宽、行首字节超量化域、截断与多余字节。 */
static void test_indexqoimask_decoder_rejects_damage(void)
{
  /* 4x1、q=6、无字典的黄金 payload（与量化黄金测试相同的流）。 */
  const unsigned char valid[] = {
    0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x9E, 0x83, 0x80
  };
  unsigned char tampered[64];
  unsigned char decoded[16];
  size_t decoded_size = 0;
  img2bin_indexqoimask_header_t header;
  img2bin_decode_status_t status = IMG2BIN_DECODE_OK;

  /* 基线必须可解。 */
  status = img2bin_decode_indexqoimask(valid, sizeof(valid), 4u, 1u, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_OK, "IndexQOI mask damage baseline must decode.");

  /* 标志位保留位（b7..b2）非 0。 */
  memcpy(tampered, valid, sizeof(valid));
  tampered[0] = 0x06;
  status = img2bin_decode_indexqoimask(tampered, sizeof(valid), 4u, 1u, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "Reserved flag bits must be rejected.");

  /* u16+u32 行数与高不符。 */
  status = img2bin_decode_indexqoimask(valid, sizeof(valid), 4u, 2u, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "Row-count mismatch against the height must be rejected.");

  /* 截断与多余字节。 */
  status = img2bin_decode_indexqoimask(valid, sizeof(valid) - 1u, 4u, 1u, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_TRUNCATED, "Truncated IndexQOI mask payload must be rejected.");
  memcpy(tampered, valid, sizeof(valid));
  tampered[sizeof(valid)] = 0x55;
  status = img2bin_decode_indexqoimask(tampered, sizeof(valid) + 1u, 4u, 1u, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_TRAILING_DATA, "Trailing bytes after the pixel stream must be rejected.");

  /* 空字典却出现 INDEX op。 */
  memcpy(tampered, valid, sizeof(valid));
  tampered[9] = 0x00;
  status = img2bin_decode_indexqoimask(tampered, sizeof(valid), 4u, 1u, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "INDEX beyond the dictionary count must be rejected.");

  /* 字典数量超上限（>64）。 */
  memcpy(tampered, valid, sizeof(valid));
  tampered[7] = 65u;
  status = img2bin_decode_indexqoimask_header(tampered, sizeof(valid), 1u, &header);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "Dictionary count above 64 must be rejected.");

  /* 字典项超出 q 位域（q=6 的域上限 63）。 */
  {
    const unsigned char bad_dict[] = {
      0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x40, 0x3F
    };
    status = img2bin_decode_indexqoimask_header(bad_dict, sizeof(bad_dict), 1u, &header);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "Dictionary entry outside the quantized domain must be rejected.");
  }

  /* 行首字节超出 q 位域。 */
  {
    const unsigned char bad_first[] = {
      0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40
    };
    status = img2bin_decode_indexqoimask(bad_first, sizeof(bad_first), 1u, 1u, decoded, sizeof(decoded), &decoded_size);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "Row-leading byte outside the quantized domain must be rejected.");
  }

  /* DIFF 产生越界中间值（63 + 3 > 63）。 */
  {
    const unsigned char diff_overflow[] = {
      0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x7C
    };
    status = img2bin_decode_indexqoimask(diff_overflow, sizeof(diff_overflow), 3u, 1u, decoded, sizeof(decoded), &decoded_size);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "DIFF overflow beyond the quantized domain must be rejected.");
  }

  /* 行尾只剩 1 像素时出现 DIFF。 */
  {
    const unsigned char diff_at_tail[] = {
      0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x64
    };
    status = img2bin_decode_indexqoimask(diff_at_tail, sizeof(diff_at_tail), 2u, 1u, decoded, sizeof(decoded), &decoded_size);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "DIFF with a single remaining pixel must be rejected.");
  }

  /* RUN 超出行宽。 */
  {
    const unsigned char run_overflow[] = {
      0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xC2
    };
    status = img2bin_decode_indexqoimask(run_overflow, sizeof(run_overflow), 2u, 1u, decoded, sizeof(decoded), &decoded_size);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "RUN beyond the row width must be rejected.");
  }

  /* ALPHA 后接字节超出 q 位域。 */
  {
    const unsigned char alpha_overflow[] = {
      0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x40
    };
    status = img2bin_decode_indexqoimask(alpha_overflow, sizeof(alpha_overflow), 2u, 1u, decoded, sizeof(decoded), &decoded_size);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_CORRUPT, "ALPHA literal outside the quantized domain must be rejected.");
  }

  /* 行偏移越界（指向像素流之外）。 */
  {
    const unsigned char bad_offset[] = {
      0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x3F
    };
    status = img2bin_decode_indexqoimask(bad_offset, sizeof(bad_offset), 1u, 1u, decoded, sizeof(decoded), &decoded_size);
    TEST_ASSERT(status == IMG2BIN_DECODE_ERR_TRUNCATED, "A row offset outside the pixel stream must be rejected.");
  }

  /* 行号越界与输出缓冲不足。 */
  status = img2bin_decode_indexqoimask_row(valid, sizeof(valid), 4u, 1u, 1u, decoded, sizeof(decoded), &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_ARGUMENTS, "Row index beyond the height must be rejected.");
  status = img2bin_decode_indexqoimask(valid, sizeof(valid), 4u, 1u, decoded, 3u, &decoded_size);
  TEST_ASSERT(status == IMG2BIN_DECODE_ERR_OUTPUT_TOO_SMALL, "An undersized output buffer must be rejected.");
}

#ifdef _WIN32
static void test_windows_icon_resource_for_executable(const char *exe_name)
{
  char binary_dir[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  wchar_t *wide_path = NULL;
  HMODULE module = NULL;
  HRSRC group_icon = NULL;

  test_get_binary_directory(binary_dir, sizeof(binary_dir));
  TEST_ASSERT(img2bin_path_join(binary_dir, exe_name, exe_path, sizeof(exe_path)), "Could not compose built executable path for icon-resource test.");
  TEST_ASSERT(img2bin_utf8_to_wide_alloc(exe_path, &wide_path), "Could not convert executable path to UTF-16 for icon-resource test.");

  module = LoadLibraryExW(wide_path, NULL, LOAD_LIBRARY_AS_DATAFILE);
  TEST_ASSERT(module != NULL, "Could not load built executable for icon-resource test.");
  group_icon = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14));
  TEST_ASSERT(group_icon != NULL, "Built executable does not contain a group icon resource.");

  FreeLibrary(module);
  free(wide_path);
}

static void test_windows_version_resource_for_executable(const char *exe_name)
{
  char binary_dir[IMG2BIN_PATH_CAPACITY];
  char exe_path[IMG2BIN_PATH_CAPACITY];
  wchar_t *wide_path = NULL;
  DWORD dummy_handle = 0;
  DWORD info_size = 0;
  void *info_buffer = NULL;
  VS_FIXEDFILEINFO *fixed_info = NULL;
  UINT fixed_info_size = 0;
  wchar_t *product_version = NULL;
  wchar_t *file_version = NULL;
  UINT product_version_length = 0;
  UINT file_version_length = 0;

  test_get_binary_directory(binary_dir, sizeof(binary_dir));
  TEST_ASSERT(img2bin_path_join(binary_dir, exe_name, exe_path, sizeof(exe_path)), "Could not compose built executable path.");
  TEST_ASSERT(img2bin_utf8_to_wide_alloc(exe_path, &wide_path), "Could not convert executable path to UTF-16.");

  info_size = GetFileVersionInfoSizeW(wide_path, &dummy_handle);
  TEST_ASSERT(info_size > 0, "Built executable does not contain version resources.");

  info_buffer = malloc((size_t)info_size);
  TEST_ASSERT(info_buffer != NULL, "Could not allocate version-info buffer.");
  TEST_ASSERT(GetFileVersionInfoW(wide_path, 0, info_size, info_buffer) != 0, "Could not read executable version info.");
  TEST_ASSERT(VerQueryValueW(info_buffer, L"\\", (LPVOID *)&fixed_info, &fixed_info_size) != 0, "Could not query fixed file version info.");
  TEST_ASSERT(HIWORD(fixed_info->dwFileVersionMS) == IMG2BIN_VERSION_MAJOR, "Major version resource mismatch.");
  TEST_ASSERT(LOWORD(fixed_info->dwFileVersionMS) == IMG2BIN_VERSION_MINOR, "Minor version resource mismatch.");
  TEST_ASSERT(HIWORD(fixed_info->dwFileVersionLS) == IMG2BIN_VERSION_PATCH, "Patch version resource mismatch.");
  TEST_ASSERT(LOWORD(fixed_info->dwFileVersionLS) == IMG2BIN_VERSION_BUILD, "Build version resource mismatch.");
  TEST_ASSERT(VerQueryValueW(info_buffer, L"\\StringFileInfo\\040904E4\\ProductVersion", (LPVOID *)&product_version, &product_version_length) != 0, "Could not query ProductVersion string.");
  TEST_ASSERT(VerQueryValueW(info_buffer, L"\\StringFileInfo\\040904E4\\FileVersion", (LPVOID *)&file_version, &file_version_length) != 0, "Could not query FileVersion string.");
  TEST_ASSERT(wcscmp(product_version, L"V0.1.0") == 0, "ProductVersion string mismatch.");
  TEST_ASSERT(wcscmp(file_version, L"0.1.0") == 0, "FileVersion string mismatch.");

  free(info_buffer);
  free(wide_path);
}
#endif

int main(void)
{
  test_raw_encoder_golden_values();
  test_alpha_edges_and_background_blend();
  test_raw_alpha_golden_values();
  test_imprle_segments_for_group_sizes();
  test_imprle_split_boundaries();
  test_imprle_reference_sample();
  test_rle_segments_for_group_sizes();
  test_rle_split_boundaries();
  test_rle_reference_sample();
  test_qoi_argb_name_order();
  test_qoi_run_split_boundaries();
  test_qoi_reference_sample();
  test_qoif_omits_index_chunks();
  test_qoif_reference_sample();
  test_indexqoi_header_and_offsets();
  test_indexqoi_v2_palette_golden_rgb565();
  test_indexqoi_v2_palette_golden_argb8888();
  test_indexqoi_default_interval_uses_image_width();
  test_indexqoimask_golden_values();
  test_indexqoimask_quantize_golden_and_default_bits();
  test_image_loading_for_png_bmp_jpg();
  test_info_json();
  test_imprle_info_json();
  test_rle_info_json();
  test_qoi_info_json();
  test_qoif_info_json();
  test_indexqoi_info_json();
  test_indexqoimask_info_json();
  test_default_mode_creates_missing_directories();
  test_cli_default_mode_and_unicode_paths();
  test_error_json_for_invalid_cli();
  test_alpha_mask_cli_tool_gate();
  test_positional_single_file_and_input_conflict();
  test_positional_batch_manifest_and_order();
  test_batch_error_json_ndjson();
  test_imprle_cli_and_manifest();
  test_rle_cli_and_manifest();
  test_qoi_cli_and_manifest();
  test_qoif_cli_and_manifest();
  test_indexqoi_cli_and_manifest();
  test_indexqoimask_cli_and_manifest();
  test_indexqoimask_format_and_option_gate();
  test_resource_header_golden();
  test_decoder_roundtrip_all();
  test_decoder_rejects_damage();
  test_indexqoimask_decoder_roundtrip();
  test_indexqoimask_u32_index_and_row_dedup();
  test_indexqoimask_decoder_rejects_damage();
#ifdef _WIN32
  test_windows_icon_resource_for_executable("img2bin_raw.exe");
  test_windows_icon_resource_for_executable("img2bin_imprle.exe");
  test_windows_icon_resource_for_executable("img2bin_rle.exe");
  test_windows_icon_resource_for_executable("img2bin_qoi.exe");
  test_windows_icon_resource_for_executable("img2bin_qoif.exe");
  test_windows_icon_resource_for_executable("img2bin_indexqoi.exe");
  test_windows_icon_resource_for_executable("img2bin_indexqoimask.exe");
  test_windows_version_resource_for_executable("img2bin_raw.exe");
  test_windows_version_resource_for_executable("img2bin_imprle.exe");
  test_windows_version_resource_for_executable("img2bin_rle.exe");
  test_windows_version_resource_for_executable("img2bin_qoi.exe");
  test_windows_version_resource_for_executable("img2bin_qoif.exe");
  test_windows_version_resource_for_executable("img2bin_indexqoi.exe");
  test_windows_version_resource_for_executable("img2bin_indexqoimask.exe");
#endif

  if (g_test_failures != 0) {
    fprintf(stderr, "%d test(s) failed.\n", g_test_failures);
    return 1;
  }

  printf("All tests passed.\n");
  return 0;
}
