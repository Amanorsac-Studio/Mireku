/**
 * Sound model — a layered "sound" (Montage-style performance): up to eight
 * parts, each an instrument from the Sanctuary library with its own key /
 * velocity window, octave, level, pan, engine macros, FX rack and envelope.
 *
 * Serialises to `<Library>/Sounds/<name>.sound.json`, whose `parts` are exactly
 * Sanctuary's rig-slot JSON (`slotToVar` in StageModel.h), so the stage app
 * loads a sound straight into free slots.
 */

import type { AmpEnvelope, FxUnitState, Instrument, Preset, World } from '@jm/jmi-format/web';
import { buildDefaultRack, defaultEngineParams } from '@jm/jmi-format/web';

export const MAX_PARTS = 8;

export interface Part {
  id: string;
  bundleDir: string;
  bundleName: string;
  world: World;
  category: string;
  label: string;
  presetName: string;
  keyLo: number;
  keyHi: number;
  velLo: number;
  velHi: number;
  octaveShift: number;
  volumeDb: number;
  pan: number;
  enabled: boolean;
  engine: Record<string, number | string | boolean>;
  fxRack: FxUnitState[];
  ampEnvelope: AmpEnvelope;
}

export interface Sound {
  formatVersion: 1;
  name: string;
  category: string;
  parts: Part[];
}

export const PART_COLOURS = ['#6C8CF5', '#38BDE0', '#8B6BF5', '#C766E8', '#F2648F', '#4FD1C5', '#F5A65B', '#9AE66E'];

let seq = 0;
export function newPartId(): string {
  return `p${Date.now().toString(36)}${(seq++).toString(36)}`;
}

export function emptySound(name = 'New Sound'): Sound {
  return { formatVersion: 1, name, category: 'FX & Other', parts: [] };
}

/** A part built from a library instrument, starting from its first preset (or the world defaults). */
export function partFromInstrument(
  entry: { dir: string; name: string; category: string },
  instrument: Instrument,
  presets: Preset[],
): Part {
  const world = instrument.meta.world;
  const p = presets[0];
  return {
    id: newPartId(),
    bundleDir: entry.dir,
    bundleName: instrument.meta.name || entry.name,
    world,
    category: entry.category,
    label: instrument.meta.name || entry.name,
    presetName: p?.name ?? 'Init',
    keyLo: 0,
    keyHi: 127,
    velLo: 1,
    velHi: 127,
    octaveShift: 0,
    volumeDb: 0,
    pan: 0,
    enabled: true,
    engine: p?.engine ?? defaultEngineParams(world),
    fxRack: p?.fxRack ?? buildDefaultRack(world),
    ampEnvelope: p?.ampEnvelope ?? {
      attackMs: instrument.soundType === 'sustaining' ? 10 : 2,
      decayMs: 0,
      sustain: 100,
      releaseMs: instrument.soundType === 'sustaining' ? 400 : 250,
    },
  };
}

/** The on-disk shape: parts as Sanctuary rig slots. */
export function toSoundJson(sound: Sound): string {
  const parts = sound.parts.map((p) => ({
    world: p.world,
    bundleDir: p.bundleDir,
    bundleName: p.bundleName,
    presetName: p.presetName,
    label: p.label,
    engine: p.engine,
    fxRack: p.fxRack,
    ampEnvelope: p.ampEnvelope,
    keyLo: p.keyLo,
    keyHi: p.keyHi,
    velLo: p.velLo,
    velHi: p.velHi,
    octaveShift: p.octaveShift,
    channel: 0,
    ignoreWheel: false,
    volumeDb: p.volumeDb,
    pan: p.pan,
    enabled: p.enabled,
  }));
  return JSON.stringify({ formatVersion: 1, name: sound.name, category: sound.category, parts }, null, 2);
}

export function fromSoundJson(json: string, categoryOf: (bundleDir: string, bundleName: string) => string): Sound {
  const raw = JSON.parse(json) as { name?: string; category?: string; parts?: Record<string, unknown>[] };
  const parts: Part[] = (raw.parts ?? []).map((s) => ({
    id: newPartId(),
    bundleDir: String(s.bundleDir ?? ''),
    bundleName: String(s.bundleName ?? ''),
    world: (s.world as World) ?? 'rhodes-ep',
    category: categoryOf(String(s.bundleDir ?? ''), String(s.bundleName ?? '')),
    label: String(s.label ?? s.bundleName ?? ''),
    presetName: String(s.presetName ?? 'Init'),
    keyLo: Number(s.keyLo ?? 0),
    keyHi: Number(s.keyHi ?? 127),
    velLo: Number(s.velLo ?? 1),
    velHi: Number(s.velHi ?? 127),
    octaveShift: Number(s.octaveShift ?? 0),
    volumeDb: Number(s.volumeDb ?? 0),
    pan: Number(s.pan ?? 0),
    enabled: s.enabled !== false,
    engine: (s.engine as Part['engine']) ?? {},
    fxRack: (s.fxRack as FxUnitState[]) ?? [],
    ampEnvelope: (s.ampEnvelope as AmpEnvelope) ?? { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 },
  }));
  return { formatVersion: 1, name: raw.name ?? 'Sound', category: raw.category ?? 'FX & Other', parts };
}

export const dbToGain = (db: number) => Math.pow(10, db / 20);
