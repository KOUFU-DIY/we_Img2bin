img2bin tools __VERSION_TEXT__

这是 img2bin 工具集的 Windows x64 发布目录。

如果你希望看更详细的用户说明，请打开：
- docs\user\README.md
- docs\user\README-pack.md
- docs\user\README-tools.md
- docs\user\README-formats.md
- docs\user\README-decoder.md
- docs\user\README-protocol.md
- docs\user\README-schema.md

目录结构:
- windows\img2bin_pack.exe    统筹管理器（批量调度 + 生成 .c/.h）
- windows\img2bin_pack.json   统筹管理器默认配置
- windows\tools\              六个取模工具 exe
- windows\examples\           自动批处理示例脚本与配置示例
- input2raw\ 等六个文件夹      按算法分类放图片
- output\                     转换结果
- decoder\                    C99 参考解码器源码（拷进下位机工程即可用）
- docs\user\                  用户文档

一、推荐使用方式（统筹管理器）
1. 把图片按算法放进 input2raw、input2imprle、input2rle、
   input2qoi、input2qoif、input2indexqoi 文件夹
2. 双击 windows\img2bin_pack.exe（或 windows\examples\run_batch.cmd）
3. 结果输出到 output\ 文件夹:
   - 每张图片的 .bin
   - 汇总生成的 img_resources.c / img_resources.h
   - img2bin_pack-manifest.json 运行清单
4. 像素格式、字节序、索引间隔等在 windows\img2bin_pack.json 里配置
5. 六个取模工具只输出纯 bin；.c/.h 由统筹管理器生成，
   数组内容与 .bin 逐字节一致

二、单个工具使用方式
1. 双击 windows\tools 下任意一个 exe
2. 如果 input 或 output 文件夹不存在，程序会自动创建（在 exe 同目录下）
3. 程序会读取当前 exe 同目录下的 input 文件夹
4. 默认输出到当前 exe 同目录下的 output 文件夹
5. 默认输出格式为 RGB565，大端模式
6. 也可以把单个图片或文件夹直接拖到 exe 上

三、七个程序的区别
- img2bin_pack.exe:
  统筹管理器，扫描 input2<算法> 文件夹并调用对应工具，
  然后把输出目录中的 .bin 汇总生成 .c/.h

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
- 统筹管理器（在发布目录根下执行）:
  windows\img2bin_pack.exe
  windows\img2bin_pack.exe --format argb8888
  windows\img2bin_pack.exe --folders input2raw,input2qoi --format rgb565
  windows\img2bin_pack.exe --emit bin
  windows\img2bin_pack.exe --split
  windows\img2bin_pack.exe --info
  更多预设见 windows\examples\ 下的 batch_*.cmd

- 单个工具（在 windows\tools 下执行）:
  img2bin_raw.exe --format rgb565
  img2bin_raw.exe --formats all
  img2bin_imprle.exe --format argb8888
  img2bin_indexqoi.exe --format argb8888 --index-interval 512
  img2bin_raw.exe --little-endian
  img2bin_raw.exe --bg-color FF0000
  img2bin_raw.exe --input input --output output

- 批处理时每个工具会在输出目录生成 img2bin_<工具>-manifest.json，
  统筹管理器额外生成 img2bin_pack-manifest.json

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
- 当前版本号: __VERSION_TEXT__ (__VERSION_SEMVER__)
- 统筹管理器无参数运行时，按 windows\img2bin_pack.json 的 root 设置
  处理发布目录根下的 input2* 文件夹
- output 文件夹可同时保存多个工具生成的结果
- img2bin_indexqoi.exe 默认按图片宽度创建索引点
- 更完整的用户说明已随发布目录一起放到 docs\user
