// audio.mjs — decode (WAV / FLAC), probe headers, resample/retune, gain, channel layout, pitch estimate.
import { readFileSync, openSync, readSync, closeSync, statSync } from 'node:fs';
import { FLACDecoder } from '@wasm-audio-decoders/flac';

// ------------------------------------------------------------------ probing
/** Cheap header read: { sampleRate, channels, frames } without decoding audio. */
export function probeAudio(path) {
  const ext = path.toLowerCase().slice(path.lastIndexOf('.'));
  const fd = openSync(path, 'r');
  try {
    const size = statSync(path).size;
    const head = Buffer.alloc(Math.min(size, 65536));
    readSync(fd, head, 0, head.length, 0);
    if (ext === '.flac' || head.toString('ascii', 0, 4) === 'fLaC') return probeFlac(head);
    return probeWav(head, size);
  } finally {
    closeSync(fd);
  }
}

function probeFlac(b) {
  if (b.toString('ascii', 0, 4) !== 'fLaC') throw new Error('not FLAC');
  // first metadata block must be STREAMINFO
  const type = b[4] & 0x7f;
  if (type !== 0) throw new Error('FLAC: STREAMINFO not first');
  const o = 8;
  const sampleRate = (b[o + 10] << 12) | (b[o + 11] << 4) | (b[o + 12] >> 4);
  const channels = ((b[o + 12] >> 1) & 0x7) + 1;
  const bits = (((b[o + 12] & 1) << 4) | (b[o + 13] >> 4)) + 1;
  const frames = ((b[o + 13] & 0x0f) * 2 ** 32) + b.readUInt32BE(o + 14);
  return { sampleRate, channels, frames, bitDepth: bits, kind: 'flac' };
}

function wavChunks(buf, fileSize) {
  if (buf.toString('ascii', 0, 4) !== 'RIFF' || buf.toString('ascii', 8, 12) !== 'WAVE') throw new Error('not WAV');
  const chunks = [];
  let pos = 12;
  while (pos + 8 <= buf.length) {
    const id = buf.toString('ascii', pos, pos + 4);
    let size = buf.readUInt32LE(pos + 4);
    if (size > fileSize - pos - 8) size = fileSize - pos - 8; // streamed / bogus sizes
    chunks.push({ id, size, offset: pos + 8 });
    pos += 8 + size + (size & 1);
  }
  return chunks;
}

function parseFmt(buf, c) {
  let audioFormat = buf.readUInt16LE(c.offset);
  const channels = buf.readUInt16LE(c.offset + 2);
  const sampleRate = buf.readUInt32LE(c.offset + 4);
  const bitDepth = buf.readUInt16LE(c.offset + 14);
  if (audioFormat === 0xfffe && c.size >= 26) audioFormat = buf.readUInt16LE(c.offset + 24); // extensible subformat
  return { audioFormat, channels, sampleRate, bitDepth };
}

function parseSmpl(buf, c) {
  const n = buf.readUInt32LE(c.offset + 28);
  if (n < 1 || c.size < 36 + 24) return null;
  const o = c.offset + 36;
  const start = buf.readUInt32LE(o + 8), end = buf.readUInt32LE(o + 12);
  return end > start ? { start, end: end + 1 } : null; // smpl end is inclusive
}

function probeWav(head, fileSize) {
  const chunks = wavChunks(head, fileSize);
  const fmtC = chunks.find((c) => c.id === 'fmt ');
  const dataC = chunks.find((c) => c.id === 'data');
  if (!fmtC || !dataC) throw new Error('WAV missing fmt/data');
  const fmt = parseFmt(head, fmtC);
  const frames = Math.floor(dataC.size / (fmt.channels * (fmt.bitDepth / 8)));
  return { sampleRate: fmt.sampleRate, channels: fmt.channels, frames, bitDepth: fmt.bitDepth, kind: 'wav' };
}

