img2bin tools __VERSION_TEXT__

这是 img2bin 工具集的 macOS 发布目录（universal 二进制，同时支持 Apple 芯片与 Intel）。

如果你希望看更详细的用户说明，请打开：
- docs/user/README.md
- docs/user/README-tools.md（总览；每个工具另有 README-img2bin_<工具>.md 单独说明）
- docs/user/README-formats.md
- docs/user/README-decoder.md
- docs/user/README-protocol.md
- docs/user/README-schema.md

目录结构:
- macos/tools/                六个取模工具（无扩展名的可执行文件）
- decoder/                    C99 参考解码器源码（拷进下位机工程即可用）
- docs/user/                  用户文档

一、首次使用前：解除隔离属性
这些可执行文件没有经过 Apple 公证签名，从网络下载后会被系统标记为隔离，
双击或执行时会提示“无法打开，因为无法验证开发者”。在解压后的目录里执行一次：

  xattr -dr com.apple.quarantine macos/tools

之后即可正常使用（本步骤每次下载新版本后做一次即可）。
如果提示没有执行权限，再执行一次：

  chmod +x macos/tools/*

二、使用方式
1. 在终端进入 macos/tools 目录，执行任意一个工具
2. 如果 input 或 output 文件夹不存在，程序会自动创建（在可执行文件同目录下）
3. 程序会读取可执行文件同目录下的 input 文件夹
4. 默认输出到可执行文件同目录下的 output 文件夹
5. 默认输出格式为 RGB565，大端模式
6. 也可以把图片路径直接作为参数传入
7. 每个 .bin = 6 字节通用资源头(类型+算法/格式码+宽高) + 算法数据

三、六个程序的区别
- img2bin_raw:
  输出无压缩像素格式 bin（含 Alpha 蒙版格式 a8/a4/a2/a1，仅本工具支持）
  输出文件命名:
  <原名>_<像素格式>_raw_<be|le>_<宽>x<高>.bin

- img2bin_imprle:
  输出改进RLE压缩像素格式 bin
  输出文件命名:
  <原名>_<像素格式>_imprle_<be|le>_<宽>x<高>.bin

- img2bin_rle:
  输出原始RLE压缩像素格式 bin
  输出文件命名:
  <原名>_<像素格式>_rle_<be|le>_<宽>x<高>.bin

- img2bin_qoi:
  输出原始QOI压缩像素格式 bin
  输出文件命名:
  <原名>_<像素格式>_qoi_<be|le>_<宽>x<高>.bin

- img2bin_qoif:
  输出原始QOI(无字典)压缩像素格式 bin
  输出文件命名:
  <原名>_<像素格式>_qoif_<be|le>_<宽>x<高>.bin

- img2bin_indexqoi:
  输出索引QOI V2(静态调色盘)压缩像素格式 bin
  默认索引间隔为图片宽度，可用 --index-interval 自定义
  输出文件命名:
  <原名>_<像素格式>_indexqoi_<be|le>_<宽>x<高>.bin

四、常用命令示例（在 macos/tools 下执行）
  ./img2bin_raw --format rgb565
  ./img2bin_raw --formats all
  ./img2bin_raw --format a4
  ./img2bin_imprle --format argb8888
  ./img2bin_indexqoi --format argb8888 --index-interval 512
  ./img2bin_raw --little-endian
  ./img2bin_raw --bg-color FF0000
  ./img2bin_raw --input input --output output
  ./img2bin_raw --info

- 每个 .bin 写出时控制台会报告体积率(压缩后 payload / RAW payload)
- 传 --manifest 时(默认关闭)工具会在输出目录生成 img2bin_<工具>-manifest.json

五、支持的输入格式
- PNG
- BMP
- JPG
- JPEG

六、支持的像素格式
六个工具通用的彩色格式:
- ARGB8888
- ARGB6666
- ARGB4444
- ARGB2222
- ARGB8565
- RGB888
- RGB565
- RGB332
- RAGB5155

仅 img2bin_raw 支持的 Alpha 蒙版格式（只存透明度，GUI 运行时染色）:
- A8 / A4 / A2 / A1

七、补充说明
- 当前版本号: __VERSION_TEXT__ (__VERSION_SEMVER__)
- 输出的 .bin 与 Windows 版逐字节一致，两个平台可混用同一套资源管线
- img2bin_indexqoi 默认按图片宽度创建索引点
- 更完整的用户说明已随发布目录一起放到 docs/user
