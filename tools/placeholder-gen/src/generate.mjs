#!/usr/bin/env node
/**
 * placeholder-gen — synthesizes four honest placeholder .jmi bundles, one per
 * world, at the real spec: 30 notes × 4 velocity layers × 3 RR, 48 kHz / 24-bit
 * stereo. Deterministic (seeded PRNG) so CI fixtures are reproducible.
 *
 * Usage: node src/generate.mjs [--quick] [--out <dir>]
 *   --quick  8 notes × 2 layers × 2 RR (fast dev/UI iteration; not spec-size)
 */
import { mkdirSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  encodeWav, sampleFileName, defaultPlanNotes, DEFAULT_VELOCITY_MAP,
  buildDefaultRack, defaultEngineParams, validateBundle,
} from '@jm/jmi-format';

const SR = 48000;
const here = dirname(fileURLToPath(import.meta.url));
const argv = process.argv.slice(2);
const QUICK = argv.includes('--quick');
const outIdx = argv.indexOf('--out');
const OUT = resolve(outIdx >= 0 ? argv[outIdx + 1] : join(here, '..', '..', '..', 'testdata'));

// ------------------------------------------------------------------ PRNG
function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
function hashStr(s) {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); }
  return h >>> 0;
}

const midiHz = (m) => 440 * Math.pow(2, (m - 69) / 12);

// ------------------------------------------------------- synthesis per world
/**
 * Every renderer returns { channels: [L, R], loopHint: null | preferred loop
 * window in seconds }. Velocity layer (1..4) drives level AND timbre so the
 * layers are audibly distinct; rr detune/gain jitter makes RR cycling audible.
 */

// Layer peak targets (dBFS) — levels encode the velocity layers, never normalized.
const LAYER_PEAK_DB = [-18, -12, -7, -3.5];

function renderRhodes(midi, layer, rnd) {
  // 2-op FM tine: slight modulator inharmonicity, velocity→index, exp decay longer in bass
  const f = midiHz(midi) * Math.pow(2, (rnd() * 6 - 3) / 1200);
  const decay = Math.min(6, Math.max(0.8, 5.5 * Math.exp(-(midi - 21) / 44)));
  const secs = Math.min(6, decay * 1.15);
  const n = Math.floor(secs * SR);
  const L = new Float32Array(n), R = new Float32Array(n);
  const index0 = 0.6 + layer * 0.85;           // velocity → brightness
  const ratio = 1.0 + 0.0023;                   // slight inharmonicity
  const barkNoise = layer >= 3 ? 0.05 * (layer - 2) : 0;
  let phC = rnd() * 2 * Math.PI, phM = rnd() * 2 * Math.PI;
  const wC = (2 * Math.PI * f) / SR, wM = wC * ratio;
  for (let i = 0; i < n; i++) {
    const t = i / SR;
    const env = Math.exp(-t / (decay * 0.35));
    const idxEnv = index0 * Math.exp(-t / (decay * 0.12)); // brightness decays faster
    phM += wM; phC += wC;
    let s = Math.sin(phC + idxEnv * Math.sin(phM)) * env;
    if (i < 400 && barkNoise > 0) s += barkNoise * (rnd() * 2 - 1) * (1 - i / 400);
    const pan = 0.5 + (midi - 64) / 220;        // gentle keyboard pan
    L[i] = s * Math.SQRT1_2 * Math.sqrt(1 - pan) * 1.35;
    R[i] = s * Math.SQRT1_2 * Math.sqrt(pan) * 1.35;
  }
  return { channels: [L, R], loopHint: null };
}

