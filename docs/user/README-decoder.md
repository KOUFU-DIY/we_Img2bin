# 解码编写说明

本页用于说明如何根据当前工具生成的 `.bin` 编写解码器。  
内容以当前实现为准。

## 现成的参考实现

不想从零写的话，项目自带一份**参考解码器**，纯 C99、无任何依赖、不用动态内存，
可以直接拷进下位机工程：

- 发布包中：`decoder\img2bin_decode.c` / `decoder\img2bin_decode.h`
- 仓库中：`builder/src/decoder/`

覆盖全部六种算法 × 九种彩色像素格式 × 大小端，外加四种 Alpha 蒙版格式
（`A8/A4/A2/A1`，仅 raw 算法），所有解码函数把压缩流还原为
RAW 打包像素字节流（与 `img2bin_raw.exe` 输出逐字节一致），并附带
`indexQOI` 的头解析、索引读取和"从第 N 个索引点开始解码"接口。
它在测试里对每种组合做"编码 → 解码 → 与 RAW 输出逐字节比对"的回环验证，
与当前工具的输出保证一致。本页其余内容既是协议说明，也是这份实现的注释。

## 先看结论

如果你要写解码器，建议按这个顺序理解：

1. 先看 [像素格式说明](README-formats.md)
2. 再看本页的压缩流规则
3. 如果要做局部跳转解码，再看 `indexQOI`
4. 对照参考实现 `img2bin_decode.c` 验证理解

## 一、所有算法共同规则

## 0. 文件都以 6 字节通用资源头开始

所有 `.bin` 文件的前 6 字节是统一的资源头：

```text
byte0   = 资源类型，恒 0x00（图片）
byte1   = 格式码：高 nibble 压缩算法，低 nibble 像素格式
byte2-3 = 宽，恒大端
byte4-5 = 高，恒大端
```

算法 nibble：`raw=0x0 rle=0x1 imprle=0x2 qoi=0x3 indexqoi=0x4 qoif=0x5`；
像素格式 nibble：`RGB565=0x0 RGB888=0x1 RGB332=0x4 ARGB8888=0x5 ARGB6666=0x6
ARGB4444=0x7 ARGB8565=0x8 ARGB2222=0x9 RAGB5155=0xA A8=0xB A4=0xC A2=0xD A1=0xE`。

解码器先读这 6 字节确定"是什么、多大"，然后从第 7 字节开始按对应算法解
payload。本页后续章节描述的都是**去掉通用头之后的 payload**。
参考解码器提供 `img2bin_decode_image()` 一步完成"解头 + 分发解码"。

## 1. 像素遍历顺序

所有工具都按以下顺序编码：

```text
从左到右
从上到下
```

也就是标准行优先顺序。

## 2. payload 不含结束标记

除了 `indexQOI` 以外，其余算法的 payload 不包含宽高、格式码或结束标记：

- `RAW`
- `RLE`
- `IMPRLE`
- `QOI`
- `QOIF`

宽高与像素格式可以从通用资源头取得；**字节序不在头里**，解码器仍需从外部拿到：

- 字节序（文件名的 `be|le` 段，或工程约定）

## 3. 字节序

当前工具支持：

- 大端：`be`
- 小端：`le`

解码时不要只看算法名，还要结合文件名或外部信息判断是否为 `little-endian`。

## 二、RAW 解码

`RAW` 最简单。

## 数据结构

```text
像素0原始字节 + 像素1原始字节 + 像素2原始字节 + ...
```

## 解码步骤

1. 根据像素格式确定每像素字节数
2. 逐像素读取固定长度字节组
3. 按 [像素格式说明](README-formats.md) 解包

## Alpha 蒙版（A8/A4/A2/A1）的 RAW 解码

Alpha 蒙版只出现在 raw 算法下（头里出现其他算法 nibble 的组合是非法流），
payload 是**按行打包**的位流而不是逐像素字节组：

```text
行字节数 row_stride = (宽 × bpp + 7) / 8
payload 大小       = 高 × row_stride
```

- 每行从新字节开始，MSB-first（最左像素在字节高位），行尾补位为 0
- 无字节序维度，`be`/`le` 输出一致
- 还原到 8bit alpha：`A8` 恒等；`A4` `(v<<4)|v`；`A2` `v*0x55`；`A1` `0/255`

参考解码器的对应接口：

- `img2bin_decode_row_stride()` / `img2bin_decode_bits_per_pixel()` 算行宽与位深
  （注意 `img2bin_decode_bytes_per_pixel()` 对亚字节格式返回 0）
