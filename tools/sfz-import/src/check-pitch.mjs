#!/usr/bin/env node
// check-pitch.mjs — measure the pitch of every sample in .jmi bundles and compare with its root note.
//   node src/check-pitch.mjs <bundle.jmi> [...]     (defaults to every bundle in the Sanctuary Library)
// YIN on the mono mix, 0.15–0.65 s after the attack. Reports cents off the root (octave-folded) and
// octave mistakes; flags |error| > 25 cents or a wrong octave.
import { readFileSync, readdirSync } from 'node:fs';
import { join, basename } from 'node:path';
import { homedir } from 'node:os';
import { decodeAudioFile } from './audio.mjs';

const LIB = join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library');
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const noteName = (m) => `${NAMES[((m % 12) + 12) % 12]}${Math.floor(m / 12) - 1}`;

export function yin(x, rate, fmin = 24, fmax = 4500) {
  const tauMax = Math.min(Math.floor(rate / fmin), Math.floor(x.length / 2));
  const tauMin = Math.max(2, Math.floor(rate / fmax));
  const W = x.length - tauMax;
  const d = new Float64Array(tauMax + 1);
  for (let tau = 1; tau <= tauMax; tau++) {
    let s = 0;
    for (let i = 0; i < W; i++) { const v = x[i] - x[i + tau]; s += v * v; }
    d[tau] = s;
  }
  const cm = new Float64Array(tauMax + 1); cm[0] = 1;
  let run = 0;
  for (let tau = 1; tau <= tauMax; tau++) { run += d[tau]; cm[tau] = run > 0 ? (d[tau] * tau) / run : 1; }
  let best = -1;
  for (let tau = tauMin; tau < tauMax; tau++) {
    if (cm[tau] < 0.15) { while (tau + 1 < tauMax && cm[tau + 1] < cm[tau]) tau++; best = tau; break; }
  }
  if (best < 0) { let m = Infinity; for (let tau = tauMin; tau < tauMax; tau++) if (cm[tau] < m) { m = cm[tau]; best = tau; } }
  const a = cm[best - 1], b = cm[best], c = cm[best + 1] ?? b;
  const shift = (a - c) / (2 * (a - 2 * b + c) || 1);
  return { f0: rate / (best + (Number.isFinite(shift) ? shift : 0)), clarity: 1 - b };
}

function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        const nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
      }
    }
  }
}

/**
 * Piano-aware pitch: find the partials of the note and fit f_n = n·f0·√(1+B·n²) (string stiffness
 * stretches the upper partials), so a weak fundamental in the bass or a strong 2nd partial in the
 * treble does not fool it. Returns f0 or NaN.
 */