function renderMks(midi, layer, rnd) {
  // Detuned saw stack through a one-pole LP, slow ensemble drift — sustaining
  const secs = 3.6;
  const n = Math.floor(secs * SR);
  const f = midiHz(midi);
  const L = new Float32Array(n), R = new Float32Array(n);
  const voices = 5;
  const phases = [], detunes = [], pans = [];
  for (let v = 0; v < voices; v++) {
    phases.push(rnd());
    detunes.push((v - (voices - 1) / 2) * 4.5 + (rnd() * 2 - 1)); // cents
    pans.push(v / (voices - 1));
  }
  const cutoff = 800 + layer * 900 + f * 1.5;
  const alpha = Math.min(0.99, (2 * Math.PI * cutoff) / SR / (1 + (2 * Math.PI * cutoff) / SR));
  let lpL = 0, lpR = 0;
  const attack = 0.02;
  for (let i = 0; i < n; i++) {
    const t = i / SR;
    let sl = 0, sr = 0;
    for (let v = 0; v < voices; v++) {
      const drift = Math.sin(2 * Math.PI * 0.31 * t + v * 1.7) * 3; // slow ensemble motion, cents
      const fv = f * Math.pow(2, (detunes[v] + drift) / 1200);
      phases[v] = (phases[v] + fv / SR) % 1;
      const saw = 2 * phases[v] - 1;
      sl += saw * (1 - pans[v]);
      sr += saw * pans[v];
    }
    lpL += alpha * (sl / voices - lpL);
    lpR += alpha * (sr / voices - lpR);
    const env = Math.min(1, t / attack);
    L[i] = lpL * env * 1.9;
    R[i] = lpR * env * 1.9;
  }
  return { channels: [L, R], loopHint: [1.2, 3.2] };
}

function renderFm(midi, layer, rnd) {
  // Bright 4-op-ish DX EP: carrier + two modulators (1:1 body, 14:1 clink), fast attack
  const f = midiHz(midi) * Math.pow(2, (rnd() * 6 - 3) / 1200);
  const decay = Math.min(5, Math.max(0.6, 4.2 * Math.exp(-(midi - 21) / 40)));
  const secs = Math.min(5, decay * 1.1);
  const n = Math.floor(secs * SR);
  const L = new Float32Array(n), R = new Float32Array(n);
  const idxBody = 0.4 + layer * 0.55;
  const idxClink = (0.12 + layer * 0.11);
  let p1 = rnd() * 6.28, p2 = rnd() * 6.28, p3 = rnd() * 6.28;
  const w = (2 * Math.PI * f) / SR;
  for (let i = 0; i < n; i++) {
    const t = i / SR;
    const env = Math.exp(-t / (decay * 0.3));
    const clinkEnv = Math.exp(-t / 0.06);
    p2 += w; p3 += w * 14.0; p1 += w;
    const mod = idxBody * Math.exp(-t / (decay * 0.15)) * Math.sin(p2)
              + idxClink * clinkEnv * Math.sin(p3);
    const s = Math.sin(p1 + mod) * env;
    const wob = Math.sin(2 * Math.PI * 0.7 * t + midi);
    L[i] = s * (0.72 + 0.05 * wob) * 1.3;
    R[i] = s * (0.72 - 0.05 * wob) * 1.3;
  }
  return { channels: [L, R], loopHint: null };
}

function renderStrings(midi, layer, rnd) {
  // Filtered saw ensemble pad, slow attack — sustaining
  const secs = 4.2;
  const n = Math.floor(secs * SR);
  const f = midiHz(midi);
  const L = new Float32Array(n), R = new Float32Array(n);
  const voices = 7;
  const phases = [], detunes = [];
  for (let v = 0; v < voices; v++) { phases.push(rnd()); detunes.push((v - 3) * 6 + (rnd() * 2 - 1)); }
  const cutoff = 600 + layer * 500 + f;
  const alpha = Math.min(0.99, (2 * Math.PI * cutoff) / SR / (1 + (2 * Math.PI * cutoff) / SR));
  let l1 = 0, l2 = 0, r1 = 0, r2 = 0; // 2-pole
  const attack = 0.45;
  for (let i = 0; i < n; i++) {
    const t = i / SR;
    let sl = 0, sr = 0;
    for (let v = 0; v < voices; v++) {
      const vib = Math.sin(2 * Math.PI * (4.6 + v * 0.13) * t + v) * 4;
      const fv = f * Math.pow(2, (detunes[v] + vib) / 1200);
      phases[v] = (phases[v] + fv / SR) % 1;
      const saw = 2 * phases[v] - 1;
      if (v % 2 === 0) sl += saw; else sr += saw;
    }
    l1 += alpha * (sl / 4 - l1); l2 += alpha * (l1 - l2);
    r1 += alpha * (sr / 3 - r1); r2 += alpha * (r1 - r2);
    const env = Math.min(1, t / attack);
    L[i] = l2 * env * 2.2;
    R[i] = r2 * env * 2.2;
  }
  return { channels: [L, R], loopHint: [1.6, 3.8] };
}

