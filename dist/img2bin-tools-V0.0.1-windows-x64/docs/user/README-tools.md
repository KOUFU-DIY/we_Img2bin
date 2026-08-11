# 工具说明

本页是六个 `exe` 的总览与共同行为说明。每个工具另有单独的完整文档：

| 工具 | 用途 | 单独文档 |
| --- | --- | --- |
| `img2bin_raw.exe` | 无压缩（唯一支持 Alpha 蒙版 `a8/a4/a2/a1`） | [README-img2bin_raw.md](README-img2bin_raw.md) |
| `img2bin_imprle.exe` | 改进 RLE（原样段 + 重复段） | [README-img2bin_imprle.md](README-img2bin_imprle.md) |
| `img2bin_rle.exe` | 原始 RLE（纯重复段） | [README-img2bin_rle.md](README-img2bin_rle.md) |
| `img2bin_qoi.exe` | 原始 QOI（64 项字典，压缩率通常最好） | [README-img2bin_qoi.md](README-img2bin_qoi.md) |
| `img2bin_qoif.exe` | 原始 QOI 无字典（解码最简单） | [README-img2bin_qoif.md](README-img2bin_qoif.md) |
| `img2bin_indexqoi.exe` | 索引 QOI V2（静态调色盘 + 跳转索引） | [README-img2bin_indexqoi.md](README-img2bin_indexqoi.md) |

如果你要根据本工具生成的 `.bin` 自己编写解码器，请优先阅读：

- [像素格式说明](README-formats.md)
- [解码编写说明](README-decoder.md)

## 共同特性

六个工具都支持：

- 双击运行
- 拖拽图片或目录到 `exe`
- `--input <file-or-dir>`
- `--output <dir>`
- `--format <name>`
- `--formats <all|fmt1,fmt2,...>`
- `--little-endian`
- `--bg-color <RRGGBB>`
- `--manifest`（在输出目录写 `img2bin_<工具>-manifest.json` 运行清单；**默认关闭**）
- `--help`
- `--info`
- `--list-formats`

共同默认值：

- 默认格式：`rgb565`
- 默认字节序：大端
- 默认输入目录：`<exe_dir>\input`
- 默认输出目录：`<exe_dir>\output`
- 默认背景色：`000000`
- 默认不写 manifest JSON 日志（需 `--manifest` 显式开启）

每写出一个 `.bin`，stdout 会同时报告体积率（压缩后 payload 相对同格式
RAW payload 的占比；两边都不含恒定 6 字节的通用资源头）：

```text
Wrote out\logo_rgb565_indexqoi_be_128x64.bin (1290 bytes, payload 1284 / raw 16384 = 7.8%)
```

## 输出命名（全工具一致）

```text
<原图名>_<像素格式>_<算法>_<be|le>_<宽>x<高>.bin
```

算法段依次为 `raw` / `imprle` / `rle` / `qoi` / `qoif` / `indexqoi`。这个命名是机器接口——下游资源管线（bin2c 类工具、自动化脚本）靠解析它取得格式/算法/字节序/宽高。所有 `.bin` 都以 6 字节通用资源头开始（见[协议与验证说明](README-protocol.md)）。

## 怎么选算法

- **要 O(1) 随机访问、或要 Alpha 蒙版染色** → `raw`
- **大块纯色为主** → `rle`（解码最简单）或 `imprle`（杂色不膨胀）
- **追求压缩率、解码端能给 64 项字典 RAM** → `qoi`
- **要最简单的 QOI 系解码器** → `qoif`
- **要局部跳转解码（按行/按区域刷屏）+ 高压缩率** → `indexqoi`（V2 静态调色盘）

工具专属参数、payload 布局与完整示例见各工具的单独文档（页首表格）。

## 背景色参数什么时候有用

以下格式不带 Alpha，因此透明区域会先和背景色混合：

- `RGB888`
- `RGB565`
- `RGB332`

示例：

```powershell
.\img2bin_raw.exe --format rgb565 --bg-color FFFFFF
```

## 大端和小端

默认是大端，也就是文件名里的 `be`。

如果使用 `--little-endian`，文件名里会变成 `le`。

示例：

```powershell
.\img2bin_raw.exe --format rgb565 --little-endian
```

输出文件会变成类似：

```text
demo_rgb565_raw_le_128x64.bin
```

## 查询工具元数据

每个工具都支持：

```powershell
.\img2bin_raw.exe --info
```

可返回：

- 工具名
- 版本号
- 算法类型
- 默认格式
- 支持的输入格式
- 支持的像素格式
- 输出命名模板
- 参数元数据

这部分主要给后续 GUI 或自动化程序集成使用。
