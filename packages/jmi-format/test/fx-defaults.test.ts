/**
 * CLIENT REVIEW ROUND 7 (item 1) — "Master effects and effects on the modules
 * should be off by default."
 *
 * The change is a DEFAULT and nothing more. These tests pin both halves of that
 * claim at the one place both apps share:
 *   1. a rack built from the defaults is present but bypassed, in every world;
 *   2. a preset that says `bypassed: false` still validates and still means
 *      `false` — a bundle exported before the change behaves exactly as it did.
 */
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import {
  DEFAULT_RACKS,
  DEFAULT_UNIT_BYPASSED,
  buildDefaultRack,
  defaultEngineParams,
  defaultUnitState,
  setBundleFxBypassed,
  validatePresetJson,
  type Preset,
  type World,
} from '../src/index.js';
const presetSchema = JSON.parse(readFileSync(new URL('../schema/preset.schema.json', import.meta.url), 'utf8'));

const WORLDS: World[] = ['rhodes-ep', 'mks', 'fm', 'strings'];

let dir: string;
beforeAll(() => {
  dir = mkdtempSync(join(tmpdir(), 'jmi-fxdefaults-'));
});
afterAll(() => {
  rmSync(dir, { recursive: true, force: true });
});

function presetWith(fxRack: Preset['fxRack'], name = 'P'): Preset {
  return {
    formatVersion: 1,
    name,
    tags: [],
    engine: defaultEngineParams('rhodes-ep'),
    fxRack,
    ampEnvelope: { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 },
  };
}

describe('default FX racks are off by default', () => {
  it('ships the flag as bypassed', () => {
    expect(DEFAULT_UNIT_BYPASSED).toBe(true);
  });

  for (const world of WORLDS) {
    it(`${world}: every default unit is present but bypassed`, () => {
      const rack = buildDefaultRack(world);
      // still the documented rack — the units are NOT removed, only silent
      expect(rack.map((u) => u.type)).toEqual(DEFAULT_RACKS[world]);
      expect(rack.length).toBeGreaterThan(0);
      expect(rack.every((u) => u.bypassed)).toBe(true);
    });
  }

  it('keeps the world parameter overrides intact', () => {
    const strings = buildDefaultRack('strings');
    const hall = strings.find((u) => u.type === 'hallShimmer');
    expect(hall?.params.shimmer).toBe(30);
    expect(hall?.bypassed).toBe(true);
  });

  it('defaultUnitState follows the same default and can be overridden', () => {
    expect(defaultUnitState('tapeDelay').bypassed).toBe(true);
    expect(defaultUnitState('tapeDelay', false).bypassed).toBe(false);
    expect(defaultUnitState('tapeDelay').params.time).toBe(375);
  });

  it('the opt-out is explicit and only flips the flag', () => {
    const off = buildDefaultRack('fm');
    const on = buildDefaultRack('fm', { bypassed: false });
    expect(on.every((u) => !u.bypassed)).toBe(true);
    expect(on.map((u) => u.type)).toEqual(off.map((u) => u.type));
    expect(on.map((u) => u.params)).toEqual(off.map((u) => u.params));
  });
});

describe('an explicit saved state always wins over the default', () => {
  it('bypassed is a REQUIRED property, so no saved preset can be silent about it', () => {
    const unit = (presetSchema as { properties: { fxRack: { items: { required: string[] } } } }).properties
      .fxRack.items.required;
    expect(unit).toContain('bypassed');
  });

  it('a preset written under the OLD default (bypassed:false) still validates and still means false', () => {
    // byte-for-byte the shape James's already-imported bundles carry
    const legacy = presetWith(buildDefaultRack('mks', { bypassed: false }), 'Legacy');
    const res = validatePresetJson(legacy);
    expect(res.filter((i) => i.level === 'error')).toEqual([]);
    const reloaded = JSON.parse(JSON.stringify(legacy)) as Preset;
    expect(reloaded.fxRack.every((u) => u.bypassed === false)).toBe(true);
  });

  it('a mixed rack round-trips unit by unit', () => {
    const rack = buildDefaultRack('rhodes-ep');
    rack[0].bypassed = false; // player switched the EP preamp back on
    rack[3].bypassed = false; // ...and the tape delay
    const reloaded = JSON.parse(JSON.stringify(presetWith(rack))) as Preset;
    expect(reloaded.fxRack.map((u) => u.bypassed)).toEqual([false, true, true, false, true]);
  });
});

describe('jmi fx-off — the remedy for bundles already on disk', () => {
  function bundleWith(name: string, bypassed: boolean): string {
    const bundle = join(dir, `${name}.jmi`);
    mkdirSync(join(bundle, 'presets'), { recursive: true });
    writeFileSync(
      join(bundle, 'presets', 'Warm.json'),
      JSON.stringify(presetWith(buildDefaultRack('mks', { bypassed }), 'Warm'), null, 2),
    );
    return bundle;
  }

  it('turns every unit of every preset off, and keeps a .bak', () => {
    const bundle = bundleWith('Legacy', false);
    const res = setBundleFxBypassed(bundle, true);
    expect(res.presets).toBe(1);
    expect(res.units).toBe(DEFAULT_RACKS.mks.length);
    const after = JSON.parse(readFileSync(join(bundle, 'presets', 'Warm.json'), 'utf8')) as Preset;
    expect(after.fxRack.every((u) => u.bypassed)).toBe(true);
    // nothing else moved
    expect(after.fxRack.map((u) => u.type)).toEqual(DEFAULT_RACKS.mks);
    expect(after.name).toBe('Warm');
    expect(existsSync(join(bundle, 'presets', 'Warm.json.bak'))).toBe(true);
    const bak = JSON.parse(readFileSync(join(bundle, 'presets', 'Warm.json.bak'), 'utf8')) as Preset;
    expect(bak.fxRack.every((u) => !u.bypassed)).toBe(true);
    // the result still validates as a preset
    expect(validatePresetJson(after).filter((i) => i.level === 'error')).toEqual([]);
  });

  it('is a no-op when nothing needs changing', () => {
    const bundle = bundleWith('AlreadyDry', true);
    expect(setBundleFxBypassed(bundle, true)).toEqual({ presets: 0, units: 0 });
    expect(existsSync(join(bundle, 'presets', 'Warm.json.bak'))).toBe(false);
  });

  it('fx-on is the exact inverse', () => {
    const bundle = bundleWith('Roundtrip', true);
    setBundleFxBypassed(bundle, false);
    const after = JSON.parse(readFileSync(join(bundle, 'presets', 'Warm.json'), 'utf8')) as Preset;
    expect(after.fxRack.every((u) => !u.bypassed)).toBe(true);
  });
});
