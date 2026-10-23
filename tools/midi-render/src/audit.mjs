#!/usr/bin/env node
/**
 * audit.mjs — score a .jmi bundle against the things players actually notice.
 *
 * Every check here is one of the tells pro keyboardists use in blind tests to call an instrument
 * "cheap": notes that do not ring out, audible looping, aliasing from stretched samples, velocity
 * layers that are "the same sample but quieter", a narrow stereo image, a squashed dynamic range,
 * and clipping baked into the recording. They are all measurable, so they can be caught before an
 * instrument ships instead of on stage.
 *
 *   node src/audit.mjs [bundle.jmi ...]        (defaults to every bundle in the Sanctuary Library)
 */
import { readFileSync, readdirSync } from 'node:fs';
import { join, basename } from 'node:path';
import { homedir } from 'node:os';
import { decodeAudioFile } from '../../sfz-import/src/audio.mjs';

const LIB = join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library');
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const nn = (m) => `${NAMES[m % 12]}${Math.floor(m / 12) - 1}`;
const db = (x) => (x > 0 ? 20 * Math.log10(x) : -120);

const rms = (ch, from, to) => {
  let e = 0, n = 0;
  for (const c of ch) for (let i = Math.max(0, from); i < Math.min(c.length, to); i++) { e += c[i] * c[i]; n++; }
  return n ? Math.sqrt(e / n) : 0;
};

/**
 * Brightness as players hear it: how much energy sits above 2 kHz relative to below 500 Hz, in dB.
 * Spectral centroid is the obvious measure and it is the wrong one here — hitting a piano harder
 * grows the fundamental as well as the highs, which drags the centroid DOWN even as the note gets
 * audibly brighter. The high/low tilt does not have that problem.
 */
function tiltDb(mono, rate, from) {
  const s = spectrum(mono, rate, from);
  if (!s) return null;
  let hi = 0, lo = 0;
  for (const { hz, mag } of s) {
    if (hz > 2000 && hz < 12000) hi += mag * mag;
    else if (hz > 80 && hz < 500) lo += mag * mag;
  }
  return lo > 0 && hi > 0 ? 10 * Math.log10(hi / lo) : null;
}

/** Brightness: the centre of gravity of the spectrum, in Hz. Rises with velocity on a real piano. */
function centroid(mono, rate, from, len) {
  const N = 4096;
  if (from + N > mono.length) return 0;
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = mono[from + i] * (0.5 - 0.5 * Math.cos((2 * Math.PI * i) / N));
  // in-place radix-2 FFT
  for (let i = 1, j = 0; i < N; i++) {
    let bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len2 = 2; len2 <= N; len2 <<= 1) {
    const ang = (-2 * Math.PI) / len2, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < N; i += len2) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len2 / 2; k++) {
        const a = i + k, b = a + len2 / 2;
        const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
  let num = 0, den = 0;
  for (let i = 1; i < N / 2; i++) {
    const mag = Math.hypot(re[i], im[i]);
    num += mag * ((i * rate) / N);
    den += mag;
  }
  return den > 0 ? num / den : 0;
}

