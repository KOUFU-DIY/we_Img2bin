# img2bin_qoif 使用说明

原始 QOI（无字典）压缩取模工具：去掉 QOI 的 64 项哈希字典，只保留小差分/大差分/行程/原始块。压缩率略逊于带字典的 `img2bin_qoi`，换来 QOI 家族里**最简单的解码器**——零字典 RAM、零哈希计算。第一次实现 QOI 系解码器建议从它入手。

## 快速开始

- **双击运行**：第一次双击自动创建 `input`、`output` 文件夹；把图片放进 `input` 再双击一次（默认 `rgb565`、大端）
- **拖拽**：把图片或文件夹拖到 `img2bin_qoif.exe` 上
- **命令行**：

```powershell
.\img2bin_qoif.exe --input .\demo.png --output .\out --format argb8888
```

## 输出是什么

```text
<原图名>_<像素格式>_qoif_<be|le>_<宽>x<高>.bin
```

文件 = **6 字节通用资源头**（算法 nibble `0x5`）+ QOIF payload。控制台同时报告体积率：

```text
Wrote out\ui_rgb565_qoif_be_128x64.bin (2400 bytes, payload 2394 / raw 16384 = 14.6%)
```

## payload 布局

与本项目的 `QOI` 完全相同，仅去掉字典：

- `0x00..0x3F` **不会出现**（解码器遇到应按损坏流处理）
- `0x40..0x7F` DIFF；`0x80..0xBF`+1 字节 LUMA；`0xC0..0xFD` RUN（1..62）
- `0xFE` 颜色原始块 / `0xFF` 完整原始块，后接字节数由像素格式决定
- 无标准 QOI 头尾；运算在量化通道域上进行（如 `RGB565` 用 R5/G6/B5）

解码只需维护“前像素”一组量化通道状态。详细规则见[解码编写说明](README-decoder.md)第六、八章。

## 参数

| 参数 | 说明 |
| --- | --- |
| `--input <file-or-dir>` | 输入文件或目录（默认 `<exe_dir>\input`） |
| `--output <dir>` | 输出目录（默认 `<exe_dir>\output`） |
| `--format <name>` | 单一像素格式（默认 `rgb565`） |
| `--formats <all\|f1,f2,...>` | 一次输出多种格式（`all` 自动跳过 Alpha 蒙版） |
| `--little-endian` | 小端输出（默认大端） |
| `--bg-color <RRGGBB>` | 非 Alpha 格式的透明区域先与该背景色混合（默认 `000000`） |
| `--manifest` | 在输出目录写 `img2bin_qoif-manifest.json` 运行清单（默认关闭） |
| `--info` / `--list-formats` / `--help` | 元数据 JSON / 格式清单 / 帮助 |

## 支持的像素格式

9 种彩色格式：`argb8888` `argb6666` `argb4444` `argb2222` `argb8565` `rgb888` `rgb565` `rgb332` `ragb5155`。
Alpha 蒙版（`a8/a4/a2/a1`）仅 `img2bin_raw` 支持——本工具显式点名会报错（退出码 1）。

## 使用示例

```powershell
.\img2bin_qoif.exe --format argb8888
.\img2bin_qoif.exe --formats all
.\img2bin_qoif.exe --input .\ui --output .\out --format rgb565 --little-endian --manifest
```

## 退出码

`0` 成功；`1` 参数错误；`2` 输入错误；`3` 编码错误；`4` 写入错误；`5` 内部错误；`6` 批处理部分失败。错误详情以单行 JSON 输出到 stderr（逐字段见[接口 Schema 说明](README-schema.md)）。

## 相关文档

- [工具总览与共同行为](README-tools.md)
- [像素格式说明](README-formats.md)
- [解码编写说明](README-decoder.md)（含 C99 参考解码器：`img2bin_decode_qoif`）
- [协议与验证说明](README-protocol.md)
