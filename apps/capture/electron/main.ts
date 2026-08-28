/**
 * JM Capture — Electron main process.
 * Owns all disk I/O: session snapshots, 24-bit WAV writing (via
 * @jm/jmi-format encodeWav), bundle export + zip + validation.
 * The renderer captures Float32 audio and ships buffers over IPC.
 */

import { app, BrowserWindow, dialog, ipcMain, session, shell } from 'electron';
import { mkdirSync, readdirSync, readFileSync, writeFileSync, existsSync, copyFileSync, rmSync, statSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { encodeWav, decodeWav, validateBundle } from '@jm/jmi-format';
import AdmZip from 'adm-zip';

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
