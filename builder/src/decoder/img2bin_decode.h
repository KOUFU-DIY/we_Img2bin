/*
 * img2bin 参考解码器 (reference decoder)
 *
 * 独立文件，不依赖本项目其他代码，也不使用动态内存，可直接拷贝到
 * 下位机工程使用。协议细节见 docs/user/README-decoder.md。
 *
 * 工具输出的 .bin 文件 = 6 字节通用资源头 + 算法 payload：
 *   - 文件级接口（推荐）：img2bin_decode_header / img2bin_decode_image /
 *     img2bin_decode_image_from_slot，输入整个 .bin 文件，按头自动分发。
 *   - payload 级接口：img2bin_decode_raw/rle/imprle/qoi/qoif/indexqoi*，
 *     输入去掉 6 字节通用头后的裸流（indexQOI V2 裸流自带 14 字节索引头，
 *     后随 u16/u24/u32 索引区、静态调色盘与 QOI 数据流，无尾部结束码）；
 *     Alpha 蒙版格式（A8/A4/A2/A1，raw 算法）走 img2bin_decode_raw_alpha；
 *     indexQOI_MASK（算法 0x6，仅 A8）走 img2bin_decode_indexqoimask*。
 *
 * 所有解码输出都是 RAW 打包像素字节流（不含任何头），与
 * img2bin_raw.exe 输出的 payload 在同格式、同字节序下逐字节一致
 * （indexQOI_MASK 量化档 q<8 时输出为量化后按高位复制扩展的 8bit Alpha）。
 * 字节序不在头里，需要由外部提供（文件名或工程约定）。
 */
#ifndef IMG2BIN_DECODE_H
#define IMG2BIN_DECODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A8/A4/A2/A1 是 Alpha 蒙版家族（只存透明度）：
   行字节对齐（行字节数 = (宽×bpp+7)/8）、MSB-first（最左像素在字节高位）、
   行尾补位为 0、无字节序维度。解码到 8bit alpha 的扩展规则：
   A8 恒等；A4 v→(v<<4)|v；A2 v→v*0x55；A1 v→0/255。
   合法算法组合：四种格式 × raw（img2bin_decode_raw_alpha），以及
   A8 × indexQOI_MASK（img2bin_decode_indexqoimask*）；其余组合是非法流。
   按 pixel_count 的 payload 级接口一律返回 ERR_ARGUMENTS。 */
typedef enum img2bin_decode_format_e {
  IMG2BIN_DECODE_FMT_ARGB8888 = 0,
  IMG2BIN_DECODE_FMT_ARGB6666,
  IMG2BIN_DECODE_FMT_ARGB4444,
  IMG2BIN_DECODE_FMT_ARGB2222,
  IMG2BIN_DECODE_FMT_ARGB8565,
  IMG2BIN_DECODE_FMT_RGB888,
  IMG2BIN_DECODE_FMT_RGB565,
  IMG2BIN_DECODE_FMT_RGB332,
  IMG2BIN_DECODE_FMT_RAGB5155,
  IMG2BIN_DECODE_FMT_A8,
  IMG2BIN_DECODE_FMT_A4,
  IMG2BIN_DECODE_FMT_A2,
  IMG2BIN_DECODE_FMT_A1,
  IMG2BIN_DECODE_FMT_COUNT
} img2bin_decode_format_t;

typedef enum img2bin_decode_endianness_e {
  IMG2BIN_DECODE_BIG_ENDIAN = 0,
  IMG2BIN_DECODE_LITTLE_ENDIAN = 1
} img2bin_decode_endianness_t;

typedef enum img2bin_decode_status_e {
  IMG2BIN_DECODE_OK = 0,
  IMG2BIN_DECODE_ERR_ARGUMENTS = 1,
  IMG2BIN_DECODE_ERR_TRUNCATED = 2,
  IMG2BIN_DECODE_ERR_CORRUPT = 3,
  IMG2BIN_DECODE_ERR_OUTPUT_TOO_SMALL = 4,
  IMG2BIN_DECODE_ERR_TRAILING_DATA = 5
} img2bin_decode_status_t;

#define IMG2BIN_DECODE_HEADER_SIZE 6u
#define IMG2BIN_DECODE_RESOURCE_TYPE_IMAGE 0x00u

typedef enum img2bin_decode_algorithm_e {
  IMG2BIN_DECODE_ALGO_RAW = 0x0,
  IMG2BIN_DECODE_ALGO_RLE = 0x1,
  IMG2BIN_DECODE_ALGO_IMPRLE = 0x2,
  IMG2BIN_DECODE_ALGO_QOI = 0x3,
  IMG2BIN_DECODE_ALGO_INDEXQOI = 0x4,
  IMG2BIN_DECODE_ALGO_QOIF = 0x5,
  IMG2BIN_DECODE_ALGO_INDEXQOIMASK = 0x6
} img2bin_decode_algorithm_t;

typedef struct img2bin_decode_header_s {
  uint8_t resource_type;
  uint8_t algorithm_nibble;
  uint8_t format_nibble;
  img2bin_decode_format_t format;
  uint16_t width;
  uint16_t height;
} img2bin_decode_header_t;

/* indexQOI V2（静态调色盘）索引头，14 字节（[0]=0x0E 兼作版本标识，V1 的
   0x0D 会被判为损坏流）。调色盘紧跟三个索引区之后，每项一个完整原始格式
   像素（含 Alpha，字节序同 0xFF 全量像素）；QOI 数据流起点 =
   palette_offset + palette_count × bytes_per_pixel，数据流无尾部结束码。 */
