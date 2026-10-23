import { contextBridge, ipcRenderer, webUtils } from 'electron';

export interface JmBridge {
  saveSession(id: string, json: string): Promise<boolean>;
  loadSession(id: string): Promise<string | null>;
  listSessions(): Promise<{ id: string; json: string }[]>;
  deleteSession(id: string): Promise<boolean>;
  /** app-wide preferences (device selections) — JSON, renderer owns the schema */
  loadSettings(): Promise<string | null>;
  saveSettings(json: string): Promise<boolean>;
  writeTake(
    id: string,
    file: string,
    sampleRate: number,
    bitDepth: 16 | 24,
    channels: ArrayBuffer[],
  ): Promise<string>;
  readTake(id: string, file: string): Promise<{ sampleRate: number; channels: ArrayBuffer[] } | null>;
  deleteTake(id: string, file: string): Promise<boolean>;
  exportBundle(req: unknown): Promise<{
    bundlePath: string;
    zipPath: string;
    valid: boolean;
    issues: { level: string; path: string; message: string }[];
    stats: { sampleCount: number; presetCount: number; totalBytes: number };
    totalBytes: number;
  }>;
  /** Read a Yamaha `.X7L` library and return what is inside it (no conversion). */
  inspectLibrary(filePath: string): Promise<ImportInspection>;
  /** Convert the chosen instruments into `.jmi` bundles under `destDir`. */
  convertLibrary(req: ImportConvertRequest): Promise<{ destDir: string; results: ImportBundleResult[] }>;
  /** Subscribe to conversion progress; returns an unsubscribe function. */
  onImportProgress(cb: (p: ImportProgress) => void): () => void;
  /**
   * The filesystem path of a file the user dropped on the window.
   *
   * `File.path` was removed in Electron 32, so this is the documented
   * replacement: `webUtils.getPathForFile` is called in the preload (the only
   * place it exists) and the renderer gets back a plain string. The renderer
   * never reads the file itself — it hands the path to `scanDrop`.
   */
  pathForDroppedFile(file: File): string;
  /** Read dropped audio files (folders are expanded) and analyse each one. */
  scanDrop(paths: string[]): Promise<DropScanResult>;
  /** Write one `.jmi` bundle from a scanned drop. */
  convertDrop(req: DropConvertRequest): Promise<DropConvertResult>;
  /** Subscribe to drop-conversion progress; returns an unsubscribe function. */
  onDropProgress(cb: (p: DropProgress) => void): () => void;
  chooseDir(): Promise<string | null>;
  chooseFile(): Promise<string | null>;
  showItem(path: string): Promise<boolean>;
}

export interface ImportProgress {
  instrument: string;
  done: number;
  total: number;
  overall: number;
  grandTotal: number;
}

export interface ImportInspection {
  filePath: string;
  fileName: string;
  fileSize: number;
  version: string;
  performances: string[];
  /** an `X7lImportPlan` — typed structurally so the preload stays dependency-free */
  plan: unknown;
}

export interface ImportConvertRequest {
  filePath: string;
  destDir: string;
  instruments: number[];
  merges?: Record<number, number | null>;
  author?: string;
  zip?: boolean;
}

/* ---- drag-and-drop audio import ---- */

export interface DropProgress {
  done: number;
  total: number;
  file: string;
}

export interface DropScanResult {
  /** one `ScannedFile` per audio file found — typed structurally so the
   *  preload stays dependency-free, exactly as `ImportInspection.plan` is */
  files: unknown[];
  /** the dropped folder's name, when a folder was dropped */
  folderName: string | null;
  /** paths that were not audio files and were skipped before any read */
  skipped: string[];
  /** set when the drop was larger than the importer will take in one go */
  truncatedAt: number | null;
  totalBytes: number;
}

export interface DropConvertRequest {
  /** the `ScannedFile[]` returned by `scanDrop` */
  files: unknown[];
  /** the `PlanDropOptions` the user settled on in the mapping screen */
  options: unknown;
  destDir: string;
  author?: string;
  zip?: boolean;
}

export interface DropConvertResult {
  instrumentName: string;
  bundlePath: string;
  zipPath: string | null;
  sampleCount: number;
  zoneCount: number;
  bytesWritten: number;
  passthroughCount: number;
  convertedCount: number;
  loopedCount: number;
  keyRangeLo: number;
  keyRangeHi: number;
  velocityMap: { layer: number; name: string; lo: number; hi: number }[];
  notes: string[];
  warnings: string[];
  valid: boolean;
  issues: { level: string; path: string; message: string }[];
  stats: { sampleCount: number; presetCount: number; totalBytes: number };
}

export interface ImportBundleResult {
  instrumentName: string;
  bundlePath: string;
  zipPath: string | null;
  sampleCount: number;
  zoneCount: number;
  bytesWritten: number;
  keyRangeLo: number;
  keyRangeHi: number;
  velocityMap: { layer: number; name: string; lo: number; hi: number }[];
  notes: string[];
  valid: boolean;
  issues: { level: string; path: string; message: string }[];
  stats: { sampleCount: number; presetCount: number; totalBytes: number };
}

const bridge: JmBridge = {
  saveSession: (id, json) => ipcRenderer.invoke('session:save', id, json),
  loadSession: (id) => ipcRenderer.invoke('session:load', id),
  listSessions: () => ipcRenderer.invoke('session:list'),
  deleteSession: (id) => ipcRenderer.invoke('session:delete', id),
  loadSettings: () => ipcRenderer.invoke('settings:load'),
  saveSettings: (json) => ipcRenderer.invoke('settings:save', json),
  writeTake: (id, file, sampleRate, bitDepth, channels) =>
    ipcRenderer.invoke('take:write', id, file, sampleRate, bitDepth, channels),
  readTake: (id, file) => ipcRenderer.invoke('take:read', id, file),
  deleteTake: (id, file) => ipcRenderer.invoke('take:delete', id, file),
  exportBundle: (req) => ipcRenderer.invoke('export:bundle', req),
  inspectLibrary: (filePath) => ipcRenderer.invoke('import:inspect', filePath),
  convertLibrary: (req) => ipcRenderer.invoke('import:convert', req),
  onImportProgress: (cb) => {
    const listener = (_e: unknown, p: ImportProgress) => cb(p);
    ipcRenderer.on('import:progress', listener);
    return () => {
      ipcRenderer.off('import:progress', listener);
    };
  },
  pathForDroppedFile: (file) => webUtils.getPathForFile(file),
  scanDrop: (paths) => ipcRenderer.invoke('drop:scan', paths),
  convertDrop: (req) => ipcRenderer.invoke('drop:convert', req),
  onDropProgress: (cb) => {
    const listener = (_e: unknown, p: DropProgress) => cb(p);
    ipcRenderer.on('drop:progress', listener);
    return () => {
      ipcRenderer.off('drop:progress', listener);
    };
  },
  chooseDir: () => ipcRenderer.invoke('dialog:chooseDir'),
  chooseFile: () => ipcRenderer.invoke('dialog:chooseFile'),
  showItem: (p) => ipcRenderer.invoke('shell:showItem', p),
};

contextBridge.exposeInMainWorld('jm', bridge);
