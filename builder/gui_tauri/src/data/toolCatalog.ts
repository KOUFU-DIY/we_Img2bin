import type { BatchItem, OutputOption, PixelFormat, PresetOption, ToolInfo } from "../types";

export const tools: ToolInfo[] = [
  {
    id: "img2bin_raw",
    label: "无压缩",
    category: "基础",
    summary: "直接输出目标像素格式的纯 bin 数据。",
    token: "raw",
    command: "img2bin_raw.exe",
    priority: 10,
    status: "ready",
    supportsIndexInterval: false
  },
  {
    id: "img2bin_imprle",
    label: "改进 RLE",
    category: "RLE",
    summary: "原样段与重复段混合编码。",
    token: "imprle",
    command: "img2bin_imprle.exe",
    priority: 20,
    status: "ready",
    supportsIndexInterval: false
  },
  {
    id: "img2bin_rle",
    label: "原始 RLE",
    category: "RLE",
    summary: "按像素组重复次数编码。",
    token: "rle",
    command: "img2bin_rle.exe",
    priority: 30,
    status: "ready",
    supportsIndexInterval: false
  },
  {
    id: "img2bin_qoi",
    label: "原始 QOI",
    category: "QOI",
    summary: "保留 64 项字典索引。",
    token: "qoi",
    command: "img2bin_qoi.exe",
    priority: 40,
    status: "ready",
    supportsIndexInterval: false
  },
  {
    id: "img2bin_qoif",
    label: "QOI 无字典",
    category: "QOI",
    summary: "去掉字典索引，解码更轻。",
    token: "qoif",
    command: "img2bin_qoif.exe",
    priority: 50,
    status: "ready",
    supportsIndexInterval: false
  },
  {
    id: "img2bin_indexqoi",
    label: "索引 QOI",
    category: "QOI",
    summary: "带索引表，支持局部跳转。",
    token: "indexqoi",
    command: "img2bin_indexqoi.exe",
    priority: 60,
    status: "ready",
    supportsIndexInterval: true
  }
];

export const pixelFormats: PixelFormat[] = [
  { name: "argb8888", label: "ARGB8888", bytesPerPixel: 4, alphaBits: "8", usesBackground: false },
  { name: "argb6666", label: "ARGB6666", bytesPerPixel: 3, alphaBits: "6", usesBackground: false },
  { name: "argb4444", label: "ARGB4444", bytesPerPixel: 2, alphaBits: "4", usesBackground: false },
  { name: "argb2222", label: "ARGB2222", bytesPerPixel: 1, alphaBits: "2", usesBackground: false },
  { name: "argb8565", label: "ARGB8565", bytesPerPixel: 3, alphaBits: "8", usesBackground: false },
  { name: "rgb888", label: "RGB888", bytesPerPixel: 3, alphaBits: "none", usesBackground: true },
  { name: "rgb565", label: "RGB565", bytesPerPixel: 2, alphaBits: "none", usesBackground: true },
  { name: "rgb332", label: "RGB332", bytesPerPixel: 1, alphaBits: "none", usesBackground: true },
  { name: "ragb5155", label: "RAGB5155", bytesPerPixel: 2, alphaBits: "1", usesBackground: false }
];

export const outputOptions: OutputOption[] = [
  { id: "bin", label: "BIN 文件", fileExt: ".bin" },
  { id: "c_array", label: "C 数组", fileExt: ".c" },
  { id: "string", label: "字符串", fileExt: ".txt" },
  { id: "struct", label: "结构体", fileExt: ".c" },
  { id: "flash1", label: "Flash 格式 1", fileExt: ".bin" },
  { id: "flash2", label: "Flash 格式 2", fileExt: ".bin" }
];

export const fitPresets: PresetOption[] = [
  { id: "stretch", label: "变比例缩放" },
  { id: "contain", label: "等比例居中" },
  { id: "cover", label: "居中裁剪" },
  { id: "extend", label: "居中延伸" }
];

export const demoBatchItems: BatchItem[] = [
  { id: "job-1", fileName: "screen_home.png", dimensions: "36 x 45", format: "rgb565", state: "done", progress: 100 },
  { id: "job-2", fileName: "icon_alert.png", dimensions: "64 x 64", format: "argb4444", state: "running", progress: 64 },
  { id: "job-3", fileName: "dial_shadow.png", dimensions: "128 x 96", format: "rgb332", state: "queued", progress: 0 }
];
