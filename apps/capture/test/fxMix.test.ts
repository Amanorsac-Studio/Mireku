/**
 * CLIENT REQUEST — "put mix knob on all the time based effects".
 *
 * Phaser and flanger were the two time-based units with no wet/dry control.
 * `mix` is appended to each unit's parameter list in the shared registry
 * (`@jm/jmi-format` fx-params), so:
 *
 *   1. a preset saved BEFORE the change has no `mix` key at all, and the load
 *      path — registry defaults first, saved keys on top — gives it 50;
 *   2. 50 is a true 50/50 blend, which is exactly the blend both units
 *      produced when the mix was hard-wired. In the plugin that is
 *      bit-identical (`0.5f * (dry + wet)`, asserted in Signature's
 *      EngineTests); in Capture it is the same BALANCE, with the unscaled
 *      `dry + wet` sum these two units used corrected to a crossfade so the
 *      two apps finally agree on what mix 50 means. Logged in
 *      DECISIONS-capture.md.
 *
 * The blend is measured, not assumed: a unit is rendered at mix 0, 50 and 100
 * and the 50 render has to be the exact average of the other two. That is only
 * true if the control is a linear crossfade AND the wet signal is independent
 * of the mix (i.e. the feedback loops were left alone).
 */

import { OfflineAudioContext } from 'node-web-audio-api';
import { describe, expect, it } from 'vitest';
import type { FxType } from '@jm/jmi-format/web';
import { defaultParams } from '@jm/jmi-format/web';
import { FxRack } from '../src/engine/fx/rack';
import type { ParamValue } from '../src/engine/fx/fxTypes';
import { createFxUnit } from '../src/engine/fx/units';

const SR = 48000;
const SECONDS = 2;

/**
 * Render one unit fed a decaying 220 Hz burst that starts at 0.5 s — late
 * enough that every `setTargetAtTime` gain ramp inside the unit has settled,
 * so what is measured is the steady-state blend and not the ramp.
 */
async function renderUnit(type: FxType, mix: number): Promise<Float32Array> {
  const ctx = new OfflineAudioContext(2, SR * SECONDS, SR) as unknown as OfflineAudioContext;
  const unit = createFxUnit(ctx as unknown as BaseAudioContext, type);
  unit.setParam('mix', mix);

  const buf = ctx.createBuffer(2, Math.round(SR * 0.5), SR);
  for (let c = 0; c < 2; c++) {
    const d = buf.getChannelData(c);
    for (let i = 0; i < d.length; i++)
      d[i] = 0.5 * Math.sin((2 * Math.PI * 220 * i) / SR) * Math.exp(-i / (0.2 * SR));
  }
  const src = ctx.createBufferSource();
  src.buffer = buf;
  src.start(0.5);
  src.connect(unit.input);
  unit.output.connect(ctx.destination);
  const rendered = await ctx.startRendering();
  return rendered.getChannelData(0).slice();
}

function peak(x: Float32Array): number {
  let p = 0;
  for (const v of x) p = Math.max(p, Math.abs(v));
  return p;
}

describe('Mix on the phaser and the flanger', () => {
  for (const type of ['phaser', 'flanger'] as const) {
    it(`${type}: the registry default is 50`, () => {
      expect(defaultParams(type).mix).toBe(50);
    });

    it(`${type}: a preset with no mix key loads at the default, not at 0`, () => {
      const ctx = new OfflineAudioContext(2, 128, SR) as unknown as OfflineAudioContext;
      const rack = new FxRack(ctx as unknown as BaseAudioContext);
      // byte-for-byte the shape a preset saved before this change carries
      const legacyParams: Record<string, ParamValue> =
        type === 'phaser'
          ? { rate: 0.3, depth: 60, feedback: 40, stages: '4' }
          : { rate: 0.2, depth: 50, feedback: 30, manual: 2 };
      rack.load([{ type, bypassed: false, params: legacyParams }]);
      expect(rack.slots).toHaveLength(1);
      expect(rack.slots[0].getParams().mix).toBe(50);
      // and nothing the preset DID say was disturbed
      for (const [id, v] of Object.entries(legacyParams))
        expect(rack.slots[0].getParams()[id]).toBe(v);
    });

    it(`${type}: mix is a linear crossfade, so 50 is exactly half dry + half wet`, async () => {
      const [dry, wet, half] = await Promise.all([
        renderUnit(type, 0),
        renderUnit(type, 100),
        renderUnit(type, 50),
      ]);
      expect(peak(dry)).toBeGreaterThan(0.1);
      expect(peak(wet)).toBeGreaterThan(0.1);

      let worst = 0;
      for (let i = 0; i < half.length; i++)
        worst = Math.max(worst, Math.abs(half[i] - 0.5 * (dry[i] + wet[i])));
      expect(worst, `${type}: mix 50 is not the average of mix 0 and mix 100`).toBeLessThan(1e-6);
    });

    it(`${type}: mix 0 is the untouched dry signal`, async () => {
      // the dry path must be unity at mix 0 — a bypassed unit and a fully dry
      // unit have to sit at the same level, or the knob changes the gain
      // staging of the whole rack
      const dry = await renderUnit(type, 0);
      expect(peak(dry)).toBeGreaterThan(0.45);
      expect(peak(dry)).toBeLessThanOrEqual(0.5 + 1e-6);
    });
  }
});
