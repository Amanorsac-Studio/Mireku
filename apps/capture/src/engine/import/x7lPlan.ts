/**
 * X7L → `.jmi` conversion planning (§3.7). Pure and Node-safe: it turns a
 * parsed library into one bundle plan per instrument, with no I/O and no
 * audio decoding. The Electron main process executes the plan; the Import
 * screen renders it so the user sees what is inside *before* converting.
 *
 * Two deliberate decisions live here, both surfaced in the UI:
 *
 *  • **One bundle per instrument.** Waveforms are grouped by their key map:
 *    every velocity layer of one instrument shares an identical set of
 *    keyzones, and two different instruments never do. This is structural,
 *    not name-based, so it holds for libraries with awkward naming.
 *
 *  • **Velocity layers.** `instrument.json` allows at most four layers but a
 *    source may carry more. Rather than dropping audio, the two adjacent
 *    layers closest in level share one velocity window as round robins. The
 *    user can pick a different pair.
 */

import type { Instrument, LoopRegion, SoundType, VelocityLayer, World, Zone } from '@jm/jmi-format/web';
import { sampleFileName } from '@jm/jmi-format/web';
import { bundleFolderName } from '../export';
import type { X7lKeyzone, X7lLibrary, X7lWaveform } from './x7l';

/** The `.jmi` schema caps `velocityMap` at four layers. */
export const MAX_VELOCITY_LAYERS = 4;

export interface PlannedSample {
  /** bundle-relative path, e.g. "samples/C3_v2_rr1.wav" */
  file: string;
  waveformIndex: number;
  keyzoneIndex: number;
  layer: number;
  rr: number;
}

export interface PlannedZone {
  rootNote: number;
  keyRangeLo: number;
  keyRangeHi: number;
  velRangeLo: number;
  velRangeHi: number;
  samples: PlannedSample[];
}

export interface PlannedVelocityWindow {
  layer: VelocityLayer['layer'];
  name: string;
  lo: number;
  hi: number;
  /** source waveform indices feeding this window (2 when a merge lands here) */
  waveformIndices: number[];
}

export interface MergeOption {
  /** merge source layer `index` with `index + 1` (0-based) */
  index: number;
  label: string;
  /** how far apart the two layers are in peak level — smaller is a safer merge */
  levelGapDb: number;
}

export interface X7lInstrumentPlan {
  index: number;
  name: string;
  bundleName: string;
  world: World;
  soundType: SoundType;
  /** source waveform indices, quietest first */
  waveformIndices: number[];
  layerNames: string[];
  layerPeakDb: number[];
  keyzoneCount: number;
  keyRangeLo: number;
  keyRangeHi: number;
  sampleRate: number;
  windows: PlannedVelocityWindow[];
  zones: PlannedZone[];
  /** chosen merge (0-based source-layer index), or null when none was needed */
  mergeAt: number | null;
  mergeOptions: MergeOption[];
  sampleCount: number;
  totalPcmBytes: number;
  totalDurationSec: number;
  notes: string[];
}

export interface X7lImportPlan {
  version: string;
  instruments: X7lInstrumentPlan[];
  totalPcmBytes: number;
  notes: string[];
}

/* ------------------------------------------------------------------ */
/* grouping                                                            */
/* ------------------------------------------------------------------ */

/** A key map fingerprint: identical fingerprints mean the same instrument. */
export function keyMapSignature(wf: X7lWaveform): string {
  return wf.keyzones.map((z) => `${z.keyRangeLo}-${z.keyRangeHi}@${z.rootNote}`).join(',');
}

/** Longest shared prefix of the layer names, trimmed to a tidy instrument name. */
export function commonInstrumentName(names: string[]): string {
  if (names.length === 0) return 'Instrument';
  if (names.length === 1) return names[0];
  let n = 0;
  const first = names[0];
  outer: for (; n < first.length; n++) {
    for (const other of names) {
      if (other[n] !== first[n]) break outer;
    }
  }
  const prefix = first.slice(0, n).replace(/[\s\-_:]+$/, '').trim();
  return prefix.length >= 2 ? prefix : first;
}

/** World inference from the instrument name; `mks` is the conservative default. */
export function inferWorld(name: string): World {
  const n = name.toLowerCase();
  if (/\bmks\b|mks\d/.test(n)) return 'mks';
  if (/rhodes|wurli|\bep\b|ep\d|electric piano/.test(n)) return 'rhodes-ep';
  if (/\bfm\b|\bdx\b|dx7/.test(n)) return 'fm';
  if (/string|violin|viola|cello|orchestr/.test(n)) return 'strings';
  return 'mks';
}

/* ------------------------------------------------------------------ */
/* velocity windows                                                    */
/* ------------------------------------------------------------------ */

/**
 * Split 1..127 into `count` contiguous windows. With `mergeAt` set, source
 * layers `mergeAt` and `mergeAt + 1` collapse into a single window that spans
 * both of their ranges, so the result still tiles 1..127 with no gap.
 */
