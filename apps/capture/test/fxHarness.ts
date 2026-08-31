/**
 * Headless FX measurement harness.
 *
 * `node-web-audio-api` is a full Web Audio implementation (the same spec text
 * Chrome implements, including the quirk that `Q` on a lowpass/highpass biquad
 * is *resonance in decibels*), so the real `units.ts` / `rack.ts` graphs can be
 * rendered offline in Node and measured sample-exactly. Verified against
 * Chrome: identical biquad magnitude responses and identical growth rates for
 * the pre-fix defect.
 */

import { OfflineAudioContext } from 'node-web-audio-api';
import type { FxType, World } from '@jm/jmi-format/web';
import { buildDefaultRack } from '@jm/jmi-format/web';
import { FxRack } from '../src/engine/fx/rack';
import type { ParamValue } from '../src/engine/fx/fxTypes';
import { createFxUnit } from '../src/engine/fx/units';

export const SR = 48000;

export interface Measurement {
  /** peak level in dBFS for each whole second of the render (−999 = silence) */
  perSecondDb: number[];
  /** count of NaN / ±Infinity samples anywhere in the render */
  nonFinite: number;
  /** dB per second of change over the tail (after the excitation has stopped) */
  tailSlopeDb: number;
  /** loudest sample anywhere in the render, in dBFS */
  peakDb: number;
}

function ctxFor(seconds: number): OfflineAudioContext {
  return new OfflineAudioContext(2, Math.round(SR * seconds), SR) as unknown as OfflineAudioContext;
}

function measure(rendered: AudioBuffer, seconds: number): Measurement {
  const perSecondDb: number[] = [];
  let nonFinite = 0;
  let peak = 0;
  for (let s = 0; s < seconds; s++) {
    let p = 0;
    for (let c = 0; c < rendered.numberOfChannels; c++) {
      const d = rendered.getChannelData(c);
      for (let i = s * SR; i < Math.min((s + 1) * SR, d.length); i++) {
        const v = d[i];
        if (!Number.isFinite(v)) {
          nonFinite++;
          continue;
        }
        const a = Math.abs(v);
        if (a > p) p = a;
      }
    }
    if (p > peak) peak = p;
    perSecondDb.push(p > 0 ? 20 * Math.log10(p) : -999);
  }
  // slope over the tail: from the second after the excitation stops to the end
  const from = Math.min(2, seconds - 2);
  const a = perSecondDb[from];
  const b = perSecondDb[seconds - 1];
  const tailSlopeDb = a <= -999 || b <= -999 ? -Infinity : (b - a) / (seconds - 1 - from);
  return { perSecondDb, nonFinite, tailSlopeDb, peakDb: peak > 0 ? 20 * Math.log10(peak) : -999 };
}

/** One-sample impulse on both channels, then silence for the rest of the render. */
function impulseBuffer(ctx: OfflineAudioContext): AudioBuffer {
  const buf = ctx.createBuffer(2, SR, SR);
  for (let c = 0; c < 2; c++) buf.getChannelData(c)[0] = 1;
  return buf;
}

/**
 * Render a single FX unit fed an impulse at t=0.5 s (long enough for the
 * unit's own `setTargetAtTime` parameter ramps to have settled) and then
 * silence, so anything still moving at the end of the render is energy
 * circulating inside the unit rather than energy arriving from the source.
 */
export async function renderUnit(
  type: FxType,
  params: Record<string, ParamValue> = {},
  seconds = 8,
): Promise<Measurement> {
  const ctx = ctxFor(seconds);
  const unit = createFxUnit(ctx as unknown as BaseAudioContext, type);
  for (const [id, v] of Object.entries(params)) unit.setParam(id, v);
  const src = ctx.createBufferSource();
  src.buffer = impulseBuffer(ctx);
  src.start(0.5);
  src.connect(unit.input);
  unit.output.connect(ctx.destination);
  return measure(await ctx.startRendering(), seconds);
}

/**
 * Render a whole default rack fed four short decaying note bursts inside the
 * first two seconds and silence afterwards — the reported repro ("audition a
 * few notes, then stop").
 */
export async function renderDefaultRack(world: World, seconds = 20): Promise<Measurement> {
  const ctx = ctxFor(seconds);
  const rack = new FxRack(ctx as unknown as BaseAudioContext);
  // The subject of these renders is the DSP, not the shipped default: since
  // client review round 7 a default rack loads bypassed (spec §4 / fx-params
  // `DEFAULT_UNIT_BYPASSED`), which would make every stability assertion below
  // pass trivially on a dry signal. Force the units ACTIVE so the feedback
  // paths are the thing being measured.
  rack.load(buildDefaultRack(world, { bypassed: false }));
  const buf = ctx.createBuffer(2, SR * 2, SR);
  for (let c = 0; c < 2; c++) {
    const d = buf.getChannelData(c);
    for (let n = 0; n < 4; n++) {
      const off = Math.round(n * 0.4 * SR);
      for (let i = 0; i < 0.2 * SR; i++) {
        d[off + i] = 0.5 * Math.sin((2 * Math.PI * 220 * (n + 1) * i) / SR) * Math.exp(-i / (0.05 * SR));
      }
    }
  }
  const src = ctx.createBufferSource();
  src.buffer = buf;
  src.start(0.2);
  src.connect(rack.input);
  rack.output.connect(ctx.destination);
  return measure(await ctx.startRendering(), seconds);
}
