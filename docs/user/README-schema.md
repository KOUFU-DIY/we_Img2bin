# 接口 Schema 说明

本页逐字段说明所有机器可读输出，面向 GUI、自动化脚本和程序集成。共三类：

1. 取模工具的 `--info` JSON
2. 取模工具的批处理清单 `img2bin_<工具>-manifest.json`
3. 错误 JSON

通道约定：

- `--info` 输出到 stdout，manifest 写入输出目录，均为 UTF-8 JSON
- 错误 JSON 输出到 **stderr**，每条一行；批处理中每张失败图片各输出一行（NDJSON）
- 版本字段随 `version.h` 演进；`schema_version` 当前为 `1.2.0`（1.2.0 起：`pixel_formats[]` 新增 `bits_per_pixel` 与 `is_alpha_only` 字段，且格式列表**按工具而异**——Alpha 蒙版格式只出现在 `img2bin_raw` 的列表中）

二进制侧的机器接口（6 字节通用资源头、算法/像素格式 nibble 编码表）见
[协议与验证说明](README-protocol.md)的"通用资源头"一节。

## 一、取模工具 `--info`

顶层结构：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `schema_version` | string | info JSON 的 schema 版本 |
| `tool.id` | string | 工具唯一 id，如 `img2bin_raw` |
| `tool.kind` | string | 固定 `image_converter` |
| `tool.version` / `tool.version_semver` | string | 如 `V0.0.1` / `0.0.1` |
| `gui.display_name` / `gui.description` / `gui.gui_category` | object | 均为 `{zh_cn, en}` 双语文本 |
| `gui.priority` | number | GUI 排序权重，越小越靠前 |
| `algorithm.id` / `algorithm.algorithm_code` | string | 算法标识；`algorithm_code` 同时是 `input2<code>` 文件夹后缀与输出文件名中的算法段 |
| `algorithm.compression` | string | 压缩类别，如 `none` |
| `algorithm.supports_multi_format` | bool | 是否支持一次输出多种像素格式 |
| `defaults.format` | string | 默认像素格式 `rgb565` |
| `defaults.endianness` | string | `big` 或 `little` |
| `defaults.input_dir` / `defaults.output_dir` | string | `exe_dir/input`、`exe_dir/output` |
| `defaults.background_color` | string | `RRGGBB`，默认 `000000` |
| `defaults.index_interval` | string | 仅索引类工具存在，值 `image_width` |
| `capabilities.*` | bool/array/string | 能力开关，字段名自描述；集成方可用 `supports_index_interval` 判断是否可传 `--index-interval` |
| `invocation.style` | string | 固定 `flag_cli` |
| `invocation.info_flag` / `invocation.help_flag` | string | `--info` / `--help` |
| `invocation.arguments[]` | array | 参数元数据，见下表 |
| `output.extension` | string | 固定 `bin` |
| `output.filename_pattern` | string | `{source_stem}_{format_name}_<算法>_{endianness_token}_{width}x{height}.bin` |
| `output.endianness_tokens` | object | `{"big":"be","little":"le"}` |
| `output.resource_header.size` | number | 固定 6 |
| `output.resource_header.resource_type` | number | 固定 0（图片） |
| `output.resource_header.algorithm_nibble` | number | 本工具的算法 nibble（写入格式码高 4 位） |
| `output.resource_header.layout` | string | `type:1,algo_format:1,width_be:2,height_be:2` |
| `exit_codes` | object | 见"退出码"一节 |
| `pixel_formats[]` | array | **本工具**支持的像素格式（按工具而异：Alpha 蒙版 `a8/a4/a2/a1` 只在 `img2bin_raw` 中列出），见下表 |

`invocation.arguments[]` 每项：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `id` | string | 参数标识（如 `format`） |
| `flag` | string | 命令行旗标（如 `--format`） |
| `value_type` | string | 值类型：`path` / `pixel_format_name` / `csv_or_keyword` / `boolean_flag` / `hex_rgb` / `positive_integer` |
| `takes_value` | bool | 是否带值 |
| `required` | bool | 是否必填（当前全部为 false） |
| `conflicts_with` | array | 互斥参数 id 列表（如 `format` 与 `formats`） |
| `default` | any/null | 默认值 |
| `display_name` | object | `{zh_cn, en}` |
| `accepts` | array | `path` 类参数接受的对象种类（`file`/`directory`） |
| `special_values` | array | 特殊关键字（如 `formats` 的 `all`） |
| `element_type` | string/null | 列表值的元素类型 |
| `value_delimiter` | string/null | 列表值分隔符（如 `,`） |

