// faithful.mjs — build a .jmi bundle that reproduces an SFZ patch exactly: its own velocity
// ranges, per-sample volume/tune/offset baked into the audio, key-off release samples with
// rt_decay, per-group amp release, hard velocity switching and the SFZ velocity curve.
import { mkdirSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { encodeWav, midiToNoteName, validateBundle, buildDefaultRack, defaultEngineParams } from '@jm/jmi-format';
import { decodeAudioFile as decodeFile, resample, peakOf, applyGain, trimStart, toChannelCount, denoise } from './audio.mjs';
import { parseSfz } from './sfz.mjs';

const RATES = [44100, 48000, 88200, 96000];
const dbToGain = (db) => Math.pow(10, db / 20);
const PEAK_TARGET_DB = -1;
const TAIL_FADE_MS = 10;

/** Number velocity layers per key group and round robins per velocity band; assign unique file names. */
function assignFiles(regions, prefix) {
  const keyGroups = new Map();
  for (const r of regions) {
    const k = `${r.root}|${r.lokey}|${r.hikey}`;
    if (!keyGroups.has(k)) keyGroups.set(k, []);
    keyGroups.get(k).push(r);
  }
  const used = new Set();
  const zones = [];
  for (const list of keyGroups.values()) {
    const bands = new Map();
    for (const r of list) {
      const b = `${r.lovel}|${r.hivel}`;
      if (!bands.has(b)) bands.set(b, []);
      bands.get(b).push(r);
    }
    const ordered = [...bands.values()].sort((a, b) => a[0].lovel - b[0].lovel);
    if (ordered.length > 4) throw new Error(`key ${list[0].root}: ${ordered.length} velocity layers (format allows 4)`);
    ordered.forEach((band, li) => {
      const zone = { ...band[0], layer: li + 1, rrs: [] };
      band.forEach((r, ri) => {
        // two key groups sharing a root note (e.g. split ranges) continue the rr numbering
        let rr = ri + 1, file;
        do file = `samples/${prefix}${midiToNoteName(r.root)}_v${li + 1}_rr${rr++}.wav`; while (used.has(file));
        used.add(file);
        zone.rrs.push({ region: r, file });
      });
      zones.push(zone);
    });
  }
  return zones.sort((a, b) => a.lokey - b.lokey || a.lovel - b.lovel);
}

export function fadeTail(channels, frames) {
  const n = channels[0].length, f = Math.min(frames, n);
  for (const ch of channels) for (let i = 0; i < f; i++) ch[n - 1 - i] *= i / f;
}

const FADE_IN_MS = 3;
const HISS_FADE_MS = 1500;   // minimum length of the dB-linear tail fade

/** A sample that starts mid-waveform (SFZ offset past the pre-roll) clicks: raised-cosine fade-in. */
export function fadeIn(channels, rate) {
  const first = Math.max(...channels.map((c) => Math.abs(c[0])));
  if (first < 1e-3) return false;
  const f = Math.min(channels[0].length, Math.round((rate * FADE_IN_MS) / 1000));
  for (const ch of channels) for (let i = 0; i < f; i++) ch[i] *= 0.5 - 0.5 * Math.cos((Math.PI * i) / f);
  return true;
}

/**
 * Where the note's natural decay sinks to within 6 dB of the recording's hiss (or a gate cuts to
 * digital silence), fade out over 300 ms and end the sample there. The note body is untouched;
 * only the hiss-only tail that the SFZ volume boost made audible is removed.
 */
export function trimHissTail(channels, rate) {
  const n = channels[0].length, win = Math.round(rate * 0.05), nw = Math.floor(n / win);
  if (nw < 12) return null;
  const rms = new Float64Array(nw);
  for (let w = 0; w < nw; w++) {
    let e = 0;
    for (const ch of channels) for (let i = w * win; i < (w + 1) * win; i++) e += ch[i] * ch[i];
    rms[w] = Math.sqrt(e / (win * channels.length));
  }
  const audible = [...rms].filter((x) => x > 1e-7).sort((a, b) => a - b);
  if (!audible.length) return null;
  const floor = audible[Math.floor(audible.length * 0.05)];
  let peakW = 0;
  for (let w = 1; w < nw; w++) if (rms[w] > rms[peakW]) peakW = w;
  // first run of 3 windows below a threshold (or gated to digital silence), after the attack
  const firstBelow = (thr, from) => {
    const q = (x) => x < 1e-7 || x < thr;
    for (let w = from; w + 2 < nw; w++) if (q(rms[w]) && q(rms[w + 1]) && q(rms[w + 2])) return w;
    return -1;
  };
  const startSearch = Math.max(peakW + 1, Math.round((0.3 * rate) / win));
  const w12 = firstBelow(floor * 4, startSearch);            // note is 12 dB above the hiss
  if (w12 < 0) return null;
  const w3 = firstBelow(floor * 1.41, w12);                  // note is 3 dB above the hiss
  // fade from the +12 dB point to the +3 dB point (at least 1.5 s), linear in dB down to −48 dB:
  // it sounds like the note decaying faster into silence, not like it being cut off
  const start = w12 * win;
  const minEnd = start + Math.round(rate * (HISS_FADE_MS / 1000));
  const end = Math.min(n, Math.max(minEnd, w3 < 0 ? n : (w3 + 1) * win));
  const fadeN = end - start;
  for (const ch of channels)
    for (let i = 0; i < fadeN; i++) ch[start + i] *= Math.pow(10, (-48 * (i / fadeN)) / 20);
  for (let c = 0; c < channels.length; c++) channels[c] = channels[c].slice(0, end);
  return { cutSec: end / rate, lostSec: (n - end) / rate };
}

export async function buildFaithfulBundle({ sfzFile, name, outDir, category, notes, select, author, regions: given, cleanTails = true, noiseReduction = null, log = console.log }) {
  // `regions` lets non-SFZ sources (SoundFont) supply regions with in-memory `pcm` channels
  const parsed = given ?? parseSfz(sfzFile, { includeRelease: true }).regions;
  const decodeAudioFile = (fileOrRegion) => (fileOrRegion?.pcm
    ? Promise.resolve({ channels: fileOrRegion.pcm.map((c) => Float32Array.from(c)), sampleRate: fileOrRegion.sampleRate })
    : decodeFile(fileOrRegion?.file ?? fileOrRegion));
  const all = select ? select(parsed) : parsed;
  const attack = all.filter((r) => r.trigger === 'attack');
  const release = all.filter((r) => r.trigger === 'release');
  if (!attack.length) throw new Error('SFZ has no attack regions');

  // format: majority source rate, stereo if any source is stereo
  const first = await decodeAudioFile(attack[0]);
  const rate = RATES.includes(first.sampleRate) ? first.sampleRate : 48000;
  const nch = first.channels.length >= 2 ? 2 : 1;

  // pass 1: one uniform trim for the whole instrument so the loudest corrected sample peaks at −1 dBFS
  let maxPeak = 0;
  for (const r of all) {
    const a = await decodeAudioFile(r);
    maxPeak = Math.max(maxPeak, peakOf(a.channels) * dbToGain(r.volumeDb || 0));
  }
  const trim = dbToGain(PEAK_TARGET_DB) / maxPeak;
  log(`  ${attack.length} attack + ${release.length} release regions · ${rate} Hz ${nch}ch · uniform trim ${(20 * Math.log10(trim)).toFixed(1)} dB`);

  const bundleDir = join(outDir, `${name.replace(/[<>:"/\\|?*]+/g, '').trim()}.jmi`);
  if (existsSync(bundleDir)) rmSync(bundleDir, { recursive: true, force: true });
  mkdirSync(join(bundleDir, 'samples'), { recursive: true });
  mkdirSync(join(bundleDir, 'presets'), { recursive: true });

  let bytes = 0;
  const cleanup = { trimmed: 0, fadedIn: 0, lostSec: 0 };
  const render = async (r, file) => {
    const a = await decodeAudioFile(r);
    const source = noiseReduction ? denoise(a.channels, noiseReduction) : a.channels;
    let ch = trimStart(source, r.offset);
    ch = resample(ch, Math.pow(2, (r.tune || 0) / 1200) * (a.sampleRate / rate));
    ch = toChannelCount(ch, nch);
    applyGain(ch, dbToGain(r.volumeDb || 0) * trim);
    if (cleanTails && r.trigger !== 'release') { const t = trimHissTail(ch, rate); if (t) { cleanup.trimmed++; cleanup.lostSec += t.lostSec; } }
    if (fadeIn(ch, rate)) cleanup.fadedIn++;
    fadeTail(ch, Math.round((rate * TAIL_FADE_MS) / 1000));
    const peak = peakOf(ch);
    const buf = encodeWav({ sampleRate: rate, bitDepth: 24, channels: ch });
    writeFileSync(join(bundleDir, file), buf);
    bytes += buf.length;
    return { file, peakDb: peak > 0 ? Math.min(0, 20 * Math.log10(peak)) : -120, lengthMs: (ch[0].length / rate) * 1000, loop: null };
  };

  const toZone = async (z, extra) => {
    const roundRobins = [];
    for (const { region, file } of z.rrs) roundRobins.push(await render(region, file));
    const zone = {
      rootNote: z.root, keyRangeLo: z.lokey, keyRangeHi: z.hikey,
      velRangeLo: Math.max(1, z.lovel), velRangeHi: Math.min(127, z.hivel), roundRobins,
    };
    if (z.ampegRelease != null) zone.releaseMs = Math.max(1, Math.min(20000, Math.round(z.ampegRelease * 1000)));
    return { ...zone, ...extra(z) };
  };

  const zones = [];
  for (const z of assignFiles(attack, '')) zones.push(await toZone(z, () => ({})));
  const releaseZones = [];
  for (const z of assignFiles(release, 'rel_')) releaseZones.push(await toZone(z, (zz) => ({ rtDecayDb: Math.max(0, Math.min(200, zz.rtDecay || 0)) })));

  log(`  cleanup: hiss tail faded + ended on ${cleanup.trimmed} samples (${cleanup.lostSec.toFixed(0)} s of hiss-only tail removed) · de-click fade-in on ${cleanup.fadedIn}`);

  const instrument = {
    formatVersion: 1,
    meta: {
      name, world: 'rhodes-ep', author: author ?? 'Versilian Studios (VCSL, CC0) — mapped by Amanorsac Studio',
      createdAt: new Date().toISOString(), captureMode: 'auto', channels: nch === 2 ? 'stereo' : 'mono',
      sampleRate: rate, bitDepth: 24, notes: notes ?? `Faithful build of ${sfzFile}`,
    },
    soundType: 'decaying',
    zones,
    velocityMap: [
      { layer: 1, name: 'pp', lo: 1, hi: 54 },
      { layer: 2, name: 'mp', lo: 55, hi: 83 },
      { layer: 3, name: 'mf', lo: 84, hi: 106 },
      { layer: 4, name: 'f', lo: 107, hi: 127 },
    ],
    reserved: { releaseSamples: [], pedalDownZones: [] },
    releaseZones,
    // piano: hard layer switching, SFZ default amp_veltrack=100, no JM world voicing on top
    playback: { velocityMode: 'switch', ampVelTrack: 100, voicing: 'neutral' },
  };
  writeFileSync(join(bundleDir, 'instrument.json'), JSON.stringify(instrument, null, 2));
  writeFileSync(join(bundleDir, 'presets', 'Default.json'), JSON.stringify({
    formatVersion: 1, name: 'Default', tags: ['piano'],
    engine: defaultEngineParams('rhodes-ep'), fxRack: buildDefaultRack('rhodes-ep'),
    ampEnvelope: { attackMs: 4, decayMs: 0, sustain: 100, releaseMs: 400 },
  }, null, 2));
  writeFileSync(join(bundleDir, 'sanctuary.json'), JSON.stringify({ category, tags: [] }, null, 2));

  const v = validateBundle(bundleDir);
  return {
    path: bundleDir, zones: zones.length, releaseZones: releaseZones.length,
    samples: zones.reduce((n, z) => n + z.roundRobins.length, 0) + releaseZones.reduce((n, z) => n + z.roundRobins.length, 0),
    bytes, trimDb: 20 * Math.log10(trim),
    errors: v.issues.filter((i) => i.level === 'error').map((i) => `${i.path}: ${i.message}`),
    warnings: v.issues.filter((i) => i.level === 'warning').map((i) => `${i.path}: ${i.message}`),
  };
}
