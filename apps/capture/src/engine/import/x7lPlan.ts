/**
 * X7L → `.jmi` conversion planning (§3.7). Pure and Node-safe: it turns a
 * parsed library into one bundle plan per instrument, with no I/O and no
 * audio decoding. The Electron main process executes the plan; the Import
 * screen renders it so the user sees what is inside *before* converting.
 *
 * Three deliberate decisions live here, all surfaced in the UI:
 *
 *  • **Key-split partners are merged.** Some libraries spread one instrument
 *    across several waveforms, each holding every fourth root note (Crystal
 *    Rhodes' "Crystal A/C/D/F" are the A-, C-, D#- and F#-rooted thirds of a
 *    single 3-semitone sample ladder). Taking those as separate instruments
 *    would give four bundles each playable over a third of the keyboard. They
 *    are merged only when the data proves the relationship — see
 *    `findKeySplitGroups`.
 *
 *  • **Velocity layers come from wherever the source puts them.** A library
 *    may split velocity *inside* one waveform's key map (Crystal Rhodes has
 *    eight windows per note) or *across* waveforms of equal key map (MKS20
 *    p/mp/mf/f). Both are handled; where the source declares its own windows
 *    they are used verbatim rather than re-split.
 *
 *  • **Waveforms are only layered together when their names agree.** Grouping
 *    on key map alone is not safe: `Montage Triton Library.X7L` has six
 *    unrelated waveforms ("Brass Low", "M1 Organ", "analogpad High" …) sharing
 *    one key map, and stacking those as velocity layers of one instrument
 *    would be silently wrong. Waveforms with a shared key map become layers of
 *    one instrument only when their names differ by a dynamic or variant token
 *    alone; otherwise each becomes its own bundle.
 */

import type { Instrument, LoopRegion, SoundType, VelocityLayer, World, Zone } from '@jm/jmi-format/web';
import { sampleFileName } from '@jm/jmi-format/web';
import { bundleFolderName } from '../export';
import type { X7lLibrary, X7lWaveform } from './x7l';

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
  /** source layer indices feeding this window (2+ when a fold lands here) */
  sources: number[];
  /** source waveform indices feeding this window */
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
  /** 1 = mono bundle, 2 = stereo bundle */
  channels: 1 | 2;
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
/* names                                                               */
/* ------------------------------------------------------------------ */

/**
 * Tokens that distinguish one layer or variant of an instrument from another
 * rather than naming a different instrument: dynamic markings, and short
 * alphanumeric variant tags like "1", "D2" or "pn1".
 *
 * `low` / `high` are deliberately absent. In `Montage Triton Library.X7L`
 * "Brass Low" and "Brass High" share a key map but are two distinct samples,
 * and nothing in the file says which way round they are layered — so they stay
 * separate bundles instead of being guessed into one.
 */
const DYNAMIC_TOKENS = new Set([
  'ppp', 'pp', 'p', 'mp', 'm', 'mf', 'f', 'ff', 'fff',
  'soft', 'med', 'medium', 'hard', 'light', 'norm',
]);

function isDynamicToken(tok: string): boolean {
  return DYNAMIC_TOKENS.has(tok.toLowerCase());
}

/** A short alphanumeric variant tag: "1", "D2", "pn1". */
function isNumberedVariant(tok: string): boolean {
  return /^[a-z]{0,3}\d{1,2}$/i.test(tok);
}

/**
 * The instrument name a waveform belongs to: its name with the tokens that
 * distinguish one layer or variant from another removed.
 *
 * A numbered variant counts only as the *last* token, so "MK 80 p" keeps its
 * model number and "DYNO 1" loses its layer number; dynamic markings are
 * dropped wherever they sit, so "AZ p SE" and "AZ mp SE" both reduce to
 * "AZ SE". "Strings MF" and "Strings P" share the stem "Strings"; "Brass Low"
 * and "M1 Organ" do not share one.
 */
export function waveformStem(name: string): string {
  let toks = name.split(/\s+/).filter((t) => t.length > 0);
  if (toks.length > 1 && isNumberedVariant(toks[toks.length - 1])) toks = toks.slice(0, -1);
  const kept = toks.filter((t) => !isDynamicToken(t));
  return kept.join(' ').trim();
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
  if (/rhodes|wurli|\bep\b|ep\d|electric piano|dyno/.test(n)) return 'rhodes-ep';
  if (/\bfm\b|\bdx\b|dx7/.test(n)) return 'fm';
  if (/string|violin|viola|cello|orchestr|symphon/.test(n)) return 'strings';
  return 'mks';
}

