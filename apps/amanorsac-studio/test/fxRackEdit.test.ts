/**
 * CLIENT REVIEW ROUND 7 — the three FX-rack reports, on Capture's side.
 *
 *   1. default racks load present but BYPASSED, and an explicit `bypassed:false`
 *      preset still loads ACTIVE;
 *   2. removing a unit is clickless and leaves a sane rack;
 *   3. drag-to-reorder round-trips through save/load and introduces no
 *      discontinuity in the rendered audio.
 *
 * (3) is measured, not asserted by eye: the same three-unit rack is rendered
 * twice through `node-web-audio-api`, once undisturbed and once with a reorder
 * in the middle of the render, and the worst sample-to-sample step of the two
 * renders is compared. A graph rewired mid-stream steps by whatever the
 * junction happened to be carrying — tens of dB above the signal's own slope —
 * so the bound below is not a formality.
 */

import { OfflineAudioContext } from 'node-web-audio-api';
import { describe, expect, it } from 'vitest';
import type { FxType, World } from '@jm/jmi-format/web';
import { buildDefaultRack } from '@jm/jmi-format/web';
import { FxRack, MAX_SLOTS, RESTRUCTURE_FADE_S } from '../src/engine/fx/rack';
import { importPreset, importPresets } from '../src/engine/import/x7lConvert';

const SR = 48000;
const WORLDS: World[] = ['rhodes-ep', 'mks', 'fm', 'strings'];

/** deterministic units only: no feedback, no randomness, so two renders match */
const STABLE_RACK: FxType[] = ['epPreamp', 'vintageEq', 'compressor'];
/** what STABLE_RACK becomes after the reorder under test (move 0 to the end) */
const REORDERED_RACK: FxType[] = ['vintageEq', 'compressor', 'epPreamp'];

function offline(seconds: number): OfflineAudioContext {
  return new OfflineAudioContext(2, Math.round(SR * seconds), SR) as unknown as OfflineAudioContext;
}

// ---------------------------------------------------------------- 1. defaults

describe('FX are off by default (client review round 7, item 1)', () => {
  for (const world of WORLDS) {
    it(`${world}: a default rack loads with every unit present and bypassed`, () => {
      const ctx = offline(0.1);
      const rack = new FxRack(ctx as unknown as BaseAudioContext);
      const states = buildDefaultRack(world);
      rack.load(states);
      expect(rack.slots.length).toBe(states.length);
      expect(rack.slots.length).toBeGreaterThan(0);
      expect(rack.slots.every((u) => u.isBypassed())).toBe(true);
      // the units are still there to be switched on, in the documented order
      expect(rack.slots.map((u) => u.type)).toEqual(states.map((s) => s.type));
    });
  }

  it('an explicit bypassed:false preset still loads ACTIVE — the default never overrides saved state', () => {
    const ctx = offline(0.1);
    const rack = new FxRack(ctx as unknown as BaseAudioContext);
    // exactly the shape of a preset written before the default changed
    rack.load(buildDefaultRack('rhodes-ep', { bypassed: false }));
    expect(rack.slots.every((u) => !u.isBypassed())).toBe(true);
    // and it survives a save/load round-trip unchanged
    const saved = rack.serialize();
    expect(saved.every((u) => u.bypassed === false)).toBe(true);
    rack.load(saved);
    expect(rack.slots.every((u) => !u.isBypassed())).toBe(true);
  });

  it('a mixed rack keeps each unit exactly as saved', () => {
    const ctx = offline(0.1);
    const rack = new FxRack(ctx as unknown as BaseAudioContext);
    const states = buildDefaultRack('rhodes-ep');
    states[0].bypassed = false;
    states[3].bypassed = false;
    rack.load(states);
    expect(rack.slots.map((u) => u.isBypassed())).toEqual([false, true, true, false, true]);
    expect(rack.serialize().map((u) => u.bypassed)).toEqual([false, true, true, false, true]);
  });
});

