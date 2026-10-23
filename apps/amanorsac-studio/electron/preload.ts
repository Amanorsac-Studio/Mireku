import { contextBridge, ipcRenderer } from 'electron';

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
    /** the sealed library file, or null if sealing failed */
    aofasPath: string | null;
    aofasError: string | null;
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
  /** Sanctuary library: every .jmi bundle with its browser category. */
  listLibrary(): Promise<LibraryEntry[]>;
  /** Read a bundle's instrument.json, presets and category (no audio). */
  openBundle(dir: string): Promise<OpenedBundle>;
  /** Decode one sample of an open bundle (bundle-relative path). */
  readBundleSample(dir: string, file: string): Promise<{ sampleRate: number; channels: ArrayBuffer[] } | null>;
  /** Write presets / name / category back into the bundle in place. */
  saveBundle(req: SaveBundleRequest): Promise<{ ok: boolean; issues: { level: string; path: string; message: string }[] }>;
  /** Layered sounds saved for Sanctuary (<Library>/Sounds/*.sound.json). */
  listSounds(): Promise<{ file: string; name: string; parts: number }[]>;
  loadSound(file: string): Promise<string>;
  saveSound(req: { name: string; json: string }): Promise<{ file: string }>;
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

export interface LibraryEntry {
  dir: string;
  name: string;
  world: string;
  category: string;
  sampleCount: number;
  zoneCount: number;
  sustaining: boolean;
}

export interface OpenedBundle {
  dir: string;
  instrument: unknown;
  presets: { fileName: string; json: string }[];
  category: string;
}

export interface SaveBundleRequest {
  dir: string;
  name?: string;
  category?: string;
  presets: { fileName: string; json: string }[];
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
  listLibrary: () => ipcRenderer.invoke('library:list'),
  openBundle: (dir) => ipcRenderer.invoke('bundle:open', dir),
  readBundleSample: (dir, file) => ipcRenderer.invoke('bundle:readSample', dir, file),
  saveBundle: (req) => ipcRenderer.invoke('bundle:save', req),
  listSounds: () => ipcRenderer.invoke('sound:list'),
  loadSound: (file) => ipcRenderer.invoke('sound:load', file),
  saveSound: (req) => ipcRenderer.invoke('sound:save', req),
  chooseDir: () => ipcRenderer.invoke('dialog:chooseDir'),
  chooseFile: () => ipcRenderer.invoke('dialog:chooseFile'),
  showItem: (p) => ipcRenderer.invoke('shell:showItem', p),
};

contextBridge.exposeInMainWorld('jm', bridge);
