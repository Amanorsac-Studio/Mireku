/**
 * Canonical FX rack parameter registry — the single source of truth for
 * parameter names, ranges, and defaults shared by JM Capture (Web Audio)
 * and JM Signature (JUCE). Preset JSON written against this registry is
 * portable between the two apps. Spec §4.
 */

import type { FxType, FxUnitState, World } from './types.js';

export type ParamKind = 'float' | 'int' | 'enum' | 'bool';

export interface ParamSpec {
  id: string;
  label: string;
  kind: ParamKind;
  min?: number;
  max?: number;
  default: number | string | boolean;
  /** enum choices when kind === 'enum' */
  choices?: string[];
  unit?: string;
  /** parameter supports host/internal tempo sync */
  syncable?: boolean;
  /** log-taper knob (frequencies, times) */
  log?: boolean;
}

export interface FxSpec {
  type: FxType;
  name: string;
  params: ParamSpec[];
}

export const FX_REGISTRY: Record<FxType, FxSpec> = {
  jmChorus: {
    type: 'jmChorus',
    name: 'JM Chorus',
    params: [
      { id: 'mode', label: 'Mode', kind: 'enum', choices: ['I', 'II', 'I+II'], default: 'I' },
      { id: 'rate', label: 'Rate', kind: 'float', min: 0.1, max: 5, default: 0.5, unit: 'Hz', log: true },
      { id: 'depth', label: 'Depth', kind: 'float', min: 0, max: 100, default: 60 },
      { id: 'width', label: 'Width', kind: 'float', min: 0, max: 100, default: 80 },
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 50 },
    ],
  },
  tremoloAutoPan: {
    type: 'tremoloAutoPan',
    name: 'Tremolo/Auto-Pan',
    params: [
      { id: 'rate', label: 'Rate', kind: 'float', min: 0.1, max: 12, default: 4.5, unit: 'Hz', syncable: true, log: true },
      { id: 'depth', label: 'Depth', kind: 'float', min: 0, max: 100, default: 50 },
      { id: 'shape', label: 'Shape', kind: 'enum', choices: ['sine', 'tri', 'square'], default: 'sine' },
      { id: 'spread', label: 'Spread', kind: 'float', min: 0, max: 100, default: 70 },
    ],
  },
  phaser: {
    type: 'phaser',
    name: 'Phaser',
    params: [
      { id: 'rate', label: 'Rate', kind: 'float', min: 0.05, max: 4, default: 0.3, unit: 'Hz', log: true },
      { id: 'depth', label: 'Depth', kind: 'float', min: 0, max: 100, default: 60 },
      { id: 'feedback', label: 'Feedback', kind: 'float', min: 0, max: 90, default: 40 },
      { id: 'stages', label: 'Stages', kind: 'enum', choices: ['2', '4', '6'], default: '4' },
      // CLIENT REQUEST — "put mix knob on all the time based effects".
      // APPENDED, never inserted: a parameter's position in this list is baked
      // into every FX preset ever saved. A preset written before this change
      // simply has no `mix` key and therefore loads at the default below.
      //
      // The default is 50 because that is EXACTLY the blend both engines
      // already produced with the mix hard-wired: Signature's PhaserFx summed
      // `0.5f * (dry + wet)`, so `dry * (1 - 0.5) + wet * 0.5` is the same
      // arithmetic. Loading an old preset after this change is therefore
      // bit-identical (asserted in EngineTests `testFxMixDefaultIdentity`).
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 50 },
    ],
  },
  flanger: {
    type: 'flanger',
    name: 'Flanger',
    params: [
      { id: 'rate', label: 'Rate', kind: 'float', min: 0.05, max: 2, default: 0.2, unit: 'Hz', log: true },
      { id: 'depth', label: 'Depth', kind: 'float', min: 0, max: 100, default: 50 },
      { id: 'feedback', label: 'Feedback', kind: 'float', min: -90, max: 90, default: 30 },
      { id: 'manual', label: 'Manual', kind: 'float', min: 0.1, max: 10, default: 2, unit: 'ms', log: true },
      // APPENDED — see the note on phaser.mix above. Signature's FlangerFx
      // summed `0.5f * (dry + wet)` too, so 50 reproduces it exactly.
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 50 },
    ],
  },
  epPreamp: {
    type: 'epPreamp',
    name: 'EP Preamp/Drive',
    params: [
      { id: 'drive', label: 'Drive', kind: 'float', min: 0, max: 100, default: 20 },
      { id: 'tone', label: 'Tone', kind: 'float', min: -100, max: 100, default: 0 },
      { id: 'output', label: 'Output', kind: 'float', min: -24, max: 12, default: 0, unit: 'dB' },
    ],
  },
  vintageEq: {
    type: 'vintageEq',
    name: 'Vintage EQ',
    params: [
      { id: 'bass', label: 'Bass', kind: 'float', min: -15, max: 15, default: 0, unit: 'dB' },
      { id: 'midFreq', label: 'Mid Freq', kind: 'float', min: 200, max: 5000, default: 1000, unit: 'Hz', log: true },
      { id: 'midGain', label: 'Mid Gain', kind: 'float', min: -15, max: 15, default: 0, unit: 'dB' },
      { id: 'treble', label: 'Treble', kind: 'float', min: -15, max: 15, default: 0, unit: 'dB' },
      { id: 'lowCut', label: 'Low Cut', kind: 'enum', choices: ['off', '40', '80', '120'], default: 'off' },
    ],
  },
  compressor: {
    type: 'compressor',
    name: 'Compressor',
    params: [
      { id: 'amount', label: 'Amount', kind: 'float', min: 0, max: 100, default: 30 },
      { id: 'attack', label: 'Attack', kind: 'float', min: 1, max: 100, default: 15, unit: 'ms', log: true },
      { id: 'release', label: 'Release', kind: 'float', min: 30, max: 800, default: 200, unit: 'ms', log: true },
    ],
  },
  tapeDelay: {
    type: 'tapeDelay',
    name: 'Tape Delay',
    params: [
      { id: 'time', label: 'Time', kind: 'float', min: 20, max: 1500, default: 375, unit: 'ms', syncable: true, log: true },
      { id: 'sync', label: 'Sync', kind: 'bool', default: true },
      { id: 'syncDivision', label: 'Division', kind: 'enum', choices: ['1/32', '1/16', '3/32', '1/8', '3/16', '1/4', '1/4d', '1/2'], default: '3/16' },
      { id: 'feedback', label: 'Feedback', kind: 'float', min: 0, max: 90, default: 35 },
      { id: 'tone', label: 'Tone', kind: 'float', min: 1000, max: 12000, default: 5000, unit: 'Hz', log: true },
      { id: 'wowFlutter', label: 'Wow/Flutter', kind: 'float', min: 0, max: 100, default: 25 },
      { id: 'pingpong', label: 'Ping-Pong', kind: 'bool', default: false },
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 25 },
    ],
  },
  digitalDelay: {
    type: 'digitalDelay',
    name: 'Digital Delay',
    params: [
      { id: 'timeL', label: 'Time L', kind: 'float', min: 20, max: 2000, default: 500, unit: 'ms', syncable: true, log: true },
      { id: 'timeR', label: 'Time R', kind: 'float', min: 20, max: 2000, default: 750, unit: 'ms', syncable: true, log: true },
      { id: 'sync', label: 'Sync', kind: 'bool', default: true },
      { id: 'syncDivisionL', label: 'Div L', kind: 'enum', choices: ['1/32', '1/16', '3/32', '1/8', '3/16', '1/4', '1/4d', '1/2'], default: '1/4' },
      { id: 'syncDivisionR', label: 'Div R', kind: 'enum', choices: ['1/32', '1/16', '3/32', '1/8', '3/16', '1/4', '1/4d', '1/2'], default: '1/4d' },
      { id: 'feedback', label: 'Feedback', kind: 'float', min: 0, max: 90, default: 30 },
      { id: 'filterHp', label: 'HP', kind: 'float', min: 20, max: 500, default: 20, unit: 'Hz', log: true },
      { id: 'filterLp', label: 'LP', kind: 'float', min: 2000, max: 20000, default: 20000, unit: 'Hz', log: true },
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 20 },
    ],
  },
  plateReverb: {
    type: 'plateReverb',
    name: 'Plate Reverb',
    params: [
      { id: 'size', label: 'Size', kind: 'float', min: 0, max: 100, default: 50 },
      { id: 'decay', label: 'Decay', kind: 'float', min: 0.3, max: 8, default: 2.2, unit: 's', log: true },
      { id: 'predelay', label: 'Pre-Delay', kind: 'float', min: 0, max: 200, default: 20, unit: 'ms' },
      { id: 'damp', label: 'Damp', kind: 'float', min: 0, max: 100, default: 40 },
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 25 },
    ],
  },
  hallShimmer: {
    type: 'hallShimmer',
    name: 'Hall/Shimmer',
    params: [
      { id: 'size', label: 'Size', kind: 'float', min: 0, max: 100, default: 70 },
      { id: 'decay', label: 'Decay', kind: 'float', min: 0.5, max: 20, default: 4, unit: 's', log: true },
      { id: 'shimmer', label: 'Shimmer', kind: 'float', min: 0, max: 100, default: 0 },
      { id: 'mod', label: 'Mod', kind: 'float', min: 0, max: 100, default: 30 },
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 25 },
    ],
  },
  widener: {
    type: 'widener',
    name: 'Widener/Ensemble',
    params: [
      { id: 'width', label: 'Width', kind: 'float', min: 0, max: 200, default: 120 },
      { id: 'ensemble', label: 'Ensemble', kind: 'float', min: 0, max: 100, default: 0 },
    ],
  },
  // Tape machine with a detuner (client, 2026-10-29): saturation, wow/flutter,
  // an "age" high roll-off, hiss, and two micro-pitched copies spread L/R.
  // Mirrors FxParams.h; the plugin is the reference implementation.
  tapeMachine: {
    type: 'tapeMachine',
    name: 'Tape Machine',
    params: [
      { id: 'drive', label: 'Drive', kind: 'float', min: 0, max: 100, default: 30 },
      { id: 'wow', label: 'Wow/Flutter', kind: 'float', min: 0, max: 100, default: 25 },
      { id: 'age', label: 'Age', kind: 'float', min: 0, max: 100, default: 30 },
      { id: 'hiss', label: 'Hiss', kind: 'float', min: 0, max: 100, default: 10 },
      { id: 'detune', label: 'Detune', kind: 'float', min: 0, max: 30, default: 8, unit: 'ct' },
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 100 },
    ],
  },
  // Modulator: ring modulation or single-sideband frequency shifting, LFO on the carrier.
  ringMod: {
    type: 'ringMod',
    name: 'Modulator',
    params: [
      { id: 'mode', label: 'Mode', kind: 'enum', choices: ['Ring', 'Shift'], default: 'Ring' },
      { id: 'freq', label: 'Freq', kind: 'float', min: 0.5, max: 5000, default: 100, unit: 'Hz', log: true },
      { id: 'lfoRate', label: 'LFO Rate', kind: 'float', min: 0.05, max: 10, default: 0.5, unit: 'Hz', log: true },
      { id: 'lfoDepth', label: 'LFO Depth', kind: 'float', min: 0, max: 100, default: 0 },
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 50 },
    ],
  },
  // Modulation pedal (client, 2026-10-30): chorus / flanger / phaser / wow+flutter
  // behind one INTENSITY knob, with RATE, MIX (locked full wet in wow+flut) and
  // a MONO switch. Mirrors FxParams.h; the plugin is the reference implementation.
  modPedal: {
    type: 'modPedal',
    name: 'Modulation',
    params: [
      { id: 'mode', label: 'Mode', kind: 'enum', choices: ['chorus', 'flanger', 'phaser', 'wow+flut'], default: 'chorus' },
      { id: 'rate', label: 'Rate', kind: 'float', min: 0.05, max: 10, default: 0.5, unit: 'Hz', log: true },
      { id: 'intensity', label: 'Intensity', kind: 'float', min: 0, max: 100, default: 50 },
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 50 },
      { id: 'mono', label: 'Mono', kind: 'bool', default: false },
    ],
  },
  // Amanorsac sound designer: resonant state-variable filter with a drive stage in front.
  svFilter: {
    type: 'svFilter',
    name: 'Filter',
    params: [
      { id: 'mode', label: 'Mode', kind: 'enum', choices: ['lowpass', 'highpass', 'bandpass'], default: 'lowpass' },
      { id: 'cutoff', label: 'Cutoff', kind: 'float', min: 40, max: 18000, default: 2200, unit: 'Hz', log: true },
      { id: 'resonance', label: 'Resonance', kind: 'float', min: 0, max: 100, default: 20 },
      { id: 'drive', label: 'Drive', kind: 'float', min: 0, max: 100, default: 0 },
      { id: 'mix', label: 'Mix', kind: 'float', min: 0, max: 100, default: 100 },
    ],
  },
};