/* ------------------------------------------------------------------ */
/* the logical shape of a waveform                                     */
/* ------------------------------------------------------------------ */

/** A key map fingerprint: identical fingerprints mean the same key layout. */
export function keyMapSignature(wf: X7lWaveform): string {
  return [...new Set(wf.keyzones.map((z) => `${z.keyRangeLo}-${z.keyRangeHi}@${z.rootNote}`))]
    .sort()
    .join(',');
}

/** The distinct velocity windows a waveform's key map declares, lowest first. */
export function velocitySplits(wf: X7lWaveform): { lo: number; hi: number }[] {
  const seen = new Map<string, { lo: number; hi: number }>();
  for (const z of wf.keyzones) seen.set(`${z.velRangeLo}-${z.velRangeHi}`, { lo: z.velRangeLo, hi: z.velRangeHi });
  return [...seen.values()].sort((a, b) => a.lo - b.lo);
}

function velocitySplitKey(wf: X7lWaveform): string {
  return velocitySplits(wf).map((w) => `${w.lo}-${w.hi}`).join(',');
}

function rootsOf(wf: X7lWaveform): number[] {
  return [...new Set(wf.keyzones.map((z) => z.rootNote))].sort((a, b) => a - b);
}

/** The constant difference of an arithmetic progression, or null. */
function arithmeticStep(xs: number[]): number | null {
  if (xs.length < 2) return null;
  const d = xs[1] - xs[0];
  if (d <= 0) return null;
  for (let i = 2; i < xs.length; i++) if (xs[i] - xs[i - 1] !== d) return null;
  return d;
}

/* ------------------------------------------------------------------ */
/* key-split partners                                                  */
/* ------------------------------------------------------------------ */

/**
 * Find sets of waveforms that are interleaved thirds (or halves, quarters …)
 * of one sample ladder.
 *
 * The test is entirely structural and deliberately narrow — every one of these
 * must hold before two waveforms are merged:
 *
 *   • identical velocity windows, channel count and sample rate;
 *   • root-note sets pairwise disjoint;
 *   • each member's roots an arithmetic progression with the same difference D;
 *   • D divisible by the member count k, and the members' lowest roots spaced
 *     exactly D/k apart — so the union is itself an arithmetic progression of
 *     difference D/k, with no root used twice and none missing;
 *   • the union's key ranges covering the whole span with no unmapped key.
 *
 * `Crystal Rhodes.X7L` satisfies all of it: four waveforms, roots every 12
 * semitones starting at 21, 24, 27 and 30, uniting into the 3-semitone ladder
 * 21…105 that covers MIDI 0–127 without a gap. Nothing else in the client's
 * corpus comes close, which is the point.
 */