// ------------------------------------------------------------- processing
function peakOf(channels) {
  let p = 0;
  for (const ch of channels) for (let i = 0; i < ch.length; i++) p = Math.max(p, Math.abs(ch[i]));
  return p;
}

function scaleToDb(channels, targetDb) {
  const p = peakOf(channels);
  if (p <= 0) return;
  const g = Math.pow(10, targetDb / 20) / p;
  for (const ch of channels) for (let i = 0; i < ch.length; i++) ch[i] *= g;
}

function fadeOut(channels, ms) {
  const n = channels[0].length;
  const fadeN = Math.min(n, Math.floor((ms / 1000) * SR));
  for (const ch of channels)
    for (let i = 0; i < fadeN; i++) ch[n - 1 - i] *= i / fadeN;
}

/** Snap a time position to the nearest positive-going zero crossing of L. */
function snapToZeroCrossing(ch, idx) {
  const search = Math.min(2000, ch.length - 2);
  for (let d = 0; d < search; d++) {
    for (const j of [idx + d, idx - d]) {
      if (j > 0 && j < ch.length - 1 && ch[j - 1] <= 0 && ch[j] > 0) return j;
    }
  }
  return idx;
}

/** Pick loop points inside the hint window: snap to zero crossings, integer periods. */
function findLoop(channels, loopHint, freq) {
  const [t0, t1] = loopHint;
  const period = SR / freq;
  let start = snapToZeroCrossing(channels[0], Math.floor(t0 * SR));
  let rawEnd = Math.floor(t1 * SR);
  const periods = Math.max(1, Math.floor((rawEnd - start) / period));
  let end = snapToZeroCrossing(channels[0], start + Math.round(periods * period));
  if (end <= start + period) end = Math.min(channels[0].length - 1, start + Math.round(periods * period));
  const crossfade = Math.min(Math.floor(0.25 * SR), Math.floor((end - start) / 2)); // 250 ms equal-power
  return { startSample: start, endSample: end, crossfadeSamples: crossfade };
}

// ------------------------------------------------------------- bundle spec
const WORLDS = [
  {
    world: 'rhodes-ep', bundleName: 'JM_Placeholder_Rhodes', soundType: 'decaying',
    render: renderRhodes, presets: ['Warm Stage', 'Night Drive'],
  },
  {
    world: 'mks', bundleName: 'JM_Placeholder_MKS', soundType: 'sustaining',
    render: renderMks, presets: ['Analog Luxury', 'Royal Pad'],
  },
  {
    world: 'fm', bundleName: 'JM_Placeholder_FM', soundType: 'decaying',
    render: renderFm, presets: ['Glass Piano', 'Crystal Echo'],
  },
  {
    world: 'strings', bundleName: 'JM_Placeholder_Strings', soundType: 'sustaining',
    render: renderStrings, presets: ['Cinematic', 'Open Sky'],
  },
];

