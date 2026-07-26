# 统筹管理器说明 (img2bin_pack)

`img2bin_pack.exe` 是六个取模工具之上的统筹管理器。它做三件事：

1. 自动发现可用的取模工具（扫描工具目录并调用每个 exe 的 `--info`）
2. 扫描工作目录下的 `input2<算法>` 文件夹，把每个文件夹交给对应工具批量转换
3. 把输出目录中的 `.bin` 汇总生成 C 源文件（`.c/.h` 数组）

六个取模工具本身仍然只输出纯 `bin`，协议不变；`.c/.h` 全部由本管理器生成。

## 目录约定

```text
<工作目录>/
├─ input2raw/        -> img2bin_raw.exe
├─ input2imprle/     -> img2bin_imprle.exe
├─ input2rle/        -> img2bin_rle.exe
├─ input2qoi/        -> img2bin_qoi.exe
├─ input2qoif/       -> img2bin_qoif.exe
├─ input2indexqoi/   -> img2bin_indexqoi.exe
└─ output/           <- 所有 .bin、.c/.h、manifest
```

- 文件夹名 = `input2` + 算法代号，管理器按发现到的工具自动匹配
- 同一个文件夹里的图片共享一组参数（像素格式、字节序等）
- 空文件夹会被跳过，不算错误
- 首次运行时如果一个 `input2*` 文件夹都没有，管理器会按发现到的工具自动创建全套文件夹

## 快速上手

发布目录中的标准用法：

1. 双击 `windows\img2bin_pack.exe`（或 `windows\examples\run_batch.cmd`）
2. 把图片放进根目录的 `input2raw`、`input2qoi` 等文件夹
3. 再次运行，结果出现在根目录 `output\`：
   - 每张图片的 `.bin`
   - 汇总的 `img_resources.c` / `img_resources.h`
   - `img2bin_pack-manifest.json` 运行清单

命令行用法：

```powershell
.\windows\img2bin_pack.exe --root . --format argb8888
.\windows\img2bin_pack.exe --root D:\我的项目 --output D:\我的项目\res --split
.\windows\img2bin_pack.exe --info
```

## 配置文件 img2bin_pack.json

查找顺序（先找到先用）：

1. `--config <文件>` 显式指定
2. `<root>\img2bin_pack.json`
3. `<exe 目录>\img2bin_pack.json`（发布目录自带一份，`root` 指向上一级）

完整示例：

```json
{
  "root": "..",
  "output": "output",
  "tools_dir": "tools",
  "defaults": {
    "format": "rgb565",
    "endianness": "big",
    "bg_color": "000000"
  },
  "codegen": {
    "enabled": true,
    "mode": "combined",
    "base_name": "img_resources"
  },
  "folders": {
    "input2qoi": { "format": "argb8888" },
    "input2indexqoi": { "format": "argb8888", "index_interval": 512 },
    "input2raw": { "formats": "rgb565,argb8888" },
    "my_icons": { "tool": "qoi", "format": "argb8888", "output": "output/icons" }
  }
}
```

字段说明：

| 字段 | 含义 | 默认 |
| --- | --- | --- |
| `root` | 工作目录（相对路径相对配置文件所在目录） | exe 目录 |
| `output` | 输出目录（相对路径相对 `root`） | `<root>/output` |
| `tools_dir` | 工具目录（相对路径相对配置文件所在目录） | `<exe目录>/tools`，其次 exe 目录 |
| `defaults.format` / `defaults.formats` | 默认像素格式，可为单个、逗号列表或 `all` | `rgb565` |
| `defaults.endianness` | `big` 或 `little` | `big` |
| `defaults.bg_color` | 背景色 `RRGGBB` | `000000` |
| `defaults.index_interval` | 索引间隔（仅对支持的工具生效） | 图片宽度 |
| `codegen.enabled` | 是否生成 `.c/.h` | `true` |
| `codegen.mode` | `combined`（合并一对）或 `split`（每个 bin 一对） | `combined` |
| `codegen.base_name` | 合并模式的文件名与头文件守卫基名 | `img_resources` |
| `folders.<名字>` | 单个文件夹的覆盖项，字段同 `defaults`，另有 `tool` 和 `output` | 无 |

关于 `folders`：

- 键是文件夹名。`input2xxx` 文件夹不写配置也会被处理，写了则按覆盖项处理
- 任意名字的文件夹（如 `my_icons`）通过 `tool` 字段指定用哪个工具（算法代号或工具 id 都可以）
- **不需要给每张图片单独写配置**：需要特殊参数的图片，放进一个单独的文件夹再给该文件夹写一条规则即可
- 每个文件夹可用 `output` 单独指定输出目录

## 生成的 .c/.h

合并模式（默认）在每个输出目录生成一对文件，内容示例：

```c
/* img_resources.h */
#define DEMO1_RGB565_RAW_BE_128X64_WIDTH 128u
#define DEMO1_RGB565_RAW_BE_128X64_HEIGHT 64u
#define DEMO1_RGB565_RAW_BE_128X64_SIZE 16384u
extern const unsigned char demo1_rgb565_raw_be_128x64[16384];
```

```c
/* img_resources.c */
const unsigned char demo1_rgb565_raw_be_128x64[16384] = {
  0x00, 0x01, ...
};
```

规则：

- 符号名来自 `.bin` 文件名：非字母数字字符替换为 `_`，数字开头时加 `img_` 前缀，重名自动加 `_2`、`_3` 后缀
- 每个资源附带 `_WIDTH` / `_HEIGHT` / `_SIZE` 宏（来自文件名中的元数据）
- 生成内容不含时间戳，同样的输入总是生成同样的文件
- 拆分模式（`--split` 或 `codegen.mode: "split"`）为每个 `.bin` 生成独立的一对 `<符号名>.c/.h`
- 生成范围是输出目录中所有命名符合 `<名>_<格式>_<算法>_<be|le>_<宽>x<高>.bin` 规则的文件

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `--root <dir>` | 工作目录 |
| `--config <file>` | 指定配置文件 |
| `--output <dir>` | 输出目录 |
| `--tools <dir>` | 工具目录 |
| `--format <名>` / `--formats <列表\|all>` | 覆盖默认像素格式 |
| `--little-endian` | 默认小端 |
| `--bg-color <RRGGBB>` | 默认背景色 |
| `--index-interval <n>` | 默认索引间隔 |
| `--combined` / `--split` | 生成模式 |
| `--name <基名>` | 合并模式基名 |
| `--no-codegen` | 只转换，不生成 `.c/.h` |
| `--help` / `--info` | 帮助 / 机器可读元数据 |

命令行参数优先于配置文件，配置文件优先于内置默认值。

## 工具自动发现

管理器启动时扫描工具目录中的 `img2bin_*.exe`，逐个调用 `--info` 读取算法代号和能力，然后才决定每个 `input2*` 文件夹交给谁处理：

- 新增取模工具后无需修改管理器，放进 `tools` 目录即可被识别
- 某个文件夹找不到对应工具时，该文件夹标记为失败，其他文件夹照常处理
- `img2bin_pack-manifest.json` 中的 `discovered_tools` 记录了本次发现结果

## 退出码

| 值 | 含义 |
| --- | --- |
| 0 | 全部成功（或无事可做） |
| 1 | 命令行或配置错误 |
| 2 | 环境错误（目录不存在、找不到工具等） |
| 5 | 内部错误 |
| 6 | 部分文件夹失败 |
