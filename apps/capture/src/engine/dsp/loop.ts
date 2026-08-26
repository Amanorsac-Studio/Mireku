/**
 * Loop detection for sustaining captures per spec §3.5 (pure, Node-safe).
 *
 * Strategy: autocorrelation over the stable 4–10 s window to find a strongly
 * self-similar loop length, then snap the loop start/end to zero crossings
 * with matching slope sign, and reserve a 200–500 ms equal-power crossfade.
 */

import { mixToMono } from './level';
import type { LoopRegion } from '@jm/jmi-format/web';

export const LOOP_SEARCH_START_S = 4;
export const LOOP_SEARCH_END_S = 10;
export const CROSSFADE_MIN_MS = 200;
export const CROSSFADE_MAX_MS = 500;

/** Find the nearest rising (or falling) zero crossing to `idx` within ±maxDrift. */
export function nearestZeroCrossing(
  x: Float32Array,
  idx: number,
  rising: boolean,
  maxDrift: number,
): number | null {
  for (let d = 0; d <= maxDrift; d++) {
    for (const i of d === 0 ? [idx] : [idx - d, idx + d]) {
      if (i < 1 || i >= x.length) continue;
      const a = x[i - 1];
      const b = x[i];
      if (rising ? a <= 0 && b > 0 : a >= 0 && b < 0) return i;
    }
  }
  return null;
}

/** Slope sign at a sample (true = rising). */
function slopeRising(x: Float32Array, i: number): boolean {
  return x[Math.min(i + 1, x.length - 1)] >= x[Math.max(i - 1, 0)];
}

export interface LoopDetectOptions {
  searchStartS?: number;
  searchEndS?: number;
  /** minimum loop length in seconds */
  minLoopS?: number;
  /** maximum loop length in seconds */
  maxLoopS?: number;
}

/**
 * Detect a loop region. Returns a LoopRegion (sample indices into the file)
 * or null if no sufficiently periodic region was found.
 */
export function detectLoop(
  channels: Float32Array[],
  sampleRate: number,
  opts: LoopDetectOptions = {},
): LoopRegion | null {
  const mono = mixToMono(channels);
  const startS = opts.searchStartS ?? LOOP_SEARCH_START_S;
  const endS = opts.searchEndS ?? LOOP_SEARCH_END_S;
  const winStart = Math.min(Math.round(startS * sampleRate), mono.length);
  const winEnd = Math.min(Math.round(endS * sampleRate), mono.length);
  if (winEnd - winStart < sampleRate) return null; // need ≥1 s of stable signal

  const minLag = Math.round((opts.minLoopS ?? 0.5) * sampleRate);
  const maxLag = Math.min(Math.round((opts.maxLoopS ?? 4) * sampleRate), winEnd - winStart - 1);
  if (maxLag <= minLag) return null;

  // Normalized autocorrelation of the window against itself, on a decimated
  // grid for speed, then refined at full resolution around the best lag.
  const seg = mono.subarray(winStart, winEnd);
  const decim = Math.max(1, Math.floor(sampleRate / 8000));

  const corrAt = (lag: number, step: number): number => {
    let num = 0;
    let e1 = 0;
    let e2 = 0;
    const n = seg.length - lag;
    for (let i = 0; i < n; i += step) {
      const a = seg[i];
      const b = seg[i + lag];
      num += a * b;
      e1 += a * a;
      e2 += b * b;
    }
    const denom = Math.sqrt(e1 * e2);
    return denom > 0 ? num / denom : 0;
  };

  let bestLag = -1;
  let bestCorr = -1;
  for (let lag = minLag; lag <= maxLag; lag += decim) {
    const c = corrAt(lag, decim * 4);
    if (c > bestCorr) {
      bestCorr = c;
      bestLag = lag;
    }
  }
  if (bestLag < 0 || bestCorr < 0.5) return null;

  // Refine around the coarse best lag at sample resolution.
  let refinedLag = bestLag;
  let refinedCorr = -1;
  for (let lag = Math.max(minLag, bestLag - decim * 2); lag <= Math.min(maxLag, bestLag + decim * 2); lag++) {
    const c = corrAt(lag, 1);
    if (c > refinedCorr) {
      refinedCorr = c;
      refinedLag = lag;
    }
  }
  if (refinedCorr < 0.6) return null;

  // Loop start inside the stable window; loop end one detected period
  // multiple later, as long as possible while staying inside the window.
  const periods = Math.max(1, Math.floor((winEnd - winStart - sampleRate * 0.25) / refinedLag));
  let startIdx = winStart;
  let endIdx = winStart + periods * refinedLag;
  if (endIdx >= mono.length) endIdx = winStart + refinedLag;
  if (endIdx >= mono.length) return null;

  // Snap both points to zero crossings with matching slope sign (§3.5).
  const drift = Math.round(sampleRate * 0.01);
  const sSnap = nearestZeroCrossing(mono, startIdx, true, drift);
  if (sSnap !== null) startIdx = sSnap;
  const rising = slopeRising(mono, startIdx);
  const eSnap = nearestZeroCrossing(mono, endIdx, rising, drift);
  if (eSnap !== null) endIdx = eSnap;
  if (endIdx <= startIdx + minLag / 2) return null;

  // Crossfade: 200–500 ms, clipped to fit before the loop start.
  const maxFade = Math.round((CROSSFADE_MAX_MS / 1000) * sampleRate);
  const minFade = Math.round((CROSSFADE_MIN_MS / 1000) * sampleRate);
  let crossfadeSamples = Math.min(maxFade, Math.floor((endIdx - startIdx) / 2), startIdx);
  if (crossfadeSamples < minFade) crossfadeSamples = Math.min(minFade, startIdx, endIdx - startIdx);

  return { startSample: startIdx, endSample: endIdx, crossfadeSamples };
}

/**
 * Render an equal-power crossfaded seamless loop pass (used by loop audition):
 * returns the loop body with the region before startSample crossfaded into
 * the tail, so consecutive plays of the returned buffer are seam-free.
 */
export function renderLoopBody(
  channels: Float32Array[],
  loop: LoopRegion,
): Float32Array[] {
  const { startSample, endSample, crossfadeSamples } = loop;
  const len = endSample - startSample;
  return channels.map((ch) => {
    const out = new Float32Array(len);
    out.set(ch.subarray(startSample, endSample));
    for (let i = 0; i < crossfadeSamples; i++) {
      const t = (i + 1) / crossfadeSamples;
      const gainIn = Math.sin((t * Math.PI) / 2); // equal power
      const gainOut = Math.cos((t * Math.PI) / 2);
      const tailIdx = len - crossfadeSamples + i;
      const preIdx = startSample - crossfadeSamples + i;
      const pre = preIdx >= 0 ? ch[preIdx] : 0;
      out[tailIdx] = out[tailIdx] * gainOut + pre * gainIn;
    }
    return out;
  });
}

/** Nudge loop points, keeping invariants (start < end, crossfade fits). */
export function nudgeLoop(
  loop: LoopRegion,
  deltaStart: number,
  deltaEnd: number,
  fileLength: number,
): LoopRegion {
  let startSample = Math.max(0, loop.startSample + deltaStart);
  let endSample = Math.min(fileLength, loop.endSample + deltaEnd);
  if (endSample <= startSample + 16) endSample = Math.min(fileLength, startSample + 16);
  const crossfadeSamples = Math.min(loop.crossfadeSamples, startSample, endSample - startSample);
  return { startSample, endSample, crossfadeSamples };
}
