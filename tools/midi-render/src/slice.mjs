#!/usr/bin/env node
/**
 * slice.mjs — turn rendered WAVs into .jmi bundles in the Sanctuary library.
 *
 * One WAV per patch, rendered from "RENDER ALL LAYERS.mid"; the file name becomes the instrument
 * name. Cuts are made on the manifest's grid, with onset detection inside a small window around
 * each expected position so a DAW's pre-roll or plugin latency cannot shift the mapping. Each
 * slice is trimmed, de-clicked, tail-faded; one uniform trim per instrument puts its loudest
 * sample at -1 dBFS, exactly as the SFZ builds do.
 *
 *   node src/slice.mjs --in <renderFolder> [--manifest path] [--out <libraryDir>]
 *                      [--cat "Name=Piano"] [--only <substring>] [--dry]
 */
import { readFileSync, readdirSync, writeFileSync, mkdirSync, rmSync, existsSync, statSync, openSync, closeSync } from 'node:fs';
import { join, basename, dirname } from 'node:path';
import { homedir } from 'node:os';
import { encodeWav, validateBundle, buildDefaultRack, defaultEngineParams } from '@jm/jmi-format';
import { decodeAudioFile, peakOf, applyGain } from '../../sfz-import/src/audio.mjs';
import { encodeFlac, verifyFlac } from './flac.mjs';
import { trimHissTail, fadeIn, fadeTail } from '../../sfz-import/src/faithful.mjs';

