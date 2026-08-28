import { describe, expect, it } from 'vitest';
import {
  centsBetween,
  detectPitch,
  midiToFrequency,
  noteName,
  pitchTrend,
  verifyTakePitch,
} from '../src/engine/dsp/pitch';

const SR = 48000;

/**
 * Synthetic instrument tone: a decaying harmonic stack, which is what a real
 * take looks like far more than a bare sine does.
 */
function tone(
  hz: number,
  seconds = 1.5,
  opts: { sampleRate?: number; harmonics?: number; decayTau?: number; amp?: number; noise?: number } = {},
): Float32Array[] {
  const sr = opts.sampleRate ?? SR;
  const harmonics = opts.harmonics ?? 5;
  const tau = opts.decayTau ?? 2.5;
  const amp = opts.amp ?? 0.6;
  const noise = opts.noise ?? 0;
  const n = Math.round(seconds * sr);
  const out = new Float32Array(n);
  let seed = 987654321;
  const rnd = () => {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    return seed / 0x7fffffff - 0.5;
  };
  for (let i = 0; i < n; i++) {
    const t = i / sr;
    const env = Math.exp(-t / tau) * Math.min(1, t / 0.005);
    let s = 0;
    for (let h = 1; h <= harmonics; h++) {
      if (hz * h > sr / 2.2) break;
      s += Math.sin(2 * Math.PI * hz * h * t) / h;
    }
    out[i] = amp * env * s + noise * rnd();
  }
  return [out];
}

function noiseBurst(seconds = 1.5, sampleRate = SR): Float32Array[] {
  const n = Math.round(seconds * sampleRate);
  const out = new Float32Array(n);
  let seed = 24680;
  for (let i = 0; i < n; i++) {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    const env = Math.exp(-(i / sampleRate) / 0.6);
    out[i] = env * (seed / 0x7fffffff - 0.5) * 1.2;
  }
  return [out];
}

/** A hand clap / rim shot: a noisy transient with no sustained fundamental. */
function percussive(seconds = 1, sampleRate = SR): Float32Array[] {
  const n = Math.round(seconds * sampleRate);
  const out = new Float32Array(n);
  let seed = 13579;
  for (let i = 0; i < n; i++) {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    const env = Math.exp(-(i / sampleRate) / 0.08);
    out[i] = env * (seed / 0x7fffffff - 0.5) * 1.5;
  }
  return [out];
}

const A3 = 57; // MIDI 57 = A3 = 220 Hz

describe('pitch detection (YIN) accuracy', () => {
  it('locks onto a pure fundamental within a couple of cents', () => {
    for (const midi of [36, 45, 57, 69, 81]) {
      const hz = midiToFrequency(midi);
      const d = detectPitch(tone(hz), SR, { fMin: hz / 4, fMax: hz * 4 });
      expect(d.hz).not.toBeNull();
      expect(Math.abs(centsBetween(d.hz!, hz))).toBeLessThan(5);
      expect(d.confidence).toBeGreaterThan(0.8);
    }
  });

  it('is not fooled into the sub-octave by a strong harmonic stack', () => {
    const hz = midiToFrequency(A3);
    const d = detectPitch(tone(hz, 1.5, { harmonics: 12 }), SR, { fMin: hz / 4, fMax: hz * 4 });
    expect(Math.abs(centsBetween(d.hz!, hz))).toBeLessThan(10);
  });

  it('works at the 44.1 kHz session rate too', () => {
    const hz = midiToFrequency(A3);
    const d = detectPitch(tone(hz, 1.5, { sampleRate: 44100 }), 44100, { fMin: hz / 4, fMax: hz * 4 });
    expect(Math.abs(centsBetween(d.hz!, hz))).toBeLessThan(5);
  });
});

