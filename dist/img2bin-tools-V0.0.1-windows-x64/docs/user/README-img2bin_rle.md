# img2bin_rle 使用说明

原始 RLE 压缩取模工具：最简单的行程压缩，每段都是“计数 + 一组像素”。解码器实现最容易，但杂色区域每个像素都要多付 1 字节计数——杂色多的图请改用 `img2bin_imprle` 或 QOI 家族。

## 快速开始

- **双击运行**：第一次双击自动创建 `input`、`output` 文件夹；把图片放进 `input` 再双击一次（默认 `rgb565`、大端）
- **拖拽**：把图片或文件夹拖到 `img2bin_rle.exe` 上
- **命令行**：

```powershell
.\img2bin_rle.exe --input .\demo.png --output .\out --format argb8888
```

## 输出是什么

```text
<原图名>_<像素格式>_rle_<be|le>_<宽>x<高>.bin
```

文件 = **6 字节通用资源头**（算法 nibble `0x1`）+ RLE payload。控制台同时报告体积率：

```text
Wrote out\demo_rgb565_rle_be_128x64.bin (2054 bytes, payload 2048 / raw 16384 = 12.5%)
```

## payload 布局

```text
{数量, 数据组, 数量, 数据组, ..., 0x00}
```

- `数量` 1 字节，取值 `1..255`：把紧随其后的一组像素重复 N 次
- `0x00` 只表示结束，不表示数据
- “一组数据”的字节数 = 该像素格式的每像素字节数，多字节格式受大小端开关影响

解码伪代码见[解码编写说明](README-decoder.md)第三章。

## 参数

| 参数 | 说明 |
| --- | --- |
| `--input <file-or-dir>` | 输入文件或目录（默认 `<exe_dir>\input`） |
| `--output <dir>` | 输出目录（默认 `<exe_dir>\output`） |
| `--format <name>` | 单一像素格式（默认 `rgb565`） |
| `--formats <all\|f1,f2,...>` | 一次输出多种格式（`all` 自动跳过 Alpha 蒙版） |
| `--little-endian` | 小端输出（默认大端） |
| `--bg-color <RRGGBB>` | 非 Alpha 格式的透明区域先与该背景色混合（默认 `000000`） |
| `--manifest` | 在输出目录写 `img2bin_rle-manifest.json` 运行清单（默认关闭） |
| `--info` / `--list-formats` / `--help` | 元数据 JSON / 格式清单 / 帮助 |

## 支持的像素格式

9 种彩色格式：`argb8888` `argb6666` `argb4444` `argb2222` `argb8565` `rgb888` `rgb565` `rgb332` `ragb5155`。
Alpha 蒙版（`a8/a4/a2/a1`）仅 `img2bin_raw` 支持——本工具显式点名会报错（退出码 1）。

## 使用示例

```powershell
.\img2bin_rle.exe --format argb8888
.\img2bin_rle.exe --formats rgb565,rgb888
.\img2bin_rle.exe --input .\bg --output .\out --format rgb565 --manifest
```

## 退出码

`0` 成功；`1` 参数错误；`2` 输入错误；`3` 编码错误；`4` 写入错误；`5` 内部错误；`6` 批处理部分失败。错误详情以单行 JSON 输出到 stderr（逐字段见[接口 Schema 说明](README-schema.md)）。

## 相关文档

- [工具总览与共同行为](README-tools.md)
- [像素格式说明](README-formats.md)
- [解码编写说明](README-decoder.md)（含 C99 参考解码器：`img2bin_decode_rle`）
- [协议与验证说明](README-protocol.md)
