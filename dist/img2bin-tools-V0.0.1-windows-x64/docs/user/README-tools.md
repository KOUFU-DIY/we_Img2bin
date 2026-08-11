# 工具说明

本页说明六个 `exe` 的用途、常用参数和典型命令。

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

## 1. img2bin_raw.exe

用途：无压缩输出。

输出命名：

```text
<原图名>_<像素格式>_raw_<be|le>_<宽>x<高>.bin
```

示例：

```powershell
.\img2bin_raw.exe --format rgb565
.\img2bin_raw.exe --formats all
.\img2bin_raw.exe --input .\demo.png --output .\out --format argb8888
.\img2bin_raw.exe --input .\icon.png --output .\out --format a4
```

特有能力：Alpha 蒙版格式 `a8 / a4 / a2 / a1`（只保存透明度通道，供 GUI 运行时染色）**只有本工具支持**。其余五个工具显式点名这些格式会报错退出（码 1），`--formats all` 会自动跳过。蒙版的行打包规则见[像素格式说明](README-formats.md)。

## 2. img2bin_imprle.exe

用途：改进版 RLE 压缩输出。

输出命名：

```text
<原图名>_<像素格式>_imprle_<be|le>_<宽>x<高>.bin
```

示例：

```powershell
.\img2bin_imprle.exe --format argb8888
.\img2bin_imprle.exe --format rgb565 --little-endian
```

## 3. img2bin_rle.exe

用途：原始 RLE 压缩输出。

输出命名：

```text
<原图名>_<像素格式>_rle_<be|le>_<宽>x<高>.bin
```

示例：

```powershell
.\img2bin_rle.exe --format argb8888
.\img2bin_rle.exe --formats rgb565,rgb888
```

## 4. img2bin_qoi.exe

用途：原始 QOI 压缩输出。

特点：保留原始 QOI 的字典逻辑。

输出命名：

```text
<原图名>_<像素格式>_qoi_<be|le>_<宽>x<高>.bin
```

示例：

```powershell
.\img2bin_qoi.exe --format argb8888
.\img2bin_qoi.exe --format rgb565 --bg-color FF0000
```

## 5. img2bin_qoif.exe

用途：原始 QOI（无字典）压缩输出。

特点：去掉 QOI 的字典逻辑，便于减轻部分解码场景的实现复杂度。

输出命名：

```text
<原图名>_<像素格式>_qoif_<be|le>_<宽>x<高>.bin
```

示例：

```powershell
.\img2bin_qoif.exe --format argb8888
.\img2bin_qoif.exe --formats all
```

## 6. img2bin_indexqoi.exe

用途：索引 QOI V2（静态调色盘）压缩输出。

特点：

- 数据流 op 与 `QOIF` 同族（`DIFF/LUMA/RUN/0xFE/0xFF`），另加静态调色盘单字节 op
- 编码时两遍统计自动选盘（0~64 项），高频“难压缩颜色”压成 1 字节；
  解码零 RAM 字典、零哈希，调色盘可原地读 flash
- 在指定索引点强制自包含（调色盘 op 或原始像素块），支持从任意索引点空降解码
- 在文件开头附加 14 字节索引头、索引表与调色盘，数据流以 `0xA0 0x88` 结尾

输出命名：

```text
<原图名>_<像素格式>_indexqoi_<be|le>_<宽>x<高>.bin
```

额外参数：

- `--index-interval <count>`

说明：

- 不传 `--index-interval` 时，默认使用图片宽度作为索引间隔
- 例如图片宽度为 `36`，则默认每 `36` 个像素建立一个索引

示例：

```powershell
.\img2bin_indexqoi.exe --format argb8888
.\img2bin_indexqoi.exe --format argb8888 --index-interval 512
.\img2bin_indexqoi.exe --formats rgb565,argb8888 --index-interval 128
```

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