function partialPitch(mono, start, rate, expected) {
  // 1.5 s window where the note is that long; short treble samples use what they have (≥ 93 ms)
  let N = 1 << 16;
  while (N > 4096 && mono.length - start < N / 2) N >>= 1;
  const re = new Float64Array(N), im = new Float64Array(N);
  const len = Math.min(N, mono.length - start);
  if (len < N / 2) return { f0: NaN };
  for (let i = 0; i < len; i++) re[i] = mono[start + i] * (0.5 - 0.5 * Math.cos((2 * Math.PI * i) / len));
  fft(re, im);
  const mag = new Float64Array(N / 2);
  for (let i = 0; i < N / 2; i++) mag[i] = Math.hypot(re[i], im[i]);
  const hz = rate / N;
  const peakNear = (f, cents) => {
    const lo = Math.max(1, Math.floor((f * Math.pow(2, -cents / 1200)) / hz)), hi = Math.min(N / 2 - 2, Math.ceil((f * Math.pow(2, cents / 1200)) / hz));
    let b = -1;
    for (let i = lo; i <= hi; i++) if (b < 0 || mag[i] > mag[b]) b = i;
    if (b <= lo || b >= hi) return null; // edge of the window = no real peak inside
    const a = Math.log(mag[b - 1] + 1e-12), c = Math.log(mag[b] + 1e-12), d = Math.log(mag[b + 1] + 1e-12);
    return { f: (b + (0.5 * (a - d)) / (a - 2 * c + d)) * hz, m: mag[b] };
  };
  const top = Math.max(...mag.subarray(Math.floor(20 / hz), Math.floor(Math.min(12000, rate / 2.2) / hz)));
  // stage 1: strongest of partials 1–3 within ±250 cents of where they should be → first estimate
  const cands = [1, 2, 3].map((n) => ({ n, p: peakNear(expected * n, 250) })).filter((x) => x.p && x.p.m > top * 0.02);
  if (!cands.length) return { f0: NaN };
  const first = cands.sort((a, b) => b.p.m - a.p.m)[0];
  let est = first.p.f / first.n;
  // stage 2: collect partials near n·est and fit (f_n/n)² = f0² + f0²·B·n²
  const pts = [];
  for (let n = 1; n <= 12 && n * est < Math.min(12000, rate / 2.2); n++) {
    const p = peakNear(n * est * Math.sqrt(1 + 0.0004 * n * n * (est < 200 ? 1 : 0)), 45);
    if (p && p.m > top * 0.01) pts.push({ n, f: p.f });
  }
  if (pts.length >= 3) {
    const xs = pts.map((p) => p.n * p.n), ys = pts.map((p) => (p.f / p.n) ** 2);
    const mx = xs.reduce((s, x) => s + x, 0) / xs.length, my = ys.reduce((s, y) => s + y, 0) / ys.length;
    let sxy = 0, sxx = 0;
    for (let i = 0; i < xs.length; i++) { sxy += (xs[i] - mx) * (ys[i] - my); sxx += (xs[i] - mx) ** 2; }
    const slope = sxx ? sxy / sxx : 0, icpt = my - slope * mx;
    if (icpt > 0 && slope >= 0) est = Math.sqrt(icpt);
    else est = pts[0].f / pts[0].n;
  } else if (pts.length) est = pts[0].f / pts[0].n;
  return { f0: est, partials: pts.length };
}

export async function measure(file, root) {
  const { channels, sampleRate: rate } = await decodeAudioFile(file);
  const n = channels[0].length;
  const mono = new Float32Array(n);
  for (const ch of channels) for (let i = 0; i < n; i++) mono[i] += ch[i] / channels.length;
  let peak = 0, pi = 0;
  for (let i = 0; i < Math.min(n, rate); i++) if (Math.abs(mono[i]) > peak) { peak = Math.abs(mono[i]); pi = i; }
  const expected = 440 * Math.pow(2, (root - 69) / 12);
  const p = partialPitch(mono, pi + Math.round(rate * 0.08), rate, expected);
  if (!Number.isFinite(p.f0)) return { expected, f0: NaN };
  const cents = 1200 * Math.log2(p.f0 / expected);
  const oct = Math.round(cents / 1200);
  return { expected, f0: p.f0, cents: cents - oct * 1200, octave: oct, clarity: p.partials };
}

/**
 * Third opinion: harmonic-sum search. Try every f0 within ±200 cents of the root (1-cent steps) and
 * every stiffness B, score = summed log-magnitude at the first partials. Independent of peak picking.
 */