// ------------------------------------------------------------------ decoding
let flacDecoder = null;
async function decodeFlac(buf) {
  if (!flacDecoder) { flacDecoder = new FLACDecoder(); await flacDecoder.ready; }
  const r = await flacDecoder.decodeFile(new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength));
  await flacDecoder.reset();
  if (!r.channelData?.length) throw new Error('FLAC decode produced no audio');
  return { sampleRate: r.sampleRate, channels: r.channelData.map((c) => new Float32Array(c)), loop: null };
}

function decodeWav(buf) {
  const chunks = wavChunks(buf, buf.length);
  const fmtC = chunks.find((c) => c.id === 'fmt ');
  const dataC = chunks.find((c) => c.id === 'data');
  const smplC = chunks.find((c) => c.id === 'smpl');
  if (!fmtC || !dataC) throw new Error('WAV missing fmt/data');
  const { audioFormat, channels: nch, sampleRate, bitDepth } = parseFmt(buf, fmtC);
  const bps = bitDepth / 8;
  const frames = Math.floor(dataC.size / (nch * bps));
  const out = Array.from({ length: nch }, () => new Float32Array(frames));
  let p = dataC.offset;
  const isFloat = audioFormat === 3;
  for (let i = 0; i < frames; i++) {
    for (let c = 0; c < nch; c++) {
      let s;
      if (isFloat && bitDepth === 32) s = buf.readFloatLE(p);
      else if (isFloat && bitDepth === 64) s = buf.readDoubleLE(p);
      else if (bitDepth === 16) s = buf.readInt16LE(p) / 32768;
      else if (bitDepth === 24) { let v = buf[p] | (buf[p + 1] << 8) | (buf[p + 2] << 16); if (v & 0x800000) v -= 0x1000000; s = v / 8388608; }
      else if (bitDepth === 32) s = buf.readInt32LE(p) / 2147483648;
      else if (bitDepth === 8) s = (buf[p] - 128) / 128;
      else throw new Error(`WAV: unsupported bit depth ${bitDepth}`);
      out[c][i] = s;
      p += bps;
    }
  }
  const loop = smplC ? parseSmpl(buf, smplC) : null;
  return { sampleRate, channels: out, loop: loop && loop.end <= frames ? loop : null };
}

/** Full decode → { sampleRate, channels: Float32Array[], loop: {start,end}|null } */
export async function decodeAudioFile(path) {
  const buf = readFileSync(path);
  if (buf.toString('ascii', 0, 4) === 'fLaC') return decodeFlac(buf);
  return decodeWav(buf);
}

// ------------------------------------------------------------------ DSP
const SINC_TAPS = 24;      // each side
const SINC_PHASES = 512;
const sincTables = new Map();
function sincTable(cutoff) {
  const key = cutoff.toFixed(5);
  if (sincTables.has(key)) return sincTables.get(key);
  const width = SINC_TAPS * 2;
  const t = new Float32Array(SINC_PHASES * width);
  for (let ph = 0; ph < SINC_PHASES; ph++) {
    const frac = ph / SINC_PHASES;
    let sum = 0;
    for (let k = 0; k < width; k++) {
      const x = k - SINC_TAPS + 1 - frac;           // distance from output position
      const s = x === 0 ? 1 : Math.sin(Math.PI * cutoff * 2 * x) / (Math.PI * cutoff * 2 * x);
      const w = 0.35875 - 0.48829 * Math.cos(2 * Math.PI * (k + 1 - frac) / width) + 0.14128 * Math.cos(4 * Math.PI * (k + 1 - frac) / width) - 0.01168 * Math.cos(6 * Math.PI * (k + 1 - frac) / width);
      const v = s * w; t[ph * width + k] = v; sum += v;
    }
    for (let k = 0; k < width; k++) t[ph * width + k] /= sum; // unity DC gain
  }
  sincTables.set(key, t);
  return t;
}

/**
 * Windowed-sinc resample. `ratio` = source frames consumed per output frame
 * (>1 shortens/raises pitch when declared at the same rate).
 */
