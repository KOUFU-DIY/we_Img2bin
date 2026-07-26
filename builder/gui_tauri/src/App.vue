<script setup lang="ts">
import { computed, reactive, ref } from "vue";
import {
  Braces,
  CheckCircle2,
  ClipboardList,
  Contrast,
  Download,
  FileCode2,
  FileImage,
  FolderOpen,
  ImageDown,
  Layers3,
  ListChecks,
  Loader2,
  Maximize2,
  Minus,
  PanelRightOpen,
  Play,
  RefreshCw,
  RotateCcw,
  RotateCw,
  Save,
  Settings2,
  SlidersHorizontal,
  Sparkles,
  SquareDashedMousePointer,
  Upload,
  Wand2
} from "lucide-vue-next";
import { demoBatchItems, fitPresets, outputOptions, pixelFormats, tools } from "./data/toolCatalog";

const selectedToolId = ref("img2bin_raw");
const selectedFormat = ref("rgb565");
const selectedOutput = ref("bin");
const selectedFit = ref("contain");
const endian = ref<"be" | "le">("be");
const mergeMode = ref<"per_file" | "merged">("per_file");
const activeDock = ref<"text" | "manifest" | "log">("text");

const transform = reactive({
  width: 36,
  height: 45,
  rotate: 0,
  flipH: false,
  flipV: false,
  brightness: 0,
  contrast: 0,
  invert: false,
  threshold: 128,
  dither: "none",
  fillAlpha: false,
  fillColor: "#000000",
  wrapCount: 16,
  indexInterval: 36
});

const selectedTool = computed(() => tools.find((tool) => tool.id === selectedToolId.value) ?? tools[0]);
const selectedPixel = computed(() => pixelFormats.find((format) => format.name === selectedFormat.value) ?? pixelFormats[6]);
const selectedOutputOption = computed(() => outputOptions.find((item) => item.id === selectedOutput.value) ?? outputOptions[0]);

const outputName = computed(() => {
  return `screen_${selectedFormat.value}_${selectedTool.value.token}_${endian.value}_${transform.width}x${transform.height}${selectedOutputOption.value.fileExt}`;
});

const readyTools = computed(() => tools.filter((tool) => tool.status === "ready").length);
const doneJobs = computed(() => demoBatchItems.filter((job) => job.state === "done").length);

const commandPreview = computed(() => {
  const parts = [
    selectedTool.value.command,
    "--input",
    "input",
    "--output",
    "output",
    "--format",
    selectedFormat.value
  ];

  if (endian.value === "le") {
    parts.push("--little-endian");
  }
  if (selectedTool.value.supportsIndexInterval) {
    parts.push("--index-interval", String(transform.indexInterval));
  }

  return parts.join(" ");
});

const logs = [
  "工具扫描完成: 6 个算法可用",
  "输出目录: output",
  "当前任务等待真实后端接入"
];

function setRotate(next: number) {
  transform.rotate = next;
}
</script>

