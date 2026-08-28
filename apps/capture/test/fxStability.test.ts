/**
 * FX rack stability (§4).
 *
 * Every unit that contains a feedback path is rendered offline with a single
 * impulse followed by silence, so anything still moving at the end of the
 * render is energy circulating inside the unit. The rule the whole rack must
 * obey: after the source goes quiet the output decays monotonically toward the
 * floor, and no sample is ever NaN or ±Infinity.
 *
 * Regression guarded here: Web Audio's `BiquadFilterNode.Q` is resonance **in
 * decibels** for `lowpass`/`highpass`, so the node's default `Q` of 1 gives a
 * ~+2 dB passband peak. The reverb combs' damping filter and the tape delay's
 * tone filter both sit inside their feedback loops, and that peak multiplied
 * the feedback coefficient on every pass — pushing the loop over unity and
 * making the rack self-oscillate from silence.
 */

import { OfflineAudioContext } from 'node-web-audio-api';
import { describe, expect, it } from 'vitest';
import type { FxType, World } from '@jm/jmi-format/web';
import { FX_REGISTRY } from '@jm/jmi-format/web';
import { BUTTERWORTH_Q_DB, feedbackGain, MAX_FEEDBACK } from '../src/engine/fx/fxTypes';
import type { ParamValue } from '../src/engine/fx/fxTypes';
import type { Measurement } from './fxHarness';
import { renderDefaultRack, renderUnit } from './fxHarness';

const ALL_TYPES = Object.keys(FX_REGISTRY) as FxType[];
const WORLDS: World[] = ['rhodes-ep', 'mks', 'fm', 'strings'];

/** Units with at least one feedback path — the ones that can self-oscillate. */
const FEEDBACK_UNITS: FxType[] = ['phaser', 'flanger', 'tapeDelay', 'digitalDelay', 'plateReverb', 'hallShimmer'];

function expectStable(m: Measurement, what: string): void {
  expect(m.nonFinite, `${what}: produced NaN/Infinity samples`).toBe(0);
  // finite peak, and nowhere near a level that could reach a speaker
  expect(Number.isFinite(m.peakDb), `${what}: peak is not finite`).toBe(true);
  expect(m.peakDb, `${what}: peak ran away (${m.peakDb.toFixed(1)} dBFS)`).toBeLessThan(24);
  // the tail must be falling, not merely bounded
  expect(m.tailSlopeDb, `${what}: tail grows at ${m.tailSlopeDb.toFixed(2)} dB/s`).toBeLessThan(0);
}

describe('Web Audio biquad Q semantics (the root cause)', () => {
  it('lowpass Q is resonance in dB, so the default Q of 1 peaks above unity', () => {
    const ctx = new OfflineAudioContext(1, 128, 48000);
    const f = new Float32Array(400);
    for (let i = 0; i < 400; i++) f[i] = 50 * i + 20;
    const peakFor = (q: number): number => {
      const b = ctx.createBiquadFilter();
      b.type = 'lowpass';
      b.frequency.value = 7800;
      b.Q.value = q;
      const mag = new Float32Array(400);
      const phase = new Float32Array(400);
      b.getFrequencyResponse(f, mag, phase);
      return Math.max(...mag);
    };
    // the node default: ~+2 dB of gain handed to any feedback loop it sits in
    expect(peakFor(1)).toBeGreaterThan(1.2);
    // what the rack now uses everywhere a flat response is meant
    expect(peakFor(BUTTERWORTH_Q_DB)).toBeCloseTo(1, 4);
  });
});

describe('feedback coefficient mapping', () => {
  it('never reaches unity anywhere in the documented range', () => {
    for (const type of ALL_TYPES) {
      const spec = FX_REGISTRY[type].params.find((p) => p.id === 'feedback');
      if (!spec) continue;
      for (const v of [spec.min!, 0, spec.max! / 2, spec.max!]) {
        expect(Math.abs(feedbackGain(v)), `${type} feedback=${v}`).toBeLessThan(1);
      }
    }
  });

  it('clamps out-of-contract and non-finite values', () => {
    expect(feedbackGain(1000)).toBe(MAX_FEEDBACK);
    expect(feedbackGain(-1000)).toBe(-MAX_FEEDBACK);
    // garbage fails safe to "no feedback at all" rather than to a live loop
    expect(feedbackGain(Number.NaN)).toBe(0);
    expect(feedbackGain(Number.POSITIVE_INFINITY)).toBe(0);
  });
});

describe('single units, default parameters', () => {
  for (const type of ALL_TYPES) {
    it(`${type} decays to silence after an impulse`, async () => {
      expectStable(await renderUnit(type), type);
    }, 30000);
  }
});

