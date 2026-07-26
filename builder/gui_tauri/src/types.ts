export type ToolStatus = "ready" | "missing" | "stale";
export type JobState = "queued" | "running" | "done" | "failed";

export interface ToolInfo {
  id: string;
  label: string;
  category: string;
  summary: string;
  token: string;
  command: string;
  priority: number;
  status: ToolStatus;
  supportsIndexInterval: boolean;
}

export interface PixelFormat {
  name: string;
  label: string;
  bytesPerPixel: number;
  alphaBits: string;
  usesBackground: boolean;
}

export interface BatchItem {
  id: string;
  fileName: string;
  dimensions: string;
  format: string;
  state: JobState;
  progress: number;
}

export interface OutputOption {
  id: string;
  label: string;
  fileExt: string;
}

export interface PresetOption {
  id: string;
  label: string;
}