- `img2bin_decode_raw_alpha()`：按宽高做尺寸校验的 payload 级解码
- `img2bin_decode_image()`：文件级接口对 Alpha 蒙版自动走上面这条路径
- 按像素数的 payload 级接口（`img2bin_decode_raw/rle/imprle/qoi/qoif/indexqoi*`）
  对 Alpha 蒙版一律返回参数错误——像素数不足以确定行打包的 payload 大小

## 三、原始 RLE 解码

## 数据结构

```text
{数量, 数据组, 数量, 数据组, ..., 0x00}
```

其中：

- `数量` 为 1 字节
- `数据组` 的长度由像素格式决定
- 末尾 `0x00` 表示结束

## 规则

- `数量` 取值范围：`1..255`
- `0x00` 只能表示结束，不能表示一段数据
- 每一段表示“把紧随其后的像素组重复 N 次”

## 伪代码

```c
while (1) {
    uint8_t count = read_u8();
    if (count == 0x00) {
        break;
    }

    read pixel_group[group_size];
    repeat count times:
        emit pixel_group;
}
```

## 四、改进 RLE 解码

## 数据结构

```text
{控制字节, 数据, 控制字节, 数据, ..., 0x00}
```

控制字节规则：

- `bit7 = 0`：后接若干组不连续数据，直接取用
- `bit7 = 1`：后接 1 组数据，重复若干次
- `bit6..0`：长度

## 规则

- 原样段长度范围：`1..127`
- 重复段长度范围：`1..127`
- `0x00` 表示结束

## 伪代码

```c
while (1) {
    uint8_t tag = read_u8();
    if (tag == 0x00) {
        break;
    }

    uint8_t count = tag & 0x7F;
    if ((tag & 0x80) == 0) {
        repeat count times:
            read pixel_group[group_size];
            emit pixel_group;
    } else {
        read pixel_group[group_size];
        repeat count times:
            emit pixel_group;
    }
}
```

## 五、QOI 家族总览

当前有三种 QOI 系列：

- `QOI`：原始 QOI，带 64 项字典索引
- `QOIF`：原始 QOI（无字典）
- `indexQOI`（V2）：索引 QOI + 静态调色盘，索引头 + 索引表 + 调色盘 + 数据流

## 和标准 QOI 的相同点

- 有 `INDEX / DIFF / LUMA / RUN / RGB / RGBA` 这套思想
- `RUN` 最大长度仍为 62

## 和标准 QOI 的不同点

- 没有标准 QOI 文件头
- 没有标准 QOI 结束码
- `OP_RGB / OP_RGBA` 后跟的原始字节长度不是固定 3/4，而是跟当前像素格式相关
- 内部比较与差分运算，作用在“量化后的目标格式通道值”上，不是原始 8 位 RGBA

## 当前实现的初始状态

解码开始前，建议按当前实现初始化：

- `prev.r = 0`
- `prev.g = 0`
- `prev.b = 0`
- `prev.a = 当前格式的最大 Alpha`

也就是：

- 非 Alpha 格式：`prev.a = 255`
- `ARGB8888` / `ARGB8565`：`prev.a = 255`
- `ARGB6666`：`prev.a = 63`
- `ARGB4444`：`prev.a = 15`
- `ARGB2222`：`prev.a = 3`
- `RAGB5155`：`prev.a = 1`

## 六、QOI opcode 规则

## 1. OP_INDEX / 调色盘 op

```text
0x00..0x3F
```

这段 op 空间在不同算法里含义不同：

- `img2bin_qoi.exe`：哈希字典 op（下述规则）
- `indexQOI`（V2）：**静态调色盘 op**，`当前像素 = 调色盘第 op 项`（见第九章），
  零哈希、零 RAM 字典
- `QOIF`：不会输出

`QOI` 的哈希字典固定 64 项，哈希公式：

```text
hash = (r * 3 + g * 5 + b * 7 + a * 11) & 63
```

说明：

- 对非 Alpha 格式，参与哈希的 `a` 固定视为 `255`
- 这里的 `r/g/b/a` 都是“量化后的目标格式通道值”

建议解码器把字典初始化为“无效项”。

## 2. OP_DIFF

```text
0x40..0x7F
```

位含义：

```text
bits5..4 = dr + 2
bits3..2 = dg + 2
bits1..0 = db + 2
```

取值范围：