typedef struct img2bin_indexqoi_header_s {
  uint16_t width;
  uint16_t height;
  uint16_t index_interval;
  uint16_t u16_bytes;
  uint16_t u24_bytes;
  uint16_t u32_bytes;
  uint8_t palette_count; /* 0..64，0 = 无调色盘 */
  size_t slot_count;
  size_t palette_offset; /* = 14 + u16_bytes + u24_bytes + u32_bytes */
} img2bin_indexqoi_header_t;

/* 整字节格式返回每像素字节数；亚字节的 Alpha 蒙版（A4/A2/A1）返回 0。 */
size_t img2bin_decode_bytes_per_pixel(img2bin_decode_format_t format);
/* 每像素位数（A8/A4/A2/A1 = 8/4/2/1，其余 = 字节数×8）。 */
size_t img2bin_decode_bits_per_pixel(img2bin_decode_format_t format);
/* 行字节数 = (宽×每像素位数+7)/8；参数非法返回 0。 */
size_t img2bin_decode_row_stride(img2bin_decode_format_t format, uint16_t width);

/* Alpha 蒙版（A8/A4/A2/A1）raw payload 解码：行打包语义下的恒等拷贝 +
   尺寸校验（input_size 必须精确等于 高×行字节数）。 */
img2bin_decode_status_t img2bin_decode_raw_alpha(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  uint16_t width,
  uint16_t height,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_header(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_header_t *out_header);

/* 解析通用头并按其中的算法/格式自动分发解码（indexQOI 也支持）。 */
img2bin_decode_status_t img2bin_decode_image(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_endianness_t endianness,
  img2bin_decode_header_t *out_header,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

/* 带通用头的 indexQOI 文件：从第 slot 个索引点解码到图片末尾。 */
img2bin_decode_status_t img2bin_decode_image_from_slot(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_endianness_t endianness,
  size_t slot,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_raw(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_rle(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_imprle(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_qoi(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

img2bin_decode_status_t img2bin_decode_qoif(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t pixel_count,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

/* indexQOI_MASK（算法 nibble 0x6，仅 A8 蒙版）payload 头。payload 不含宽高
   （宽高在 6 字节通用资源头里），布局（多字节字段恒大端）：
     [0] 标志位（b1:b0 量化位数 q：00=8bit 01=7bit 10=6bit 11=5bit，b7..b2 恒 0）
     [1..2] u16 行索引项数量 m   [3..4] u32 行索引项数量（必须 = 高−m）
     [u16 行索引表 m×2B][u32 行索引表 (高−m)×4B][字典数量 n(1B, 0..64)]
     [字典 n×1B（q 位域值）][像素流]
   行索引 = 该行数据相对像素流起点的字节偏移；行去重后多行可指向同一偏移，
   第 r 行：r<m 查 u16 表第 r 项，否则查 u32 表第 r−m 项。 */
typedef struct img2bin_indexqoimask_header_s {
  uint8_t quantize_bits; /* 5..8 */
  uint16_t u16_count;    /* m */
  uint16_t u32_count;    /* = 高 − m */
  uint8_t dict_count;    /* 0..64，0 = 无字典（INDEX op 不可用） */
  size_t dict_offset;    /* 字典首项在 payload 内的偏移 */
  size_t stream_offset;  /* 像素流起点在 payload 内的偏移 */
} img2bin_indexqoimask_header_t;

img2bin_decode_status_t img2bin_decode_indexqoi_header(
  const uint8_t *input,
  size_t input_size,
  img2bin_indexqoi_header_t *out_header);

img2bin_decode_status_t img2bin_decode_indexqoi_offset(
  const uint8_t *input,
  size_t input_size,
  size_t slot,
  uint32_t *out_offset);

img2bin_decode_status_t img2bin_decode_indexqoi(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

/* 从第 slot 个索引点开始解码到图片末尾。首像素位置 = slot * index_interval。 */
img2bin_decode_status_t img2bin_decode_indexqoi_from_slot(
  const uint8_t *input,
  size_t input_size,
  img2bin_decode_format_t format,
  img2bin_decode_endianness_t endianness,
  size_t slot,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

/* ===== indexQOI_MASK（算法 0x6，仅 A8）payload 级接口 =====
   高（height）来自 6 字节通用资源头，用于校验/切分两张行索引表。
   解码输出为 8bit Alpha（每像素 1 字节）：量化档 q<8 时按高位复制扩展
   out = (v<<s)|(v>>(q−s))（s = 8−q），q=8 恒等（与 raw a8 payload 逐字节一致）。 */
img2bin_decode_status_t img2bin_decode_indexqoimask_header(
  const uint8_t *input,
  size_t input_size,
  uint16_t height,
  img2bin_indexqoimask_header_t *out_header);

/* 取第 row 行数据相对像素流起点的字节偏移（行去重后偏移不随行号单调）。 */
img2bin_decode_status_t img2bin_decode_indexqoimask_row_offset(
  const uint8_t *input,
  size_t input_size,
  uint16_t height,
  size_t row,
  uint32_t *out_offset);

/* 按行随机访问：解码第 row 行到 width 字节的 8bit Alpha。 */
img2bin_decode_status_t img2bin_decode_indexqoimask_row(
  const uint8_t *input,
  size_t input_size,
  uint16_t width,
  uint16_t height,
  size_t row,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

/* 整图解码到 宽×高 字节的 8bit Alpha（逐行经行索引定位，不假设行间连续）。 */
img2bin_decode_status_t img2bin_decode_indexqoimask(
  const uint8_t *input,
  size_t input_size,
  uint16_t width,
  uint16_t height,
  uint8_t *output,
  size_t output_capacity,
  size_t *out_written);

#ifdef __cplusplus
}
#endif

#endif