/** Default FX racks per world, spec §4 footer. */
export const DEFAULT_RACKS: Record<World, FxType[]> = {
  'rhodes-ep': ['epPreamp', 'tremoloAutoPan', 'phaser', 'tapeDelay', 'plateReverb'],
  mks: ['jmChorus', 'vintageEq', 'widener', 'hallShimmer'],
  fm: ['compressor', 'jmChorus', 'digitalDelay', 'plateReverb'],
  strings: ['widener', 'vintageEq', 'hallShimmer'],
};

/** World-specific overrides applied on top of registry defaults for default racks. */
const DEFAULT_RACK_OVERRIDES: Record<World, Partial<Record<FxType, Record<string, number | string | boolean>>>> = {
  'rhodes-ep': {},
  mks: {},
  fm: {},
  strings: { hallShimmer: { shimmer: 30 } },
};

export function defaultParams(type: FxType): Record<string, number | string | boolean> {
  const out: Record<string, number | string | boolean> = {};
  for (const p of FX_REGISTRY[type].params) out[p.id] = p.default;
  return out;
}

/**
 * CLIENT REVIEW ROUND 7 (item 1) — "Master effects and effects on the modules
 * should be off by default."
 *
 * A default rack is now built PRESENT BUT BYPASSED: every unit the world calls
 * for is still in the rack, in its documented order, with its documented
 * parameters, so the player can see what is available and switch it on — but
 * the instrument sounds dry the moment it loads.
 *
 * This is a *default*, and nothing else. `bypassed` is a REQUIRED property of
 * every rack unit in `schema/preset.schema.json`, so every preset ever written
 * — by Capture, by the placeholder generator, by James's own bundles — carries
 * an explicit value, and every load path (this package's parsers, Capture's
 * `FxRack.load`, Signature's `rackFromVar` / `rackFromJmiPreset`) reads that
 * explicit value verbatim. A preset that says `bypassed: false` therefore still
 * loads with its unit ACTIVE, exactly as it did before this change. The only
 * behaviour that moved is what you get when there is no saved state to read.
 *
 * The same constant governs Signature's master rack (`FxDsp.h`
 * `kDefaultUnitBypassed`), so the two apps cannot drift apart.
 */