- `dr`：`-2..1`
- `dg`：`-2..1`
- `db`：`-2..1`

要求：

- Alpha 必须和上一个像素相同

## 3. OP_LUMA

第一字节：

```text
0x80..0xBF
bits5..0 = dg + 32
```

第二字节：

```text
bits7..4 = (dr - dg) + 8
bits3..0 = (db - dg) + 8
```

取值范围：

- `dg`：`-32..31`
- `dr - dg`：`-8..7`
- `db - dg`：`-8..7`

要求：

- Alpha 必须和上一个像素相同

## 4. OP_RUN

```text
0xC0..0xFD
count = (opcode & 0x3F) + 1
```

范围：

- `1..62`

表示把上一个像素重复输出 `count` 次。

## 5. OP_RGB

```text
0xFE
```

注意：在本项目里它不是固定后接 3 字节，而是“后接该格式的颜色原始字节”。

### 后接字节数

| 格式 | `OP_RGB` 后接字节 |
| --- | --- |
| `ARGB8888` | 3 字节颜色，Alpha 保持上一个像素 |
| `ARGB8565` | 2 字节 `RGB565`，Alpha 保持上一个像素 |
| `RGB888` | 3 字节 |
| `RGB565` | 2 字节 |
| `RGB332` | 1 字节 |

注意算法差异：**`indexQOI`（V2）中 `0xFE` 只用于 `ARGB8888` / `ARGB8565`**
（剥透明度全量，能省 1 字节的两种格式）；非 Alpha 格式在 `indexQOI` 里
全量一律用 `0xFF`。上表中 `RGB888/RGB565/RGB332` 的 `0xFE` 行只适用于
`QOI` / `QOIF`。

## 6. OP_RGBA

```text
0xFF
```

注意：在本项目里它也不是固定 4 字节，而是“后接该格式的完整原始像素字节”。

### 后接字节数

| 格式 | `OP_RGBA` 后接字节 |
| --- | --- |
| `ARGB8888` | 4 |
| `ARGB6666` | 3 |
| `ARGB4444` | 2 |
| `ARGB2222` | 1 |
| `ARGB8565` | 3 |
| `RAGB5155` | 2 |

## 七、QOI 解码注意点

## 1. current/prev 状态应保存量化通道，不是 8 位 RGBA

例如对于 `RGB565`，建议内部维护：

```text
R5, G6, B5, A=255
```

对于 `ARGB4444`，建议维护：

```text
A4, R4, G4, B4
```

只有这样，`INDEX / DIFF / LUMA` 才会与当前工具输出一致。

## 2. 遇到 OP_RGB / OP_RGBA 时，要先把原始字节解包成量化通道

不能只把字节原样复制到状态里。  
因为后面还要参与：

- `DIFF`
- `LUMA`
- `INDEX`

## 3. 输出像素时，再按当前格式重新打包

推荐流程：

1. 解出当前像素的量化通道状态
2. 如需写回目标格式字节流，则按 [像素格式说明](README-formats.md) 的规则重新组包

## 八、QOIF 解码

`QOIF` 就是“去掉字典索引后的 QOI”。

因此：

- 不会出现 `OP_INDEX`
- 不需要维护 64 项字典
- 其余规则与当前项目中的 `QOI` 相同

如果你只需要实现一种更简单的解码器，通常建议优先实现 `QOIF`。

## 九、indexQOI（V2，静态调色盘）解码

```text
indexQOI = 6字节通用资源头 + 14字节索引头 + u16索引区 + u24索引区
         + u32索引区 + 静态调色盘 + QOI数据流
```

一句话定义：indexQOI V2 = 索引 QOI + 静态字典（调色盘）。在保留“通过索引
从任意位置空降解码”的同时，用一张编码时统计生成的全局调色盘把高频的
“难压缩颜色”压成单字节 op；解码器零 RAM 字典、零哈希计算，调色盘直接
存放在图像数据内（可原地读 flash）。

本节的偏移量都相对**通用资源头之后**（即文件第 7 字节起）。

## 1. 索引头格式

固定 14 字节：

```text
byte0    = 头长度，当前固定 0x0E（兼作版本标识；旧 V1 的 0x0D 视为不支持）
byte1-2  = 宽度，16bit，大端
byte3-4  = 高度，16bit，大端
byte5-6  = 索引间隔，16bit，大端
byte7-8  = u16 索引区字节长度，16bit，大端
byte9-10 = u24 索引区字节长度，16bit，大端
byte11-12 = u32 索引区字节长度，16bit，大端
byte13   = 调色盘条目数，0..64（0 = 无调色盘，不必填满）
```