const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i >= 0 ? process.argv[i + 1] : d; };
const IN = arg('in', join(homedir(), 'Documents', 'Amanorsac Studio', 'Render', 'WAV'));
const MANIFEST = arg('manifest', join(homedir(), 'Documents', 'Amanorsac Studio', 'Render', 'MIDI', 'manifest.json'));
const LIB = arg('out', join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library'));
const ONLY = arg('only', null);
// override the time scale when the fit has nothing to work with (e.g. 120/97 for a 97 BPM Song)
const SCALE = arg('scale', null) ? Number(arg('scale', null)) : arg('tempo', null) ? 120 / Number(arg('tempo', null)) : null;
const DRY = process.argv.includes('--dry');
const catOverrides = new Map(
  process.argv.flatMap((a, i) => (a === '--cat' ? [process.argv[i + 1]] : [])).map((s) => {
    const k = s.indexOf('=');
    return [s.slice(0, k).toLowerCase(), s.slice(k + 1)];
  }),
);

const dbToGain = (db) => Math.pow(10, db / 20);
/**
 * Where the loudest sample of an instrument peaks. NOT near 0 dBFS: a sampled instrument is played
 * polyphonically and two layers blend at every velocity between anchors, so peaks add. At -1 dBFS
 * a single fortissimo note already hit +0.3 dBFS and the master limiter clamped every loud note —
 * which is audible as a click on a percussive attack. -9 dBFS leaves a four-note chord at about
 * -3 dBFS and only large chords touch the limiter. Make it up on the slot/master level, not here.
 */
const PEAK_TARGET_DB = Number(arg('peak', -9));
// velocity response written into the bundle (see packages/jmi-format/src/velocity.ts)
const XFADE_ARG = arg('xfade', null);            // half-width of the blend around a layer boundary
const BRIGHTNESS = Number(arg('brightness', 60)); // darkening under an anchor, 0..100
const LEVEL_WINDOW_MS = 300;                      // loudness measured over the note's first 300 ms
const FORMAT = (arg('format', 'flac') === 'wav' ? 'wav' : 'flac');  // sample file format
const SUFFIX = arg('suffix', '');                                   // appended to the instrument name
// Time to silence when a key is lifted. The samples hold a full-length decay, so this is what makes
// staccato sound staccato: without it the recording keeps ringing as though the key were still down.
const RELEASE_BASS = Number(arg('release-bass', 320));              // below C3
const RELEASE_MID = Number(arg('release-mid', 230));                // C3–B4
const RELEASE_TREBLE = Number(arg('release-treble', 150));          // C5 and up
const RELEASE_SUSTAINING = Number(arg('release-sustaining', 450));  // pads, strings, organ
const RT_DECAY_DB = Number(arg('rt-decay', 3));                     // key-off level loss per second held
const TRIM_HISS = process.argv.includes('--trim-hiss');             // off: keep the whole decay
const SILENCE_DB = -90;                                             // only this is cut from the end

/**
 * Drop trailing digital silence, nothing else: find the last frame above -90 dBFS and end a few
 * milliseconds after it. A note's audible decay is never touched (that is what --trim-hiss is for),
 * which matters because FLAC already stores near-silence almost for free.
 */
function trimSilenceTail(channels, rate) {
  const n = channels[0].length;
  const thr = Math.pow(10, SILENCE_DB / 20);
  let last = -1;
  for (let i = n - 1; i >= 0; i--) {
    let a = 0;
    for (const ch of channels) a = Math.max(a, Math.abs(ch[i]));
    if (a > thr) { last = i; break; }
  }
  if (last < 0) return null;
  const end = Math.min(n, last + Math.round(rate * 0.02));
  if (end >= n - Math.round(rate * 0.05)) return null;          // nothing worth cutting
  for (let c = 0; c < channels.length; c++) channels[c] = channels[c].slice(0, end);
  return { cutSec: end / rate, lostSec: (n - end) / rate };
}

/** RMS over the first `ms` of the slice, in dBFS — what the layers are level-matched on. */
function rmsDb(channels, rate, ms) {
  const n = Math.min(channels[0].length, Math.round((rate * ms) / 1000));
  if (n <= 0) return -120;
  let e = 0;
  for (const ch of channels) for (let i = 0; i < n; i++) e += ch[i] * ch[i];
  const r = Math.sqrt(e / (n * channels.length));
  return r > 0 ? Math.max(-120, Math.min(0, 20 * Math.log10(r))) : -120;
}
const PRE_ROLL_MS = 5;      // keep a hair before the onset so the attack is intact
const TAIL_FADE_MS = 10;
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const noteName = (m) => `${NAMES[m % 12]}${Math.floor(m / 12) - 1}`;

/** Keyboard-player category from the patch name, so bundles file themselves. */
const CATEGORY_RULES = [
  [/\b(grand|upright|piano)\b/i, 'Piano'],
  [/\b(rhodes|wurli|wurlitzer|dx|fm|ep|e\.?piano|electric piano|clav|clavinet|tine)\b/i, 'Electric Piano'],
  [/\b(organ|hammond|b3|leslie|farfisa|vox)\b/i, 'Organ'],
  [/\b(pad|choir|voices|aah|ooh|atmos|ambient)\b/i, 'Pads & Choir'],
  [/\b(string|violin|viola|cello|orchestra|ensemble)\b/i, 'Strings'],
  [/\b(brass|trumpet|trombone|horn|sax|tuba)\b/i, 'Brass'],
  [/\b(flute|clarinet|oboe|bassoon|woodwind|pipe)\b/i, 'Woodwinds'],
  [/\b(bell|mallet|marimba|vibe|glock|kalimba|celeste)\b/i, 'Bells & Mallets'],
  [/\b(guitar|nylon|steel|pluck|harp|koto|banjo)\b/i, 'Guitar & Plucked'],
  [/\b(synth ?bass|sub bass|808)\b/i, 'Synth Bass'],
  [/\b(bass|upright bass|fingered)\b/i, 'Bass'],
  [/\b(lead|solo synth|saw lead|square)\b/i, 'Synth'],
  [/\b(synth|poly|analog|juno|moog|key)\b/i, 'Synth'],
  [/\b(drum|kit|perc|conga|djembe|shaker)\b/i, 'Percussion'],
];
const SUSTAINING = new Set(['Organ', 'Pads & Choir', 'Strings', 'Brass', 'Woodwinds']);
const categoryFor = (name) =>
  catOverrides.get(name.toLowerCase()) ?? CATEGORY_RULES.find(([re]) => re.test(name))?.[1] ?? 'FX & Other';
import { fitGrid, findOnset, slice } from './grid.mjs';

async function buildOne(wav, m) {
  const name = basename(wav).replace(/\.(wav|flac|aiff?)$/i, '') + SUFFIX;
  const category = categoryFor(name);
  const { channels, sampleRate: rate } = await decodeAudioFile(wav);
  const n = channels[0].length;
  const mono = new Float32Array(n);
  for (const ch of channels) for (let i = 0; i < n; i++) mono[i] += ch[i] / channels.length;

  let peak = 0;
  for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(mono[i]));
  const thr = Math.max(peak * 0.002, 2e-4);   // ~-54 dB of the loudest note, above a render's noise floor

  // where the grid really sits in this file (DAW tempo + pre-roll), fitted from the attacks
  const fit = SCALE ? { offsetSec: 0, scale: SCALE, onsets: 0, fit: 1 } : fitGrid(mono, rate, m.events);
  if (SCALE) {
    const firstOnset = findOnset(mono, 0, Math.round((m.events[0].at * SCALE + 6) * rate), thr);
    fit.offsetSec = (firstOnset < 0 ? 0 : firstOnset / rate) - m.events[0].at * SCALE;
  }
  // A bad fit means this WAV was not rendered from this manifest: slicing it anyway would map
  // the wrong audio to every note, so stop instead.
  if (!SCALE && fit.fit < 0.8) {
    throw new Error(
      `only ${(fit.fit * 100).toFixed(0)}% of the ${fit.onsets} note attacks line up with the grid — `
      + 'this render does not match this manifest (re-render with the current "RENDER ALL LAYERS.mid", '
      + 'or pass --manifest for the grid it was rendered from)',
    );
  }
  const expectedLen = Math.round(m.totalSec * fit.scale * rate);
  if (n < expectedLen * 0.9) throw new Error(`file is ${(n / rate / 60).toFixed(1)} min, expected ${(expectedLen / rate / 60).toFixed(1)} min — export the full length`);
  const atOf = (e) => Math.round((fit.offsetSec + e.at * fit.scale) * rate);

  const win = Math.round(0.35 * rate);         // onset search window around the grid position
  const pre = Math.round((PRE_ROLL_MS * rate) / 1000);
  const cuts = [];
  for (let k = 0; k < m.events.length; k++) {
    const e = m.events[k];
    const at = atOf(e);
    const onset = findOnset(mono, at - win, at + win, thr);
    const start = Math.max(0, (onset < 0 ? at : onset) - pre);
    const next = m.events[k + 1];
    const end = next ? atOf(next) - Math.round(0.02 * rate) : n;
    cuts.push({ e, start, end, missed: onset < 0 });
  }
  const missed = cuts.filter((c) => c.missed);

  // one uniform trim for the whole instrument
  let maxPeak = 0;
  for (const c of cuts) maxPeak = Math.max(maxPeak, peakOf(slice(channels, c.start, c.end)));
  if (maxPeak <= 0) throw new Error('every slice is silent');
  const trim = dbToGain(PEAK_TARGET_DB) / maxPeak;

  /**
   * No blending by default. Two layers are two separate renders of the same note, and a DAW
   * instrument varies every note a little (humanised start, round robins), so their attack
   * transients never line up exactly — summing them flams the attack, which is audible as a pop,
   * worst on a percussive tine or hammer. Level matching at the anchors already removed the jumps
   * that blending was there to hide, so a hard switch between level-matched layers is cleaner.
   * `--xfade N` turns it back on for material with soft attacks (pads, strings), where the sum is
   * harmless and the extra timbre continuity helps.
   */
  const xfade = XFADE_ARG != null ? Number(XFADE_ARG) : 0;

  const bundleDir = join(LIB, `${name.replace(/[<>:"/\\|?*]+/g, '').trim()}.jmi`);
  const sustaining = SUSTAINING.has(category);
  const report = {
    name, category, samples: cuts.length, missed: missed.length, bytes: 0, trimmed: 0, fadedIn: 0, sustaining,
    grid: `start ${fit.offsetSec.toFixed(2)}s · time scale ${fit.scale.toFixed(4)}${Math.abs(fit.scale - 1) > 0.002 ? ` (rendered at ${(m.bpm / fit.scale).toFixed(1)} BPM)` : ''} · ${fit.onsets} attacks found, ${(fit.fit * 100).toFixed(0)}% on the grid`,
    missedList: missed.slice(0, 8).map((c) => `${noteName(c.e.note)} v${c.e.layer}`),
  };
  if (DRY) return report;

  if (existsSync(bundleDir)) rmSync(bundleDir, { recursive: true, force: true });
  mkdirSync(join(bundleDir, 'samples'), { recursive: true });
  mkdirSync(join(bundleDir, 'presets'), { recursive: true });

  let verified = false;
  const byNote = new Map();
  for (const c of cuts) {
    let ch = slice(channels, c.start, c.end);
    applyGain(ch, trim);
    // The decay is kept in full. All that comes off the end is run-out that is already digital
    // silence (below -90 dBFS) — audible tail is never shortened. --trim-hiss opts into the older,
    // more aggressive cleanup that fades a tail once it sinks into a recording's noise floor.
    const t = TRIM_HISS ? trimHissTail(ch, rate) : trimSilenceTail(ch, rate);
    if (t) report.trimmed++;
    if (fadeIn(ch, rate)) report.fadedIn++;
    fadeTail(ch, Math.round((rate * TAIL_FADE_MS) / 1000));
    const bits = m.bundle?.bitDepth ?? 16;
    const file = `samples/${noteName(c.e.note)}_v${c.e.layer}_rr1.${FORMAT}`;
    // FLAC is bit-identical to the WAV it replaces and roughly a third the size, so it is the
    // default; the first sample of every instrument is verified before the rest are trusted
    let buf;
    if (FORMAT === 'flac') {
      buf = await encodeFlac(ch, rate, bits);
      if (!verified) {
        const worst = await verifyFlac(buf, ch, bits);
        if (worst !== 0) throw new Error(`FLAC encoding was not lossless (worst difference ${worst} steps) — rerun with --format wav`);
        verified = true;
        report.losslessVerified = true;
      }
    } else {
      buf = encodeWav({ sampleRate: rate, bitDepth: bits, channels: ch });
    }
    writeFileSync(join(bundleDir, file), buf);
    report.bytes += buf.length;
    const p = peakOf(ch);
    const level = rmsDb(ch, rate, LEVEL_WINDOW_MS);
    // sustained sounds loop inside the held part so a key can be held indefinitely
    const holdEnd = Math.round(c.e.hold * rate);
    const loop = sustaining && ch[0].length > holdEnd && holdEnd > rate
      ? { startSample: Math.round(holdEnd * 0.45), endSample: Math.round(holdEnd * 0.9), crossfadeSamples: Math.round(0.05 * rate) }
      : null;
    const rr = { file, peakDb: p > 0 ? Math.min(0, 20 * Math.log10(p)) : -120, lengthMs: (ch[0].length / rate) * 1000, loop };
    if (!byNote.has(c.e.note)) byNote.set(c.e.note, new Map());
    byNote.get(c.e.note).set(c.e.layer, { rr, levelDb: level, anchorVelocity: c.e.velocity });
  }

  // key ranges: each rendered note covers up to half the gap to its neighbours
  const notes = [...byNote.keys()].sort((a, b) => a - b);
  const zones = [];
  notes.forEach((note, i) => {
    const prev = notes[i - 1], next = notes[i + 1];
    const lo = prev === undefined ? Math.min(note, m.notes[0]) : note - Math.floor((note - prev) / 2);
    const hi = next === undefined ? Math.max(note, m.notes.at(-1)) : note + Math.ceil((next - note) / 2) - 1;
    for (const v of m.velocityMap) {
      const got = byNote.get(note).get(v.layer);
      if (!got) continue;
      zones.push({
        rootNote: note, keyRangeLo: lo, keyRangeHi: hi, velRangeLo: v.lo, velRangeHi: v.hi, roundRobins: [got.rr],
        // anchored playback: the velocity this layer was played at, and how loud it came out
        anchorVelocity: got.anchorVelocity, levelDb: Math.round(got.levelDb * 10) / 10,
        // how fast the note stops when the key is lifted — a damper, not a fade-out. Heavy bass
        // strings take longer to still than the top octave, as on a real instrument.
        releaseMs: sustaining ? RELEASE_SUSTAINING : note < 48 ? RELEASE_BASS : note < 72 ? RELEASE_MID : RELEASE_TREBLE,
      });
    }
  });

  const instrument = {
    formatVersion: 1,
    meta: {
      // the trim this instrument was built with, so a later key-off pass can match its level
      renderTrimDb: Math.round(20 * Math.log10(trim) * 100) / 100,
      name, world: 'rhodes-ep', author: 'Rendered in Studio One · mapped by Amanorsac Studio',
      createdAt: new Date().toISOString(), captureMode: 'auto', channels: channels.length >= 2 ? 'stereo' : 'mono',
      sampleRate: rate, bitDepth: m.bundle?.bitDepth ?? 16,
      notes: `Rendered from ${m.mainFile} (${m.notes.length} notes every ${m.step} semitones, ${m.velocityMap.length} velocity layers) and sliced on the render grid.`,
    },
    soundType: sustaining ? 'sustaining' : 'decaying',
    zones,
    velocityMap: m.velocityMap.map((v) => ({ layer: v.layer, name: ['pp', 'mp', 'mf', 'f'][v.layer - 1] ?? `v${v.layer}`, lo: v.lo, hi: v.hi })),
    reserved: { releaseSamples: [], pedalDownZones: [] },
    // level matched at the recorded anchors, blended across boundaries, darker under an anchor:
    // the recordings already carry the instrument's own dynamics, so no curve goes on top
    playback: { velocityMode: 'anchored', velocityXfade: xfade, velocityBrightness: BRIGHTNESS, voicing: 'neutral' },
  };
  writeFileSync(join(bundleDir, 'instrument.json'), JSON.stringify(instrument, null, 2));
  writeFileSync(join(bundleDir, 'presets', 'Default.json'), JSON.stringify({
    formatVersion: 1, name: 'Default', tags: [category.toLowerCase()],
    // as sampled: empty rack, no added attack — the player adds effects deliberately
    engine: defaultEngineParams('rhodes-ep'), fxRack: [],
    ampEnvelope: { attackMs: 0, decayMs: 0, sustain: 100, releaseMs: sustaining ? 500 : 300 },
  }, null, 2));
  writeFileSync(join(bundleDir, 'sanctuary.json'), JSON.stringify({ category, tags: [] }, null, 2));

  const v = validateBundle(bundleDir);
  report.zones = zones.length;
  report.errors = v.issues.filter((i) => i.level === 'error').map((i) => `${i.path}: ${i.message}`);
  report.path = bundleDir;
  return report;
}