export function resample(channels, ratio) {
  if (Math.abs(ratio - 1) < 1e-9) return channels;
  const n = channels[0].length;
  const outN = Math.max(1, Math.floor(n / ratio));
  const cutoff = 0.5 * Math.min(1, 1 / ratio) * 0.96;
  const table = sincTable(cutoff);
  const width = SINC_TAPS * 2;
  return channels.map((src) => {
    const dst = new Float32Array(outN);
    for (let m = 0; m < outN; m++) {
      const pos = m * ratio;
      const i0 = Math.floor(pos);
      const ph = Math.min(SINC_PHASES - 1, Math.round((pos - i0) * SINC_PHASES));
      const base = ph * width;
      let acc = 0;
      const start = i0 - SINC_TAPS + 1;
      for (let k = 0; k < width; k++) {
        const j = start + k;
        if (j >= 0 && j < n) acc += src[j] * table[base + k];
      }
      dst[m] = acc;
    }
    return dst;
  });
}

export function peakOf(channels) {
  let p = 0;
  for (const ch of channels) for (let i = 0; i < ch.length; i++) { const a = Math.abs(ch[i]); if (a > p) p = a; }
  return p;
}

export function applyGain(channels, g) {
  if (g === 1) return;
  for (const ch of channels) for (let i = 0; i < ch.length; i++) ch[i] *= g;
}

export function trimStart(channels, offset) {
  if (!offset || offset <= 0) return channels;
  return channels.map((c) => c.subarray(Math.min(offset, c.length - 1)));
}

export function toChannelCount(channels, want) {
  if (channels.length === want) return channels;
  if (want === 2) return [channels[0], channels[0].slice()];
  const n = channels[0].length, m = new Float32Array(n);
  for (const ch of channels) for (let i = 0; i < n; i++) m[i] += ch[i] / channels.length;
  return [m];
}

/** Rough fundamental → MIDI note (float) via normalised autocorrelation; null if unvoiced. */
export function estimateMidi(channels, rate) {
  const src = channels[0];
  const winLen = Math.min(src.length, Math.floor(rate * 0.12));
  if (winLen < rate * 0.03) return null;
  const from = Math.min(src.length - winLen, Math.floor(src.length * 0.08));
  const x = src.subarray(from, from + winLen);
  const minLag = Math.max(2, Math.floor(rate / 2200)), maxLag = Math.min(winLen - 2, Math.floor(rate / 27));
  let e0 = 0; for (let i = 0; i < winLen; i++) e0 += x[i] * x[i];
  if (e0 < 1e-6) return null;
  let best = -1, bestV = 0.55;
  for (let lag = minLag; lag <= maxLag; lag++) {
    let c = 0, e1 = 0;
    for (let i = 0; i + lag < winLen; i++) { c += x[i] * x[i + lag]; e1 += x[i + lag] * x[i + lag]; }
    const v = c / Math.sqrt(e0 * e1 + 1e-12);
    if (v > bestV) { bestV = v; best = lag; }
    else if (best > 0 && v < bestV - 0.15) break; // past the first strong peak
  }
  if (best < 0) return null;
  const f = rate / best;
  return 69 + 12 * Math.log2(f / 440);
}

// ------------------------------------------------------------------ noise reduction
function fftInPlace(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k, b = a + len / 2;
        const tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        const ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr;
      }
    }
  }
}

/**
 * Spectral noise reduction for a sampled note. The hiss fingerprint is learned per frequency bin
 * from the quiet frames of the file itself (low percentile of each bin's magnitude over time);
 * a Wiener-style mask computed on the mid signal is smoothed across frequency and time and applied
 * identically to every channel (stereo image preserved, phase untouched). Reduction is capped at
 * `maxReductionDb` so the tone is not hollowed out. Returns new channels, same length.
 */