/** Magnitude spectrum of a 4096-sample window, as {hz, mag} bins. */
function spectrum(mono, rate, from) {
  const N = 4096;
  if (from + N > mono.length) return null;
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = mono[from + i] * (0.5 - 0.5 * Math.cos((2 * Math.PI * i) / N));
  for (let i = 1, j = 0; i < N; i++) {
    let bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= N; len <<= 1) {
    const ang = (-2 * Math.PI) / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < N; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
  const out = [];
  for (let i = 1; i < N / 2; i++) out.push({ hz: (i * rate) / N, mag: Math.hypot(re[i], im[i]) });
  return out;
}

/** Are these two takes the same audio at different gain? 1.0 = identical waveform. */
function sameRecording(a, b, rate) {
  const n = Math.min(a.length, b.length, Math.round(rate * 0.25));
  if (n < 1000) return 0;
  let sa = 0, sb = 0;
  for (let i = 0; i < n; i++) { sa += a[i] * a[i]; sb += b[i] * b[i]; }
  if (sa <= 0 || sb <= 0) return 0;
  const ga = Math.sqrt(sa / n), gb = Math.sqrt(sb / n);
  let dot = 0;
  for (let i = 0; i < n; i++) dot += (a[i] / ga) * (b[i] / gb);
  return Math.abs(dot / n);
}

async function audit(dir) {
  const inst = JSON.parse(readFileSync(join(dir, 'instrument.json'), 'utf8'));
  const name = basename(dir).replace(/\.jmi$/, '');
  const byNote = new Map();
  for (const z of inst.zones) {
    if (!byNote.has(z.rootNote)) byNote.set(z.rootNote, []);
    byNote.get(z.rootNote).push(z);
  }
  const roots = [...byNote.keys()].sort((a, b) => a - b);
  // three probe notes: bass, middle, treble
  const probes = [roots[Math.floor(roots.length * 0.1)], roots[Math.floor(roots.length * 0.5)], roots[Math.floor(roots.length * 0.85)]];

  const out = { name, zones: inst.zones.length, notes: roots.length, flags: [], lines: [] };

  // 1. biggest pitch shift any key has to suffer — the source of "stretched sample" aliasing
  let maxShift = 0;
  for (const z of inst.zones) maxShift = Math.max(maxShift, Math.abs(z.keyRangeLo - z.rootNote), Math.abs(z.keyRangeHi - z.rootNote));
  out.lines.push(`pitch shift: at most ±${maxShift} semitone${maxShift === 1 ? '' : 's'} from a recorded note`);
  if (maxShift > 2) out.flags.push(`keys are stretched up to ${maxShift} semitones — that is where "aliasing / crackle in the top end" comes from`);

  // 2. looping on a decaying instrument = the "I can hear it looping" tell
  const looped = inst.zones.filter((z) => z.roundRobins.some((r) => r.loop)).length;
  out.lines.push(`loops: ${looped} of ${inst.zones.length} zones${inst.soundType === 'decaying' && looped ? ' (on a DECAYING instrument)' : ''}`);
  if (inst.soundType === 'decaying' && looped) out.flags.push('a decaying instrument should not loop — looping is what players hear as "cheap"');

  // 3. per-probe-note checks
  let anyDupLayers = 0, totalPairs = 0;
  for (const root of probes) {
    const zs = byNote.get(root).slice().sort((a, b) => (a.anchorVelocity ?? a.velRangeLo) - (b.anchorVelocity ?? b.velRangeLo));
    const bufs = [];
    for (const z of zs) {
      const a = await decodeAudioFile(join(dir, z.roundRobins[0].file));
      const n = a.channels[0].length;
      const mono = new Float32Array(n);
      for (const c of a.channels) for (let i = 0; i < n; i++) mono[i] += c[i] / a.channels.length;
      bufs.push({ z, a, mono, rate: a.sampleRate, n });
    }
    const b0 = bufs[0], bl = bufs[bufs.length - 1];

    // does the note ring out, or is it cut while still sounding?
    const tailDb = db(rms(bl.a.channels, bl.n - Math.round(bl.rate * 0.2), bl.n));
    const peakDb = db(Math.max(...bl.a.channels.map((c) => Math.max(...Array.from({ length: 200 }, (_, k) => Math.abs(c[Math.floor((k * c.length) / 200)]))))));
    const lenSec = bl.n / bl.rate;
    out.lines.push(`${nn(root).padEnd(4)} loudest layer: ${lenSec.toFixed(1)} s long, ends ${(tailDb - peakDb).toFixed(0)} dB under its peak`);
    if (tailDb - peakDb > -35) out.flags.push(`${nn(root)} is still sounding when the sample ends (${(tailDb - peakDb).toFixed(0)} dB under peak) — "the notes don't ring out"`);

    // dynamic range across the velocity layers
    const soft = db(rms(b0.a.channels, 0, Math.round(b0.rate * 0.3)));
    const loud = db(rms(bl.a.channels, 0, Math.round(bl.rate * 0.3)));
    out.lines.push(`${nn(root).padEnd(4)} dynamic range softest→loudest: ${(loud - soft).toFixed(1)} dB`);
    if (loud - soft < 20) out.flags.push(`${nn(root)} spans only ${(loud - soft).toFixed(0)} dB from softest to loudest — a real piano spans 30–40 dB`);

    // does the TIMBRE change with velocity, or is it only louder?
    const tSoft = tiltDb(b0.mono, b0.rate, Math.round(b0.rate * 0.05));
    const tLoud = tiltDb(bl.mono, bl.rate, Math.round(bl.rate * 0.05));
    const opens = tSoft != null && tLoud != null ? tLoud - tSoft : null;
    out.lines.push(`${nn(root).padEnd(4)} brightness (>2 kHz vs <500 Hz) soft→loud: ${tSoft?.toFixed(1) ?? '–'} dB → ${tLoud?.toFixed(1) ?? '–'} dB (${opens != null ? (opens >= 0 ? '+' : '') + opens.toFixed(1) + ' dB' : 'n/a'})`);
    if (opens != null && opens < 3) out.flags.push(`${nn(root)} opens up only ${opens.toFixed(1)} dB when hit harder — "it's the same sample, just quieter"`);

    // are neighbouring layers actually different recordings?
    for (let i = 0; i + 1 < bufs.length; i++) {
      const c = sameRecording(bufs[i].mono, bufs[i + 1].mono, bufs[i].rate);
      totalPairs++;
      if (c > 0.97) anyDupLayers++;
    }

    // stereo width: 1.0 means the two channels are the same signal
    if (bl.a.channels.length === 2) {
      const [l, r] = bl.a.channels;
      let num = 0, dl = 0, dr = 0;
      const n = Math.min(l.length, Math.round(bl.rate * 2));
      for (let i = 0; i < n; i++) { num += l[i] * r[i]; dl += l[i] * l[i]; dr += r[i] * r[i]; }
      const corr = dl > 0 && dr > 0 ? num / Math.sqrt(dl * dr) : 1;
      out.lines.push(`${nn(root).padEnd(4)} stereo correlation: ${corr.toFixed(2)} ${corr > 0.98 ? '(effectively mono)' : corr > 0.8 ? '(narrow)' : '(wide)'}`);
      if (corr > 0.98) out.flags.push(`${nn(root)} is effectively mono — "there's a narrowness to it, not a wide sound"`);
    }

    // clipping baked into the recording
    let clipped = 0;
    for (const c of bl.a.channels) for (let i = 0; i < c.length; i++) if (Math.abs(c[i]) >= 0.999) clipped++;
    if (clipped > 8) out.flags.push(`${nn(root)} has ${clipped} full-scale samples — clipping is baked into the recording`);
  }

  if (totalPairs) {
    out.lines.push(`layer pairs that are the same recording at a different gain: ${anyDupLayers} of ${totalPairs}`);
    if (anyDupLayers / totalPairs > 0.3) {
      out.flags.push(`${anyDupLayers} of ${totalPairs} neighbouring layers are the SAME take at a different level — the source patch has fewer real layers than we rendered`);
    }
  }
  return out;
}

const dirs = process.argv.length > 2
  ? process.argv.slice(2)
  : readdirSync(LIB).filter((d) => d.endsWith('.jmi')).map((d) => join(LIB, d));

for (const d of dirs) {
  const r = await audit(d);
  console.log(`\n=== ${r.name} — ${r.zones} zones, ${r.notes} notes ===`);
  for (const l of r.lines) console.log('  ' + l);
  if (r.flags.length === 0) console.log('  ✓ nothing a player would call cheap');
  else for (const f of r.flags) console.log('  ⚠ ' + f);
}
