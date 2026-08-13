# 协议与验证说明

本页只说明“当前工具自己定义并已经实现”的协议，以及如何用本工具自己生成验证资料。  
阅读和实现解码器时，不需要依赖任何外部历史资料、旧样例或同行文档。

## 协议优先级

如果出现理解冲突，按以下优先级判断：

1. 当前工具实际输出的 `.bin`
2. 当前工具 `--info` 返回的元数据
3. 当前用户文档

对当前项目来说，这三项已经足够覆盖使用、集成和解码实现。

## 当前工具范围

当前已实现并可直接使用的工具有：

- `img2bin_raw.exe`
- `img2bin_imprle.exe`
- `img2bin_rle.exe`
- `img2bin_qoi.exe`
- `img2bin_qoif.exe`
- `img2bin_indexqoi.exe`
- `img2bin_indexqoimask.exe`

前六个工具共同支持的彩色像素格式：

- `ARGB8888`
- `ARGB6666`
- `ARGB4444`
- `ARGB2222`
- `ARGB8565`
- `RGB888`
- `RGB565`
- `RGB332`
- `RAGB5155`

Alpha 蒙版格式（只存透明度，运行时由 GUI 染色）：

- `A8`、`A4`、`A2`、`A1`：`img2bin_raw.exe` 全部支持（无压缩行打包）
- `A8` 另可由 `img2bin_indexqoimask.exe` 压缩输出——该工具**只支持 `a8`**（默认格式即 `a8`，彩色格式与 `a4/a2/a1` 都会被拒绝）

工具收到自身不支持的格式时的行为（全工具一致）：显式 `--format/--formats` 点名时报 CLI 错误（退出码 1），`--formats all` 时静默滤除；`--info` 与 `--list-formats` 只列出本工具真正支持的格式。

当前工具输出：

- `.bin` 文件 = **6 字节通用资源头 + 算法 payload**
- 每个 `.bin` 写出时 stdout 报告体积率（压缩后 payload / 同格式 RAW payload）
- 显式传 `--manifest` 时输出运行清单 `manifest.json`（默认关闭）

当前工具不输出：

- `.c/.h`
- 数组文本
- 结构体文本

补充：需要 `.c/.h` 数组时可用任意 bin2c 类工具把 `.bin` 转成数组，保持数组内容与 `.bin` 逐字节一致（含通用头）即可。

## 通用资源头

所有 `.bin` 输出（七种算法全部）都以 6 字节通用资源头开始：

| 偏移 | 字节 | 含义 | 取值 |
| --- | --- | --- | --- |
| 0 | 1 | 资源类型 | 恒 `0x00`（图片；`0x01` 预留给字库） |
| 1 | 1 | 格式码 | 高 nibble = 压缩算法，低 nibble = 像素格式 |
| 2~3 | 2 | 宽 | 恒大端 |
| 4~5 | 2 | 高 | 恒大端 |

算法 nibble：`raw=0x0`、`rle=0x1`、`imprle=0x2`、`qoi=0x3`、`indexqoi=0x4`、`qoif=0x5`、`indexqoimask=0x6`。

像素格式 nibble：`RGB565=0x0`、`RGB888=0x1`、`RGB332=0x4`、`ARGB8888=0x5`、`ARGB6666=0x6`、`ARGB4444=0x7`、`ARGB8565=0x8`、`ARGB2222=0x9`、`RAGB5155=0xA`、`A8=0xB`、`A4=0xC`、`A2=0xD`、`A1=0xE`（`0x2/0x3` 属于旧枚举的 RGB555/RGB444，本工具不使用；`0xF` 保留给 OLED 点阵）。

要点：

- 头内**不含字节序**，字节序仍由文件名的 `be|le` 段或工程约定提供
- 通用头之后紧跟算法 payload；`indexQOI` 自己的 14 字节索引头位于通用头**之后**
- 宽高各占 16 位，上限 65535
- `indexqoi` 的算法 nibble 维持 `0x4` 不变：indexQOI V2（静态调色盘）与旧 V1
  靠**索引头首字节**区分（V2 恒 `0x0E`，V1 为 `0x0D`），不占用新的算法 nibble