export function denoise(channels, { maxReductionDb = 18, overSubtract = 2.0, percentile = 0.1 } = {}) {
  const N = 2048, hop = 512, nb = N / 2 + 1, len = channels[0].length;
  const win = new Float64Array(N);
  for (let i = 0; i < N; i++) win[i] = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / N);
  const frames = Math.max(1, Math.ceil((len + N) / hop));
  const pad = (ch) => { const p = new Float64Array(frames * hop + N); p.set(ch, N / 2); return p; };
  const padded = channels.map(pad);
  const re = new Float64Array(N), im = new Float64Array(N);

  // pass 1: mid-signal magnitudes per frame → per-bin noise estimate
  const mags = new Float32Array(frames * nb);
  const live = new Uint8Array(frames);
  for (let f = 0; f < frames; f++) {
    let e = 0;
    for (let i = 0; i < N; i++) {
      let m = 0;
      for (const p of padded) m += p[f * hop + i];
      m /= padded.length;
      re[i] = m * win[i]; im[i] = 0; e += m * m;
    }
    live[f] = e > 1e-12 ? 1 : 0;
    fftInPlace(re, im);
    for (let b = 0; b < nb; b++) mags[f * nb + b] = Math.hypot(re[b], im[b]);
  }
  // hiss fingerprint: average spectrum of the quietest 20 % of non-silent frames (the tail, where
  // the note has died away) — a mean, not a low percentile, so the typical hiss level is captured
  const energy = [];
  for (let f = 0; f < frames; f++) {
    if (!live[f]) continue;
    let e = 0;
    for (let b = 0; b < nb; b++) { const m = mags[f * nb + b]; e += m * m; }
    energy.push([e, f]);
  }
  if (energy.length < 8) return channels;
  energy.sort((x, y) => x[0] - y[0]);
  const quiet = energy.slice(0, Math.max(4, Math.floor(energy.length * Math.max(percentile, 0.2))));
  const noise = new Float64Array(nb);
  for (let b = 0; b < nb; b++) {
    let s = 0;
    for (const [, f] of quiet) s += mags[f * nb + b];
    noise[b] = s / quiet.length;
  }

  // pass 2: mask, smoothed in frequency (5 bins) and time (fast rise, slow fall), shared by channels
  const gMin = Math.pow(10, -maxReductionDb / 20);
  const prev = new Float64Array(nb).fill(1), raw = new Float64Array(nb), gain = new Float64Array(nb);
  const outs = channels.map(() => new Float64Array(frames * hop + N));
  const norm = new Float64Array(frames * hop + N);
  const specs = padded.map(() => ({ re: new Float64Array(N), im: new Float64Array(N) }));
  for (let f = 0; f < frames; f++) {
    for (let b = 0; b < nb; b++) {
      const m = mags[f * nb + b], nn = noise[b] * overSubtract;
      raw[b] = m > 0 ? Math.max(gMin, Math.sqrt(Math.max(0, 1 - (nn * nn) / (m * m)))) : gMin;
    }
    for (let b = 0; b < nb; b++) {
      let s = 0, c = 0;
      for (let d = -2; d <= 2; d++) { const q = b + d; if (q >= 0 && q < nb) { s += raw[q]; c++; } }
      const target = s / c;
      gain[b] = target > prev[b] ? target : prev[b] * 0.8 + target * 0.2;
      prev[b] = gain[b];
    }
    padded.forEach((p, ci) => {
      const sp = specs[ci];
      for (let i = 0; i < N; i++) { sp.re[i] = p[f * hop + i] * win[i]; sp.im[i] = 0; }
      fftInPlace(sp.re, sp.im);
      for (let b = 0; b < nb; b++) {
        sp.re[b] *= gain[b]; sp.im[b] *= gain[b];
        if (b > 0 && b < N / 2) { sp.re[N - b] *= gain[b]; sp.im[N - b] *= gain[b]; }
      }
      for (let i = 0; i < N; i++) sp.im[i] = -sp.im[i];     // inverse FFT via conjugate trick
      fftInPlace(sp.re, sp.im);
      const o = outs[ci];
      for (let i = 0; i < N; i++) o[f * hop + i] += (sp.re[i] / N) * win[i];
    });
    for (let i = 0; i < N; i++) norm[f * hop + i] += win[i] * win[i];
  }
  return outs.map((o) => {
    const r = new Float32Array(len);
    for (let i = 0; i < len; i++) { const w = norm[i + N / 2]; r[i] = w > 1e-9 ? o[i + N / 2] / w : 0; }
    return r;
  });
}
