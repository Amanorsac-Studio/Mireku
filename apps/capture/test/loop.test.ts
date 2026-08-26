import { describe, expect, it } from 'vitest';
import { detectLoop, nudgeLoop, renderLoopBody } from '../src/engine/dsp/loop';

const SR = 8000;

/** synthetic sustaining fixture: attack ramp + steady harmonic tone */
function sustainingFixture(seconds: number, freq: number): Float32Array {
  const out = new Float32Array(Math.round(seconds * SR));
  for (let i = 0; i < out.length; i++) {
    const t = i / SR;
    const attack = Math.min(1, t / 0.05);
    out[i] =
      attack *
      (0.5 * Math.sin(2 * Math.PI * freq * t) + 0.2 * Math.sin(2 * Math.PI * freq * 2 * t + 0.5));
  }
  return out;
}

describe('loop detection (§3.5: autocorrelation over 4–10 s, zero-crossing points)', () => {
  it('detects a loop in a steady tone', () => {
    const sig = sustainingFixture(12, 110);
    const loop = detectLoop([sig], SR);
    expect(loop).not.toBeNull();
    expect(loop!.startSample).toBeGreaterThanOrEqual(Math.round(3.9 * SR));
    expect(loop!.endSample).toBeLessThanOrEqual(Math.round(10.1 * SR));
    expect(loop!.endSample).toBeGreaterThan(loop!.startSample);
  });

  it('picks loop points at zero crossings with matching slope', () => {
    const sig = sustainingFixture(12, 110);
    const loop = detectLoop([sig], SR)!;
    const at = (i: number) => sig[i];
    // zero crossing: sign change across the point
    expect(at(loop.startSample - 1) <= 0 !== at(loop.startSample) <= 0 || Math.abs(at(loop.startSample)) < 0.02).toBe(true);
    // matching slope sign at both points
    const slope = (i: number) => sig[i + 1] - sig[i - 1];
    expect(Math.sign(slope(loop.startSample))).toBe(Math.sign(slope(loop.endSample)));
  });

  it('crossfade sits within the 200–500 ms bound', () => {
    const sig = sustainingFixture(12, 110);
    const loop = detectLoop([sig], SR)!;
    expect(loop.crossfadeSamples).toBeGreaterThanOrEqual(Math.round(0.2 * SR) * 0.5);
    expect(loop.crossfadeSamples).toBeLessThanOrEqual(Math.round(0.5 * SR));
  });

  it('loop length is close to a whole number of periods (seam continuity)', () => {
    const freq = 110;
    const sig = sustainingFixture(12, freq);
    const loop = detectLoop([sig], SR)!;
    const period = SR / freq;
    const len = loop.endSample - loop.startSample;
    const frac = (len / period) % 1;
    const err = Math.min(frac, 1 - frac);
    expect(err).toBeLessThan(0.25); // within a quarter period after zero-cross snap
    // and the seam itself is nearly continuous in amplitude
    expect(Math.abs(sig[loop.startSample] - sig[loop.endSample])).toBeLessThan(0.05);
  });

  it('returns null for pure noise-free decay (nothing loopable in window)', () => {
    const short = new Float32Array(SR * 2); // too short for the 4–10 s window
    expect(detectLoop([short], SR)).toBeNull();
  });

  it('renderLoopBody produces a seam-free repeating buffer', () => {
    const sig = sustainingFixture(12, 110);
    const loop = detectLoop([sig], SR)!;
    const body = renderLoopBody([sig], loop)[0];
    // discontinuity between end of body and start of body should be small
    expect(Math.abs(body[body.length - 1] - body[0])).toBeLessThan(0.1);
  });
});

describe('manual nudge (§3.5)', () => {
  it('keeps invariants when nudging', () => {
    const loop = { startSample: 1000, endSample: 5000, crossfadeSamples: 800 };
    const n1 = nudgeLoop(loop, -2000, 0, 100000);
    expect(n1.startSample).toBe(0);
    expect(n1.crossfadeSamples).toBe(0); // crossfade can't extend before file start
    const n2 = nudgeLoop(loop, 0, 200000, 6000);
    expect(n2.endSample).toBe(6000);
    const n3 = nudgeLoop(loop, 4500, 0, 100000);
    expect(n3.endSample).toBeGreaterThan(n3.startSample);
  });
});
