import { describe, expect, it } from 'vitest';
import { dbToLin } from '../src/engine/dsp/level';
import { findAttackStart, findSilenceEnd, trimCapture } from '../src/engine/dsp/silence';
import { detectSoundType } from '../src/engine/processing';

const SR = 8000;

function tone(seconds: number, freq: number, amp: number, sr = SR): Float32Array {
  const out = new Float32Array(Math.round(seconds * sr));
  for (let i = 0; i < out.length; i++) out[i] = amp * Math.sin((2 * Math.PI * freq * i) / sr);
  return out;
}

function silence(seconds: number, sr = SR): Float32Array {
  return new Float32Array(Math.round(seconds * sr));
}

function concat(...parts: Float32Array[]): Float32Array {
  const n = parts.reduce((a, p) => a + p.length, 0);
  const out = new Float32Array(n);
  let pos = 0;
  for (const p of parts) {
    out.set(p, pos);
    pos += p.length;
  }
  return out;
}

describe('silence detection (§3.5: −60 dBFS / 500 ms)', () => {
  it('finds the silence end after the tone stops', () => {
    const sig = concat(tone(1, 440, 0.5), silence(1));
    const end = findSilenceEnd([sig], SR);
    expect(end).not.toBeNull();
    expect(end!).toBeGreaterThanOrEqual(SR * 0.95);
    expect(end!).toBeLessThanOrEqual(SR * 1.05);
  });

  it('returns null while the signal is still ringing', () => {
    expect(findSilenceEnd([tone(2, 440, 0.5)], SR)).toBeNull();
  });

  it('ignores quiet gaps shorter than the 500 ms hold', () => {
    const sig = concat(tone(0.5, 440, 0.5), silence(0.3), tone(0.5, 440, 0.5), silence(1));
    const end = findSilenceEnd([sig], SR);
    expect(end!).toBeGreaterThan(SR * 1.2);
  });

  it('finds the attack start above −60 dBFS', () => {
    const sig = concat(silence(0.5), tone(1, 440, 0.5));
    const start = findAttackStart([sig]);
    expect(start!).toBeGreaterThanOrEqual(SR * 0.5 - 5);
  });
});

describe('trimCapture (§3.5)', () => {
  it('trims leading silence keeping a 5 ms pre-attack pad', () => {
    const raw = concat(silence(1), tone(0.5, 440, 0.5), silence(1));
    const { channels, trimmedLeading } = trimCapture([raw], SR);
    const padSamples = Math.round(0.005 * SR);
    expect(trimmedLeading).toBeGreaterThanOrEqual(SR - padSamples - 4);
    expect(trimmedLeading).toBeLessThanOrEqual(SR);
    // pad region should be quiet
    const pad = channels[0].subarray(0, padSamples - 2);
    for (const s of pad) expect(Math.abs(s)).toBeLessThan(dbToLin(-60));
  });

  it('applies a 20 ms fade-out ending at zero', () => {
    const raw = concat(tone(1, 440, 0.5), silence(1));
    const { channels } = trimCapture([raw], SR);
    const out = channels[0];
    expect(Math.abs(out[out.length - 1])).toBeLessThan(1e-4);
  });

  it('enforces the 30 s hard cap', () => {
    const raw = [tone(35, 100, 0.5)];
    const { channels } = trimCapture(raw, SR, { endAt: raw[0].length });
    expect(channels[0].length).toBeLessThanOrEqual(30 * SR + Math.round(0.005 * SR));
  });

  it('never changes gain of the kept audio (no normalization)', () => {
    const raw = concat(silence(0.2), tone(1, 440, 0.123), silence(1));
    const { channels } = trimCapture([raw], SR);
    let peak = 0;
    for (const s of channels[0]) peak = Math.max(peak, Math.abs(s));
    expect(peak).toBeGreaterThan(0.12);
    expect(peak).toBeLessThanOrEqual(0.1231);
  });
});

describe('auto sound-type detection (§3.4)', () => {
  it('classifies a held tone as sustaining', () => {
    expect(detectSoundType([tone(9, 220, 0.5)], SR)).toBe('sustaining');
  });

  it('classifies an exponential decay as decaying', () => {
    const sig = tone(9, 220, 0.5);
    for (let i = 0; i < sig.length; i++) sig[i] *= Math.exp(-i / SR / 0.8);
    expect(detectSoundType([sig], SR)).toBe('decaying');
  });
});
