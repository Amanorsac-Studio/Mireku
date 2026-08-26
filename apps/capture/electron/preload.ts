import { contextBridge, ipcRenderer } from 'electron';

export interface JmBridge {
  saveSession(id: string, json: string): Promise<boolean>;
  loadSession(id: string): Promise<string | null>;
  listSessions(): Promise<{ id: string; json: string }[]>;
  deleteSession(id: string): Promise<boolean>;
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
  chooseDir(): Promise<string | null>;
  showItem(path: string): Promise<boolean>;
}

const bridge: JmBridge = {
  saveSession: (id, json) => ipcRenderer.invoke('session:save', id, json),
  loadSession: (id) => ipcRenderer.invoke('session:load', id),
  listSessions: () => ipcRenderer.invoke('session:list'),
  deleteSession: (id) => ipcRenderer.invoke('session:delete', id),
  writeTake: (id, file, sampleRate, bitDepth, channels) =>
    ipcRenderer.invoke('take:write', id, file, sampleRate, bitDepth, channels),
  readTake: (id, file) => ipcRenderer.invoke('take:read', id, file),
  deleteTake: (id, file) => ipcRenderer.invoke('take:delete', id, file),
  exportBundle: (req) => ipcRenderer.invoke('export:bundle', req),
  chooseDir: () => ipcRenderer.invoke('dialog:chooseDir'),
  showItem: (p) => ipcRenderer.invoke('shell:showItem', p),
};

contextBridge.exposeInMainWorld('jm', bridge);
