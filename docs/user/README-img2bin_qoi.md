# img2bin_qoi 使用说明

原始 QOI 压缩取模工具：QOI 家族中压缩手段最全的一个——64 项哈希字典 + 小差分/大差分/行程/原始块。通常压缩率最好，代价是解码端需要维护 64 项字典 RAM，且不支持从中间跳转解码。

## 快速开始

- **双击运行**：第一次双击自动创建 `input`、`output` 文件夹；把图片放进 `input` 再双击一次（默认 `rgb565`、大端）
- **拖拽**：把图片或文件夹拖到 `img2bin_qoi.exe` 上
- **命令行**：

```powershell
.\img2bin_qoi.exe --input .\demo.png --output .\out --format argb8888
```

## 输出是什么

```text
<原图名>_<像素格式>_qoi_<be|le>_<宽>x<高>.bin
```

文件 = **6 字节通用资源头**（算法 nibble `0x3`）+ QOI payload。控制台同时报告体积率：

```text
Wrote out\photo_rgb565_qoi_be_128x64.bin (3200 bytes, payload 3194 / raw 16384 = 19.5%)
```

## payload 与标准 QOI 的差异

- **没有**标准 QOI 的 14 字节文件头和 8 字节结束码，payload 直接是 op 流
- `0xFE`（颜色原始块）/`0xFF`（完整原始块）后接的字节数由**当前像素格式**决定，不是固定 3/4 字节
- 所有比较、哈希与差分运算都在“量化到目标格式位宽后的通道值”上进行（如 `RGB565` 用 R5/G6/B5），不是 8 位 RGBA

op 一览（详细规则见[解码编写说明](README-decoder.md)第六章）：

| op | 含义 |
| --- | --- |
| `0x00..0x3F` | 字典命中：`hash = (r*3+g*5+b*7+a*11) & 63` |
| `0x40..0x7F` | DIFF 小差分（每通道 -2..1） |
| `0x80..0xBF` + 1 字节 | LUMA 大差分 |
| `0xC0..0xFD` | RUN 重复前像素 1..62 次 |
| `0xFE` + 颜色字节 | 颜色原始块，Alpha 沿用前像素 |
| `0xFF` + 完整像素 | 完整原始块（含 Alpha） |

解码端资源：64 项字典（每项 4 个量化通道值）+ 前像素状态。若想省掉字典，用 `img2bin_qoif`；若还要跳转解码，用 `img2bin_indexqoi`。

## 参数

| 参数 | 说明 |
| --- | --- |
| `--input <file-or-dir>` | 输入文件或目录（默认 `<exe_dir>\input`） |
| `--output <dir>` | 输出目录（默认 `<exe_dir>\output`） |
| `--format <name>` | 单一像素格式（默认 `rgb565`） |
| `--formats <all\|f1,f2,...>` | 一次输出多种格式（`all` 自动跳过 Alpha 蒙版） |
| `--little-endian` | 小端输出（默认大端） |
| `--bg-color <RRGGBB>` | 非 Alpha 格式的透明区域先与该背景色混合（默认 `000000`） |
| `--manifest` | 在输出目录写 `img2bin_qoi-manifest.json` 运行清单（默认关闭） |
| `--info` / `--list-formats` / `--help` | 元数据 JSON / 格式清单 / 帮助 |

## 支持的像素格式

9 种彩色格式：`argb8888` `argb6666` `argb4444` `argb2222` `argb8565` `rgb888` `rgb565` `rgb332` `ragb5155`。
Alpha 蒙版（`a8/a4/a2/a1`）仅 `img2bin_raw` 支持——本工具显式点名会报错（退出码 1）。

## 使用示例

```powershell
.\img2bin_qoi.exe --format argb8888
.\img2bin_qoi.exe --format rgb565 --bg-color FF0000
.\img2bin_qoi.exe --input .\photos --output .\out --formats all --manifest
```

## 退出码

`0` 成功；`1` 参数错误；`2` 输入错误；`3` 编码错误；`4` 写入错误；`5` 内部错误；`6` 批处理部分失败。错误详情以单行 JSON 输出到 stderr（逐字段见[接口 Schema 说明](README-schema.md)）。

## 相关文档

- [工具总览与共同行为](README-tools.md)
- [像素格式说明](README-formats.md)
- [解码编写说明](README-decoder.md)（含 C99 参考解码器：`img2bin_decode_qoi`）
- [协议与验证说明](README-protocol.md)