export function velocityWindows(count: number, mergeAt: number | null): { lo: number; hi: number; sources: number[] }[] {
  const edges: number[] = [];
  for (let i = 0; i <= count; i++) edges.push(1 + Math.round((i * 127) / count));
  const raw = [];
  for (let i = 0; i < count; i++) raw.push({ lo: edges[i], hi: i === count - 1 ? 127 : edges[i + 1] - 1, sources: [i] });
  if (mergeAt === null) return raw;
  if (mergeAt < 0 || mergeAt + 1 >= count) throw new Error(`Cannot merge layer ${mergeAt} of ${count}`);
  const merged = {
    lo: raw[mergeAt].lo,
    hi: raw[mergeAt + 1].hi,
    sources: [mergeAt, mergeAt + 1],
  };
  return [...raw.slice(0, mergeAt), merged, ...raw.slice(mergeAt + 2)];
}

const WINDOW_NAMES = ['Soft', 'Medium', 'Hard', 'Very Hard'];

/* ------------------------------------------------------------------ */
/* plan                                                                */
/* ------------------------------------------------------------------ */

export interface PlanOptions {
  /** per-instrument override of which adjacent pair to merge (0-based) */
  merges?: Record<number, number | null>;
}

/**
 * Build the conversion plan.
 *
 * `levelsDb[i]` is the measured peak of waveform `i` (one representative
 * keyzone is enough). Layers are ordered by that measurement, and a note is
 * raised if the library's own catalogue order disagrees — the audio wins.
 */
export function planImport(lib: X7lLibrary, levelsDb: number[], opts: PlanOptions = {}): X7lImportPlan {
  if (levelsDb.length !== lib.waveforms.length) {
    throw new Error(`Expected ${lib.waveforms.length} level measurements, got ${levelsDb.length}`);
  }

  const groups = new Map<string, X7lWaveform[]>();
  for (const wf of lib.waveforms) {
    const sig = keyMapSignature(wf);
    const list = groups.get(sig);
    if (list) list.push(wf);
    else groups.set(sig, [wf]);
  }

  const notes: string[] = [];
  const instruments: X7lInstrumentPlan[] = [];
  let index = 0;
  const usedNames = new Set<string>();

  for (const members of groups.values()) {
    const byLevel = [...members].sort((a, b) => levelsDb[a.index] - levelsDb[b.index]);
    const perInstrumentNotes: string[] = [];
    if (byLevel.some((wf, i) => wf.index !== members[i].index)) {
      perInstrumentNotes.push(
        'Velocity layer order was taken from the measured level of the audio, not the library catalogue order.',
      );
    }

    let name = commonInstrumentName(byLevel.map((w) => w.name));
    let suffix = 2;
    while (usedNames.has(name.toLowerCase())) name = `${commonInstrumentName(byLevel.map((w) => w.name))} ${suffix++}`;
    usedNames.add(name.toLowerCase());

    const layerCount = byLevel.length;
    const mergeOptions: MergeOption[] = [];
    for (let i = 0; i + 1 < layerCount; i++) {
      mergeOptions.push({
        index: i,
        label: `${byLevel[i].name} + ${byLevel[i + 1].name}`,
        levelGapDb: round2(Math.abs(levelsDb[byLevel[i + 1].index] - levelsDb[byLevel[i].index])),
      });
    }

    let mergeAt: number | null = null;
    if (layerCount > MAX_VELOCITY_LAYERS) {
      if (layerCount > MAX_VELOCITY_LAYERS + 1) {
        throw new Error(
          `"${name}" has ${layerCount} velocity layers; the .jmi format allows ${MAX_VELOCITY_LAYERS} and this importer can fold at most one pair.`,
        );
      }
      const override = opts.merges?.[index];
      mergeAt =
        override !== undefined && override !== null
          ? override
          : mergeOptions.reduce((best, o) => (o.levelGapDb < best.levelGapDb ? o : best)).index;
      const chosen = mergeOptions[mergeAt];
      perInstrumentNotes.push(
        `Source has ${layerCount} velocity layers but .jmi allows ${MAX_VELOCITY_LAYERS}. ` +
          `Layers "${chosen.label}" (${chosen.levelGapDb} dB apart) share one velocity window as round robins — no audio is dropped.`,
      );
    }

    const windows = velocityWindows(layerCount, mergeAt).map((w, i) => ({
      layer: (i + 1) as VelocityLayer['layer'],
      name: WINDOW_NAMES[i] ?? `Layer ${i + 1}`,
      lo: w.lo,
      hi: w.hi,
      waveformIndices: w.sources.map((s) => byLevel[s].index),
    }));

    const template = byLevel[0].keyzones;
    const zones: PlannedZone[] = [];
    const seenFiles = new Set<string>();
    for (const win of windows) {
      template.forEach((kz: X7lKeyzone, kzIndex: number) => {
        const samples: PlannedSample[] = win.waveformIndices.map((wfIndex, rrIndex) => {
          const file = `samples/${sampleFileName(kz.rootNote, win.layer, rrIndex + 1)}`;
          if (seenFiles.has(file)) {
            throw new Error(`Sample name collision building "${name}": ${file}`);
          }
          seenFiles.add(file);
          return { file, waveformIndex: wfIndex, keyzoneIndex: kzIndex, layer: win.layer, rr: rrIndex + 1 };
        });
        zones.push({
          rootNote: kz.rootNote,
          keyRangeLo: kz.keyRangeLo,
          keyRangeHi: kz.keyRangeHi,
          velRangeLo: win.lo,
          velRangeHi: win.hi,
          samples,
        });
      });
    }

    const sustaining = byLevel.some((wf) => !wf.oneShot);
    if (!sustaining) {
      perInstrumentNotes.push(
        'Every keyzone declares its loop end at the final sample and the tails decay to digital silence, so the samples are imported as one-shots (soundType "decaying", loop null).',
      );
    }

    let totalPcmBytes = 0;
    let totalSamples = 0;
    for (const wf of byLevel) {
      totalPcmBytes += wf.totalPcmBytes;
      for (const z of wf.keyzones) totalSamples += z.lengthSamples;
    }

    instruments.push({
      index,
      name,
      bundleName: bundleFolderName(name),
      world: inferWorld(name),
      soundType: sustaining ? 'sustaining' : 'decaying',
      waveformIndices: byLevel.map((w) => w.index),
      layerNames: byLevel.map((w) => w.name),
      layerPeakDb: byLevel.map((w) => round2(levelsDb[w.index])),
      keyzoneCount: template.length,
      keyRangeLo: Math.min(...template.map((z) => z.keyRangeLo)),
      keyRangeHi: Math.max(...template.map((z) => z.keyRangeHi)),
      sampleRate: template[0].sampleRate,
      windows,
      zones,
      mergeAt,
      mergeOptions,
      sampleCount: zones.reduce((n, z) => n + z.samples.length, 0),
      totalPcmBytes,
      totalDurationSec: round2(totalSamples / (template[0]?.sampleRate || 44100)),
      notes: perInstrumentNotes,
    });
    index++;
  }

  return { version: lib.version, instruments, totalPcmBytes: lib.totalPcmBytes, notes };
}