- `indexqoimask` 只与 `A8` 组合：格式码恒为 `0x6B`，头里出现 `0x6` 搭配其他
  像素格式 nibble 的组合是非法流

## Alpha 蒙版格式（A8/A4/A2/A1）payload 契约

Alpha 蒙版只保存透明度通道，不保存颜色，供 GUI 在运行时用前景色染色（图标换色、主题切换、字形式渲染）。协议规则：

- **合法算法组合只有两种**：四种蒙版格式 × raw（算法 nibble `0x0`，无压缩行打包），
  以及 `A8` × indexQOI_MASK（算法 nibble `0x6`，压缩格式见下节）；其余组合都是非法流
- **来源**：取输入图片的 Alpha 通道；无 Alpha 的输入（如 JPG）视为全不透明。raw 编码量化为 `(a × ((1<<bpp)-1) + 127) / 255`，忽略 `--bg-color`
- raw **行打包**：每行从新字节开始，行字节数 = `(宽 × bpp + 7) / 8`，行尾补位填 `0`
- **MSB-first**：最左像素在字节高位（A4 高 nibble、A2 bits[7:6]、A1 bit7）
- **无字节序维度**：`be`/`le` 输出逐字节一致（与 `RGB332`/`ARGB2222` 同类）
- **解码扩展**到 8bit alpha：`A8` 恒等；`A4` `v→(v<<4)|v`；`A2` `v→v*0x55`；`A1` `v→0/255`
- `A1` 与 OLED 点阵（`0xF`）不是一回事：`A1` 是行主序蒙版、参与混合，OLED 是页式屏区布局

## 怎样从工具里拿到协议信息

`--info` JSON 与各类 manifest 的逐字段说明见 [接口 Schema 说明](README-schema.md)。

## 1. 用 `--info` 获取机器可读元数据

每个工具都支持：

```powershell
.\img2bin_raw.exe --info
.\img2bin_indexqoi.exe --info
```

它会返回：

- 工具名
- 版本号
- 算法类型
- 默认格式
- 默认字节序
- 支持的输入格式
- 支持的像素格式
- 输出命名模板
- 参数元数据
- 退出码约定

如果你在做 GUI、自动化程序集成或下位机配套工具，这个 JSON 应该作为机器接口入口。

## 2. 用用户文档获取人工可读规则

建议按这个顺序看：

1. [用户总览](README.md)
2. [工具说明](README-tools.md)
3. [像素格式说明](README-formats.md)
4. [解码编写说明](README-decoder.md)

## 怎样自己生成验证资料

如果你要写解码器，最稳的做法不是去找历史样例，而是直接用当前工具生成你自己的验证集。

推荐做法：

1. 准备一张固定输入图片
2. 记录使用的工具、命令行、像素格式、字节序
3. 保留生成的 `.bin`
4. 如为批处理，加 `--manifest` 生成并保留对应 `manifest.json`（默认不写出）
5. 如需程序侧检索，再保存一份 `--info` 输出

例如：

```powershell
.\img2bin_raw.exe --input .\demo.png --output .\out --format rgb565
.\img2bin_qoif.exe --input .\demo.png --output .\out --format argb8888
.\img2bin_indexqoi.exe --input .\demo.png --output .\out --format argb8888 --index-interval 128
.\img2bin_indexqoi.exe --info
```

这样你就能得到一组完全由当前工具生成、与当前版本协议一致的验证资料：

- 输入图片
- 输出 `.bin`
- 运行参数
- `manifest.json`
- `--info` JSON

## 建议保留哪些验证材料

对于每个需要对接的算法，建议至少保留：

- 一张原始输入图
- 一个目标 `.bin`
- 一份命令记录
- 一份 `--info` 输出

如果你要做批处理流程验证，再额外保留：

- `img2bin_<algo>-manifest.json`（运行时需带 `--manifest`）

如果你要做跳转解码验证，再额外保留：

- `indexQOI` 的不同 `--index-interval` 输出

## indexQOI（V2）的当前协议结论