前 13 字节与旧版 indexQOI 头逐字节同构，仅在尾部追加 1 字节。

## 2. 调色盘与数据流起始位置

```text
palette_pos = 14 + u16_bytes + u24_bytes + u32_bytes
stream_pos  = palette_pos + 调色盘条目数 × 每像素字节数
```

调色盘每项就是一个**完整原始格式像素**（含 Alpha），字节序与 `0xFF`
全量像素完全相同（受取模时大小端开关影响）：`RGB565` 2 字节、`RGB888`
3 字节、`RGB332`/`ARGB2222` 1 字节、`ARGB8888` 4 字节、`ARGB6666`/
`ARGB8565` 3 字节、`ARGB4444`/`RAGB5155` 2 字节。调色盘全局有效，
解码过程中永不修改。

## 3. 调色盘 op

```text
op ∈ 0x00..0x3F 且 op < 调色盘条目数：
    当前像素 = 调色盘第 op 项（含 Alpha，可表达透明度变化）
```

- 编码器保证 op 不超出条目数；解码器遇到 `op >= 条目数` 应按损坏流处理
- 查到的条目要**解包成量化通道状态**（供后续 `DIFF/LUMA` 使用），
  再照常打包输出——参考实现直接走 `0xFF` 全量的解包路径
- 其余 op（`DIFF/LUMA/RUN/0xFE/0xFF`）与 `QOIF` 完全相同；
  `0xFE` 只用于 `ARGB8888` / `ARGB8565`

## 4. 索引值的含义

每个索引值都是：

```text
相对 QOI 数据流起点（stream_pos）的字节偏移
```

不是相对文件起点。真正的数据指针是：

```text
target = stream_start + offset
```

## 5. 索引点（段首）规则

- 默认索引间隔 = 图片宽度，也可由 `--index-interval` 指定
- 每 `间隔` 个像素为一“段”，段首即索引点
- 段首前编码器必然已强制结束 RUN（RUN 不跨段）
- 段首 op 只可能是两种：**调色盘 op**（颜色在盘中）或 **`0xFF` 原始全量**，
  两者都不依赖任何上文，因此从索引空降解码是自包含的

## 6. 如何定位第 N 个像素

设：

- `interval = 索引间隔`
- `pixel_index = 想读取的像素位置`

则：

```text
index_slot = pixel_index / interval
base_pixel = index_slot * interval
offset = 第 index_slot 个索引值
cursor = stream_start + offset
```

然后从 `cursor` 开始解码，初始像素位置就是 `base_pixel`，无状态需复位
（调色盘全局有效）。

## 7. indexQOI 和普通 QOIF 的关系

在 `QOIF` 解码器基础上仅需两处改动，无需任何 RAM 字典：

1. 初始化时从头解析出调色盘位置（只需一次）
2. 解码循环新增一个 `op < 条目数` 的查盘分支

解码按像素数“到数即停”，数据流没有尾部结束码。

## 十、当前实现中的关键兼容约定

这些点最容易在不同实现之间产生分歧，建议你直接按这里实现：

- `QOI / QOIF / indexQOI` 没有标准 QOI 头，也没有尾部结束码
- `indexQOI` 偏移值相对的是 QOI 数据流起点（调色盘之后），不是文件起点
- `0xFE`（剥透明度全量）的适用范围：
  - `QOI / QOIF`：非 Alpha 格式与 `ARGB8888/ARGB8565` 都可能使用
  - `indexQOI`（V2）：**只用于 `ARGB8888/ARGB8565`**，非 Alpha 格式全量一律 `0xFF`
- `indexQOI` 段首只允许 调色盘 op 或 `0xFF`
- `RGB888` 在当前实现里按工具定义的字节顺序处理

## 十一、推荐的实现顺序

如果你是第一次写解码器，建议这样做：

1. 先实现 `RAW`
2. 再实现 `RLE`
3. 再实现 `IMPRLE`
4. 再实现 `QOIF`
5. 再实现 `QOI`
6. 最后实现 `indexQOI`

## 十二、写解码器时最有用的资料

- [像素格式说明](README-formats.md)
- [协议与验证说明](README-protocol.md)
- 目标工具执行 `--info` 返回的 JSON
- 由当前工具自己生成的 `.bin`、`manifest.json` 和命令记录
