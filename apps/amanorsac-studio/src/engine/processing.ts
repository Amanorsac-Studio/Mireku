/**
 * Capture processing pipeline (§3.5) — pure, Node-safe.
 * Raw capture → trimmed/faded (decaying) or loop-detected (sustaining)
 * buffers + metadata. Never normalizes; peakDb is stored per file.
 */

import type { LoopRegion, SoundType } from '@jm/jmi-format/web';
import { detectLoop } from './dsp/loop';
import { isEffectivelyMono, peakDb, rms, mixToMono } from './dsp/level';
import { trimCapture } from './dsp/silence';

export const SUSTAIN_HOLD_S = 12;

export interface ProcessedTake {
  channels: Float32Array[];
  peakDb: number;
  lengthMs: number;
  loop: LoopRegion | null;
  trimmedLeading: number;
  /** stereo capture that nulls to mono below −60 dB (§3.5 fold check) */
  monoFoldCandidate: boolean;
}

export function processCapture(
  raw: Float32Array[],
  sampleRate: number,
  soundType: SoundType,
): ProcessedTake {
  if (soundType === 'decaying') {
    const { channels, trimmedLeading } = trimCapture(raw, sampleRate);
    return {
      channels,
      peakDb: peakDb(channels),
      lengthMs: (channels[0].length / sampleRate) * 1000,
      loop: null,
      trimmedLeading,
      monoFoldCandidate: isEffectivelyMono(channels),
    };
  }
  // Sustaining: trim leading silence only (no end trim — keep the 12 s hold
  // and its release tail), then detect a loop in the 4–10 s window.
  const { channels, trimmedLeading } = trimCapture(raw, sampleRate, {
    endAt: raw[0].length,
    fadeOut: true,
  });
  const loop = detectLoop(channels, sampleRate);
  return {
    channels,
    peakDb: peakDb(channels),
    lengthMs: (channels[0].length / sampleRate) * 1000,
    loop,
    trimmedLeading,
    monoFoldCandidate: isEffectivelyMono(channels),
  };
}

/**
 * Auto sound-type detection (§3.4): if the level at 8 s is still within
 * 1.5 dB of its 3 s value, the source sustains; otherwise it decays.
 * Measured as short-window RMS around each probe point.
 */
export function detectSoundType(channels: Float32Array[], sampleRate: number): SoundType {
  const mono = mixToMono(channels);
  const win = Math.round(sampleRate * 0.2);
  const at = (sec: number) => {
    const c = Math.round(sec * sampleRate);
    return rms(mono, c - win / 2, c + win / 2);
  };
  const r3 = at(3);
  const r8 = at(8);
  if (r3 <= 0) return 'decaying';
  if (r8 <= 0) return 'decaying';
  const dropDb = 20 * Math.log10(r3 / r8);
  return dropDb <= 1.5 ? 'sustaining' : 'decaying';
}