export function findKeySplitGroups(waveforms: X7lWaveform[]): X7lWaveform[][] {
  const pools = new Map<string, X7lWaveform[]>();
  for (const wf of waveforms) {
    const key = `${velocitySplitKey(wf)}|${wf.channels}|${wf.keyzones[0].sampleRate}`;
    const list = pools.get(key);
    if (list) list.push(wf);
    else pools.set(key, [wf]);
  }

  const groups: X7lWaveform[][] = [];
  for (const pool of pools.values()) {
    if (pool.length < 2) continue;
    // group by the ladder spacing each waveform uses on its own
    const byStep = new Map<number, X7lWaveform[]>();
    for (const wf of pool) {
      const step = arithmeticStep(rootsOf(wf));
      if (step === null) continue;
      const list = byStep.get(step);
      if (list) list.push(wf);
      else byStep.set(step, [wf]);
    }
    for (const [D, members] of byStep) {
      const k = members.length;
      if (k < 2 || D % k !== 0) continue;
      const step = D / k;
      const sorted = [...members].sort((a, b) => rootsOf(a)[0] - rootsOf(b)[0]);
      const base = rootsOf(sorted[0])[0];
      let ok = true;
      const union = new Set<number>();
      for (let j = 0; j < k && ok; j++) {
        const roots = rootsOf(sorted[j]);
        if (roots[0] !== base + j * step) ok = false;
        for (const r of roots) {
          if ((r - roots[0]) % D !== 0) ok = false;
          if (union.has(r)) ok = false;
          union.add(r);
        }
      }
      if (!ok) continue;
      // the union must itself be an even ladder of `step`
      const unionRoots = [...union].sort((a, b) => a - b);
      if (arithmeticStep(unionRoots) !== step) continue;
      // and the key ranges must leave no unmapped key across the span
      const lo = Math.min(...sorted.flatMap((w) => w.keyzones.map((z) => z.keyRangeLo)));
      const hi = Math.max(...sorted.flatMap((w) => w.keyzones.map((z) => z.keyRangeHi)));
      const covered = new Uint8Array(128);
      for (const w of sorted) {
        for (const z of w.keyzones) for (let m = z.keyRangeLo; m <= z.keyRangeHi; m++) covered[m] = 1;
      }
      let gap = false;
      for (let m = lo; m <= hi; m++) if (!covered[m]) gap = true;
      if (gap) continue;
      groups.push(sorted);
    }
  }
  return groups;
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

/**
 * Fold the source's own velocity windows down to at most four, by grouping
 * adjacent windows as evenly as possible. Each output window spans its group
 * and keeps every source window as a round robin, so no audio is dropped and
 * the result still tiles 1..127.
 */
export function foldSourceWindows(
  source: { lo: number; hi: number }[],
  max = MAX_VELOCITY_LAYERS,
): { lo: number; hi: number; sources: number[] }[] {
  const n = source.length;
  if (n <= max) return source.map((w, i) => ({ lo: w.lo, hi: w.hi, sources: [i] }));
  const groups = max;
  const out: { lo: number; hi: number; sources: number[] }[] = [];
  let taken = 0;
  for (let g = 0; g < groups; g++) {
    const size = Math.floor((n - taken) / (groups - g)) + (((n - taken) % (groups - g)) > 0 ? 1 : 0);
    const sources: number[] = [];
    for (let i = 0; i < size; i++) sources.push(taken + i);
    out.push({ lo: source[taken].lo, hi: source[taken + size - 1].hi, sources });
    taken += size;
  }
  return out;
}

const WINDOW_NAMES = ['Soft', 'Medium', 'Hard', 'Very Hard'];

/* ------------------------------------------------------------------ */
/* plan                                                                */
/* ------------------------------------------------------------------ */

export interface PlanOptions {
  /** per-instrument override of which adjacent pair to merge (0-based) */
  merges?: Record<number, number | null>;
}

/** One source layer of an instrument: a waveform, optionally narrowed to one
 *  of that waveform's own velocity windows. */
interface SourceLayer {
  /** the unit supplying this layer's audio (one waveform, or key-split partners) */
  unit: Unit;
  /** velocity window within the key map, or null for "all of it" */
  vel: { lo: number; hi: number } | null;
  label: string;
}

/** A group of waveforms that make up one instrument. */
interface Unit {
  /** waveforms that together provide the key map (key-split partners) */
  members: X7lWaveform[];
  /** the instrument's key signature for layer grouping */
  signature: string;
  stem: string;
  keySplit: boolean;
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

  const notes: string[] = [];

  /* ---- 1. merge key-split partners into units ---- */
  const splitGroups = findKeySplitGroups(lib.waveforms);
  const claimed = new Set<number>();
  const units: Unit[] = [];
  for (const g of splitGroups) {
    if (g.some((w) => claimed.has(w.index))) continue;
    g.forEach((w) => claimed.add(w.index));
    units.push({
      members: g,
      signature: unitSignature(g),
      stem: waveformStem(commonInstrumentName(g.map((w) => w.name))) || g[0].name,
      keySplit: true,
    });
  }
  for (const wf of lib.waveforms) {
    if (claimed.has(wf.index)) continue;
    units.push({ members: [wf], signature: keyMapSignature(wf), stem: waveformStem(wf.name) || wf.name, keySplit: false });
  }
  if (splitGroups.length) {
    notes.push(
      `${splitGroups.length} set(s) of waveforms were merged as key-split partners of one instrument: ` +
        `their root notes interleave into a single even ladder covering the whole key range.`,
    );
  }

  /* ---- 2. group units into instruments by key map AND name stem ---- */
  const groups = new Map<string, Unit[]>();
  for (const u of units) {
    const key = `${u.signature}||${u.stem.toLowerCase()}`;
    const list = groups.get(key);
    if (list) list.push(u);
    else groups.set(key, [u]);
  }

  const instruments: X7lInstrumentPlan[] = [];
  let index = 0;
  const usedNames = new Set<string>();

  for (const unitGroup of groups.values()) {
    const plan = planInstrument(unitGroup, levelsDb, index, usedNames, opts);
    instruments.push(plan);
    index++;
  }

  return { version: lib.version, instruments, totalPcmBytes: lib.totalPcmBytes, notes };
}

function unitSignature(members: X7lWaveform[]): string {
  return [
    ...new Set(members.flatMap((w) => w.keyzones.map((z) => `${z.keyRangeLo}-${z.keyRangeHi}@${z.rootNote}`))),
  ]
    .sort()
    .join(',');
}

/**
 * One key/velocity slot of the merged key map, with the source keyzone that
 * fills it.
 */
interface Slot {
  rootNote: number;
  keyRangeLo: number;
  keyRangeHi: number;
  waveformIndex: number;
  keyzoneIndex: number;
}

function planInstrument(
  unitGroup: Unit[],
  levelsDb: number[],
  index: number,
  usedNames: Set<string>,
  opts: PlanOptions,
): X7lInstrumentPlan {
  const perInstrumentNotes: string[] = [];

  // Units in a group are velocity layers of one another; order them by the
  // measured level of their loudest member, quietest first.
  const unitLevel = (u: Unit): number =>
    u.members.reduce((best, w) => Math.max(best, levelsDb[w.index]), -Infinity);
  const byLevel = [...unitGroup].sort((a, b) => unitLevel(a) - unitLevel(b));
  if (byLevel.some((u, i) => u !== unitGroup[i])) {
    perInstrumentNotes.push(
      'Velocity layer order was taken from the measured level of the audio, not the library catalogue order.',
    );
  }

  const allWaveforms = byLevel.flatMap((u) => u.members);
  const template = byLevel[0];
  const sourceSplits = velocitySplits(template.members[0]);

  // Verify the source's own velocity windows tile 1..127 before trusting them.
  let splitsUsable = sourceSplits.length > 1;
  if (splitsUsable) {
    if (sourceSplits[0].lo !== 1 || sourceSplits[sourceSplits.length - 1].hi !== 127) splitsUsable = false;
    for (let i = 1; i < sourceSplits.length; i++) {
      if (sourceSplits[i].lo !== sourceSplits[i - 1].hi + 1) splitsUsable = false;
    }
    if (!splitsUsable) {
      throw new Error(
        `"${template.members[0].name}" declares ${sourceSplits.length} velocity windows in its key map ` +
          `(${sourceSplits.map((w) => `${w.lo}-${w.hi}`).join(', ')}) but they do not tile 1..127 without a ` +
          `gap or overlap, so the velocity mapping cannot be reproduced faithfully.`,
      );
    }
  }
  if (splitsUsable && byLevel.length > 1) {
    throw new Error(
      `This instrument has ${byLevel.length} waveform layers *and* ${sourceSplits.length} velocity windows ` +
        `inside each key map. No library in the verified corpus combines the two, so the importer will not ` +
        `guess how they stack.`,
    );
  }

  /* ---- source layers ---- */
  const unitLabel = (u: Unit): string =>
    u.members.length > 1 ? commonInstrumentName(u.members.map((w) => w.name)) : u.members[0].name;
  const layers: SourceLayer[] = splitsUsable
    ? sourceSplits.map((v) => ({
        unit: template,
        vel: v,
        label: `${unitLabel(template)} vel ${v.lo}-${v.hi}`,
      }))
    : byLevel.map((u) => ({ unit: u, vel: null, label: unitLabel(u) }));

  /* ---- name ---- */
  const stemName = template.stem || commonInstrumentName(allWaveforms.map((w) => w.name));
  let name = stemName;
  let suffix = 2;
  while (usedNames.has(name.toLowerCase())) name = `${stemName} ${suffix++}`;
  usedNames.add(name.toLowerCase());

  /* ---- velocity windows ---- */
  const layerCount = layers.length;
  const mergeOptions: MergeOption[] = [];
  if (!splitsUsable) {
    for (let i = 0; i + 1 < layerCount; i++) {
      mergeOptions.push({
        index: i,
        label: `${layers[i].label} + ${layers[i + 1].label}`,
        levelGapDb: round2(Math.abs(unitLevel(byLevel[i + 1]) - unitLevel(byLevel[i]))),
      });
    }
  }

  let mergeAt: number | null = null;
  let rawWindows: { lo: number; hi: number; sources: number[] }[];

  if (splitsUsable) {
    rawWindows = foldSourceWindows(sourceSplits);
    if (sourceSplits.length > MAX_VELOCITY_LAYERS) {
      perInstrumentNotes.push(
        `The source key map declares ${sourceSplits.length} velocity windows but .jmi allows ` +
          `${MAX_VELOCITY_LAYERS}. Adjacent windows were grouped into ${rawWindows.length} ` +
          `(${rawWindows.map((w) => `${w.lo}-${w.hi}: ${w.sources.length} layer${w.sources.length === 1 ? '' : 's'}`).join(', ')}) ` +
          `and the windows sharing a group play as round robins — no audio is dropped, and each window ` +
          `keeps the velocity span the library gave it.`,
      );
    } else {
      perInstrumentNotes.push(
        `Velocity windows were taken from the source key map verbatim ` +
          `(${sourceSplits.map((w) => `${w.lo}-${w.hi}`).join(', ')}).`,
      );
    }
  } else if (layerCount > MAX_VELOCITY_LAYERS) {
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
    rawWindows = velocityWindows(layerCount, mergeAt);
  } else {
    rawWindows = velocityWindows(layerCount, null);
  }

  const windows: PlannedVelocityWindow[] = rawWindows.map((w, i) => ({
    layer: (i + 1) as VelocityLayer['layer'],
    name: WINDOW_NAMES[i] ?? `Layer ${i + 1}`,
    lo: w.lo,
    hi: w.hi,
    sources: w.sources,
    waveformIndices: [...new Set(w.sources.flatMap((s) => layers[s].unit.members.map((m) => m.index)))],
  }));

  /* ---- the merged key map ---- */
  const slots = mergedSlots(template, perInstrumentNotes);

  /* ---- zones and sample names ---- */
  // `rr` is only there to keep sample file names unique and to index the
  // alternatives available at one note and layer. A per-(root, layer) counter
  // guarantees uniqueness however awkward the source key map is.
  const rrCounter = new Map<string, number>();
  const nextRr = (root: number, layer: number): number => {
    const key = `${root}/${layer}`;
    const n = (rrCounter.get(key) ?? 0) + 1;
    rrCounter.set(key, n);
    return n;
  };

  const zones: PlannedZone[] = [];
  const seenFiles = new Set<string>();
  for (const win of windows) {
    for (const slot of slots) {
      const samples: PlannedSample[] = [];
      for (const src of win.sources) {
        const layer = layers[src];
        const wf = sourceWaveformFor(layer.unit, slot);
        const kzIndex = keyzoneIndexFor(wf, slot, layer.vel);
        if (kzIndex === null) continue;
        const rr = nextRr(slot.rootNote, win.layer);
        const file = `samples/${sampleFileName(slot.rootNote, win.layer, rr)}`;
        if (seenFiles.has(file)) {
          throw new Error(`Sample name collision building "${name}": ${file}`);
        }
        seenFiles.add(file);
        samples.push({ file, waveformIndex: wf.index, keyzoneIndex: kzIndex, layer: win.layer, rr });
      }
      if (samples.length === 0) continue;
      zones.push({
        rootNote: slot.rootNote,
        keyRangeLo: slot.keyRangeLo,
        keyRangeHi: slot.keyRangeHi,
        velRangeLo: win.lo,
        velRangeHi: win.hi,
        samples,
      });
    }
  }

  const sustaining = allWaveforms.some((wf) => !wf.oneShot);
  if (!sustaining) {
    perInstrumentNotes.push(
      'Every keyzone declares a loop spanning the whole sample, so there is nothing to loop: the samples ' +
        'are imported as one-shots (soundType "decaying", loop null).',
    );
  }

  const channels = template.members[0].channels;
  if (channels === 2) {
    perInstrumentNotes.push(
      'The source waveforms are stereo: each keyzone stores its left and right channels as two ' +
        'consecutive mono blocks, which are interleaved into one stereo WAV here. Every 16-bit sample ' +
        'word is the source word, unchanged.',
    );
  }

  let totalPcmBytes = 0;
  let totalFrames = 0;
  for (const wf of allWaveforms) {
    totalPcmBytes += wf.totalPcmBytes;
    for (const z of wf.keyzones) totalFrames += z.lengthSamples;
  }

  const sampleRate = template.members[0].keyzones[0].sampleRate;
  return {
    index,
    name,
    bundleName: bundleFolderName(name),
    world: inferWorld(name),
    soundType: sustaining ? 'sustaining' : 'decaying',
    waveformIndices: allWaveforms.map((w) => w.index),
    layerNames: layers.map((l) => l.label),
    layerPeakDb: layers.map((l) => round2(unitLevel(l.unit))),
    keyzoneCount: slots.length,
    keyRangeLo: Math.min(...slots.map((s) => s.keyRangeLo)),
    keyRangeHi: Math.max(...slots.map((s) => s.keyRangeHi)),
    sampleRate,
    channels,
    windows,
    zones,
    mergeAt,
    mergeOptions,
    sampleCount: zones.reduce((n, z) => n + z.samples.length, 0),
    totalPcmBytes,
    totalDurationSec: round2(totalFrames / (sampleRate || 44100)),
    notes: perInstrumentNotes,
  };
}

/**
 * The merged key map of a unit: one slot per root note.
 *
 * For a single waveform the source key ranges are kept exactly. For key-split
 * partners the members' ranges overlap at the top of the keyboard (each
 * member's highest sample is stretched to key 127), so boundaries are taken
 * from the roots instead — midpoints between adjacent roots, the same rule
 * `keyRanges()` uses for a recorded bundle. That touches the mapping only,
 * never the audio.
 */
function mergedSlots(unit: Unit, notes: string[]): Slot[] {
  const perRoot = new Map<number, Slot>();
  for (const wf of unit.members) {
    for (const z of wf.keyzones) {
      if (perRoot.has(z.rootNote)) continue;
      perRoot.set(z.rootNote, {
        rootNote: z.rootNote,
        keyRangeLo: z.keyRangeLo,
        keyRangeHi: z.keyRangeHi,
        waveformIndex: wf.index,
        keyzoneIndex: z.index,
      });
    }
  }
  const slots = [...perRoot.values()].sort((a, b) => a.rootNote - b.rootNote);
  if (unit.members.length === 1) return slots;

  const lo = Math.min(...unit.members.flatMap((w) => w.keyzones.map((z) => z.keyRangeLo)));
  const hi = Math.max(...unit.members.flatMap((w) => w.keyzones.map((z) => z.keyRangeHi)));
  for (let i = 0; i < slots.length; i++) {
    const prev = slots[i - 1];
    const next = slots[i + 1];
    slots[i].keyRangeLo = prev ? Math.floor((prev.rootNote + slots[i].rootNote) / 2) + 1 : lo;
    slots[i].keyRangeHi = next ? Math.floor((slots[i].rootNote + next.rootNote) / 2) : hi;
  }
  notes.push(
    `Key-split partners were merged into one ${slots.length}-note key map spanning ${lo}..${hi}. ` +
      `The source ranges overlap where each partner's top sample is stretched to the end of the keyboard, ` +
      `so zone boundaries were placed at the midpoints between adjacent root notes.`,
  );
  return slots;
}

/** For a key-split unit, the member waveform that owns `slot`'s root note. */
function sourceWaveformFor(unit: Unit, slot: Slot): X7lWaveform {
  for (const wf of unit.members) {
    if (wf.keyzones.some((z) => z.rootNote === slot.rootNote)) return wf;
  }
  return unit.members[0];
}

/** The keyzone inside `wf` for this slot's root and (optional) velocity window. */
function keyzoneIndexFor(
  wf: X7lWaveform,
  slot: Slot,
  vel: { lo: number; hi: number } | null,
): number | null {
  for (const z of wf.keyzones) {
    if (z.rootNote !== slot.rootNote) continue;
    if (vel && (z.velRangeLo !== vel.lo || z.velRangeHi !== vel.hi)) continue;
    return z.index;
  }
  return null;
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
    `Imported from the Yamaha YSFC library "${opts.sourceFileName}".`,
    `The source PCM was copied through unchanged — ${plan.sampleRate} Hz, 16-bit, ` +
      `${plan.channels === 2 ? 'stereo' : 'mono'}, bit-perfect; nothing was resampled or requantised.`,
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
      channels: plan.channels === 2 ? 'stereo' : 'mono',
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
