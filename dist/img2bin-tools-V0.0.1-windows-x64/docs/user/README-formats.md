# 像素格式说明

本页说明当前支持的 13 种像素格式（9 种彩色 + 4 种 Alpha 蒙版），以及它们在这套工具中的精确打包规则。  
如果你要自己写解码器，这一页就是“如何把一个像素组装/拆开”的基础文档。

## 通用规则

- 默认使用大端
- 单字节格式不区分大小端
- 多字节格式在小端模式下，按“整像素字节反转”处理
- 像素顺序固定为：从左到右、从上到下
- 所有 `.bin` 文件都以 6 字节通用资源头开始（类型 + 算法/格式码 + 宽高，见 [协议与验证说明](README-protocol.md)）
- 通用头之后的 `RAW / RLE / IMPRLE / QOI / QOIF` payload 不再包含任何头
- `indexQOI` 在通用头之后另有 13 字节索引头
- Alpha 蒙版格式（`A8/A4/A2/A1`）**只在 `img2bin_raw` 中可用**，按行打包，见本页末尾专节

## 颜色量化与透明处理

以下规则主要用于理解编码过程，以及 `QOI` 系列在内部维护的像素状态。

### 通道量化

当 8 位颜色通道被压缩到 `n` 位时，使用：

```text
Qn = (value8 * ((1 << n) - 1) + 127) / 255
```

### 1bit Alpha 量化

`RAGB5155` 的 Alpha 不是线性量化，而是阈值规则：

```text
A1 = (A8 >= 128) ? 1 : 0
```

### 不带 Alpha 格式的透明处理

以下格式不带 Alpha：

- `RGB888`
- `RGB565`
- `RGB332`

因此编码前会先将透明像素与背景色混合：

```text
out = (src * alpha + bg * (255 - alpha) + 127) / 255
```

默认背景色：

```text
000000
```

可通过 `--bg-color RRGGBB` 修改。

## 格式总表

| 格式 | 每像素字节数 | Alpha 位数 | 是否使用背景色 | QOI 原始色块 |
| --- | --- | --- | --- | --- |
| `ARGB8888` | 4 | 8 | 否 | `OP_RGB` 或 `OP_RGBA` |
| `ARGB6666` | 3 | 6 | 否 | `OP_RGBA` |
| `ARGB4444` | 2 | 4 | 否 | `OP_RGBA` |
| `ARGB2222` | 1 | 2 | 否 | `OP_RGBA` |
| `ARGB8565` | 3 | 8 | 否 | `OP_RGB` 或 `OP_RGBA` |
| `RGB888` | 3 | 无 | 是 | `OP_RGB` |
| `RGB565` | 2 | 无 | 是 | `OP_RGB` |
| `RGB332` | 1 | 无 | 是 | `OP_RGB` |
| `RAGB5155` | 2 | 1 | 否 | `OP_RGBA` |
| `A8` | 1 | 8（仅 Alpha） | 否 | 不适用（仅 raw） |
| `A4` | 1/2（4bit/像素） | 4（仅 Alpha） | 否 | 不适用（仅 raw） |
| `A2` | 1/4（2bit/像素） | 2（仅 Alpha） | 否 | 不适用（仅 raw） |
| `A1` | 1/8（1bit/像素） | 1（仅 Alpha） | 否 | 不适用（仅 raw） |

说明：

- `QOI` 家族里的 `OP_RGB = 0xFE`
- `QOI` 家族里的 `OP_RGBA = 0xFF`
- 这里的 `OP_RGB / OP_RGBA` 不是标准 QOI 的固定 3/4 字节形式，而是“跟随当前像素格式的原始打包字节数”
- `A8/A4/A2/A1` 只保存 Alpha 通道，只有 `img2bin_raw` 支持；亚字节格式按行打包（见专节），“每像素字节数”对它们不适用

## 各格式详细规则

## ARGB8888

### RAW 输出

- 大端：`A, R, G, B`
- 小端：`B, G, R, A`