function generateBundle(spec) {
  const notes = QUICK ? defaultPlanNotes().filter((_, i) => i % 4 === 0) : defaultPlanNotes();
  const layers = QUICK ? DEFAULT_VELOCITY_MAP.filter((l) => l.layer === 2 || l.layer === 4) : DEFAULT_VELOCITY_MAP;
  const rrCount = QUICK ? 2 : 3;

  const bundlePath = join(OUT, `${spec.bundleName}.jmi`);
  if (existsSync(bundlePath)) rmSync(bundlePath, { recursive: true, force: true });
  mkdirSync(join(bundlePath, 'samples'), { recursive: true });
  mkdirSync(join(bundlePath, 'presets'), { recursive: true });

  const zones = [];
  const t0 = Date.now();
  for (let ni = 0; ni < notes.length; ni++) {
    const midi = notes[ni];
    for (const layer of layers) {
      const rrs = [];
      for (let rr = 1; rr <= rrCount; rr++) {
        const rnd = mulberry32(hashStr(`${spec.bundleName}:${midi}:${layer.layer}:${rr}`));
        const { channels, loopHint } = spec.render(midi, layer.layer, rnd);
        // per-RR randomization: ±0.5 dB (±3 cents applied inside renderers via rnd)
        const rrDb = LAYER_PEAK_DB[layer.layer - 1] + (rnd() - 0.5);
        scaleToDb(channels, rrDb);
        fadeOut(channels, 20);
        let loop = null;
        if (spec.soundType === 'sustaining' && loopHint) {
          loop = findLoop(channels, loopHint, midiHz(midi));
        }
        const file = `samples/${sampleFileName(midi, layer.layer, rr)}`;
        writeFileSync(join(bundlePath, file), encodeWav({ sampleRate: SR, bitDepth: 24, channels }));
        rrs.push({
          file,
          peakDb: Math.round(rrDb * 100) / 100,
          lengthMs: Math.round((channels[0].length / SR) * 1000),
          loop,
        });
      }
      // zone spans root ±1 st; edge zones stretch to keyboard limits
      const lo = ni === 0 ? 0 : midi - 1;
      const hi = ni === notes.length - 1 ? 127 : midi + 1;
      zones.push({
        rootNote: midi, keyRangeLo: lo, keyRangeHi: hi,
        velRangeLo: layer.lo, velRangeHi: layer.hi, roundRobins: rrs,
      });
    }
    if (ni % 6 === 0) process.stdout.write(`  ${spec.bundleName}: note ${ni + 1}/${notes.length}\r`);
  }

  const instrument = {
    formatVersion: 1,
    meta: {
      name: spec.bundleName.replace(/_/g, ' '),
      world: spec.world,
      author: 'JM placeholder-gen',
      createdAt: '2026-08-26T00:00:00Z',
      captureMode: 'auto',
      channels: 'stereo',
      sampleRate: SR,
      bitDepth: 24,
      notes: `Synthesized placeholder for the ${spec.world} world. Deterministic; replace with James's real bundle.`,
    },
    soundType: spec.soundType,
    zones,
    velocityMap: DEFAULT_VELOCITY_MAP,
    reserved: { releaseSamples: [], pedalDownZones: [] },
  };
  writeFileSync(join(bundlePath, 'instrument.json'), JSON.stringify(instrument, null, 2));

  for (const presetName of spec.presets) {
    const preset = {
      formatVersion: 1,
      name: presetName,
      tags: ['factory', 'placeholder'],
      engine: defaultEngineParams(spec.world),
      fxRack: buildDefaultRack(spec.world),
      ampEnvelope: spec.soundType === 'decaying'
        ? { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 }
        : { attackMs: 10, decayMs: 0, sustain: 100, releaseMs: 400 },
    };
    writeFileSync(join(bundlePath, 'presets', `${presetName}.json`), JSON.stringify(preset, null, 2));
  }

  const secs = ((Date.now() - t0) / 1000).toFixed(1);
  console.log(`\n  wrote ${bundlePath} (${zones.length} zones) in ${secs}s`);
  return bundlePath;
}

console.log(`placeholder-gen → ${OUT}${QUICK ? ' (quick mode)' : ''}`);
mkdirSync(OUT, { recursive: true });
const paths = WORLDS.map(generateBundle);

console.log('\nValidating…');
let failed = false;
for (const p of paths) {
  const res = validateBundle(p);
  const errs = res.issues.filter((i) => i.level === 'error');
  console.log(`  ${p}: ${res.valid ? 'VALID' : 'INVALID'} (${res.stats?.sampleCount} samples)`);
  for (const e of errs) console.log(`    ERROR ${e.path}: ${e.message}`);
  if (!res.valid) failed = true;
}
process.exit(failed ? 1 : 0);