<template>
  <main class="app">
    <header class="topbar">
      <section class="brand-block">
        <div class="app-mark">
          <Sparkles :size="18" />
        </div>
        <div>
          <h1>img2bin 工作台</h1>
          <p>V0.0.1 · Windows</p>
        </div>
      </section>

      <section class="top-actions" aria-label="主操作">
        <button class="toolbar-button" title="刷新算法">
          <RefreshCw :size="17" />
          <span>刷新</span>
        </button>
        <button class="toolbar-button" title="打开输出目录">
          <FolderOpen :size="17" />
          <span>输出</span>
        </button>
        <button class="run-button" title="开始取模">
          <Play :size="18" />
          <span>开始取模</span>
        </button>
      </section>
    </header>

    <section class="status-strip">
      <div>
        <span>算法</span>
        <strong>{{ readyTools }}/{{ tools.length }}</strong>
      </div>
      <div>
        <span>格式</span>
        <strong>{{ selectedPixel.label }}</strong>
      </div>
      <div>
        <span>输出</span>
        <strong>{{ selectedOutputOption.label }}</strong>
      </div>
      <div>
        <span>批处理</span>
        <strong>{{ doneJobs }}/{{ demoBatchItems.length }}</strong>
      </div>
    </section>

    <section class="main-grid">
      <aside class="source-column panel">
        <div class="panel-heading">
          <div>
            <span>输入</span>
            <small>文件、目录和批处理队列</small>
          </div>
          <button class="icon-button" title="添加文件">
            <Upload :size="17" />
          </button>
        </div>

        <button class="drop-zone">
          <FileImage :size="34" />
          <strong>screen.png</strong>
          <span>36 x 45 · PNG</span>
        </button>

        <div class="section-title">
          <ListChecks :size="15" />
          <span>任务队列</span>
        </div>

        <div class="job-list">
          <article v-for="job in demoBatchItems" :key="job.id" class="job-row" :class="job.state">
            <div class="job-main">
              <strong>{{ job.fileName }}</strong>
              <span>{{ job.dimensions }} · {{ job.format.toUpperCase() }}</span>
            </div>
            <div class="job-state">
              <CheckCircle2 v-if="job.state === 'done'" :size="16" />
              <Loader2 v-else-if="job.state === 'running'" :size="16" class="spin" />
              <Minus v-else :size="16" />
              <em>{{ job.progress }}%</em>
            </div>
            <div class="progress">
              <span :style="{ width: `${job.progress}%` }" />
            </div>
          </article>
        </div>

        <div class="merge-switch">
          <button :class="{ active: mergeMode === 'per_file' }" @click="mergeMode = 'per_file'">逐图输出</button>
          <button :class="{ active: mergeMode === 'merged' }" @click="mergeMode = 'merged'">合成 bin</button>
        </div>
      </aside>

      <section class="preview-column">
        <div class="preview-toolbar panel">
          <div class="tool-tabs">
            <button v-for="tool in tools" :key="tool.id" :class="{ active: selectedToolId === tool.id }" @click="selectedToolId = tool.id">
              {{ tool.label }}
            </button>
          </div>
          <div class="selected-tool">
            <strong>{{ selectedTool.command }}</strong>
            <span>{{ selectedTool.summary }}</span>
          </div>
        </div>

        <div class="preview-split">
          <article class="preview-pane panel">
            <div class="pane-heading">
              <div>
                <span>输入预览</span>
                <small>预处理后图像</small>
              </div>
              <button class="icon-button" title="导出预览图">
                <ImageDown :size="17" />
              </button>
            </div>
            <div class="canvas input-canvas">
              <div class="sample-image" :style="{ filter: `brightness(${100 + transform.brightness}%) contrast(${100 + transform.contrast}%) ${transform.invert ? 'invert(1)' : ''}` }">
                <div class="sun" />
                <div class="mountain one" />
                <div class="mountain two" />
                <span>{{ transform.width }} x {{ transform.height }}</span>
              </div>
            </div>
          </article>

          <article class="preview-pane panel">
            <div class="pane-heading">
              <div>
                <span>输出预览</span>
                <small>{{ selectedFormat.toUpperCase() }} · {{ selectedTool.token }}</small>
              </div>
              <button class="icon-button" title="全屏预览">
                <Maximize2 :size="17" />
              </button>
            </div>
            <div class="canvas output-canvas">
              <div class="pixel-preview">
                <i v-for="index in 96" :key="index" :class="{ hot: index % 7 === 0, dim: index % 5 === 0 }" />
              </div>
            </div>
          </article>
        </div>

        <section class="dock panel">
          <div class="dock-tabs">
            <button :class="{ active: activeDock === 'text' }" @click="activeDock = 'text'">
              <FileCode2 :size="16" />
              文本
            </button>
            <button :class="{ active: activeDock === 'manifest' }" @click="activeDock = 'manifest'">
              <ClipboardList :size="16" />
              清单
            </button>
            <button :class="{ active: activeDock === 'log' }" @click="activeDock = 'log'">
              <Braces :size="16" />
              日志
            </button>
          </div>

          <pre v-if="activeDock === 'text'">const unsigned char screen_rgb565_raw_be_36x45[] = {
  0x00, 0x00, 0x18, 0xE3, 0x31, 0x86, 0x52, 0xAA
};</pre>
          <pre v-else-if="activeDock === 'manifest'">{
  "output": "{{ outputName }}",
  "algorithm": "{{ selectedTool.token }}",
  "format": "{{ selectedFormat }}"
}</pre>
          <div v-else class="log-lines">
            <p v-for="line in logs" :key="line">{{ line }}</p>
          </div>
        </section>
      </section>

      <aside class="inspector-column">
        <section class="panel">
          <div class="panel-heading">
            <div>
              <span>取模参数</span>
              <small>{{ selectedTool.label }}</small>
            </div>
            <Settings2 :size="18" />
          </div>

          <label class="field">
            <span>颜色格式</span>
            <select v-model="selectedFormat">
              <option v-for="format in pixelFormats" :key="format.name" :value="format.name">
                {{ format.label }} · {{ format.bytesPerPixel }}B
              </option>
            </select>
          </label>

          <div class="meta-grid">
            <span>Alpha {{ selectedPixel.alphaBits }}</span>
            <span>{{ selectedPixel.usesBackground ? "背景混合" : "保留透明" }}</span>
          </div>

          <label class="field">
            <span>输出类型</span>
            <select v-model="selectedOutput">
              <option v-for="option in outputOptions" :key="option.id" :value="option.id">
                {{ option.label }}
              </option>
            </select>
          </label>

          <div class="segmented">
            <button :class="{ active: endian === 'be' }" @click="endian = 'be'">大端</button>
            <button :class="{ active: endian === 'le' }" @click="endian = 'le'">小端</button>
          </div>

          <label v-if="selectedTool.supportsIndexInterval" class="field">
            <span>索引间隔</span>
            <input v-model.number="transform.indexInterval" type="number" min="1" />
          </label>
        </section>

        <section class="panel">
          <div class="panel-heading">
            <div>
              <span>预处理</span>
              <small>尺寸、方向和颜色</small>
            </div>
            <SlidersHorizontal :size="18" />
          </div>

          <div class="size-grid">
            <label class="field">
              <span>宽</span>
              <input v-model.number="transform.width" type="number" min="1" />
            </label>
            <label class="field">
              <span>高</span>
              <input v-model.number="transform.height" type="number" min="1" />
            </label>
          </div>

          <label class="field">
            <span>缩放</span>
            <select v-model="selectedFit">
              <option v-for="fit in fitPresets" :key="fit.id" :value="fit.id">
                {{ fit.label }}
              </option>
            </select>
          </label>

          <div class="icon-strip">
            <button title="左转 90 度" @click="setRotate((transform.rotate + 270) % 360)">
              <RotateCcw :size="17" />
            </button>
            <button title="右转 90 度" @click="setRotate((transform.rotate + 90) % 360)">
              <RotateCw :size="17" />
            </button>
            <button :class="{ active: transform.flipH }" title="水平镜像" @click="transform.flipH = !transform.flipH">
              <PanelRightOpen :size="17" />
            </button>
            <button :class="{ active: transform.flipV }" title="垂直镜像" @click="transform.flipV = !transform.flipV">
              <Layers3 :size="17" />
            </button>
          </div>

          <label class="range-field">
            <span>亮度 {{ transform.brightness }}</span>
            <input v-model.number="transform.brightness" type="range" min="-100" max="100" />
          </label>
          <label class="range-field">
            <span>对比度 {{ transform.contrast }}</span>
            <input v-model.number="transform.contrast" type="range" min="-100" max="100" />
          </label>
          <label class="range-field">
            <span>二值阈值 {{ transform.threshold }}</span>
            <input v-model.number="transform.threshold" type="range" min="0" max="255" />
          </label>

          <div class="toggle-list">
            <label>
              <input v-model="transform.invert" type="checkbox" />
              <Contrast :size="15" />
              反色
            </label>
            <label>
              <input v-model="transform.fillAlpha" type="checkbox" />
              <SquareDashedMousePointer :size="15" />
              透明填充
            </label>
            <label>
              <input v-model="transform.dither" true-value="ordered" false-value="none" type="checkbox" />
              <Wand2 :size="15" />
              点阵抖动
            </label>
          </div>

          <div class="color-row">
            <span>填充色</span>
            <input v-model="transform.fillColor" type="color" />
            <em>{{ transform.fillColor }}</em>
          </div>
        </section>

        <section class="panel">
          <div class="panel-heading">
            <div>
              <span>输出</span>
              <small>{{ outputName }}</small>
            </div>
            <Download :size="18" />
          </div>

          <label class="field">
            <span>数组换行数量</span>
            <input v-model.number="transform.wrapCount" type="number" min="1" />
          </label>

          <div class="command-box">
            <span>命令预览</span>
            <code>{{ commandPreview }}</code>
          </div>

          <div class="action-grid">
            <button class="secondary-action">
              <Save :size="17" />
              保存方案
            </button>
            <button class="primary-action">
              <Play :size="17" />
              执行
            </button>
          </div>
        </section>
      </aside>
    </section>
  </main>
</template>