describe('what a bundle written from now on ships with', () => {
  // importPreset reads only `world` and `soundType` off the plan; the rest of
  // X7lInstrumentPlan is irrelevant here and is deliberately not invented.
  const plan = { name: 'Imported', world: 'mks', soundType: 'decaying' } as unknown as Parameters<
    typeof importPreset
  >[0];

  it('the X7L importer writes a DRY Default preset', () => {
    const preset = importPreset(plan);
    expect(preset.name).toBe('Default');
    expect(preset.fxRack.length).toBeGreaterThan(0);
    expect(preset.fxRack.every((u) => u.bypassed)).toBe(true);
  });

  it('...and a "With FX" preset beside it, so the voicing is one click away', () => {
    const files = importPresets(plan);
    expect(files.map((f) => f.fileName)).toEqual(['Default.json', 'With FX.json']);
    expect(files[0].preset.fxRack.every((u) => u.bypassed)).toBe(true);
    expect(files[1].preset.fxRack.every((u) => !u.bypassed)).toBe(true);
    // same rack, only the flag differs
    expect(files[1].preset.fxRack.map((u) => u.type)).toEqual(files[0].preset.fxRack.map((u) => u.type));
  });
});

// ------------------------------------------------- 2 + 3. remove and reorder

describe('removing and reordering units (items 2 and 3)', () => {
  function loaded(types: FxType[] = STABLE_RACK): { ctx: OfflineAudioContext; rack: FxRack } {
    const ctx = offline(0.2);
    const rack = new FxRack(ctx as unknown as BaseAudioContext);
    rack.load(types.map((type) => ({ type, bypassed: false, params: {} })));
    return { ctx, rack };
  }

  it('remove takes the unit out of the rack and the freed slot is reusable', () => {
    const { rack } = loaded();
    rack.removeUnit(1);
    expect(rack.slots.map((u) => u.type)).toEqual(['epPreamp', 'compressor']);
    // the freed slot comes back: the rack is not stuck at its old size
    rack.addUnit('phaser');
    expect(rack.slots.map((u) => u.type)).toEqual(['epPreamp', 'compressor', 'phaser']);
    expect(rack.slots.length).toBeLessThanOrEqual(MAX_SLOTS);
  });

  it('remove ignores an out-of-range index instead of corrupting the rack', () => {
    const { rack } = loaded();
    rack.removeUnit(9);
    rack.removeUnit(-1);
    expect(rack.slots.length).toBe(3);
  });

  it('remove survives a save/load round-trip', () => {
    const { ctx, rack } = loaded();
    rack.removeUnit(0);
    const saved = rack.serialize();
    const other = new FxRack(ctx as unknown as BaseAudioContext);
    other.load(saved);
    expect(other.slots.map((u) => u.type)).toEqual(['vintageEq', 'compressor']);
  });

  it('reorder puts the unit where it was dropped, in both directions', () => {
    const { rack } = loaded();
    rack.moveUnit(0, 2); // drag the first unit to the end
    expect(rack.slots.map((u) => u.type)).toEqual(['vintageEq', 'compressor', 'epPreamp']);
    rack.moveUnit(2, 0); // and back
    expect(rack.slots.map((u) => u.type)).toEqual(['epPreamp', 'vintageEq', 'compressor']);
    rack.moveUnit(1, 1); // dropping a unit in its own gap is a no-op
    expect(rack.slots.map((u) => u.type)).toEqual(['epPreamp', 'vintageEq', 'compressor']);
  });

  it('the new ORDER is what gets saved — that is what makes it persist', () => {
    const { ctx, rack } = loaded();
    rack.moveUnit(2, 0);
    const saved = rack.serialize();
    expect(saved.map((u) => u.type)).toEqual(['compressor', 'epPreamp', 'vintageEq']);
    const reloaded = new FxRack(ctx as unknown as BaseAudioContext);
    reloaded.load(saved);
    expect(reloaded.slots.map((u) => u.type)).toEqual(['compressor', 'epPreamp', 'vintageEq']);
    // per-unit bypass travels with the unit, not with the slot
    rack.slots[0].setBypassed(true);
    expect(rack.serialize()[0]).toMatchObject({ type: 'compressor', bypassed: true });
  });
});

// -------------------------------------------------- 3. clickless, measured

/**
 * Render the stable rack fed a steady 220 Hz tone. When `reorderAt` is given,
 * `moveUnit` is called at that time and the rack's deferred rewire is driven
 * through `OfflineAudioContext.suspend`, so the whole edit lands on an exact,
 * repeatable sample.
 */
