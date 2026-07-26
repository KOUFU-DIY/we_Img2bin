# img2bin 用户入口

这是一个面向嵌入式图片资源取模的工具集项目。当前已经提供以下 Windows 可执行文件：

- `img2bin_raw.exe`
- `img2bin_imprle.exe`
- `img2bin_rle.exe`
- `img2bin_qoi.exe`
- `img2bin_qoif.exe`
- `img2bin_indexqoi.exe`

所有工具当前都输出纯 `bin` 数据，不带信息头、不输出数组文本、不生成 `.c/.h` 文件。

## 用户文档

面向最终用户的说明已经拆分到以下文件：

- [用户总览](docs/user/README.md)
- [工具说明](docs/user/README-tools.md)
- [像素格式说明](docs/user/README-formats.md)
- [解码编写说明](docs/user/README-decoder.md)
- [协议与验证说明](docs/user/README-protocol.md)

## 快速开始

1. 双击任意一个 `exe`
2. 若同目录下没有 `input` 和 `output` 文件夹，工具会自动创建
3. 把图片放进 `input`
4. 再次运行后，结果会输出到 `output`

也可以直接把图片或文件夹拖到 `exe` 上，或者使用命令行：

```powershell
.\img2bin_raw.exe --format rgb565
.\img2bin_imprle.exe --format argb8888
.\img2bin_indexqoi.exe --format argb8888 --index-interval 512
```

## 当前支持

- 输入格式：`PNG`、`BMP`、`JPG`、`JPEG`
- 像素格式：`ARGB8888`、`ARGB6666`、`ARGB4444`、`ARGB2222`、`ARGB8565`、`RGB888`、`RGB565`、`RGB332`、`RAGB5155`
- 默认行为：`RGB565`、大端、`<exe_dir>/input -> <exe_dir>/output`

## 协议与验证

用户使用、程序集成和解码编写，都以“当前工具实际输出 + `--info` 元数据 + 当前用户文档”为准。  
如果需要验证样例，建议直接用当前工具自行生成。详细说明见 [协议与验证说明](docs/user/README-protocol.md)。
