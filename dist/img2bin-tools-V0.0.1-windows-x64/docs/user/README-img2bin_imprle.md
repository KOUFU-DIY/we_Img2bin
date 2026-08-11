# img2bin_imprle 使用说明

改进 RLE 压缩取模工具：在原始 RLE 的基础上增加“原样段”，纯色区域按重复段压缩、杂色区域按原样段直拷，避免了原始 RLE 在杂色区域的体积膨胀。适合大块纯色 + 局部细节的 GUI 素材。

## 快速开始

- **双击运行**：第一次双击自动创建 `input`、`output` 文件夹；把图片放进 `input` 再双击一次（默认 `rgb565`、大端）
- **拖拽**：把图片或文件夹拖到 `img2bin_imprle.exe` 上
- **命令行**：

```powershell
.\img2bin_imprle.exe --input .\demo.png --output .\out --format argb8888
```

## 输出是什么

```text
<原图名>_<像素格式>_imprle_<be|le>_<宽>x<高>.bin
```

文件 = **6 字节通用资源头**（算法 nibble `0x2`）+ 改进 RLE payload。控制台同时报告体积率：

```text
Wrote out\demo_rgb565_imprle_be_128x64.bin (4102 bytes, payload 4096 / raw 16384 = 25.0%)
```

## payload 布局

```text
{控制字节, 数据, 控制字节, 数据, ..., 0x00}
```

- 控制字节 `bit7 = 1`：重复段——后接 **1 组**像素数据，重复 `bit6..0` 次（1..127）
- 控制字节 `bit7 = 0`：原样段——后接 `bit6..0` 组（1..127）互不相同的像素数据，逐组取用
- `0x00` 表示结束
- “一组数据”的字节数 = 该像素格式的每像素字节数，多字节格式受大小端开关影响

解码伪代码见[解码编写说明](README-decoder.md)第四章。

## 参数

| 参数 | 说明 |
| --- | --- |
| `--input <file-or-dir>` | 输入文件或目录（默认 `<exe_dir>\input`） |
| `--output <dir>` | 输出目录（默认 `<exe_dir>\output`） |
| `--format <name>` | 单一像素格式（默认 `rgb565`） |
| `--formats <all\|f1,f2,...>` | 一次输出多种格式（`all` 自动跳过 Alpha 蒙版） |
| `--little-endian` | 小端输出（默认大端） |
| `--bg-color <RRGGBB>` | 非 Alpha 格式的透明区域先与该背景色混合（默认 `000000`） |
| `--manifest` | 在输出目录写 `img2bin_imprle-manifest.json` 运行清单（默认关闭） |
| `--info` / `--list-formats` / `--help` | 元数据 JSON / 格式清单 / 帮助 |

## 支持的像素格式

9 种彩色格式：`argb8888` `argb6666` `argb4444` `argb2222` `argb8565` `rgb888` `rgb565` `rgb332` `ragb5155`。
Alpha 蒙版（`a8/a4/a2/a1`）仅 `img2bin_raw` 支持——本工具显式点名会报错（退出码 1）。

## 使用示例

```powershell
.\img2bin_imprle.exe --format argb8888
.\img2bin_imprle.exe --format rgb565 --little-endian
.\img2bin_imprle.exe --input .\ui --output .\out --formats rgb565,argb4444 --manifest
```

## 退出码

`0` 成功；`1` 参数错误；`2` 输入错误；`3` 编码错误；`4` 写入错误；`5` 内部错误；`6` 批处理部分失败。错误详情以单行 JSON 输出到 stderr（逐字段见[接口 Schema 说明](README-schema.md)）。

## 相关文档

- [工具总览与共同行为](README-tools.md)
- [像素格式说明](README-formats.md)
- [解码编写说明](README-decoder.md)（含 C99 参考解码器：`img2bin_decode_imprle`）
- [协议与验证说明](README-protocol.md)