async function renderWithOptionalReorder(
  reorderAt: number | null,
  order: FxType[] = STABLE_RACK,
  seconds = 3,
): Promise<Float32Array> {
  const ctx = offline(seconds);
  const rack = new FxRack(ctx as unknown as BaseAudioContext);
  rack.load(order.map((type) => ({ type, bypassed: false, params: {} })));

  const buf = ctx.createBuffer(2, SR * seconds, SR);
  for (let c = 0; c < 2; c++) {
    const d = buf.getChannelData(c);
    for (let i = 0; i < d.length; i++) d[i] = 0.4 * Math.sin((2 * Math.PI * 220 * i) / SR);
  }
  const src = ctx.createBufferSource();
  src.buffer = buf;
  src.loop = true;
  src.start(0);
  src.connect(rack.input);
  rack.output.connect(ctx.destination);

  const pending: (() => void)[] = [];
  rack.scheduleRestructure = (fn) => {
    pending.push(fn);
  };

  if (reorderAt == null) return (await ctx.startRendering()).getChannelData(0) as Float32Array;

  const atEdit = ctx.suspend(reorderAt);
  const atRewire = ctx.suspend(reorderAt + RESTRUCTURE_FADE_S + 0.004);
  const render = ctx.startRendering();
  await atEdit;
  rack.moveUnit(0, 2);
  ctx.resume();
  await atRewire;
  for (const fn of pending.splice(0)) fn();
  ctx.resume();
  return (await render).getChannelData(0) as Float32Array;
}

function worstStep(d: Float32Array, from: number, to: number): number {
  let worst = 0;
  for (let i = from + 1; i < to; i++) worst = Math.max(worst, Math.abs(d[i] - d[i - 1]));
  return worst;
}

function rms(d: Float32Array, from: number, to: number): number {
  let s = 0;
  for (let i = from; i < to; i++) s += d[i] * d[i];
  return Math.sqrt(s / (to - from));
}

describe('reordering is clickless and does not drop audio', () => {
  it('introduces no sample-to-sample discontinuity beyond the signal itself', async () => {
    const before = await renderWithOptionalReorder(null, STABLE_RACK);
    const after = await renderWithOptionalReorder(null, REORDERED_RACK);
    const moved = await renderWithOptionalReorder(1.0, STABLE_RACK);

    expect(moved.every((v) => Number.isFinite(v))).toBe(true);

    // The bound is set by the SIGNAL, measured in steady state in each of the
    // two orders: a chain ending in the preamp's waveshaper is legitimately
    // steeper than one that starts with it, and the reorder must not be judged
    // against the gentler of the two.
    const from = Math.round(0.95 * SR);
    const to = Math.round(1.15 * SR);
    const reference = Math.max(worstStep(before, from, to), worstStep(after, from, to));
    const measured = worstStep(moved, from, to);
    // eslint-disable-next-line no-console
    console.log(
      `  reorder: worst per-sample step across the change ${measured.toExponential(3)} ` +
        `(steady-state reference for the two orders ${reference.toExponential(3)})`,
    );
    // A graph rewired without the crossfade steps by whatever the junction was
    // carrying at that instant — of the order of the signal's own amplitude,
    // many times this bound.
    expect(measured).toBeLessThan(reference * 1.25 + 1e-6);

    // ...and the audio never goes away: the crossfade lands on the DRY input,
    // it does not mute the rack
    const during = rms(moved, Math.round(1.0 * SR), Math.round(1.0 * SR + RESTRUCTURE_FADE_S * SR));
    const steady = rms(moved, Math.round(0.5 * SR), Math.round(0.9 * SR));
    // eslint-disable-next-line no-console
    console.log(`  reorder: RMS during the crossfade ${during.toFixed(4)} vs steady ${steady.toFixed(4)}`);
    expect(during).toBeGreaterThan(steady * 0.3);
  }, 60000);

  it('the reorder actually changed the signal chain (the measurement means something)', async () => {
    const control = await renderWithOptionalReorder(null);
    const moved = await renderWithOptionalReorder(1.0);
    const tail = Math.round(2.5 * SR);
    let diff = 0;
    for (let i = tail; i < control.length; i++) diff = Math.max(diff, Math.abs(control[i] - moved[i]));
    expect(diff).toBeGreaterThan(1e-6);
  }, 60000);
});
