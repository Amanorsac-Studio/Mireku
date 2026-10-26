/**
 * JM Capture — Electron main process.
 * Owns all disk I/O: session snapshots, 24-bit WAV writing (via
 * @jm/jmi-format encodeWav), bundle export + zip + validation.
 * The renderer captures Float32 audio and ships buffers over IPC.
 */

import { app, BrowserWindow, dialog, ipcMain, session, shell } from 'electron';
import { mkdirSync, readdirSync, readFileSync, writeFileSync, existsSync, copyFileSync, rmSync, statSync } from 'node:fs';
import { join, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { encodeWav, decodeWav, validateBundle } from '@jm/jmi-format';
import AdmZip from 'adm-zip';
import {
  measureKeyzone,
  parseX7l,
  representativeKeyzone,
  SUPPORTED_EXTENSIONS,
  UNVERIFIED_EXTENSIONS,
  type X7lLibrary,
} from '../src/engine/import/x7l';
import { planImport } from '../src/engine/import/x7lPlan';
import { convertInstrument, type ImportFs } from '../src/engine/import/x7lConvert';
import { DROPPABLE_EXTENSIONS, extensionOf } from '../src/engine/import/audioFile';
import {
  planDrop,
  scanDroppedFiles,
  type DropReadFs,
  type PlanDropOptions,
  type ScannedFile,
} from '../src/engine/import/dropPlan';
import { convertDrop, type DropConvertFs } from '../src/engine/import/dropConvert';

const here = dirname(fileURLToPath(import.meta.url));

function sessionsDir(): string {
  const d = join(app.getPath('userData'), 'sessions');
  mkdirSync(d, { recursive: true });
  return d;
}

function sessionDir(id: string): string {
  const safe = id.replace(/[^\w-]/g, '_');
  const d = join(sessionsDir(), safe);
  mkdirSync(join(d, 'samples'), { recursive: true });
  return d;
}

/* ---------------- Permissions ---------------- */

/**
 * JM Capture needs exactly two privileged renderer capabilities:
 *   • microphone / line-in capture  (`media` with audio, `audioCapture`)
 *     — the audio input device and its labels in `enumerateDevices` (§3.1);
 *   • Web MIDI (`midi`, `midiSysex`) — Guided-mode MIDI in and Auto-mode
 *     MIDI out (§3.1, §3.4).
 *
 * Electron's built-in handlers are permissive; these replace them with an
 * explicit allow-list so a compromised renderer cannot reach for the camera,
 * screen capture, geolocation, notifications, or a WebHID/USB/serial device.
 * Every permission not named here is denied.
 */
const ALLOWED_PERMISSIONS = new Set(['media', 'audioCapture', 'midi', 'midiSysex']);

/** `media` is granted for audio only — Capture never records video. */
function mediaKindsAllowed(kinds: readonly string[] | undefined): boolean {
  if (!kinds || kinds.length === 0) return true; // enumerateDevices-style check
  return kinds.every((k) => k === 'audio' || k === 'unknown');
}

function installPermissionHandlers(): void {
  const ses = session.defaultSession;

  ses.setPermissionRequestHandler((_wc, permission, callback, details) => {
    if (!ALLOWED_PERMISSIONS.has(permission)) return callback(false);
    if (permission === 'media') {
      const d = details as { mediaTypes?: string[] };
      return callback(mediaKindsAllowed(d.mediaTypes));
    }
    callback(true);
  });

  ses.setPermissionCheckHandler((_wc, permission, _origin, details) => {
    if (!ALLOWED_PERMISSIONS.has(permission)) return false;
    if (permission === 'media') {
      const d = details as { mediaType?: string };
      return mediaKindsAllowed(d.mediaType ? [d.mediaType] : undefined);
    }
    return true;
  });

  // No WebHID / WebUSB / Web Serial device is ever needed.
  ses.setDevicePermissionHandler(() => false);
}

function createWindow(): void {
  const win = new BrowserWindow({
    width: 1440,
    height: 940,
    minWidth: 1100,
    minHeight: 720,
    backgroundColor: '#0C0A07',
    title: 'JM Capture',
    webPreferences: {
      preload: join(here, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.setMenuBarVisibility(false);
  if (process.env.VITE_DEV_SERVER_URL) {
    void win.loadURL(process.env.VITE_DEV_SERVER_URL);
  } else {
    void win.loadFile(join(here, '../dist/index.html'));
  }
}

/* ---------------- IPC: session persistence ---------------- */

ipcMain.handle('session:save', (_e, id: string, json: string) => {
  writeFileSync(join(sessionDir(id), 'session.json'), json, 'utf8');
  return true;
});

ipcMain.handle('session:load', (_e, id: string) => {
  const p = join(sessionDir(id), 'session.json');
  return existsSync(p) ? readFileSync(p, 'utf8') : null;
});

ipcMain.handle('session:list', () => {
  const out: { id: string; json: string }[] = [];
  for (const entry of readdirSync(sessionsDir(), { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const p = join(sessionsDir(), entry.name, 'session.json');
    if (existsSync(p)) out.push({ id: entry.name, json: readFileSync(p, 'utf8') });
  }
  return out;
});

ipcMain.handle('session:delete', (_e, id: string) => {
  rmSync(sessionDir(id), { recursive: true, force: true });
  return true;
});

/* ---------------- IPC: app settings ---------------- */

/**
 * Device selections and other app-wide preferences, so the picks made in the
 * Settings panel survive a relaunch. Same shape as the session snapshots: the
 * renderer owns the schema, the main process owns the file.
 */
function settingsPath(): string {
  return join(app.getPath('userData'), 'settings.json');
}

ipcMain.handle('settings:load', () => {
  const p = settingsPath();
  return existsSync(p) ? readFileSync(p, 'utf8') : null;
});

ipcMain.handle('settings:save', (_e, json: string) => {
  writeFileSync(settingsPath(), json, 'utf8');
  return true;
});

/* ---------------- IPC: sample WAV I/O ---------------- */

ipcMain.handle(
  'take:write',
  (
    _e,
    id: string,
    file: string,
    sampleRate: number,
    bitDepth: 16 | 24,
    channelBuffers: ArrayBuffer[],
  ) => {
    const channels = channelBuffers.map((b) => new Float32Array(b));
    const wav = encodeWav({ sampleRate, bitDepth, channels });
    const p = join(sessionDir(id), 'samples', file.replace(/[^\w.\-#]/g, '_'));
    writeFileSync(p, wav);
    return p;
  },
);

ipcMain.handle('take:read', (_e, id: string, file: string) => {
  const p = join(sessionDir(id), 'samples', file);
  if (!existsSync(p)) return null;
  const { sampleRate, channels } = decodeWav(readFileSync(p));
  return {
    sampleRate,
    channels: channels.map((c) => c.buffer),
  };
});

ipcMain.handle('take:delete', (_e, id: string, file: string) => {
  const p = join(sessionDir(id), 'samples', file);
  if (existsSync(p)) rmSync(p);
  return true;
});

/* ---------------- IPC: export ---------------- */

interface ExportRequest {
  sessionId: string;
  destDir: string;
  bundleName: string;
  instrumentJson: string;
  presets: { fileName: string; json: string }[];
  files: { from: string; to: string }[];
  /** decode each sample, mix to mono, re-encode (mono fold, §3.5) */
  foldToMono?: boolean;
  bitDepth?: 16 | 24;
}

ipcMain.handle('export:bundle', (_e, req: ExportRequest) => {
  const bundlePath = join(req.destDir, req.bundleName);
  mkdirSync(join(bundlePath, 'samples'), { recursive: true });
  mkdirSync(join(bundlePath, 'presets'), { recursive: true });
  writeFileSync(join(bundlePath, 'instrument.json'), req.instrumentJson, 'utf8');
  for (const p of req.presets) {
    writeFileSync(join(bundlePath, 'presets', p.fileName.replace(/[\\/]/g, '_')), p.json, 'utf8');
  }
  const srcSamples = join(sessionDir(req.sessionId), 'samples');
  for (const f of req.files) {
    const src = join(srcSamples, f.from);
    if (!existsSync(src)) continue;
    const dest = join(bundlePath, f.to);
    if (req.foldToMono) {
      const { sampleRate, channels } = decodeWav(readFileSync(src));
      const n = channels[0].length;
      const mono = new Float32Array(n);
      for (const ch of channels) for (let i = 0; i < n; i++) mono[i] += ch[i] / channels.length;
      writeFileSync(dest, encodeWav({ sampleRate, bitDepth: req.bitDepth ?? 24, channels: [mono] }));
    } else {
      copyFileSync(src, dest);
    }
  }

  // Validate with the shared format package (§3.7).
  const result = validateBundle(bundlePath);

  // Zip alongside (store the bundle folder at the zip root).
  const zip = new AdmZip();
  zip.addLocalFolder(bundlePath, req.bundleName);
  const zipPath = `${bundlePath}.zip`;
  zip.writeZip(zipPath);

  let totalBytes = 0;
  const walk = (dir: string) => {
    for (const e of readdirSync(dir, { withFileTypes: true })) {
      const p = join(dir, e.name);
      if (e.isDirectory()) walk(p);
      else totalBytes += statSync(p).size;
    }
  };
  walk(bundlePath);

  return {
    bundlePath,
    zipPath,
    valid: result.valid,
    issues: result.issues,
    stats: result.stats ?? { sampleCount: 0, presetCount: 0, totalBytes },
    totalBytes,
  };
});

/* ---------------- IPC: library import (X7L → .jmi) ---------------- */

/**
 * Converting a Yamaha library is a main-process job: the file is ~200 MB and
 * the samples are copied byte-for-byte, so nothing crosses IPC but the plan
 * and the results. One parsed library is cached so Inspect → Convert does not
 * re-read the file.
 */
let libCache: { path: string; mtimeMs: number; bytes: Uint8Array; lib: X7lLibrary; levels: number[] } | null = null;

function loadLibrary(filePath: string) {
  const ext = filePath.split('.').pop()?.toLowerCase() ?? '';
  if (!(SUPPORTED_EXTENSIONS as readonly string[]).includes(ext)) {
    const hint = (UNVERIFIED_EXTENSIONS as readonly string[]).includes(ext)
      ? ` ".${ext}" libraries look like the same YAMAHA-YSFC container but have not been verified against a real file, so they are not accepted yet.`
      : '';
    throw new Error(
      `Unsupported library type ".${ext}" — expected ${SUPPORTED_EXTENSIONS.map((e) => `.${e.toUpperCase()}`).join(' or ')}.${hint}`,
    );
  }
  const st = statSync(filePath);
  if (libCache && libCache.path === filePath && libCache.mtimeMs === st.mtimeMs) return libCache;
  const bytes = new Uint8Array(readFileSync(filePath));
  const lib = parseX7l(bytes);
  const levels = lib.waveforms.map((wf) => measureKeyzone(bytes, representativeKeyzone(wf)).peakDb);
  libCache = { path: filePath, mtimeMs: st.mtimeMs, bytes, lib, levels };
  return libCache;
}

interface ImportConvertRequest {
  filePath: string;
  destDir: string;
  /** plan indices to convert */
  instruments: number[];
  /** per-instrument velocity merge override, keyed by plan index */
  merges?: Record<number, number | null>;
  author?: string;
  zip?: boolean;
}

ipcMain.handle('import:inspect', (_e, filePath: string) => {
  const { lib, levels } = loadLibrary(filePath);
  const plan = planImport(lib, levels);
  return {
    filePath,
    fileName: basename(filePath),
    fileSize: statSync(filePath).size,
    version: lib.version,
    performances: lib.performances,
    plan,
  };
});

ipcMain.handle('import:convert', (e, req: ImportConvertRequest) => {
  const { bytes, lib, levels } = loadLibrary(req.filePath);
  const plan = planImport(lib, levels, { merges: req.merges });
  const chosen = req.instruments.map((i) => {
    const p = plan.instruments[i];
    if (!p) throw new Error(`No instrument at index ${i} in this library`);
    return p;
  });

  const nodeFs: ImportFs = {
    mkdir: (d) => mkdirSync(d, { recursive: true }),
    writeFile: (p, data) => writeFileSync(p, typeof data === 'string' ? data : Buffer.from(data)),
    join: (...parts) => join(...parts),
  };

  const grandTotal = chosen.reduce((n, p) => n + p.sampleCount, 0);
  let doneOverall = 0;
  const results = [];

  for (const p of chosen) {
    const bundle = convertInstrument(bytes, lib, p, req.destDir, nodeFs, {
      author: req.author || 'Mireku',
      sourceFileName: basename(req.filePath),
      onProgress: ({ instrument, done, total }) => {
        // one event per 1% of the whole job keeps the renderer responsive
        const overall = doneOverall + done;
        if (done === total || overall % Math.max(1, Math.floor(grandTotal / 100)) === 0) {
          e.sender.send('import:progress', { instrument, done, total, overall, grandTotal });
        }
      },
    });
    doneOverall += p.sampleCount;

    const validation = validateBundle(bundle.bundlePath);
    let zipPath: string | null = null;
    if (req.zip) {
      const zip = new AdmZip();
      zip.addLocalFolder(bundle.bundlePath, p.bundleName);
      zipPath = `${bundle.bundlePath}.zip`;
      zip.writeZip(zipPath);
    }
    results.push({
      ...bundle,
      zipPath,
      valid: validation.valid,
      issues: validation.issues,
      stats: validation.stats ?? { sampleCount: 0, presetCount: 0, totalBytes: bundle.bytesWritten },
    });
  }
  return { destDir: req.destDir, results };
});

/* ---------------- IPC: drag-and-drop audio import ---------------- */

/**
 * Dropping audio is a main-process job for the same reason the X7L import is:
 * the files can be hundreds of megabytes and the whole point is that their PCM
 * is copied byte-for-byte, so the audio never crosses IPC. Only the scan (one
 * small record per file) and the plan do.
 *
 * Dropped paths are untrusted. The walk is depth- and count-bounded, follows
 * no symlinks, and reads nothing that is not a `.wav`; the reader itself
 * (`audioFile.ts`) bounds every chunk and refuses anything it cannot account
 * for, so a malformed file is reported, never converted.
 */
const DROP_MAX_FILES = 2000;
const DROP_MAX_DEPTH = 4;

function expandAudioPaths(paths: string[]): { files: string[]; skipped: string[]; truncatedAt: number | null } {
  const files: string[] = [];
  const skipped: string[] = [];
  let truncatedAt: number | null = null;

  const isAudio = (p: string) => (DROPPABLE_EXTENSIONS as readonly string[]).includes(extensionOf(p));

  const walk = (dir: string, depth: number): void => {
    if (depth > DROP_MAX_DEPTH || truncatedAt !== null) return;
    let entries;
    try {
      entries = readdirSync(dir, { withFileTypes: true });
    } catch {
      skipped.push(dir);
      return;
    }
    for (const e of [...entries].sort((a, b) => a.name.localeCompare(b.name))) {
      if (truncatedAt !== null) return;
      const p = join(dir, e.name);
      if (e.isSymbolicLink()) continue; // never follow a link out of the drop
      if (e.isDirectory()) walk(p, depth + 1);
      else if (e.isFile()) {
        if (!isAudio(p)) continue; // quietly ignore artwork / readme / .DS_Store
        if (files.length >= DROP_MAX_FILES) {
          truncatedAt = DROP_MAX_FILES;
          return;
        }
        files.push(p);
      }
    }
  };

  for (const p of paths) {
    if (truncatedAt !== null) break;
    let st;
    try {
      st = statSync(p);
    } catch {
      skipped.push(p);
      continue;
    }
    if (st.isDirectory()) walk(p, 0);
    else if (!isAudio(p)) skipped.push(p);
    else if (files.length >= DROP_MAX_FILES) truncatedAt = DROP_MAX_FILES;
    else files.push(p);
  }
  return { files, skipped, truncatedAt };
}

const dropReadFs: DropReadFs = {
  readFile: (p) => new Uint8Array(readFileSync(p)),
  baseName: (p) => basename(p),
};

ipcMain.handle('drop:scan', (_e, paths: string[]) => {
  if (!Array.isArray(paths) || paths.length === 0) throw new Error('Nothing was dropped.');
  const { files: expanded, skipped, truncatedAt } = expandAudioPaths(paths);
  if (expanded.length === 0) {
    throw new Error(
      `No .wav files were found in what you dropped${skipped.length ? ` (${skipped.length} item(s) were not audio this importer reads)` : ''}.`,
    );
  }
  const scanned = scanDroppedFiles(expanded, dropReadFs);
  // A folder drop names the instrument; a loose set of files does not.
  let folderName: string | null = null;
  for (const p of paths) {
    try {
      if (statSync(p).isDirectory()) {
        folderName = basename(p);
        break;
      }
    } catch {
      /* already recorded in `skipped` */
    }
  }
  const totalBytes = scanned.reduce((n, f) => n + (f.audio?.dataBytes ?? 0), 0);
  return { files: scanned, folderName, skipped, truncatedAt, totalBytes };
});

ipcMain.handle(
  'drop:convert',
  (e, req: { files: ScannedFile[]; options: PlanDropOptions; destDir: string; author?: string; zip?: boolean }) => {
    const plan = planDrop(req.files, req.options ?? {});
    if (!plan.usable) throw new Error('Nothing in this drop can be converted.');

    const nodeFs: DropConvertFs = {
      mkdir: (d) => mkdirSync(d, { recursive: true }),
      writeFile: (p, data) => writeFileSync(p, typeof data === 'string' ? data : Buffer.from(data)),
      join: (...parts) => join(...parts),
      readFile: (p) => new Uint8Array(readFileSync(p)),
    };

    const every = Math.max(1, Math.floor(plan.sampleCount / 100));
    const bundle = convertDrop(plan, req.destDir, nodeFs, {
      author: req.author || 'Mireku',
      sourceLabel: dropSourceLabel(plan.sampleCount, req.options?.folderName ?? null),
      onProgress: ({ done, total, file }) => {
        if (done === total || done % every === 0) e.sender.send('drop:progress', { done, total, file });
      },
    });

    const validation = validateBundle(bundle.bundlePath);
    let zipPath: string | null = null;
    if (req.zip) {
      const zip = new AdmZip();
      zip.addLocalFolder(bundle.bundlePath, plan.bundleName);
      zipPath = `${bundle.bundlePath}.zip`;
      zip.writeZip(zipPath);
    }
    return {
      ...bundle,
      zipPath,
      valid: validation.valid,
      issues: validation.issues,
      stats: validation.stats ?? { sampleCount: 0, presetCount: 0, totalBytes: bundle.bytesWritten },
    };
  },
);

function dropSourceLabel(count: number, folderName: string | null): string {
  return folderName ? `the folder "${folderName}" (${count} audio files)` : `${count} audio files`;
}

ipcMain.handle('dialog:chooseFile', async () => {
  const r = await dialog.showOpenDialog({
    title: 'Choose a Yamaha MONTAGE library',
    properties: ['openFile'],
    filters: [
      { name: 'Yamaha YSFC library', extensions: ['X7L', 'x7l', 'X8L', 'x8l'] },
      { name: 'All files', extensions: ['*'] },
    ],
  });
  return r.canceled ? null : r.filePaths[0];
});

ipcMain.handle('dialog:chooseDir', async () => {
  const r = await dialog.showOpenDialog({ properties: ['openDirectory', 'createDirectory'] });
  return r.canceled ? null : r.filePaths[0];
});

ipcMain.handle('shell:showItem', (_e, p: string) => {
  shell.showItemInFolder(p);
  return true;
});

app.whenReady().then(() => {
  installPermissionHandlers();
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});
