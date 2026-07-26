img2bin_raw V0.0.1

这是 img2bin_raw 的 Windows x64 发布目录。

一、默认使用方式
1. 双击 img2bin_raw.exe
2. 如果 input 或 output 文件夹不存在，程序会自动创建
3. 程序会读取当前 exe 同目录下的 input 文件夹
4. 默认输出到当前 exe 同目录下的 output 文件夹
5. 默认输出格式为 RGB565，大端模式

二、拖拽使用
- 可以把单个图片文件直接拖到 img2bin_raw.exe 上
- 也可以把一个文件夹拖到 img2bin_raw.exe 上
- 批处理时会在 output 文件夹里生成 img2bin_raw-manifest.json

三、常用命令示例
- 单格式输出:
  img2bin_raw.exe --format rgb565

- 输出全部格式:
  img2bin_raw.exe --formats all

- 小端输出:
  img2bin_raw.exe --little-endian

- 指定背景色:
  img2bin_raw.exe --bg-color FF0000

- 指定输入与输出:
  img2bin_raw.exe --input input --output output

四、支持的输入格式
- PNG
- BMP
- JPG
- JPEG

五、支持的像素格式
- ARGB8888
- ARGB6666
- ARGB4444
- ARGB2222
- ARGB8565
- RGB888
- RGB565
- RGB332
- RAGB5155

六、补充说明
- 当前版本号: V0.0.1 (0.0.1)
- 无参数运行时，默认从 <exe_dir>\input 读取图片
- 输出文件命名规则:
  <原名>_<像素格式>_raw_<be|le>_<宽>x<高>.bin