describe('take verification against the expected note (§3.5)', () => {
  it('an exact-pitch take reads ~0 cents and passes', () => {
    const v = verifyTakePitch(tone(midiToFrequency(A3)), SR, A3);
    expect(v.status).toBe('ok');
    expect(Math.abs(v.cents!)).toBeLessThan(5);
    expect(v.issue).toBeNull();
    expect(v.message).toContain('PITCH OK');
  });

  it('a take an octave up is reported as an octave, not as noise', () => {
    const v = verifyTakePitch(tone(midiToFrequency(A3 + 12)), SR, A3);
    expect(v.status).toBe('off');
    expect(v.issue).toBe('octave');
    expect(v.semitones).toBe(12);
    expect(Math.abs(v.cents! - 1200)).toBeLessThan(10);
    expect(v.message).toContain('OCTAVE');
    expect(v.message).toContain('ABOVE');
  });

  it('an octave down is reported as an octave below', () => {
    const v = verifyTakePitch(tone(midiToFrequency(A3 - 12)), SR, A3);
    expect(v.issue).toBe('octave');
    expect(v.semitones).toBe(-12);
    expect(v.message).toContain('BELOW');
  });

  it('a semitone off is called out as a semitone with the note actually heard', () => {
    const v = verifyTakePitch(tone(midiToFrequency(A3 + 1)), SR, A3);
    expect(v.issue).toBe('semitone');
    expect(v.semitones).toBe(1);
    expect(v.nearestNote).toBe(A3 + 1);
    expect(v.message).toContain(noteName(A3 + 1));
  });

  it('a wrong-patch interval is reported with the interval and the heard note', () => {
    const v = verifyTakePitch(tone(midiToFrequency(A3 + 7)), SR, A3);
    expect(v.issue).toBe('interval');
    expect(v.semitones).toBe(7);
    expect(v.message).toContain('7 SEMITONES');
  });

  it('a detuned tone reports the correct cents deviation', () => {
    for (const cents of [-40, -25, 25, 40]) {
      const hz = midiToFrequency(A3) * Math.pow(2, cents / 1200);
      const v = verifyTakePitch(tone(hz), SR, A3, { toleranceCents: 15 });
      expect(v.status).toBe('off');
      expect(v.issue).toBe('detuned');
      expect(Math.abs(v.cents! - cents)).toBeLessThan(6);
    }
  });

  it('stays inside the default 50-cent tolerance for a mildly detuned source', () => {
    const hz = midiToFrequency(A3) * Math.pow(2, 30 / 1200);
    expect(verifyTakePitch(tone(hz), SR, A3).status).toBe('ok');
  });

  it('noise is "can\'t tell", never a false accusation', () => {
    const v = verifyTakePitch(noiseBurst(), SR, A3);
    expect(v.status).toBe('unknown');
    expect(v.issue).toBeNull();
    expect(v.confidence).toBeLessThan(0.6);
    expect(v.message).toContain('UNCLEAR');
  });

  it('a percussive unpitched hit is "can\'t tell" too', () => {
    const v = verifyTakePitch(percussive(), SR, A3);
    expect(v.status).toBe('unknown');
  });

  it('silence is "can\'t tell"', () => {
    expect(verifyTakePitch([new Float32Array(SR)], SR, A3).status).toBe('unknown');
  });

  it('survives a noisy but pitched take', () => {
    const v = verifyTakePitch(tone(midiToFrequency(A3), 1.5, { noise: 0.02 }), SR, A3);
    expect(v.status).toBe('ok');
  });
});

describe('session-wide pitch trend', () => {
  const obs = (semitones: number, cents = semitones * 100) => ({
    note: 60,
    semitones,
    cents,
    confidence: 0.9,
  });

  it('calls out a consistent transposition across takes', () => {
    const t = pitchTrend([obs(12), obs(12), obs(12)]);
    expect(t.consistentSemitones).toBe(12);
    expect(t.message).toContain('TRANSPOSED');
    expect(t.message).toContain('+1 OCTAVE');
  });

  it('needs more than one take before claiming a trend', () => {
    expect(pitchTrend([obs(12)]).consistentSemitones).toBeNull();
  });

  it('does not claim a trend when the takes disagree', () => {
    expect(pitchTrend([obs(12), obs(0, 3), obs(12)]).message).toBeNull();
  });

  it('reports a consistent sub-semitone detune', () => {
    const t = pitchTrend([obs(0, 32), obs(0, 34), obs(0, 30)]);
    expect(t.consistentSemitones).toBe(0);
    expect(t.message).toContain('SHARP');
  });

  it('stays quiet when takes are in tune', () => {
    expect(pitchTrend([obs(0, 2), obs(0, -3), obs(0, 1)]).message).toBeNull();
  });
});
