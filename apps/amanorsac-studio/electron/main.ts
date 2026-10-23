/**
 * Amanorsac Studio — Electron main process.
 * Owns all disk I/O: session snapshots, 24-bit WAV writing (via
 * @jm/jmi-format encodeWav), bundle export + zip + validation, and sealing the
 * finished bundle into the Amanorsac library as a .aofas file.
 * The renderer captures Float32 audio and ships buffers over IPC.
 */

import { app, BrowserWindow, dialog, ipcMain, session, shell } from 'electron';
import { mkdirSync, readdirSync, readFileSync, writeFileSync, existsSync, copyFileSync, rmSync, statSync } from 'node:fs';
import { homedir } from 'node:os';
import { join, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { encodeWav, decodeWav, validateBundle } from '@jm/jmi-format';
import { makeKeyRing, pack } from '@amanorsac/aofas-format';
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
 * Amanorsac Studio needs exactly two privileged renderer capabilities:
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
    title: 'Amanorsac Studio',
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

/**
 * Decode a bundle sample, WAV or FLAC. FLAC is lossless and about a third the size, so library
 * bundles ship in it; the decoded samples are bit-identical to the WAV they replaced.
 */
async function decodeSampleFile(p: string): Promise<{ sampleRate: number; channels: Float32Array[] }> {
  const bytes = readFileSync(p);
  if (!/\.flac$/i.test(p) && bytes.toString('ascii', 0, 4) !== 'fLaC') return decodeWav(bytes);
  const { FLACDecoder } = await import('@wasm-audio-decoders/flac');
  const dec = new FLACDecoder();
  await dec.ready;
  try {
    const { channelData, samplesDecoded, sampleRate } = await dec.decodeFile(new Uint8Array(bytes));
    return { sampleRate, channels: channelData.map((c) => c.subarray(0, samplesDecoded)) };
  } finally {
    dec.free();
  }
}

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
  /** Amanorsac catalogue shelf, sealed into the .aofas card */
  category?: string;
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

  // Seal the bundle into the Amanorsac library so the stage app can load it
  // (docs/AOFAS-FORMAT-SPEC.md §10). This is deliberately NOT fatal: the .jmi
  // bundle and its zip are already on disk, and a session's captures must
  // never be lost because a key file is missing.
  let aofasPath: string | null = null;
  let aofasError: string | null = null;
  try {
    const libraryDir = join(homedir(), 'Documents', 'Amanorsac Studio', 'Library');
    mkdirSync(libraryDir, { recursive: true });
    const out = join(libraryDir, `${req.bundleName.replace(/\.jmi$/i, '')}.aofas`);
    pack({
      bundleDir: bundlePath,
      out,
      keyRing: makeKeyRing(),
      author: 'Amanorsac Studio',
      category: req.category as never,
      // A take that captured nothing is reported in the validation issues
      // above; it must not stop the rest of the instrument being sealed.
      dropEmptySamples: true,
    });
    aofasPath = out;
  } catch (e) {
    aofasError = e instanceof Error ? e.message : String(e);
  }

  return {
    bundlePath,
    zipPath,
    aofasPath,
    aofasError,
    valid: result.valid,
    issues: result.issues,
    stats: result.stats ?? { sampleCount: 0, presetCount: 0, totalBytes },
    totalBytes,
  };
});

/* ---------------- IPC: Sanctuary library (open / edit / save in place) ---------------- */

function sanctuaryLibraryDir(): string {
  const d = join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library');
  mkdirSync(d, { recursive: true });
  return d;
}

function readJson(path: string): unknown {
  try {
    return JSON.parse(readFileSync(path, 'utf8'));
  } catch {
    return null;
  }
}