### 解包

大端：

```text
A = byte0
R = byte1
G = byte2
B = byte3
```

小端：

```text
B = byte0
G = byte1
R = byte2
A = byte3
```

### QOI 原始块

- `OP_RGB` 后接 3 字节：颜色部分，Alpha 维持上一个像素不变
- `OP_RGBA` 后接 4 字节：完整像素

## ARGB6666

### RAW 输出

总长度 24bit，按名称顺序 `ARGB` 打包。

大端：

```text
byte0 = A5..0 R5..4
byte1 = R3..0 G5..2
byte2 = G1..0 B5..0
```

小端：

```text
byte0 = 原 byte2
byte1 = 原 byte1
byte2 = 原 byte0
```

### 解包

先在小端模式下把 3 字节整体反转回大端顺序，再解：

```text
A6 = byte0 >> 2
R6 = ((byte0 & 0x03) << 4) | (byte1 >> 4)
G6 = ((byte1 & 0x0F) << 2) | (byte2 >> 6)
B6 = byte2 & 0x3F
```

### QOI 原始块

- 只使用 `OP_RGBA`
- 后接 3 字节完整 `ARGB6666` 原始像素

## ARGB4444

### RAW 输出

大端：

```text
byte0 = A3..0 R3..0
byte1 = G3..0 B3..0
```

小端：两字节反转。

### 解包

先按需要恢复到大端顺序，再解：

```text
A4 = byte0 >> 4
R4 = byte0 & 0x0F
G4 = byte1 >> 4
B4 = byte1 & 0x0F
```

### QOI 原始块

- 只使用 `OP_RGBA`
- 后接 2 字节完整 `ARGB4444`

## ARGB2222

### RAW 输出

单字节：

```text
bits7..6 = A
bits5..4 = R
bits3..2 = G
bits1..0 = B
```

### 解包

```text
A2 = (byte >> 6) & 0x03
R2 = (byte >> 4) & 0x03
G2 = (byte >> 2) & 0x03
B2 = byte & 0x03
```

### QOI 原始块

- 只使用 `OP_RGBA`
- 后接 1 字节完整 `ARGB2222`

## ARGB8565

### RAW 输出

大端：

```text
byte0 = A8
byte1 = RGB565 高字节
byte2 = RGB565 低字节
```

小端：

```text
byte0 = RGB565 低字节
byte1 = RGB565 高字节
byte2 = A8
```

其中：

```text
RGB565 = R5 G6 B5
```

### 解包

先取出 `A8` 和 `RGB565`，再解：

```text
R5 = (rgb565 >> 11) & 0x1F
G6 = (rgb565 >> 5) & 0x3F
B5 = rgb565 & 0x1F
```

### QOI 原始块

- `OP_RGB` 后接 2 字节 `RGB565`，Alpha 保持上一个像素不变
- `OP_RGBA` 后接 3 字节完整 `ARGB8565`

## RGB888

### RAW 输出

- 大端：`R, G, B`
- 小端：`B, G, R`

### 解包

大端：

```text
R = byte0
G = byte1
B = byte2
```

小端：

```text
B = byte0
G = byte1
R = byte2
```

### QOI 原始块

- 使用 `OP_RGB`
- 后接 3 字节完整 `RGB888`

注意：当前实现中 `RGB888` 在 `QOI` 族中使用的是工具自身定义的顺序，应直接以本工具输出和本页规则为准。

## RGB565

### RAW 输出

16bit：

```text
value = (R5 << 11) | (G6 << 5) | B5
```

大端：

```text
byte0 = value 高字节
byte1 = value 低字节
```

小端：两字节反转。

### 解包

```text
R5 = (value >> 11) & 0x1F
G6 = (value >> 5) & 0x3F
B5 = value & 0x1F
```

### QOI 原始块

- 使用 `OP_RGB`
- 后接 2 字节完整 `RGB565`

## RGB332

### RAW 输出

单字节：

