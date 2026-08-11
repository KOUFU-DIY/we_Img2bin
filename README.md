# img2bin 用户入口

这是一个面向嵌入式图片资源取模的工具集项目。当前提供的 Windows 程序为
`windows\tools\` 下的六个取模工具（输出 = 6 字节通用资源头 + 算法 payload 的 `.bin`）：

- `img2bin_raw.exe`
- `img2bin_imprle.exe`
- `img2bin_rle.exe`
- `img2bin_qoi.exe`
- `img2bin_qoif.exe`
- `img2bin_indexqoi.exe`

## 目录结构

```text
img2c/
├─ windows/
│  └─ tools/                 # 六个取模 exe
├─ docs/user/                 # 用户文档
└─ builder/                   # C 源码工程（CMake）
```

## 快速开始

每个取模工具独立使用：双击、拖拽图片/文件夹到 exe 上，或命令行：

```powershell
.\windows\tools\img2bin_raw.exe --format rgb565
.\windows\tools\img2bin_raw.exe --format a4
.\windows\tools\img2bin_imprle.exe --format argb8888
.\windows\tools\img2bin_indexqoi.exe --format argb8888 --index-interval 512
```

双击运行时，工具读取 exe 同目录的 `input` 文件夹、输出到同目录的 `output` 文件夹
（不存在会自动创建）；目录批处理会在输出目录写 `img2bin_<工具>-manifest.json`。

## 用户文档

- [用户总览](docs/user/README.md)
- [工具说明](docs/user/README-tools.md)
- [像素格式说明](docs/user/README-formats.md)
- [解码编写说明](docs/user/README-decoder.md)
- [协议与验证说明](docs/user/README-protocol.md)
- [接口 Schema 说明](docs/user/README-schema.md)

## 参考解码器

[builder/src/decoder/](builder/src/decoder/) 提供纯 C99、零依赖、不用动态内存的参考解码器
`img2bin_decode.c/.h`（发布包里在 `decoder\` 目录），覆盖全部六种算法 × 九种彩色像素格式 × 大小端，
外加 Alpha 蒙版格式（`A8/A4/A2/A1`，仅 raw 算法）与 indexQOI 跳转解码接口。
测试套件对每种组合做"编码 → 解码 → 与 RAW 逐字节比对"回环验证。

## 当前支持

- 输入格式：`PNG`、`BMP`、`JPG`、`JPEG`
- 彩色像素格式（六工具通用）：`ARGB8888`、`ARGB6666`、`ARGB4444`、`ARGB2222`、`ARGB8565`、`RGB888`、`RGB565`、`RGB332`、`RAGB5155`
- Alpha 蒙版格式（仅 `img2bin_raw`）：`A8`、`A4`、`A2`、`A1`
- 默认行为：`RGB565`、大端、`<exe_dir>\input` → `<exe_dir>\output`

## 协议与验证

`.bin` 输出统一为 **6 字节通用资源头（类型 + 算法/格式码 + 宽高）+ 算法 payload**。
用户使用、程序集成和解码编写，都以"当前工具实际输出 + `--info` 元数据 + 当前用户文档"为准。
详细说明见 [协议与验证说明](docs/user/README-protocol.md)。

## 从源码构建

```powershell
.\build_release.ps1          # 完整发布构建（需要 VS 2022 + CMake + Ninja）
.\build_release.ps1 -SkipTests
```