/* ------------------------------------------------------------------ */
/* instrument.json                                                     */
/* ------------------------------------------------------------------ */

export interface SampleFacts {
  peakDb: number;
  lengthMs: number;
  loop: LoopRegion | null;
}

export interface BuildInstrumentOptions {
  author: string;
  createdAt?: string;
  sourceFileName: string;
}

/**
 * `meta.captureMode` only admits "guided" | "auto". An import is neither, and
 * adding a third value would change the shared format for Signature and the
 * validator too, so it is *not* changed here: the closest legal value is used
 * and the real provenance is written into `meta.notes`.
 */
export const IMPORT_CAPTURE_MODE = 'auto' as const;

export function buildImportInstrument(
  plan: X7lInstrumentPlan,
  facts: Record<string, SampleFacts>,
  opts: BuildInstrumentOptions,
): Instrument {
  const zones: Zone[] = plan.zones.map((z) => ({
    rootNote: z.rootNote,
    keyRangeLo: z.keyRangeLo,
    keyRangeHi: z.keyRangeHi,
    velRangeLo: z.velRangeLo,
    velRangeHi: z.velRangeHi,
    roundRobins: z.samples.map((s) => {
      const f = facts[s.file];
      if (!f) throw new Error(`No measurement for ${s.file}`);
      return { file: s.file, peakDb: f.peakDb, lengthMs: f.lengthMs, loop: f.loop };
    }),
  }));

  const provenance = [
    `Imported from the Yamaha MONTAGE library "${opts.sourceFileName}".`,
    `The source PCM was copied through unchanged — ${plan.sampleRate} Hz, 16-bit, mono, bit-perfect; nothing was resampled or requantised.`,
    'captureMode is recorded as "auto" because the .jmi format has no value for an import; this bundle was not recorded.',
    ...plan.notes,
  ].join(' ');

  return {
    formatVersion: 1,
    meta: {
      name: plan.name,
      world: plan.world,
      author: opts.author,
      createdAt: opts.createdAt ?? new Date().toISOString(),
      captureMode: IMPORT_CAPTURE_MODE,
      channels: 'mono',
      sampleRate: plan.sampleRate,
      bitDepth: 16,
      notes: provenance,
    },
    soundType: plan.soundType,
    zones,
    velocityMap: plan.windows.map((w) => ({ layer: w.layer, name: w.name, lo: w.lo, hi: w.hi })),
    reserved: { releaseSamples: [], pedalDownZones: [] },
  };
}

function round2(n: number): number {
  return Math.round(n * 100) / 100;
}