/**
 * The key-off pass. The notes are short, so the interesting audio is what happens AFTER each
 * note-off: the damper landing, the key mechanism, the pedal. Those windows become the
 * instrument's release samples, played on note-off and attenuated by how long the key was held
 * (SFZ calls that rt_decay). This does not build a new instrument — it adds to one that exists,
 * matching the level trim it was built with so the release sits correctly under the body.
 */
async function buildKeyOff(wav, m) {
  const base = basename(wav).replace(/\.(wav|flac|aiff?)$/i, '').replace(/\s*keyoff$/i, '') + SUFFIX;
  const bundleDir = join(LIB, `${base.replace(/[<>:"/\\|?*]+/g, '').trim()}.jmi`);
  const instPath = join(bundleDir, 'instrument.json');
  if (!existsSync(instPath)) throw new Error(`no instrument called "${base}" in the library — render and slice its main pass first`);
  const inst = JSON.parse(readFileSync(instPath, 'utf8'));

  const { channels, sampleRate: rate } = await decodeAudioFile(wav);
  const n = channels[0].length;
  const mono = new Float32Array(n);
  for (const ch of channels) for (let i = 0; i < n; i++) mono[i] += ch[i] / channels.length;
  let peak = 0;
  for (let i = 0; i < n; i++) peak = Math.max(peak, Math.abs(mono[i]));
  const thr = Math.max(peak * 0.002, 2e-4);

  const fit = SCALE ? { offsetSec: 0, scale: SCALE, onsets: 0, fit: 1 } : fitGrid(mono, rate, m.events);
  if (!SCALE && fit.fit < 0.8) {
    throw new Error(`only ${(fit.fit * 100).toFixed(0)}% of the ${fit.onsets} attacks line up with the key-off grid — wrong manifest for this render`);
  }
  const atSec = (sec) => Math.round((fit.offsetSec + sec * fit.scale) * rate);

  // the level the main pass was built at: the release must sit under the body by the same amount
  const trimDb = inst.meta?.renderTrimDb;
  if (trimDb == null) throw new Error('that instrument was built before key-off support (no meta.renderTrimDb) — re-slice its main pass first');
  const trim = dbToGain(trimDb);

  const report = { name: base, category: 'release pass', samples: 0, bytes: 0, trimmed: 0, fadedIn: 0, missed: 0, missedList: [],
    grid: `start ${fit.offsetSec.toFixed(2)}s · time scale ${fit.scale.toFixed(4)} · ${fit.onsets} attacks found, ${(fit.fit * 100).toFixed(0)}% on the grid` };
  if (DRY) { report.samples = m.events.length; return report; }

  const fadeInMs = Number(arg('keyoff-fade', 8));   // the cut starts mid-ring: fade it in
  const byNote = new Map();
  let verified = false;
  for (let k = 0; k < m.events.length; k++) {
    const e = m.events[k];
    const relAt = e.releaseAt ?? e.at + e.hold;
    const start = Math.max(0, atSec(relAt) - Math.round(0.002 * rate));
    const next = m.events[k + 1];
    const end = next ? atSec(next.at) - Math.round(0.02 * rate) : n;
    let ch = slice(channels, start, end);
    if (ch[0].length < rate * 0.1) continue;
    applyGain(ch, trim);
    const f = Math.min(ch[0].length, Math.round((rate * fadeInMs) / 1000));
    for (const c of ch) for (let i = 0; i < f; i++) c[i] *= 0.5 - 0.5 * Math.cos((Math.PI * i) / f);
    if (trimSilenceTail(ch, rate)) report.trimmed++;
    fadeTail(ch, Math.round((rate * TAIL_FADE_MS) / 1000));
    const level = rmsDb(ch, rate, LEVEL_WINDOW_MS);
    const bits = inst.meta?.bitDepth ?? 16;
    const file = `samples/rel_${noteName(e.note)}_v${e.layer}_rr1.${FORMAT}`;
    let buf;
    if (FORMAT === 'flac') {
      buf = await encodeFlac(ch, rate, bits);
      if (!verified) {
        const worst = await verifyFlac(buf, ch, bits);
        if (worst !== 0) throw new Error(`FLAC encoding was not lossless (worst ${worst} steps)`);
        verified = true;
      }
    } else {
      buf = encodeWav({ sampleRate: rate, bitDepth: bits, channels: ch });
    }
    writeFileSync(join(bundleDir, file), buf);
    report.bytes += buf.length;
    report.samples++;
    const p = peakOf(ch);
    const rr = { file, peakDb: p > 0 ? Math.min(0, 20 * Math.log10(p)) : -120, lengthMs: (ch[0].length / rate) * 1000, loop: null };
    if (!byNote.has(e.note)) byNote.set(e.note, new Map());
    byNote.get(e.note).set(e.layer, { rr, levelDb: Math.round(level * 10) / 10, anchorVelocity: e.velocity });
  }

  // key ranges as in the main pass, velocity windows split between the rendered velocities
  const notes = [...byNote.keys()].sort((a, b) => a - b);
  const vels = m.velocityMap.map((v) => v.velocity).sort((a, b) => a - b);
  const bounds = vels.map((v, i) => (i === 0 ? 1 : Math.floor((vels[i - 1] + v) / 2) + 1));
  const releaseZones = [];
  notes.forEach((note, i) => {
    const prev = notes[i - 1], next = notes[i + 1];
    const lo = prev === undefined ? Math.min(note, m.notes[0]) : note - Math.floor((note - prev) / 2);
    const hi = next === undefined ? Math.max(note, m.notes.at(-1)) : note + Math.ceil((next - note) / 2) - 1;
    for (const [li, v] of vels.entries()) {
      const got = byNote.get(note).get(li + 1);
      if (!got) continue;
      releaseZones.push({
        rootNote: note, keyRangeLo: lo, keyRangeHi: hi,
        velRangeLo: bounds[li], velRangeHi: li + 1 < vels.length ? bounds[li + 1] - 1 : 127,
        roundRobins: [got.rr], anchorVelocity: got.anchorVelocity, levelDb: got.levelDb,
        // how much quieter the key-off is when the key was held longer: the string has already
        // decayed, so there is less left for the damper to stop
        rtDecayDb: RT_DECAY_DB,
      });
    }
  });

  inst.releaseZones = releaseZones;
  writeFileSync(instPath, JSON.stringify(inst, null, 2));
  const v = validateBundle(bundleDir);
  report.zones = releaseZones.length;
  report.errors = v.issues.filter((i) => i.level === 'error').map((i) => `${i.path}: ${i.message}`);
  report.path = bundleDir;
  return report;
}

