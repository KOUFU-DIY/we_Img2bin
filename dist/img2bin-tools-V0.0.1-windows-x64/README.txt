img2bin tools V0.0.1

这是 img2bin 工具集的 Windows x64 发布目录。

如果你希望看更详细的用户说明，请打开：
- docs\user\README.md
- docs\user\README-tools.md
- docs\user\README-formats.md
- docs\user\README-decoder.md
- docs\user\README-protocol.md

包含六个可执行文件:
- img2bin_raw.exe
- img2bin_imprle.exe
- img2bin_rle.exe
- img2bin_qoi.exe
- img2bin_qoif.exe
- img2bin_indexqoi.exe

一、默认使用方式
1. 双击任意一个 exe
2. 如果 input 或 output 文件夹不存在，程序会自动创建
3. 程序会读取当前 exe 同目录下的 input 文件夹
4. 默认输出到当前 exe 同目录下的 output 文件夹
5. 默认输出格式为 RGB565，大端模式

二、拖拽使用
- 可以把单个图片文件直接拖到 exe 上
- 也可以把一个文件夹拖到 exe 上
- 批处理时会在 output 文件夹里生成对应的 manifest 文件
- img2bin_raw.exe 生成 img2bin_raw-manifest.json
- img2bin_imprle.exe 生成 img2bin_imprle-manifest.json
- img2bin_rle.exe 生成 img2bin_rle-manifest.json
- img2bin_qoi.exe 生成 img2bin_qoi-manifest.json
- img2bin_qoif.exe 生成 img2bin_qoif-manifest.json
- img2bin_indexqoi.exe 生成 img2bin_indexqoi-manifest.json

三、六个工具的区别
- img2bin_raw.exe:
  输出无压缩像素格式 bin
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
  输出索引QOI压缩像素格式 bin
  默认索引间隔为图片宽度，可用 --index-interval 自定义
  输出文件命名:
  <原名>_<像素格式>_indexqoi_<be|le>_<宽>x<高>.bin

四、常用命令示例
- RAW 单格式输出:
  img2bin_raw.exe --format rgb565

- RAW 输出全部格式:
  img2bin_raw.exe --formats all

- IMPRLE 单格式输出:
  img2bin_imprle.exe --format argb8888

- RLE 单格式输出:
  img2bin_rle.exe --format argb8888

- QOI 单格式输出:
  img2bin_qoi.exe --format argb8888

- QOIF 单格式输出:
  img2bin_qoif.exe --format argb8888

- IndexQOI 单格式输出:
  img2bin_indexqoi.exe --format argb8888

- IndexQOI 自定义索引间隔:
  img2bin_indexqoi.exe --format argb8888 --index-interval 512

- 小端输出:
  img2bin_raw.exe --little-endian
  img2bin_imprle.exe --little-endian
  img2bin_rle.exe --little-endian
  img2bin_qoi.exe --little-endian
  img2bin_qoif.exe --little-endian
  img2bin_indexqoi.exe --little-endian

- 指定背景色:
  img2bin_raw.exe --bg-color FF0000
  img2bin_imprle.exe --bg-color FF0000
  img2bin_rle.exe --bg-color FF0000
  img2bin_qoi.exe --bg-color FF0000
  img2bin_qoif.exe --bg-color FF0000
  img2bin_indexqoi.exe --bg-color FF0000

- 指定输入与输出:
  img2bin_raw.exe --input input --output output
  img2bin_imprle.exe --input input --output output
  img2bin_rle.exe --input input --output output
  img2bin_qoi.exe --input input --output output
  img2bin_qoif.exe --input input --output output
  img2bin_indexqoi.exe --input input --output output

五、支持的输入格式
- PNG
- BMP
- JPG
- JPEG

六、支持的像素格式
- ARGB8888
- ARGB6666
- ARGB4444
- ARGB2222
- ARGB8565
- RGB888
- RGB565
- RGB332
- RAGB5155

七、补充说明
- 当前版本号: V0.0.1 (0.0.1)
- 无参数运行时，默认从 <exe_dir>\input 读取图片
- output 文件夹可同时保存多个工具生成的结果
- img2bin_indexqoi.exe 默认按图片宽度创建索引点
- 更完整的用户说明已随发布目录一起放到 docs\user