```text
bits7..5 = R
bits4..2 = G
bits1..0 = B
```

### 解包

```text
R3 = (byte >> 5) & 0x07
G3 = (byte >> 2) & 0x07
B2 = byte & 0x03
```

### QOI 原始块

- 使用 `OP_RGB`
- 后接 1 字节完整 `RGB332`

## RAGB5155

### RAW 输出

16bit：

```text
value = (R5 << 11) | (A1 << 10) | (G5 << 5) | B5
```

大端高字节在前，小端整体字节反转。

### 解包

```text
R5 = (value >> 11) & 0x1F
A1 = (value >> 10) & 0x01
G5 = (value >> 5) & 0x1F
B5 = value & 0x1F
```

### QOI 原始块

- 只使用 `OP_RGBA`
- 后接 2 字节完整 `RAGB5155`

## A8 / A4 / A2 / A1（Alpha 蒙版）

只保存透明度、不保存颜色的蒙版格式，供 GUI 运行时用前景色染色（单色图标、主题换色、字形式渲染）。**只有 `img2bin_raw` 支持**；其余五个工具显式点名会报错，`--formats all` 会静默跳过。

### 编码来源与量化

- 取输入图片的 Alpha 通道；JPG 等无 Alpha 输入视为全部不透明
- 忽略 `--bg-color`（没有颜色可混合）
- 量化用通用公式：`Qn = (A8 * ((1 << n) - 1) + 127) / 255`
  - `A1` 的实际效果即阈值 128：`A8 <= 127 → 0`，`A8 >= 128 → 1`

### RAW 输出（行打包）

- **每行从新字节开始**：行字节数 `row_stride = (宽 × bpp + 7) / 8`
- payload 总大小 = `高 × row_stride`
- **MSB-first**：最左像素放在字节高位
  - `A8`：1 像素/字节
  - `A4`：2 像素/字节，左像素在高 nibble
  - `A2`：4 像素/字节，左像素在 bits7..6
  - `A1`：8 像素/字节，左像素在 bit7
- 行尾不足一个字节的补位填 `0`
- 无字节序维度：`be`/`le` 输出逐字节一致

示例：宽 5 的 `A4` 行占 3 字节，第 3 字节高 nibble 是第 5 个像素、低 nibble 恒为 0。

### 解包（还原到 8bit Alpha）

```text
A8: a = byte
A4: a = (v << 4) | v
A2: a = v * 0x55
A1: a = v ? 255 : 0
```

### 与 OLED 点阵的区别

`A1` 不是 OLED 点阵（格式 nibble `0xF`）：`A1` 是行主序、MSB-first 的透明度蒙版，参与 Alpha 混合；OLED 点阵是页式屏区布局。二者不可混用。

## 写解码器时的建议状态表示

对 `RAW / RLE / IMPRLE`：

- 直接按目标格式字节组处理即可

对 `QOI / QOIF / indexQOI`：

- 最好在解码器内部维护“量化后的通道值状态”
- 再根据当前格式把状态重新打包成目标像素字节

推荐内部状态：

```text
current.r
current.g
current.b
current.a
```

其中这些值不是 8 位原始颜色，而是当前目标格式位宽下的量化值，例如：

- `RGB565` 用 `R5/G6/B5`
- `ARGB4444` 用 `A4/R4/G4/B4`
- `RGB332` 用 `R3/G3/B2`

只有这样，`INDEX / DIFF / LUMA` 才会与当前工具输出一致。

## 推荐使用建议

- 通用屏幕资源：`RGB565`
- 完整 Alpha：`ARGB8888`
- 节省空间同时保留 Alpha：`ARGB4444`、`ARGB8565`
- 非常小的资源：`RGB332`、`ARGB2222`
- 只需要透明/不透明两种状态：`RAGB5155`
- 单色图标 / 运行时染色 / 蒙版：`A8`（质量优先）或 `A4`（尺寸优先，通常够用）；`A2/A1` 用于极端省空间的粗蒙版
