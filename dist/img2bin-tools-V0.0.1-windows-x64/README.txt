img2bin tools V0.0.1

这是 img2bin 工具集的 Windows x64 发布目录。

如果你希望看更详细的用户说明，请打开：
- docs\user\README.md
- docs\user\README-tools.md（总览；每个工具另有 README-img2bin_<工具>.md 单独说明）
- docs\user\README-formats.md
- docs\user\README-decoder.md
- docs\user\README-protocol.md
- docs\user\README-schema.md

目录结构:
- windows\tools\              六个取模工具 exe
- decoder\                    C99 参考解码器源码（拷进下位机工程即可用）
- docs\user\                  用户文档

一、使用方式
1. 双击 windows\tools 下任意一个 exe
2. 如果 input 或 output 文件夹不存在，程序会自动创建（在 exe 同目录下）
3. 程序会读取当前 exe 同目录下的 input 文件夹
4. 默认输出到当前 exe 同目录下的 output 文件夹
5. 默认输出格式为 RGB565，大端模式
6. 也可以把单个图片或文件夹直接拖到 exe 上
7. 每个 .bin = 6 字节通用资源头(类型+算法/格式码+宽高) + 算法数据

二、六个程序的区别
- img2bin_raw.exe:
  输出无压缩像素格式 bin（含 Alpha 蒙版格式 a8/a4/a2/a1，仅本工具支持）
  输出文件命名:
  <原名>_<像素格式>_raw_<be|le>_<宽>x<高>.bin

- img2bin_imprle.exe:
  输出改进RLE压缩像素格式 bin
  输出文件命名:
  <原名>_<像素格式>_imprle_<be|le>_<宽>x<高>.bin

- img2bin_rle.exe:
  输出原始RLE压缩像素格式 bin
  输出文件命名:
  <原名>_<像素格式>_rle_<be|le>_<宽>x<高>.bin

- img2bin_qoi.exe:
  输出原始QOI压缩像素格式 bin
  输出文件命名:
  <原名>_<像素格式>_qoi_<be|le>_<宽>x<高>.bin

- img2bin_qoif.exe:
  输出原始QOI(无字典)压缩像素格式 bin
  输出文件命名:
  <原名>_<像素格式>_qoif_<be|le>_<宽>x<高>.bin

- img2bin_indexqoi.exe:
  输出索引QOI V2(静态调色盘)压缩像素格式 bin
  默认索引间隔为图片宽度，可用 --index-interval 自定义
  输出文件命名:
  <原名>_<像素格式>_indexqoi_<be|le>_<宽>x<高>.bin

三、常用命令示例（在 windows\tools 下执行）
  img2bin_raw.exe --format rgb565
  img2bin_raw.exe --formats all
  img2bin_raw.exe --format a4
  img2bin_imprle.exe --format argb8888
  img2bin_indexqoi.exe --format argb8888 --index-interval 512
  img2bin_raw.exe --little-endian
  img2bin_raw.exe --bg-color FF0000
  img2bin_raw.exe --input input --output output
  img2bin_raw.exe --info

- 每个 .bin 写出时控制台会报告体积率(压缩后 payload / RAW payload)
- 传 --manifest 时(默认关闭)工具会在输出目录生成 img2bin_<工具>-manifest.json

四、支持的输入格式
- PNG
- BMP
- JPG
- JPEG

五、支持的像素格式
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

六、补充说明
- 当前版本号: V0.0.1 (0.0.1)
- img2bin_indexqoi.exe 默认按图片宽度创建索引点
- 更完整的用户说明已随发布目录一起放到 docs\user