当前 `indexQOI` 规则以工具实现为准：

- 文件布局：6 字节通用资源头 + 14 字节索引头 + u16/u24/u32 索引区
  + 静态调色盘 + QOI 数据流
- 索引头首字节 `0x0E` 兼作版本标识（旧 V1 为 `0x0D`，参考解码器按损坏流拒绝）
- 静态调色盘 0~64 项，每项一个完整原始格式像素（含 Alpha，字节序同全量像素）；
  编码器两遍法统计选盘：净收益 > 每像素字节数才进盘，收益降序、同收益按
  32 位有符号颜色键 `(r<<24)|(g<<16)|(b<<8)|a` 降序取前 64，结果确定可对拍
- 数据流 op：`op < 条目数` 静态查盘；`DIFF/LUMA/RUN` 与 `QOIF` 相同；
  `0xFE` 剥透明度全量**只用于 `ARGB8888/ARGB8565`**；`0xFF` 原始全量
- 段首（索引点）只允许 调色盘 op 或 `0xFF`，RUN 不跨段，空降解码自包含
- 默认索引间隔为图片宽度，可用 `--index-interval` 自定义
- 索引值相对 QOI 数据流起点（调色盘之后），不是文件起点
- 解码按像素数到数即停，无尾部结束码

## indexQOI_MASK（v1.0）的当前协议结论

`indexQOI_MASK` 是 `A8` 蒙版专用的压缩格式（算法 nibble `0x6`，格式码恒 `0x6B`），
面向按行随机访问与 MCU 流式解码。规则以工具实现为准：

- 文件布局：6 字节通用资源头 + 标志位(1B) + u16/u32 行索引项数量(各 2B)
  + u16 行索引表 + u32 行索引表 + 字典数量(1B, 0..64) + 字典 + 像素流；
  payload 内多字节字段恒大端，无字节序维度
- 标志位 `b1:b0` = 量化位数 q（`00`=8bit `01`=7bit `10`=6bit `11`=5bit），
  `b7..b2` 保留恒 0；q<8 时 `v = a >> (8−q)`，解码按高位复制扩展
  `out=(v<<s)|(v>>(q−s))`（s=8−q）；q=8 严格无损
- 行索引 = 该行数据相对像素流起点的字节偏移；内容相同的行只存一份（行去重），
  多行索引可指向同一偏移；m 取"偏移 ≤65535 的最长行前缀"行数，第 m 行起
  一律进 u32 表（按行序分表，不按偏移值分表）
- 行结构：行首 1 字节 = 首像素 q 位域原始值（无 tag），其后 tag 流按高 2 位
  判别：`00` INDEX（6bit 字典下标）、`01` DIFF（两个 3bit 差分 [-4,+3]，2 像素）、
  `10` DELTA（6bit 差分 [-32,+31]）、`11` RUN（计数 0..62，复制 prev 1..63 次）；
  整字节 `0xFF` 为 ALPHA 转义（后接 1 字节 q 位域原始值）
- 行完全独立（prev/RUN/DIFF 均不跨行）；行尾只剩 1 像素时不得使用 DIFF；
  DIFF/DELTA 在 q 位域内加减不回绕，越界即损坏流；输出满宽度即行结束，无结束符
- 字典两遍法：统计将跌入 ALPHA 的像素值频次，频次 ≥2 才进典（净收益 > 1 字节
  存储成本），频次降序、同频次按值降序取前 64，结果确定可对拍
- 完整规格（含编码器贪心优先级与解码参考流程）见
  [README-img2bin_indexqoimask.md](README-img2bin_indexqoimask.md)

## 用户如何利用这份说明

如果你只是要正常使用工具：

- 先看 [用户总览](README.md)
- 再看 [工具说明](README-tools.md)

如果你要自己写解码器：

- 先看 [像素格式说明](README-formats.md)
- 再看 [解码编写说明](README-decoder.md)
- 然后用当前工具自己生成测试样例

如果你要做 GUI、自动化程序集成或批处理接入：

- 先读 `--info`
- 再结合 [工具说明](README-tools.md) 和 `manifest.json`