export const DEFAULT_UNIT_BYPASSED = true;

/** One rack unit at its registry defaults. `bypassed` follows the default above. */
export function defaultUnitState(type: FxType, bypassed = DEFAULT_UNIT_BYPASSED): FxUnitState {
  return { type, bypassed, params: defaultParams(type) };
}

/**
 * The world's default rack.
 *
 * @param opts.bypassed overrides the shipped default. Pass `false` only where
 * an ACTIVE rack is the subject under test (the offline FX-stability renders),
 * never in product code — product code must get the shipped default so the two
 * apps agree.
 */
export function buildDefaultRack(world: World, opts?: { bypassed?: boolean }): FxUnitState[] {
  const bypassed = opts?.bypassed ?? DEFAULT_UNIT_BYPASSED;
  return DEFAULT_RACKS[world].map((type) => ({
    type,
    bypassed,
    params: { ...defaultParams(type), ...(DEFAULT_RACK_OVERRIDES[world][type] ?? {}) },
  }));
}

/**
 * Model-page engine control ids per world, spec §5.2 (normalized 0..100 unless enum).
 *
 * EVERY DEFAULT HERE IS THE CONTROL'S UNITY POINT — the value at which the
 * engine does nothing to the recorded audio. Client instruction: "all effects
 * should be off or default — it shouldn't affect the sound." The FX rack was
 * already bypassed by default, but the per-world voicing chain
 * (apps/signature/src/engine/Sampler.h, applyVoicing + the per-voice drive and
 * LP) is a SECOND processing stage, and its old defaults were not neutral.
 *
 * The unity point is read off the DSP, not assumed, and it is not always 50:
 *   • `tine` / `body` / `glass` are `shelfDb(v) = (v-50)/50 * 8 dB`
 *     — neutral at 50 (knob at 12 o'clock does nothing);
 *   • `warmth` is `tilt = -v/100`, `air` is `highDb = v/100 * 8 dB`,
 *     `bark` is a drive depth `v/100` plus a velocity-crossfade bias
 *     `+v*0.30`, `motion` gates a pan/level LFO above 1, and `attack`/`swell`
 *     only ever RAISE the amp attack (`max(atkMs, ...)`)
 *     — all one-directional, so neutral is the bottom end, 0;
 *   • `brightness` opens a per-voice velocity->brightness low-pass whose
 *     cutoff rises with the control, so neutral is the TOP end, 100.
 * The remaining controls (tremolo, space, echo, shimmer, ensemble,
 * chorusDepth) are macros onto FX units and are consumed by nothing today;
 * they are set to 0 = "nothing added" for consistency. `width` and
 * `chorusMode` keep their values: 0 width would mean "collapse to mono", which
 * is an active change rather than neutrality, and an enum has no off position.
 *
 * ONLY THE DEFAULTS MOVED. A saved preset that stores a value explicitly still
 * loads at that value (engineParamsFromJmiPreset / paramValuesFromVar seed
 * from these defaults and then overwrite every key the preset carries).
 */
