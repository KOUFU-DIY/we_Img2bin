# img2bin_indexqoi 使用说明

索引 QOI V2（静态调色盘）压缩取模工具：在 `QOIF` 式数据流之上加两件事——**跳转索引**（可从任意索引点“空降”解码局部区域，不必从头解到尾）和**静态调色盘**（编码时统计出最值钱的 0~64 个颜色，压成单字节 op）。解码零 RAM 字典、零哈希，调色盘可原地读 flash，适合下位机按行/按区域刷屏的场景。

## 快速开始

- **双击运行**：第一次双击自动创建 `input`、`output` 文件夹；把图片放进 `input` 再双击一次（默认 `rgb565`、大端、索引间隔 = 图片宽度）
- **拖拽**：把图片或文件夹拖到 `img2bin_indexqoi.exe` 上
- **命令行**：

```powershell
.\img2bin_indexqoi.exe --input .\demo.png --output .\out --format argb8888 --index-interval 128
```

## 输出是什么

```text
<原图名>_<像素格式>_indexqoi_<be|le>_<宽>x<高>.bin
```

文件 = **6 字节通用资源头**（算法 nibble `0x4`）+ indexQOI V2 payload。控制台同时报告体积率：

```text
Wrote out\gui_rgb565_indexqoi_be_128x64.bin (1290 bytes, payload 1284 / raw 16384 = 7.8%)
```

设计实测参考（`RGB565`，相对 RAW 的体积率，V1 → V2）：照片类 22.5% → 16.4%，GUI 界面 12.3% → 5.9%，图标 6.7% → 4.3%。

## payload 布局

```text
[14字节索引头][u16索引区][u24索引区][u32索引区][静态调色盘][QOI数据流][0xA0 0x88]
```

- **索引头**（恒大端）：`[0]` 头长度 `0x0E`（兼作版本标识，旧 V1 为 `0x0D`）、宽、高、索引间隔、三个索引区字节长度、`[13]` 调色盘条目数（0~64，0 = 无调色盘）
- **索引区**：每 `间隔` 个像素记一个索引点，索引值 = 该段首 op 相对**QOI 数据流起点**的字节偏移；偏移 ≤0xFFFF 存 u16、≤0xFFFFFF 存 u24、更大存 u32，三区依次排列，均大端
- **调色盘**：每项一个完整原始格式像素（含 Alpha，字节序同 `0xFF` 全量像素），全局有效、解码中永不修改
- **数据流 op**：`op < 条目数` 静态查盘（含透明度变化的颜色也能命中）；`DIFF/LUMA/RUN` 与 `QOIF` 相同；`0xFE` 剥透明度全量**只用于 `argb8888/argb8565`**；`0xFF` 原始全量
- **段首（索引点）**只允许 调色盘 op 或 `0xFF`，RUN 不跨段——因此从任何索引点开始解码都是自包含的
- 解码按像素数“到数即停”，流尾 `0xA0 0x88` 仅作完整性校验

编码为两遍法：第一遍统计每个颜色进盘可省的字节数（净收益 > 每像素字节数才有资格），按收益降序（同收益按 32 位有符号颜色键降序）取前 64 项；第二遍正式编码。选盘结果确定，可跨实现对拍。完整规则见[解码编写说明](README-decoder.md)第九章。

## 索引间隔怎么选

- 默认 = 图片宽度（每行一个索引点），配合“按行刷新”场景最自然
- 间隔越小：跳转粒度越细，但索引区越大、段首打断压缩越频繁，体积率变差
- 间隔越大：体积率更好，但跳转后需要顺序解码的像素更多
- 范围 `1..65535`；全图只要顺序解码一次的场景可以给一个很大的间隔

## 参数

| 参数 | 说明 |
| --- | --- |
| `--index-interval <count>` | 像素索引间隔（本工具特有；默认 = 图片宽度） |
| `--input <file-or-dir>` | 输入文件或目录（默认 `<exe_dir>\input`） |
| `--output <dir>` | 输出目录（默认 `<exe_dir>\output`） |
| `--format <name>` | 单一像素格式（默认 `rgb565`） |
| `--formats <all\|f1,f2,...>` | 一次输出多种格式（`all` 自动跳过 Alpha 蒙版） |
| `--little-endian` | 小端输出（默认大端） |
| `--bg-color <RRGGBB>` | 非 Alpha 格式的透明区域先与该背景色混合（默认 `000000`） |
| `--manifest` | 在输出目录写 `img2bin_indexqoi-manifest.json` 运行清单（默认关闭） |
| `--info` / `--list-formats` / `--help` | 元数据 JSON / 格式清单 / 帮助 |

## 支持的像素格式

9 种彩色格式：`argb8888` `argb6666` `argb4444` `argb2222` `argb8565` `rgb888` `rgb565` `rgb332` `ragb5155`。
Alpha 蒙版（`a8/a4/a2/a1`）仅 `img2bin_raw` 支持——本工具显式点名会报错（退出码 1）。
宽、高、索引间隔都不能超过 65535。

## 使用示例

```powershell
.\img2bin_indexqoi.exe --format argb8888
.\img2bin_indexqoi.exe --format argb8888 --index-interval 512
.\img2bin_indexqoi.exe --formats rgb565,argb8888 --index-interval 128 --manifest
```

## 退出码

`0` 成功；`1` 参数错误；`2` 输入错误；`3` 编码错误；`4` 写入错误；`5` 内部错误；`6` 批处理部分失败。错误详情以单行 JSON 输出到 stderr（逐字段见[接口 Schema 说明](README-schema.md)）。

## 相关文档

- [工具总览与共同行为](README-tools.md)
- [像素格式说明](README-formats.md)
- [解码编写说明](README-decoder.md)第九章（含 C99 参考解码器：`img2bin_decode_indexqoi` / `img2bin_decode_indexqoi_from_slot` 空降解码）
- [协议与验证说明](README-protocol.md)（indexQOI V2 协议结论、V1/V2 版本区分）