`pixel_formats[]` 每项：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | string | 格式名（小写，用于 `--format` 与输出文件名） |
| `display_name` | object | `{zh_cn, en}` |
| `bytes_per_pixel` | number | 每像素字节数；**亚字节格式（`a4/a2/a1`）为 0**，尺寸计算须改用 `bits_per_pixel` |
| `bits_per_pixel` | number | 每像素位数（1.2.0 新增；整字节格式 = 字节数×8） |
| `is_alpha_only` | bool | 是否为 Alpha 蒙版格式（1.2.0 新增；`a8/a4/a2/a1` 为 true，仅存透明度、按行打包、仅 raw 算法） |
| `stores_alpha` | bool | 是否存储 Alpha |
| `uses_background_color` | bool | 是否参与背景色混合 |
| `endianness_affects_output` | bool | 大小端是否改变输出字节 |
| `header_nibble` | number | 该格式在通用资源头格式码低 4 位中的取值 |

Alpha 蒙版 payload 大小按行计算：`行字节数 = (宽 × bits_per_pixel + 7) / 8`，总大小 = `高 × 行字节数`（行打包契约见[协议与验证说明](README-protocol.md)）。非 raw 工具收到显式点名的 Alpha 蒙版格式时报 CLI 错误（`code` 为 `cli_parse_failed`、退出码 1）；`--formats all` 则静默滤除。

### 退出码（取模工具）

| 值 | 键 | 含义 |
| --- | --- | --- |
| 0 | `success` | 全部成功 |
| 1 | `cli_error` | 命令行参数错误 |
| 2 | `input_error` | 输入不存在/无法读取 |
| 3 | `encode_error` | 编码失败 |
| 4 | `write_error` | 写输出失败 |
| 5 | `internal_error` | 内部错误 |
| 6 | `batch_partial_failure` | 批处理部分失败 |

## 二、取模工具 manifest（`img2bin_<工具>-manifest.json`）

目录批处理时写入输出目录。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `tool.id` / `tool.version` | string | 生成者 |
| `run.output_directory` | string | 本次输出目录 |
| `run.endianness` | string | `big` / `little` |
| `run.requested_formats[]` | array | 本次请求的格式名列表 |
| `summary.source_images_total` | number | 输入图片总数 |
| `summary.source_images_succeeded` / `source_images_failed` | number | 成功/失败张数 |
| `summary.generated_bin_files_total` | number | 生成的 bin 总数（多格式时 > 图片数） |
| `items[]` | array | 每张输入图片一项 |

`items[]` 成功项：

```json
{
  "source_path": "…",
  "status": "success",
  "width": 128, "height": 64,
  "outputs": [ { "format": "rgb565", "path": "…", "bytes": 16384 } ]
}
```

`items[]` 失败项：

```json
{
  "source_path": "…",
  "status": "error",
  "error": {
    "code": "image_load_failed",
    "stage": "load",
    "exit_code": 2,
    "message": { "zh_cn": "…", "en": "…" },
    "detail": "…"
  }
}
```

## 三、错误 JSON

所有程序的致命错误都在 **stderr** 输出单行 JSON；批处理里每张失败图片各一行（NDJSON），可逐行解析：

```json
{"error":{"code":"input_path_invalid","exit_code":2,"message":{"zh_cn":"…","en":"…"},"file":"…","detail":"…","stage":"scan"}}
```

| 字段 | 说明 |
| --- | --- |
| `code` | 机器可读错误码（如 `cli_parse_failed`、`image_load_failed`、`input_path_invalid`） |
| `exit_code` | 对应的进程退出码 |
| `message` | `{zh_cn, en}` 人类可读消息 |
| `file` | 涉及的文件（可选） |
| `detail` | 补充细节（可选） |
| `stage` | 出错阶段，如 `cli` / `scan` / `load` / `encode` / `write` |

错误码集合允许扩展，集成方应把未知 `code` 当作一般错误处理，以 `exit_code` 决定流程。

## 四、输出文件名协议

`.bin` 文件名本身是机器接口，下游资源管线（如 bin2c 类工具或自动化脚本）可直接解析它取得元数据：

```text
<原图名>_<像素格式>_<算法>_<be|le>_<宽>x<高>.bin
```

从右往左解析（原图名可含下划线）：尺寸段 `<宽>x<高>`、字节序段 `be|le`、算法段、像素格式段（必须是合法格式名），剩余为原图名。