export async function measureHarmonicSum(file, root) {
  const { channels, sampleRate: rate } = await decodeAudioFile(file);
  const n = channels[0].length;
  const mono = new Float32Array(n);
  for (const ch of channels) for (let i = 0; i < n; i++) mono[i] += ch[i] / channels.length;
  let peak = 0, pi = 0;
  for (let i = 0; i < Math.min(n, rate); i++) if (Math.abs(mono[i]) > peak) { peak = Math.abs(mono[i]); pi = i; }
  const start = pi + Math.round(rate * 0.08);
  let N = 1 << 16;
  while (N > 4096 && n - start < N / 2) N >>= 1;
  const re = new Float64Array(N), im = new Float64Array(N);
  const len = Math.min(N, n - start);
  for (let i = 0; i < len; i++) re[i] = mono[start + i] * (0.5 - 0.5 * Math.cos((2 * Math.PI * i) / len));
  fft(re, im);
  const hz = rate / N;
  const lmag = new Float64Array(N / 2);
  for (let i = 0; i < N / 2; i++) lmag[i] = Math.log(Math.hypot(re[i], im[i]) + 1e-9);
  const at = (f) => { const x = f / hz, i = Math.floor(x); if (i < 1 || i >= N / 2 - 1) return -Infinity; let m = -Infinity; for (let k = i - 1; k <= i + 2; k++) m = Math.max(m, lmag[k]); return m; };
  const expected = 440 * Math.pow(2, (root - 69) / 12);
  const nyq = Math.min(12000, rate / 2.2);
  let best = { score: -Infinity, cents: NaN };
  for (const B of [0, 0.0001, 0.0003, 0.0008, 0.002, 0.005, 0.01]) {
    for (let c = -200; c <= 200; c++) {
      const f0 = expected * Math.pow(2, c / 1200);
      let s = 0, k = 0;
      for (let h = 1; h <= 10; h++) { const f = h * f0 * Math.sqrt(1 + B * h * h); if (f > nyq) break; s += at(f); k++; }
      if (!k) continue;
      s /= Math.sqrt(k);
      if (s > best.score) best = { score: s, cents: c, B };
    }
  }
  return { cents: best.cents, B: best.B };
}

export async function measureYin(file, root) {
  const { channels, sampleRate: rate } = await decodeAudioFile(file);
  const n = channels[0].length;
  const mono = new Float32Array(n);
  for (const ch of channels) for (let i = 0; i < n; i++) mono[i] += ch[i] / channels.length;
  let peak = 0, pi = 0;
  for (let i = 0; i < Math.min(n, rate); i++) if (Math.abs(mono[i]) > peak) { peak = Math.abs(mono[i]); pi = i; }
  const expected = 440 * Math.pow(2, (root - 69) / 12);
  const len = Math.round(rate * (root < 40 ? 0.6 : 0.35));
  const results = [];
  for (const startSec of [0.15, 0.4]) {
    const s = pi + Math.round(rate * startSec);
    if (s + len > n) continue;
    const r = yin(mono.subarray(s, s + len), rate, Math.max(20, expected / 2.2), Math.min(8000, expected * 2.2));
    results.push(r);
  }
  if (!results.length) return { expected, f0: NaN };
  const r = results.sort((a, b) => b.clarity - a.clarity)[0];
  const cents = 1200 * Math.log2(r.f0 / expected);
  const oct = Math.round(cents / 1200);
  return { expected, f0: r.f0, cents: cents - oct * 1200, octave: oct, clarity: r.clarity, all: results.map((x) => 1200 * Math.log2(x.f0 / expected)) };
}

if (process.argv[1]?.endsWith('check-pitch.mjs')) {
  const bundles = process.argv.length > 2 ? process.argv.slice(2) : readdirSync(LIB).filter((d) => d.endsWith('.jmi')).map((d) => join(LIB, d));
  for (const b of bundles) {
    const inst = JSON.parse(readFileSync(join(b, 'instrument.json'), 'utf8'));
    const bad = [];
    let count = 0;
    for (const z of inst.zones) for (const rr of z.roundRobins) {
      count++;
      const m = await measure(join(b, rr.file), z.rootNote);
      const flag = !Number.isFinite(m.f0) || Math.abs(m.cents) > 25 || m.octave !== 0;
      if (flag || process.env.VERBOSE) bad.push(`${basename(rr.file)} root ${noteName(z.rootNote)} keys ${noteName(z.keyRangeLo)}–${noteName(z.keyRangeHi)}: measured ${m.f0?.toFixed(1)} Hz (${m.octave ? `${m.octave > 0 ? '+' : ''}${m.octave} oct ` : ''}${m.cents >= 0 ? '+' : ''}${m.cents?.toFixed(0)} c, clarity ${m.clarity?.toFixed(2)})`);
    }
    console.log(`\n### ${basename(b)} — ${count} samples, ${bad.length} flagged`);
    for (const l of bad) console.log('  ' + l);
  }
}