ipcMain.handle('library:list', () => {
  const lib = sanctuaryLibraryDir();
  const out: { dir: string; name: string; world: string; category: string; sampleCount: number; zoneCount: number; sustaining: boolean }[] = [];
  for (const e of readdirSync(lib, { withFileTypes: true })) {
    if (!e.isDirectory() || !e.name.toLowerCase().endsWith('.jmi')) continue;
    const dir = join(lib, e.name);
    const inst = readJson(join(dir, 'instrument.json')) as {
      meta?: { name?: string; world?: string };
      soundType?: string;
      zones?: { roundRobins: unknown[] }[];
    } | null;
    if (!inst?.meta) continue;
    const side = readJson(join(dir, 'sanctuary.json')) as { category?: string } | null;
    out.push({
      dir,
      name: inst.meta.name ?? e.name,
      world: inst.meta.world ?? '',
      category: side?.category ?? '',
      sampleCount: (inst.zones ?? []).reduce((n, z) => n + z.roundRobins.length, 0),
      zoneCount: (inst.zones ?? []).length,
      sustaining: inst.soundType === 'sustaining',
    });
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
});

ipcMain.handle('bundle:open', (_e, dir: string) => {
  const instrument = readJson(join(dir, 'instrument.json'));
  if (!instrument) throw new Error('Not a .jmi bundle: ' + dir);
  const presetsDir = join(dir, 'presets');
  const presets = existsSync(presetsDir)
    ? readdirSync(presetsDir)
        .filter((f) => f.toLowerCase().endsWith('.json'))
        .sort()
        .map((f) => ({ fileName: f, json: readFileSync(join(presetsDir, f), 'utf8') }))
    : [];
  const side = readJson(join(dir, 'sanctuary.json')) as { category?: string } | null;
  return { dir, instrument, presets, category: side?.category ?? '' };
});

ipcMain.handle('bundle:readSample', async (_e, dir: string, file: string) => {
  const p = join(dir, file.replace(/^[\\/]+/, ''));
  if (!p.startsWith(dir) || !existsSync(p)) return null;
  const { sampleRate, channels } = await decodeSampleFile(p);
  return { sampleRate, channels: channels.map((c) => c.buffer) };
});

ipcMain.handle('bundle:save', (_e, req: { dir: string; name?: string; category?: string; presets: { fileName: string; json: string }[] }) => {
  const instPath = join(req.dir, 'instrument.json');
  const inst = readJson(instPath) as { meta: { name: string } } | null;
  if (!inst) throw new Error('Not a .jmi bundle: ' + req.dir);
  if (req.name && req.name.trim() && req.name.trim() !== inst.meta.name) {
    inst.meta.name = req.name.trim();
    writeFileSync(instPath, JSON.stringify(inst, null, 2), 'utf8');
  }
  if (req.category) {
    const sidePath = join(req.dir, 'sanctuary.json');
    const side = (readJson(sidePath) as Record<string, unknown> | null) ?? { tags: [] };
    side.category = req.category;
    writeFileSync(sidePath, JSON.stringify(side, null, 2), 'utf8');
  }
  const presetsDir = join(req.dir, 'presets');
  mkdirSync(presetsDir, { recursive: true });
  for (const f of readdirSync(presetsDir)) if (f.toLowerCase().endsWith('.json')) rmSync(join(presetsDir, f));
  for (const p of req.presets) writeFileSync(join(presetsDir, p.fileName.replace(/[\\/]/g, '_')), p.json, 'utf8');
  const result = validateBundle(req.dir);
  return { ok: result.valid, issues: result.issues };
});

/* ---------------- IPC: layered sounds for Sanctuary ---------------- */

function soundsDir(): string {
  const d = join(sanctuaryLibraryDir(), 'Sounds');
  mkdirSync(d, { recursive: true });
  return d;
}

ipcMain.handle('sound:list', () => {
  const out: { file: string; name: string; parts: number }[] = [];
  for (const e of readdirSync(soundsDir())) {
    if (!e.toLowerCase().endsWith('.sound.json')) continue;
    const file = join(soundsDir(), e);
    const j = readJson(file) as { name?: string; parts?: unknown[] } | null;
    if (j) out.push({ file, name: j.name ?? e, parts: j.parts?.length ?? 0 });
  }
  return out.sort((a, b) => a.name.localeCompare(b.name));
});

ipcMain.handle('sound:load', (_e, file: string) => {
  if (!file.startsWith(soundsDir())) throw new Error('Not a library sound: ' + file);
  return readFileSync(file, 'utf8');
});

ipcMain.handle('sound:save', (_e, req: { name: string; json: string }) => {
  const safe = req.name.trim().replace(/[<>:"/\\|?*]+/g, '').trim() || 'Sound';
  const file = join(soundsDir(), `${safe}.sound.json`);
  writeFileSync(file, req.json, 'utf8');
  return { file };
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
    throw new Error(`Unsupported library type ".${ext}" — expected .X7L.${hint}`);
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
      author: req.author || 'Amanorsac Studio',
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

ipcMain.handle('dialog:chooseFile', async () => {
  const r = await dialog.showOpenDialog({
    title: 'Choose a Yamaha MONTAGE library',
    properties: ['openFile'],
    filters: [
      { name: 'Yamaha MONTAGE library', extensions: ['X7L', 'x7l'] },
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
