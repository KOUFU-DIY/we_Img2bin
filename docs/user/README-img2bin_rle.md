# img2bin_rle 使用说明

原始 RLE 压缩取模工具：最简单的行程压缩，每段都是"计数 + 一组像素"，解码实现最容易。杂色区域每个像素要多付 1 字节计数（会比无压缩更大），杂色多的图建议改用 `img2bin_imprle` 或 QOI 系工具。

## 快速开始

- **双击运行**：第一次双击自动创建 `input`、`output` 文件夹；把图片放进 `input` 再双击一次（默认 `rgb565`、大端）
- **拖拽**：把图片或文件夹拖到 `img2bin_rle.exe` 上
- **命令行**：

```powershell
.\img2bin_rle.exe --input .\demo.png --output .\out --format argb8888
```

## 参数

| 参数 | 说明 |
| --- | --- |
| `--input <file-or-dir>` | 输入文件或目录（默认 `<exe_dir>\input`） |
| `--output <dir>` | 输出目录（默认 `<exe_dir>\output`） |
| `--format <name>` | 单一像素格式（默认 `rgb565`） |
| `--formats <all\|f1,f2,...>` | 一次输出多种格式（`all` 自动跳过 `a8/a4/a2/a1`） |
| `--little-endian` | 小端输出（默认大端） |
| `--bg-color <RRGGBB>` | 非 Alpha 格式的透明区域先与该背景色混合（默认 `000000`） |
| `--manifest` | 在输出目录写 `img2bin_rle-manifest.json` 运行清单（默认关闭） |
| `--info` / `--list-formats` / `--help` | 元数据 JSON / 格式清单 / 帮助 |

每写出一个文件，stdout 报告一行体积率（压缩后 payload / 同格式无压缩 payload，通用头 6 字节两边不计入）：

```text
Wrote out\demo_rgb565_rle_be_128x64.bin (2054 bytes, payload 2048 / raw 16384 = 12.5%)
```

退出码：`0` 成功；`1` 参数错误；`2` 输入错误；`3` 编码错误；`4` 写入错误；`5` 内部错误；`6` 批处理部分失败。错误详情以单行 JSON 写到 stderr（字段：`code`、`exit_code`、`message.zh_cn/en`、`file`、`detail`、`stage`）。

## 输出文件结构

### 文件命名（机器接口）

```text
<原图名>_<像素格式>_rle_<be|le>_<宽>x<高>.bin
```

字节序**不存在于文件内部**，只体现在文件名的 `be|le` 段（或由工程约定提供）。

### 6 字节通用资源头

所有 `.bin` 都以 6 字节头开始，之后紧跟 RLE payload：

```text
byte0   = 资源类型，恒 0x00（图片）
byte1   = 格式码：高 nibble 压缩算法，低 nibble 像素格式
byte2-3 = 宽，恒大端
byte4-5 = 高，恒大端
```

- 本工具算法 nibble：`0x1`（全表：`raw=0x0 rle=0x1 imprle=0x2 qoi=0x3 indexqoi=0x4 qoif=0x5 indexqoimask=0x6`）
- 像素格式 nibble：`RGB565=0x0 RGB888=0x1 RGB332=0x4 ARGB8888=0x5 ARGB6666=0x6 ARGB4444=0x7 ARGB8565=0x8 ARGB2222=0x9 RAGB5155=0xA`（`0x2/0x3/0xF` 保留不用）

## 像素来源与预处理（编码语义）

1. 输入图片统一解码为每像素 8bit 的 `R,G,B,A`；没有 Alpha 通道的输入（如 JPG）视为 `A=255`
2. **非 Alpha 目标格式**（`rgb888/rgb565/rgb332`）先把透明区域与背景色混合（整数运算，除法向下取整）：

   ```text
   out = (src * A + bg * (255 - A) + 127) / 255
   ```

3. 8bit 通道量化到 n 位：`Qn = (v8 * ((1 << n) - 1) + 127) / 255`
4. 例外：`RAGB5155` 的 1bit Alpha 用阈值 `A1 = (A8 >= 128) ? 1 : 0`
5. 带 Alpha 的目标格式不做背景混合，RGB 通道直接量化原始值

## 彩色格式打包规则（9 种）

像素顺序恒为**从左到右、从上到下**。大端布局如下；**小端 = 把单个像素的全部字节整体反转**；单字节格式（`rgb332/argb2222`）两种模式输出一致。

| 格式 | 字节数 | 大端布局（bit 高→低） |
| --- | --- | --- |
| `argb8888` | 4 | `byte0=A8, byte1=R8, byte2=G8, byte3=B8` |
| `argb6666` | 3 | `byte0=A[5:0]R[5:4]`，`byte1=R[3:0]G[5:2]`，`byte2=G[1:0]B[5:0]` |
| `argb4444` | 2 | `byte0=A[3:0]R[3:0]`，`byte1=G[3:0]B[3:0]` |
| `argb2222` | 1 | `A[1:0] R[1:0] G[1:0] B[1:0]` |
| `argb8565` | 3 | `byte0=A8`，`byte1-2 = (R5<<11)|(G6<<5)|B5` 大端 |
| `rgb888` | 3 | `byte0=R8, byte1=G8, byte2=B8` |
| `rgb565` | 2 | `(R5<<11)|(G6<<5)|B5` 大端 |
| `rgb332` | 1 | `(R3<<5)|(G3<<2)|B2` |
| `ragb5155` | 2 | `(R5<<11)|(A1<<10)|(G5<<5)|B5` 大端 |

Alpha 蒙版格式（`a8/a4/a2/a1`）由 `img2bin_raw`（全部四种）与 `img2bin_indexqoimask`（仅 `a8`）支持，本工具显式点名会报错（退出码 1）。

## RLE payload 规则（唯一解）

以下"数据组"= 按上表打包好的**一个完整像素**的字节（长度 = 每像素字节数）。

### 数据结构

```text
{数量, 数据组, 数量, 数据组, ..., 0x00}
```

- `数量` 为 1 字节，取值 `1..255`：把紧随其后的一组像素连续输出 `数量` 次
- `0x00` 只表示整个流结束，永远不表示数据
- 结束符之前的所有段解出的像素总数必须恰好等于 `宽 × 高`

### 解码伪代码

```c
while (1) {
    count = read_u8();
    if (count == 0x00) break;
    read pixel_group[group_size];
    repeat count times: emit pixel_group;
}
/* 此时必须恰好产出 宽×高 个像素，且流中不再有剩余字节 */
```

解码校验建议：`数量` 段解出的像素数超过 `宽 × 高` = 损坏流；到达流尾仍未见 `0x00` = 截断；`0x00` 之后仍有字节 = 多余数据。

### 编码器规则（保证输出唯一，可对拍）

从第一个像素起贪心取**最长**连续相同像素游程；游程长度超过 255 时按 `255,255,...,余数` 依次拆段。逐段写出后追加 `0x00`。相邻两段的像素组允许相同（拆段产生）。

## 使用示例

```powershell
.\img2bin_rle.exe --format argb8888
.\img2bin_rle.exe --formats rgb565,rgb888
.\img2bin_rle.exe --input .\bg --output .\out --format rgb565 --manifest
```
