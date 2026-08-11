# img2bin_raw 使用说明

无压缩取模工具：把 PNG/BMP/JPG/JPEG 图片转换为按目标像素格式逐像素打包的 `.bin`，是六个工具中唯一支持 Alpha 蒙版格式（`a8/a4/a2/a1`）的一个，也是所有压缩算法体积率的对照基准（体积率恒为 100%）。

## 快速开始

- **双击运行**：第一次双击会在 exe 旁自动创建 `input`、`output` 文件夹；把图片放进 `input` 再双击一次，结果出现在 `output`（默认 `rgb565`、大端）
- **拖拽**：把图片或文件夹直接拖到 `img2bin_raw.exe` 上
- **命令行**：

```powershell
.\img2bin_raw.exe --input .\demo.png --output .\out --format argb8888
```

## 输出是什么

```text
<原图名>_<像素格式>_raw_<be|le>_<宽>x<高>.bin
```

文件 = **6 字节通用资源头**（类型 `0x00` + 格式码 + 恒大端宽高，raw 的算法 nibble 为 `0x0`）+ RAW payload。文件名是机器接口：下游工具（bin2c 类）靠解析它取得格式/算法/字节序/宽高。

每写出一个文件，控制台同时报告体积率（raw 恒为 100%）：

```text
Wrote out\demo_rgb565_raw_be_128x64.bin (16390 bytes, payload 16384 / raw 16384 = 100.0%)
```

## payload 布局

- 彩色格式：像素按“从左到右、从上到下”逐个打包，每像素字节数由格式决定，多字节格式受 `--little-endian` 影响（整像素字节反转）
- 定位第 N 个像素：`offset = N × 每像素字节数`——RAW 是唯一可 O(1) 随机访问任意像素的算法
- Alpha 蒙版格式（`a8/a4/a2/a1`，本工具独有）：只存透明度通道，**按行打包**——行字节数 = `(宽 × bpp + 7) / 8`，MSB-first，行尾补 0，无字节序维度。打包细节见[像素格式说明](README-formats.md)

## 参数

| 参数 | 说明 |
| --- | --- |
| `--input <file-or-dir>` | 输入文件或目录（默认 `<exe_dir>\input`） |
| `--output <dir>` | 输出目录（默认 `<exe_dir>\output`） |
| `--format <name>` | 单一像素格式（默认 `rgb565`） |
| `--formats <all\|f1,f2,...>` | 一次输出多种格式 |
| `--little-endian` | 小端输出（默认大端） |
| `--bg-color <RRGGBB>` | 非 Alpha 格式的透明区域先与该背景色混合（默认 `000000`） |
| `--manifest` | 在输出目录写 `img2bin_raw-manifest.json` 运行清单（默认关闭） |
| `--info` / `--list-formats` / `--help` | 元数据 JSON / 格式清单 / 帮助 |

## 支持的像素格式

- 彩色（9 种）：`argb8888` `argb6666` `argb4444` `argb2222` `argb8565` `rgb888` `rgb565` `rgb332` `ragb5155`
- Alpha 蒙版（4 种，仅本工具）：`a8` `a4` `a2` `a1`——供 GUI 运行时用前景色染色（图标换色、主题切换），编码取输入图的 Alpha 通道，忽略 `--bg-color`

## 使用示例

```powershell
.\img2bin_raw.exe --format rgb565
.\img2bin_raw.exe --formats all
.\img2bin_raw.exe --input .\icons --output .\out --format a4 --manifest
.\img2bin_raw.exe --format rgb565 --bg-color FFFFFF --little-endian
```

## 退出码

`0` 成功；`1` 参数错误；`2` 输入错误；`3` 编码错误；`4` 写入错误；`5` 内部错误；`6` 批处理部分失败。错误详情以单行 JSON 输出到 stderr（逐字段见[接口 Schema 说明](README-schema.md)）。

## 相关文档

- [工具总览与共同行为](README-tools.md)
- [像素格式说明](README-formats.md)（打包/量化规则）
- [解码编写说明](README-decoder.md)（含 C99 参考解码器：`img2bin_decode_raw` / `img2bin_decode_raw_alpha`）
- [协议与验证说明](README-protocol.md)（6 字节通用资源头）
