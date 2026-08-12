# img2bin 用户入口

面向嵌入式图片资源取模的工具集：把 `PNG/BMP/JPG/JPEG` 转换成下位机可直接使用的纯 `.bin`
像素数据（输出 = 6 字节通用资源头 + 算法 payload）。六个 CLI，每个对应一种编码算法：

| 工具 | 算法 | 特点 |
| --- | --- | --- |
| `img2bin_raw` | 无压缩 | 可 O(1) 随机寻址；唯一支持 Alpha 蒙版 `a8/a4/a2/a1` |
| `img2bin_imprle` | 改进 RLE | 重复段 + 原样段，杂色区不膨胀 |
| `img2bin_rle` | 原始 RLE | 结构最简单，解码最容易 |
| `img2bin_qoi` | 原始 QOI | 带 64 项哈希字典，压缩率通常最好 |
| `img2bin_qoif` | QOI 无字典 | 解码零字典 RAM、零哈希 |
| `img2bin_indexqoi` | 索引 QOI V2 | 静态调色盘 + 跳转索引，可从任意索引点空降解码 |

## 下载

预编译包在 [Releases](https://github.com/KOUFU-DIY/we_Img2bin/releases) 页面，由 GitHub Actions
在云端编译产出：

- **Windows x64**：`img2bin-tools-<版本>-windows-x64.zip`
- **macOS universal**：`img2bin-tools-<版本>-macos-universal.tar.gz`（Apple 芯片与 Intel 通用）

两个平台输出的 `.bin` 逐字节一致，可混用同一套资源管线。每个包内含可执行文件、
C99 参考解码器源码（`decoder/`）和完整用户文档（`docs/user/`）。

> macOS 首次使用前需解除隔离属性：`xattr -dr com.apple.quarantine macos/tools`

## 快速开始

双击运行，或拖拽图片/文件夹到可执行文件上，或命令行：

```powershell
.\img2bin_raw.exe --format rgb565
.\img2bin_raw.exe --format a4
.\img2bin_imprle.exe --format argb8888
.\img2bin_indexqoi.exe --format argb8888 --index-interval 512
```

双击运行时，工具读取可执行文件同目录的 `input` 文件夹、输出到同目录的 `output` 文件夹
（不存在会自动创建）。每写出一个 `.bin` 都会报告体积率；传 `--manifest` 才会写
`img2bin_<工具>-manifest.json` 运行清单（默认关闭）。

## 用户文档

- [用户总览](docs/user/README.md)
- [工具说明](docs/user/README-tools.md)（总览与共同行为）
- 每个工具的独立完整规格：
  [raw](docs/user/README-img2bin_raw.md) ·
  [imprle](docs/user/README-img2bin_imprle.md) ·
  [rle](docs/user/README-img2bin_rle.md) ·
  [qoi](docs/user/README-img2bin_qoi.md) ·
  [qoif](docs/user/README-img2bin_qoif.md) ·
  [indexqoi](docs/user/README-img2bin_indexqoi.md)
- [像素格式说明](docs/user/README-formats.md)
- [解码编写说明](docs/user/README-decoder.md)
- [协议与验证说明](docs/user/README-protocol.md)
- [接口 Schema 说明](docs/user/README-schema.md)

单独工具文档是自包含规格：量化、打包、算法 payload 规则全部内嵌且有唯一解，
拿走单个文件就能写出与工具输出逐字节一致的解码器。

## 参考解码器

[builder/src/decoder/](builder/src/decoder/) 提供纯 C99、零依赖、不用动态内存的参考解码器
`img2bin_decode.c/.h`（发布包里在 `decoder/` 目录），覆盖全部六种算法 × 九种彩色像素格式 × 大小端，
外加 Alpha 蒙版格式（`A8/A4/A2/A1`，仅 raw 算法）与 indexQOI V2（静态调色盘）跳转解码接口。
测试套件对每种组合做“编码 → 解码 → 与 RAW 逐字节比对”回环验证。

## 当前支持

- 输入格式：`PNG`、`BMP`、`JPG`、`JPEG`
- 彩色像素格式（六工具通用）：`ARGB8888`、`ARGB6666`、`ARGB4444`、`ARGB2222`、`ARGB8565`、`RGB888`、`RGB565`、`RGB332`、`RAGB5155`
- Alpha 蒙版格式（仅 `img2bin_raw`）：`A8`、`A4`、`A2`、`A1`
- 默认行为：`RGB565`、大端、`<exe_dir>/input` → `<exe_dir>/output`

## 协议与验证

`.bin` 输出统一为 **6 字节通用资源头（类型 + 算法/格式码 + 宽高）+ 算法 payload**。
用户使用、程序集成和解码编写，都以“当前工具实际输出 + `--info` 元数据 + 当前用户文档”为准。
详细说明见 [协议与验证说明](docs/user/README-protocol.md)。

## 从源码构建

跨平台 CMake 工程（C99，仓库根不是 CMake 根，`builder/` 才是）：

```bash
cmake -S builder -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Windows 上另有一键发布脚本（需要 VS 2022 + CMake + Ninja），会构建、跑测试并在
`dist/` 下生成完整发布目录：

```powershell
.\build_release.ps1
```

推送 `v*` 标签会触发 GitHub Actions 在 Windows 与 macOS 上编译、跑测试，并把两个平台的
发布包上传到 Releases（工作流见 [.github/workflows/release.yml](.github/workflows/release.yml)）。