const m = JSON.parse(readFileSync(MANIFEST, 'utf8'));
// Renders may be WAV, FLAC or AIFF — exporting FLAC from the DAW is lossless and roughly half the
// size, which matters when a single patch's render is most of a gigabyte.
const RENDER_EXT = /\.(wav|flac|aiff?)$/i;
// a key-off manifest only applies to "… keyoff" renders, and the main manifest only to the rest
const wavs = readdirSync(IN).filter((f) => RENDER_EXT.test(f)
  && (!ONLY || f.toLowerCase().includes(ONLY.toLowerCase()))
  && (m.kind === 'keyoff') === /\s*keyoff\.(wav|flac|aiff?)$/i.test(f));
if (!wavs.length) {
  console.log(`no renders (.wav/.flac/.aiff) in ${IN}`);
  process.exit(0);
}
console.log(`${wavs.length} render${wavs.length === 1 ? '' : 's'} in ${IN}${DRY ? ' (dry run)' : ''}`);
/** A file the DAW is still writing grows between two looks, or refuses to open: leave it alone. */
const stillRendering = async (p) => {
  const a = statSync(p).size;
  await new Promise((r) => setTimeout(r, 1200));
  const b = statSync(p).size;
  if (a !== b) return `still being written (${(a / 1e6).toFixed(0)} → ${(b / 1e6).toFixed(0)} MB)`;
  try {
    closeSync(openSync(p, 'r+'));
  } catch (e) {
    if (e && typeof e === 'object' && 'code' in e && (e.code === 'EBUSY' || e.code === 'EPERM')) return 'locked by the DAW';
  }
  return null;
};

for (const f of wavs) {
  try {
    const busy = await stillRendering(join(IN, f));
    if (busy) {
      console.log(`\n${f}: skipped — ${busy}`);
      continue;
    }
    const r = m.kind === 'keyoff' ? await buildKeyOff(join(IN, f), m) : await buildOne(join(IN, f), m);
    console.log(`\n${r.name} → ${r.category}${r.sustaining ? ' (sustaining, looped)' : ''}`);
    console.log(`  grid: ${r.grid}`);
    console.log(`  ${r.samples} samples${r.zones ? ` · ${r.zones} zones` : ''} · ${(r.bytes / 1e6).toFixed(0)} MB · tails cleaned ${r.trimmed} · de-clicked ${r.fadedIn}`);
    if (r.missed) console.log(`  WARNING ${r.missed} notes had no detectable attack (silent patch range?): ${r.missedList?.join(', ')}`);
    for (const e of r.errors ?? []) console.log('  ERROR', e);
  } catch (e) {
    console.log(`\n${f}: FAILED — ${e instanceof Error ? e.message : String(e)}`);
  }
}
