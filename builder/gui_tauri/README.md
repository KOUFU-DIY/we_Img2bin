# img2bin GUI

这是 `img2bin` 的 Tauri GUI 工程首版界面骨架。

## 当前状态

- 已完成 Vue 3 + TypeScript + Vite 界面结构
- 已预留 Tauri v2 工程目录
- 当前界面使用模拟工具数据，后续替换为真实 `--info` 扫描

## 开发命令

```powershell
npm install
npm run dev
```

安装 Rust 和 Tauri 依赖后，可运行：

```powershell
npm run tauri:dev
```

## 首版界面目标

- 算法选择
- 像素格式选择
- 输入/输出预览
- 图片调整参数
- 输出格式参数
- 批处理任务与日志区域