export const WORLD_ENGINE_CONTROLS: Record<World, ParamSpec[]> = {
  'rhodes-ep': [
    { id: 'bark', label: 'Bark', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'tine', label: 'Tine', kind: 'float', min: 0, max: 100, default: 50 },
    { id: 'body', label: 'Body', kind: 'float', min: 0, max: 100, default: 50 },
    { id: 'tremolo', label: 'Tremolo', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'space', label: 'Space', kind: 'float', min: 0, max: 100, default: 0 },
  ],
  mks: [
    { id: 'chorusMode', label: 'Chorus Mode', kind: 'enum', choices: ['I', 'II', 'I+II'], default: 'I' },
    { id: 'chorusDepth', label: 'Chorus Depth', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'width', label: 'Width', kind: 'float', min: 0, max: 100, default: 60 },
    { id: 'ensemble', label: 'Ensemble', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'warmth', label: 'Warmth', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'air', label: 'Air', kind: 'float', min: 0, max: 100, default: 0 },
  ],
  fm: [
    { id: 'brightness', label: 'Brightness', kind: 'float', min: 0, max: 100, default: 100 },
    { id: 'attack', label: 'Attack', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'glass', label: 'Glass', kind: 'float', min: 0, max: 100, default: 50 },
    { id: 'chorusDepth', label: 'Chorus Depth', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'echo', label: 'Echo', kind: 'float', min: 0, max: 100, default: 0 },
  ],
  strings: [
    { id: 'swell', label: 'Swell', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'ensemble', label: 'Ensemble', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'shimmer', label: 'Shimmer', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'warmth', label: 'Warmth', kind: 'float', min: 0, max: 100, default: 0 },
    { id: 'motion', label: 'Motion', kind: 'float', min: 0, max: 100, default: 0 },
  ],
};

export function defaultEngineParams(world: World): Record<string, number | string | boolean> {
  const out: Record<string, number | string | boolean> = {};
  for (const p of WORLD_ENGINE_CONTROLS[world]) out[p.id] = p.default;
  return out;
}
