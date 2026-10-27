/**
 * Re-decide the loops in `.jmi` bundles that are ALREADY on disk.
 *
 * The seam rule in loopSeam.ts arrived after the client's library had been
 * imported, and re-importing is a multi-gigabyte job against source files two
 * of which are still truncated. The audio in a bundle is the source PCM
 * unchanged, so the same measurement can be made from the bundle's own WAVs and
 * only `instrument.json` has to change.
 *
 * Nothing here touches audio. The only edits are: a round robin's `loop` set to
 * null, and `soundType` following the loops that remain.
 */
import { readAudioFile } from './audioFile';
import { TAIL_LOOP_SEAM_DB, SEAM_WINDOW } from './loopSeam';

export interface BundleFs {
  readFile(path: string): Uint8Array;
  writeFile(path: string, data: Uint8Array | string): void;
  readDir(path: string): string[];
  exists(path: string): boolean;
  join(...parts: string[]): string;
}

export interface BundleLoopReport {
  bundle: string;
  /** median seam across every looped sample in the bundle, dB; null if none loop */
  medianSeamDb: number | null;
  loopedSamples: number;
  /** loops this pass would remove (or removed) */
  removed: number;
  soundTypeBefore: string;
  soundTypeAfter: string;
  verdict: 'no loops' | 'sustain loop kept' | 'decay tail unlooped' | 'unreadable';
  detail?: string;
}

/** RMS of one channel of an interleaved 16-bit LE span, in dBFS. */
function rmsDb(
  bytes: Uint8Array,
  dataOffset: number,
  channels: number,
  frames: number,
  from: number,
  n: number,
): number {
  const lo = Math.max(0, Math.min(from, Math.max(0, frames - 1)));
  const hi = Math.min(frames, lo + n);
  let sum = 0;
  let count = 0;
  for (let i = lo; i < hi; i++) {
    const o = dataOffset + i * channels * 2;
    let v = bytes[o] | (bytes[o + 1] << 8);
    if (v > 32767) v -= 65536;
    const f = v / 32768;
    sum += f * f;
    count++;
  }
  if (!count || sum === 0) return -120;
  return 10 * Math.log10(sum / count);
}

/**
 * Measure, and optionally apply, the loop decision for one bundle.
 * `apply` writes `instrument.json` and keeps the previous copy as `.bak`.
 */
export function reviewBundleLoops(
  bundleDir: string,
  fs: BundleFs,
  apply: boolean,
): BundleLoopReport {
  const name = bundleDir.replace(/\\/g, '/').split('/').pop() ?? bundleDir;
  const jsonPath = fs.join(bundleDir, 'instrument.json');
  const base: BundleLoopReport = {
    bundle: name,
    medianSeamDb: null,
    loopedSamples: 0,
    removed: 0,
    soundTypeBefore: '?',
    soundTypeAfter: '?',
    verdict: 'unreadable',
  };
  if (!fs.exists(jsonPath)) return { ...base, detail: 'no instrument.json' };

  let inst: {
    soundType: string;
    zones?: { roundRobins?: { file: string; loop?: unknown }[] }[];
    releaseZones?: { roundRobins?: { file: string; loop?: unknown }[] }[];
  };
  try {
    inst = JSON.parse(new TextDecoder().decode(fs.readFile(jsonPath)));
  } catch (e) {
    return { ...base, detail: (e as Error).message };
  }

  const rrs = [...(inst.zones ?? []), ...(inst.releaseZones ?? [])].flatMap((z) => z.roundRobins ?? []);
  const looped = rrs.filter((rr) => rr.loop);
  const before = inst.soundType;
  if (!looped.length) {
    return {
      ...base,
      soundTypeBefore: before,
      soundTypeAfter: before,
      verdict: 'no loops',
      loopedSamples: 0,
    };
  }

  const seams: number[] = [];
  for (const rr of looped) {
    const loop = rr.loop as { startSample: number; endSample: number };
    const wav = fs.join(bundleDir, ...rr.file.split('/'));
    if (!fs.exists(wav)) continue;
    let info;
    const bytes = fs.readFile(wav);
    try {
      info = readAudioFile(bytes);
    } catch {
      continue;
    }
    if (info.bitDepth !== 16 || info.encoding !== 'int') continue;
    const a = rmsDb(bytes, info.dataOffset, info.channels, info.frames, loop.startSample, SEAM_WINDOW);
    const b = rmsDb(
      bytes,
      info.dataOffset,
      info.channels,
      info.frames,
      loop.endSample - SEAM_WINDOW,
      SEAM_WINDOW,
    );
    seams.push(a - b);
  }
  if (!seams.length) {
    return { ...base, soundTypeBefore: before, soundTypeAfter: before, detail: 'no readable 16-bit audio' };
  }

  seams.sort((x, y) => x - y);
  const median = seams[Math.floor(seams.length / 2)];
  const isTail = median >= TAIL_LOOP_SEAM_DB;

  if (!isTail) {
    return {
      bundle: name,
      medianSeamDb: median,
      loopedSamples: looped.length,
      removed: 0,
      soundTypeBefore: before,
      soundTypeAfter: before,
      verdict: 'sustain loop kept',
    };
  }

  const after = 'decaying';
  if (apply) {
    for (const rr of looped) rr.loop = null;
    inst.soundType = after;
    fs.writeFile(fs.join(bundleDir, 'instrument.json.bak'), fs.readFile(jsonPath));
    fs.writeFile(jsonPath, JSON.stringify(inst, null, 2));
  }
  return {
    bundle: name,
    medianSeamDb: median,
    loopedSamples: looped.length,
    removed: looped.length,
    soundTypeBefore: before,
    soundTypeAfter: after,
    verdict: 'decay tail unlooped',
  };
}

export function reportLines(reports: BundleLoopReport[], applied: boolean): string[] {
  const out: string[] = [
    '',
    'bundle                   looped  medSeam  soundType            verdict',
    '------------------------------------------------------------------------------',
  ];
  for (const r of reports) {
    const seam = r.medianSeamDb === null ? '     -' : `${r.medianSeamDb >= 0 ? '+' : ''}${r.medianSeamDb.toFixed(1)}`;
    const st = r.soundTypeBefore === r.soundTypeAfter ? r.soundTypeBefore : `${r.soundTypeBefore} -> ${r.soundTypeAfter}`;
    out.push(
      `${r.bundle.slice(0, 24).padEnd(24)} ${String(r.loopedSamples).padStart(6)}  ${seam.padStart(7)}  ${st.padEnd(20)} ${r.verdict}${r.detail ? ` (${r.detail})` : ''}`,
    );
  }
  const changed = reports.filter((r) => r.removed > 0);
  out.push('');
  out.push(
    applied
      ? `${changed.length} bundle(s) unlooped; the previous instrument.json is beside each as instrument.json.bak`
      : `${changed.length} bundle(s) would be unlooped  (dry run — pass --apply to write)`,
  );
  return out;
}
