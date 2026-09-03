/**
 * Silence detection + capture trimming per spec §3.5 (pure, Node-safe).
 *
 * Decaying: record until signal below −60 dBFS for 500 ms, hard cap 30 s.
 * Trim leading silence keeping a 5 ms pre-attack pad; fade out last 20 ms.
 */

import { dbToLin, mixToMono } from './level';

export const SILENCE_THRESHOLD_DB = -60;
export const SILENCE_HOLD_MS = 500;
export const HARD_CAP_S = 30;
export const PRE_ATTACK_PAD_MS = 5;
export const FADE_OUT_MS = 20;

/**
 * Find the sample index at which the take ends: the first point where the
 * signal has stayed below threshold for holdMs. Returns the end index
 * (exclusive) to keep, or the hard cap, whichever comes first.
 * Returns null if the signal never went silent within the buffer
 * (caller decides: still capturing, or cap reached).
 */
export function findSilenceEnd(
  channels: Float32Array[],
  sampleRate: number,
  opts: { thresholdDb?: number; holdMs?: number } = {},
): number | null {
  const threshold = dbToLin(opts.thresholdDb ?? SILENCE_THRESHOLD_DB);
  const holdSamples = Math.round(((opts.holdMs ?? SILENCE_HOLD_MS) / 1000) * sampleRate);
  const mono = mixToMono(channels);
  let quietRun = 0;
  for (let i = 0; i < mono.length; i++) {
    if (Math.abs(mono[i]) < threshold) {
      quietRun++;
      if (quietRun >= holdSamples) return i - holdSamples + 1;
    } else {
      quietRun = 0;
    }
  }
  return null;
}

/** First sample index at which the signal exceeds the threshold, or null. */
export function findAttackStart(
  channels: Float32Array[],
  opts: { thresholdDb?: number } = {},
): number | null {
  const threshold = dbToLin(opts.thresholdDb ?? SILENCE_THRESHOLD_DB);
  const mono = mixToMono(channels);
  for (let i = 0; i < mono.length; i++) {
    if (Math.abs(mono[i]) >= threshold) return i;
  }
  return null;
}

export interface TrimResult {
  channels: Float32Array[];
  /** samples removed from the start of the raw capture */
  trimmedLeading: number;
}

/**
 * Trim a completed capture: cut leading silence (keep PRE_ATTACK_PAD_MS pad),
 * cut everything after the silence end point, apply a linear fade over the
 * last FADE_OUT_MS. Never modifies gain elsewhere (no normalization, §3.5).
 */
export function trimCapture(
  channels: Float32Array[],
  sampleRate: number,
  opts: { fadeOut?: boolean; endAt?: number | null } = {},
): TrimResult {
  const padSamples = Math.round((PRE_ATTACK_PAD_MS / 1000) * sampleRate);
  const attack = findAttackStart(channels);
  const start = attack === null ? 0 : Math.max(0, attack - padSamples);

  const capSamples = HARD_CAP_S * sampleRate;
  let end = opts.endAt ?? findSilenceEnd(channels, sampleRate) ?? channels[0].length;
  end = Math.min(end, channels[0].length, start + capSamples);
  if (end <= start) end = Math.min(start + 1, channels[0].length);

  const out = channels.map((ch) => ch.slice(start, end));

  if (opts.fadeOut !== false) {
    const fadeSamples = Math.min(Math.round((FADE_OUT_MS / 1000) * sampleRate), out[0].length);
    const n = out[0].length;
    for (const ch of out) {
      for (let i = 0; i < fadeSamples; i++) {
        const idx = n - fadeSamples + i;
        ch[idx] *= 1 - (i + 1) / fadeSamples;
      }
    }
  }
  return { channels: out, trimmedLeading: start };
}