describe('single units, feedback/decay/mix at their documented maxima', () => {
  const cases: [FxType, Record<string, ParamValue>][] = [
    // tape delay: tone filter is inside the repeat loop, feedback at max,
    // shortest and longest repeat times, wow/flutter modulating the loop length
    ['tapeDelay', { feedback: 90, sync: false, time: 20, tone: 12000, wowFlutter: 0, mix: 100 }],
    ['tapeDelay', { feedback: 90, sync: false, time: 20, tone: 1000, wowFlutter: 100, mix: 100 }],
    ['tapeDelay', { feedback: 90, sync: false, time: 1500, tone: 12000, wowFlutter: 100, mix: 100 }],
    ['tapeDelay', { feedback: 90, sync: false, time: 20, pingpong: true, mix: 100 }],
    ['tapeDelay', { feedback: 90, sync: true, syncDivision: '1/32', mix: 100 }],
    // digital delay: plain feedback loop, shortest times, filters wide open
    ['digitalDelay', { feedback: 90, sync: false, timeL: 20, timeR: 20, filterHp: 20, filterLp: 20000, mix: 100 }],
    ['digitalDelay', { feedback: 90, sync: false, timeL: 2000, timeR: 2000, mix: 100 }],
    // phaser: allpass chain inside the loop, fastest sweep, deepest, most stages
    ['phaser', { feedback: 90, depth: 100, rate: 4, stages: '6' }],
    ['phaser', { feedback: 90, depth: 100, rate: 0.05, stages: '2' }],
    // flanger: both feedback polarities, shortest manual delay, deepest sweep
    ['flanger', { feedback: 90, depth: 100, rate: 2, manual: 0.1 }],
    ['flanger', { feedback: -90, depth: 100, rate: 2, manual: 0.1 }],
    ['flanger', { feedback: 90, depth: 100, rate: 0.05, manual: 10 }],
    // plate: longest decay, largest and smallest size, damping fully open
    ['plateReverb', { decay: 8, size: 100, damp: 0, predelay: 200, mix: 100 }],
    ['plateReverb', { decay: 8, size: 0, damp: 0, predelay: 0, mix: 100 }],
    ['plateReverb', { decay: 8, size: 50, damp: 100, mix: 100 }],
    // hall: longest decay, full modulation, full shimmer (the loop around the
    // comb bank), both size extremes
    ['hallShimmer', { decay: 20, size: 100, mod: 100, shimmer: 100, mix: 100 }],
    ['hallShimmer', { decay: 20, size: 0, mod: 100, shimmer: 100, mix: 100 }],
    ['hallShimmer', { decay: 20, size: 100, mod: 0, shimmer: 0, mix: 100 }],
  ];
  for (const [type, params] of cases) {
    it(`${type} ${JSON.stringify(params)}`, async () => {
      expectStable(await renderUnit(type, params, 10), `${type} ${JSON.stringify(params)}`);
    }, 30000);
  }
});

describe('every feedback unit, feedback swept across its whole range', () => {
  for (const type of FEEDBACK_UNITS) {
    it(`${type} is stable at every feedback/decay step`, async () => {
      const spec = FX_REGISTRY[type].params.find((p) => p.id === 'feedback' || p.id === 'decay');
      expect(spec, `${type} has no feedback or decay parameter`).toBeDefined();
      const { min, max, id } = spec as { min: number; max: number; id: string };
      for (let i = 0; i <= 8; i++) {
        const v = min + ((max - min) * i) / 8;
        const m = await renderUnit(type, { [id]: v, mix: 100 }, 8);
        expectStable(m, `${type} ${id}=${v.toFixed(2)}`);
      }
    }, 120000);
  }
});

describe('default racks for all four worlds', () => {
  for (const world of WORLDS) {
    it(`${world}: peaks sensibly on notes and returns to the floor after them`, async () => {
      const m = await renderDefaultRack(world);
      expect(m.nonFinite, `${world}: NaN/Infinity in the rack output`).toBe(0);
      // the four note bursts peak in the first two seconds…
      const notePeak = Math.max(m.perSecondDb[0], m.perSecondDb[1]);
      expect(notePeak).toBeGreaterThan(-30);
      expect(notePeak, `${world}: notes peak at ${notePeak.toFixed(1)} dBFS`).toBeLessThan(12);
      // …and 18 seconds after they stop the rack is at least 60 dB down and
      // still falling, rather than sustaining itself
      const tail = m.perSecondDb[m.perSecondDb.length - 1];
      expect(tail, `${world}: tail is ${tail.toFixed(1)} dBFS`).toBeLessThan(notePeak - 60);
      expect(m.tailSlopeDb, `${world}: tail grows at ${m.tailSlopeDb.toFixed(2)} dB/s`).toBeLessThan(0);
    }, 120000);
  }
});

describe('musical behaviour is preserved', () => {
  it('the reverb tail length still tracks the decay parameter', async () => {
    const short = await renderUnit('plateReverb', { decay: 0.5, mix: 100, predelay: 0 }, 8);
    const long = await renderUnit('plateReverb', { decay: 8, mix: 100, predelay: 0 }, 8);
    // one second after the impulse, a long decay must still be far louder
    expect(long.perSecondDb[2]).toBeGreaterThan(short.perSecondDb[2] + 40);
    // and it must still be audibly ringing at the end of the render
    expect(long.perSecondDb[7]).toBeGreaterThan(long.perSecondDb[1] - 60);
  }, 60000);

  it('the tape delay still regenerates: more feedback means more repeats', async () => {
    const low = await renderUnit('tapeDelay', { feedback: 10, sync: false, time: 375, mix: 100 }, 10);
    const high = await renderUnit('tapeDelay', { feedback: 90, sync: false, time: 375, mix: 100 }, 10);
    expect(high.perSecondDb[5]).toBeGreaterThan(low.perSecondDb[5] + 40);
    // still audible many repeats later, i.e. it is a tape delay and not a slap
    expect(high.perSecondDb[6]).toBeGreaterThan(high.perSecondDb[1] - 60);
  }, 60000);
});
