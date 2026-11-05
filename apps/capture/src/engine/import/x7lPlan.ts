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
 *  • **Grouping comes from the library's own performances when it can.** The
 *    DPFM block says, per Part, exactly which waveforms play together — see
 *    `dpfm.ts`. That outranks everything below it, because it is the library
 *    author's own answer rather than a guess about his naming. It is still
 *    narrowed by the key map: two waveforms the DPFM groups but whose key maps
 *    differ cannot be velocity layers of each other note for note, so they
 *    stay apart. Where the DPFM is silent or was not understood, the name
 *    heuristic below still applies.
 *
 *  • **Otherwise, waveforms are only layered together when their names agree.**
 *    Grouping on key map alone is not safe: `Montage Triton Library.X7L` has
 *    six unrelated waveforms ("Brass Low", "M1 Organ", "analogpad High" …)
 *    sharing one key map, and stacking those as velocity layers of one
 *    instrument would be silently wrong. Waveforms with a shared key map
 *    become layers of one instrument only when their names differ by a dynamic
 *    or variant token alone; otherwise each becomes its own bundle.
 */

import type {
  Instrument,
  LoopRegion,
  PlaybackOptions,
  ReleaseZone,
  SoundType,
  VelocityLayer,
  World,
  Zone,
} from '@jm/jmi-format/web';
import { sampleFileName } from '@jm/jmi-format/web';
import { bundleFolderName } from '../export';
import { nominalSampleRate, type X7lLibrary, type X7lWaveform } from './x7l';
import { dpfmUsage, type DpfmLayer, type DpfmUsage } from './dpfm';

/**
 * How many velocity windows this importer will *synthesise* when it has to
 * split 1..127 itself. The `.jmi` schema allows sixteen
 * (`instrument.schema.json` → `velocityMap.maxItems`, which is what
 * `dropPlan.ts` uses); four is this path's own self-imposed limit on invented
 * windows, kept because inventing more than four even bands is a guess about
 * material nobody measured. Windows the library states — in its DPFM block,
 * its key maps, or waveform names ending in each layer's top velocity (see
 * `namedVelocityBounds`) — are not invented and go up to the schema's sixteen.
 */
export const MAX_VELOCITY_LAYERS = 4;

/**
 * The cap when the windows are *not* invented — when they are read straight
 * out of the library's DPFM block. Reproducing the source's own five or six
 * windows is not a guess, so the only limit that applies is the format's.
 */
export const MAX_SCHEMA_VELOCITY_LAYERS = 16;

/** `playback.velocityXfade` is capped here by `instrument.schema.json`. */
const MAX_VELOCITY_XFADE = 32;

/** SFZ `rt_decay` for an imported key-off sample. See `buildImportInstrument`. */
const IMPORT_RT_DECAY_DB = 0;

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
  /**
   * Key-off samples: one planned zone per keyzone of each waveform the
   * library's performances play on an XA Control "Key Off" element. Empty
   * unless the DPFM block named one.
   */
  releaseZones: PlannedZone[];
  /** the source's own Vel Cross Fade when the DPFM supplied the layers */
  velXfade: number;
  /** true when this instrument's layers came from the DPFM, not from names */
  fromDpfm: boolean;
}

/** An instrument the planner could not lay out; the rest of the library still imports. */
export interface X7lSkippedInstrument {
  name: string;
  reason: string;
}

export interface X7lImportPlan {
  version: string;
  instruments: X7lInstrumentPlan[];
  skipped: X7lSkippedInstrument[];
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

/**
 * Tokens that name a *position in the instrument* rather than the instrument.
 *
 * These are deliberately absent from `waveformStem`, which uses names as
 * evidence of grouping: "Brass Low" and "Brass High" share a key map but
 * nothing in their names says they are layered, so the heuristic must not
 * merge them. Once the DPFM has settled that they *are* one instrument,
 * though, the token is only clutter in its name — so it is dropped here, on
 * the naming path alone.
 */
const POSITION_TOKENS = new Set(['low', 'high', 'lo', 'hi', 'up', 'upper', 'lower', 'top', 'bottom']);

/** The instrument name for a set of waveforms the DPFM grouped together. */
export function groupedInstrumentName(names: string[]): string {
  if (names.length === 0) return 'Instrument';
  const trim = (name: string): string => {
    let toks = name.split(/\s+/).filter((t) => t.length > 0);
    if (toks.length > 1 && isNumberedVariant(toks[toks.length - 1])) toks = toks.slice(0, -1);
    const kept = toks.filter((t) => !isDynamicToken(t) && !POSITION_TOKENS.has(t.toLowerCase()));
    const out = (kept.length ? kept : toks).join(' ').trim();
    return out || name;
  };
  const stems = [...new Set(names.map(trim))];
  if (stems.length === 1) return stems[0];
  // The members disagree — the library spells one "RomancePiano" and the next
  // "Romance Piano", or one "String Low" and the next "Strung High". A shared
  // prefix is only a better name than the first layer's when most of the name
  // is actually shared; "Str" is not a name for String Low + Strung High.
  let n = 0;
  const first = stems[0];
  outer: for (; n < first.length; n++) {
    for (const other of stems) {
      if ((other[n] ?? '').toLowerCase() !== first[n].toLowerCase()) break outer;
    }
  }
  const prefix = first.slice(0, n).replace(/[\s\-_:]+$/, '').trim();
  const shortest = Math.min(...stems.map((s) => s.length));
  return prefix.length >= 3 && prefix.length >= 0.6 * shortest ? prefix : first;
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
/**
 * The client's four agreed categories, in the order they are tested:
 *
 *   JM Set    (rhodes-ep)  uprights, full grands, warm Rhodes, bell-like EPs
 *   MKS       (mks)        MKS sounds, rich stage keys, classic keyboards
 *   FM        (fm)         clear digital EPs, crystalline bells, percussive keys
 *   Strings   (strings)    EVERYTHING ELSE -- organs, bells, pads, choirs, brass
 *
 * Strings is the deliberate catch-all, per the client: "anything that does not
 * fall in the first 3 categories put them under strings, like organs bells
 * pads." The old fallback was `mks`, which silently filed 18 of his 25 bundles
 * -- grand pianos, an organ, two pads, a choir and brass -- under MKS.
 *
 * A bare "bell" is NOT treated as a piano: his JM Set category means an EP
 * that happens to sound bell-like, which the ep/rhodes patterns already catch,
 * while a standalone bell or pad belongs to the catch-all.
 */
export function inferWorld(name: string): World {
  const n = name.toLowerCase();
  if (/\bmks\b|mks\d/.test(n)) return 'mks';
  if (/rhodes|wurli|\bep\b|ep\d|electric piano|dyno|piano|grand|upright|klavier/.test(n)) return 'rhodes-ep';
  if (/\bfm\b|\bdx\b|dx7/.test(n)) return 'fm';
  return 'strings';
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
    const key = `${velocitySplitKey(wf)}|${wf.channels}|${nominalSampleRate(wf.keyzones[0].sampleRate)}`;
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
/* grouping from the library's own performances (DPFM)                 */
/* ------------------------------------------------------------------ */

/** One velocity window the DPFM declares, with the waveform that fills it. */
export interface DpfmWindow {
  lo: number;
  hi: number;
  waveformIndex: number;
}

/** What the DPFM says about one instrument. */
export interface DpfmInstrument {
  /** group key, shared by every waveform the DPFM puts in this instrument */
  key: string;
  /** body waveform indices, lowest catalogue index first */
  body: number[];
  /** waveforms this instrument uses only as key-off (release) samples */
  release: number[];
  /**
   * The instrument's velocity windows, taken from one Part's element velocity
   * limits and tiled over 1..127. `null` when no Part expressed this
   * instrument as a clean velocity split, in which case the importer falls
   * back to splitting 1..127 by measured level, exactly as before.
   */
  windows: DpfmWindow[] | null;
  /** which performance and part `windows` came from */
  windowSource: string | null;
  /** the source's own Vel Cross Fade, the largest across the windows used */
  velXfade: number;
  notes: string[];
}

export interface DpfmPlan {
  /** group key for each waveform index the DPFM accounts for */
  keyOf: Map<number, string>;
  groups: Map<string, DpfmInstrument>;
  notes: string[];
}

/** DPFM layers of one usage that belong to `members`, in element order. */
function layersIn(usage: DpfmUsage, members: Set<number>): DpfmLayer[] {
  return usage.body.filter((l) => members.has(l.waveformIndex));
}

/**
 * Turn one Part's element velocity limits into windows that tile 1..127.
 *
 * The *count* and the *order* of the windows, and which waveform fills each,
 * are the source's verbatim. Only the exact boundary velocity is placed by
 * this function, because it has to be: MONTAGE elements may overlap (that is
 * what Vel Cross Fade blends) or step (element 4 of `Init Normal (AWM2)` ends
 * at 58 and element 5 starts at 60), while `velocityMap` must tile without a
 * gap or an overlap. Each boundary goes to the midpoint of the disputed span,
 * so no window loses more than half of a shared region.
 *
 * Returns null when the elements are not a velocity split at all — when two of
 * them claim the same window, when their order by `velLo` and by `velHi`
 * disagree, or when they do not all cover the same keys so that the split is
 * really a key split as well.
 */
export function dpfmVelocityWindows(layers: DpfmLayer[]): DpfmWindow[] | null {
  if (layers.length === 0) return null;
  const noteLo = layers[0].noteLo;
  const noteHi = layers[0].noteHi;
  if (layers.some((l) => l.noteLo !== noteLo || l.noteHi !== noteHi)) return null;

  // Adjacent elements on the same waveform are the same audio at two levels,
  // not two layers: they collapse into one window spanning both.
  const sorted = [...layers].sort((a, b) => a.velLo - b.velLo || a.velHi - b.velHi);
  const merged: { lo: number; hi: number; waveformIndex: number }[] = [];
  for (const l of sorted) {
    const prev = merged[merged.length - 1];
    if (prev && prev.waveformIndex === l.waveformIndex) {
      prev.lo = Math.min(prev.lo, l.velLo);
      prev.hi = Math.max(prev.hi, l.velHi);
      continue;
    }
    merged.push({ lo: l.velLo, hi: l.velHi, waveformIndex: l.waveformIndex });
  }

  // One waveform may not appear in two separate windows: that is a stack, not
  // a split, and nothing in `velocityMap` can express it.
  const seen = new Set<number>();
  for (const w of merged) {
    if (seen.has(w.waveformIndex)) return null;
    seen.add(w.waveformIndex);
  }
  // Both edges must agree on the order, or these are overlapping stacked
  // elements rather than a ladder of layers.
  for (let i = 1; i < merged.length; i++) {
    if (merged[i].lo <= merged[i - 1].lo) return null;
    if (merged[i].hi <= merged[i - 1].hi) return null;
  }

  const out: DpfmWindow[] = merged.map((w) => ({ ...w }));
  out[0].lo = 1;
  out[out.length - 1].hi = 127;
  for (let i = 1; i < out.length; i++) {
    const a = out[i - 1].hi;
    const b = out[i].lo - 1;
    const boundary = Math.floor((Math.min(a, b) + Math.max(a, b)) / 2);
    out[i - 1].hi = boundary;
    out[i].lo = boundary + 1;
  }
  // The placement above can only fail to tile if a window was swallowed whole.
  for (const w of out) if (w.lo > w.hi) return null;
  for (let i = 1; i < out.length; i++) if (out[i].lo !== out[i - 1].hi + 1) return null;
  if (out[0].lo !== 1 || out[out.length - 1].hi !== 127) return null;
  return out;
}

/**
 * The grouping the library itself declares, or null when its DPFM block says
 * nothing this planner can use.
 *
 * Two waveforms are put in one instrument when some Part sounds them together.
 * Their key maps do **not** have to match: "RomancePiano Low" is sampled at 41
 * root notes and "Romance Piano High" at 15, which is exactly how the synth
 * plays them — each element reads its own waveform's key map — and `.jmi` can
 * say the same thing, because a zone carries its own key range and velocity
 * range together. So each velocity window here is built from the key map of
 * the waveform that fills it, not from one grid shared across the instrument.
 *
 * That only works when the velocity windows come from the DPFM, one waveform
 * per window. Where they do not — where the importer has to split 1..127 by
 * measured level and a window may draw on several waveforms at once — one
 * shared key grid is unavoidable, so such a group is split by key map
 * signature before it is planned.
 */
export function dpfmPlan(lib: X7lLibrary): DpfmPlan | null {
  if (!lib.dpfm) return null;
  const usages = dpfmUsage(lib.dpfm);
  if (usages.length === 0) return null;

  const signature = lib.waveforms.map((w) => keyMapSignature(w));
  const splits = lib.waveforms.map((w) => velocitySplits(w).length);

  /* ---- union waveforms that share a Part and a key map ---- */
  const parent = lib.waveforms.map((_, i) => i);
  const find = (i: number): number => {
    while (parent[i] !== i) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  const union = (a: number, b: number): void => {
    const ra = find(a);
    const rb = find(b);
    if (ra !== rb) parent[Math.max(ra, rb)] = Math.min(ra, rb);
  };

  const bodyWaveforms = new Set<number>();
  const releaseWaveforms = new Set<number>();
  for (const u of usages) {
    for (const l of u.body) bodyWaveforms.add(l.waveformIndex);
    for (const l of u.release) releaseWaveforms.add(l.waveformIndex);
    for (let i = 1; i < u.body.length; i++) {
      union(u.body[0].waveformIndex, u.body[i].waveformIndex);
    }
  }
  // A waveform that is ever played as a body element is a body element; only
  // one used *solely* as a key-off sample becomes a release zone.
  for (const r of [...releaseWaveforms]) if (bodyWaveforms.has(r)) releaseWaveforms.delete(r);

  /* ---- the key-split partners this library already proved are one ---- */
  // Those are merged before grouping, so a DPFM group must not cut through
  // one; unioning their members keeps the two decisions consistent.
  for (const g of findKeySplitGroups(lib.waveforms)) {
    for (let i = 1; i < g.length; i++) union(g[0].index, g[i].index);
  }

  /* ---- candidate instruments, one per union root ---- */
  const rootBody = new Map<number, number[]>();
  for (const i of [...bodyWaveforms].sort((a, b) => a - b)) {
    const root = find(i);
    const list = rootBody.get(root);
    if (list) list.push(i);
    else rootBody.set(root, [i]);
  }
  if (rootBody.size === 0) return null;

  /* ---- which key-split unit each waveform belongs to ---- */
  const unitLeader = new Map<number, number>();
  const unitSig = new Map<number, string>();
  for (const g of findKeySplitGroups(lib.waveforms)) {
    for (const w of g) {
      unitLeader.set(w.index, g[0].index);
      unitSig.set(w.index, unitSignature(g));
    }
  }
  const leaderOf = (i: number): number => unitLeader.get(i) ?? i;
  const sigOf = (i: number): string => unitSig.get(i) ?? signature[i];

  /**
   * The windows a Part declares for exactly this set of waveforms, or null.
   *
   * Every condition here is a thing the planner would otherwise have to guess:
   * a waveform whose own key map already carries several velocity windows
   * cannot also be one window of a DPFM split; a bundle has one sample rate
   * and one channel count; and each window has to come from a different unit
   * so that one waveform does not try to be two layers.
   */
  const plannable = (body: number[]): boolean => {
    if (!body.every((i) => splits[i] === 1)) return false;
    const ch = lib.waveforms[body[0]].channels;
    const sr = nominalSampleRate(lib.waveforms[body[0]].keyzones[0].sampleRate);
    return !body.some(
      (i) =>
        lib.waveforms[i].channels !== ch ||
        lib.waveforms[i].keyzones.some((z) => nominalSampleRate(z.sampleRate) !== sr),
    );
  };

  /**
   * The largest subset of `body` that some Part expresses as a clean velocity
   * split, with that Part's windows — or null when no Part does.
   *
   * A subset rather than the whole group, because a Part may stack something
   * on top that no velocity map can hold: `Nord Stage`'s "Nord Grand Lady
   * EDIT" sounds the four "Grand Lady" layers *and* a "Note on" transient
   * across the whole range at once. The four are a split and are taken as
   * one; the transient drops out of that instrument and is resolved on its
   * own, which is how the same library's "red Grand Lady" has it.
   */
  const bestWindows = (
    body: number[],
  ): { subset: number[]; windows: DpfmWindow[]; windowSource: string; velXfade: number } | null => {
    let best: { subset: number[]; windows: DpfmWindow[]; windowSource: string; velXfade: number } | null = null;
    const inBody = new Set(body);
    for (const u of usages) {
      const mine = layersIn(u, inBody);
      const subset = [...new Set(mine.map((l) => l.waveformIndex))].sort((a, b) => a - b);
      if (subset.length === 0) continue;
      if (best && subset.length <= best.subset.length) continue;
      if (!plannable(subset)) continue;
      // A key-split unit may not be cut in half by a velocity split.
      const closed = subset.every((i) =>
        body.every((j) => leaderOf(j) !== leaderOf(i) || subset.includes(j)),
      );
      if (!closed) continue;
      const w = dpfmVelocityWindows(mine);
      if (!w || w.length > MAX_SCHEMA_VELOCITY_LAYERS) continue;
      if (new Set(w.map((x) => leaderOf(x.waveformIndex))).size !== w.length) continue;
      best = {
        subset,
        windows: w,
        windowSource: `${u.performanceName} part ${u.partIndex + 1}`,
        velXfade: Math.max(...mine.map((l) => l.velXfade)),
      };
    }
    return best;
  };

  interface Resolved {
    key: string;
    body: number[];
    windows: DpfmWindow[] | null;
    windowSource: string | null;
    velXfade: number;
  }
  const resolved: Resolved[] = [];
  let serial = 0;
  const resolve = (body: number[]): void => {
    if (body.length === 0) return;
    const got = bestWindows(body);
    if (got) {
      resolved.push({
        key: `dpfm:${serial++}`,
        body: got.subset,
        windows: got.windows,
        windowSource: got.windowSource,
        velXfade: got.velXfade,
      });
      resolve(body.filter((i) => !got.subset.includes(i)));
      return;
    }
    // No Part expressed this set as a velocity split, so the importer will
    // have to split 1..127 itself — and then every layer has to answer on the
    // same key grid. Split by key map so it can, and look again at each piece.
    const bySig = new Map<string, number[]>();
    for (const i of body) {
      const k = sigOf(i);
      const list = bySig.get(k);
      if (list) list.push(i);
      else bySig.set(k, [i]);
    }
    if (bySig.size <= 1) {
      resolved.push({ key: `dpfm:${serial++}`, body, windows: null, windowSource: null, velXfade: 0 });
      return;
    }
    for (const sub of bySig.values()) resolve(sub);
  };
  for (const body of rootBody.values()) resolve(body);

  const keyOf = new Map<number, string>();
  const bodyOf = new Map<string, number[]>();
  const windowsOf = new Map<string, Resolved>();
  for (const r of resolved) {
    for (const i of r.body) keyOf.set(i, r.key);
    bodyOf.set(r.key, r.body);
    windowsOf.set(r.key, r);
  }

  /* ---- attach release-only waveforms to the group they play with ---- */
  const releaseOf = new Map<string, number[]>();
  for (const r of [...releaseWaveforms].sort((a, b) => a - b)) {
    const score = new Map<string, number>();
    for (const u of usages) {
      if (!u.release.some((l) => l.waveformIndex === r)) continue;
      for (const l of u.body) {
        const key = keyOf.get(l.waveformIndex);
        if (key) score.set(key, (score.get(key) ?? 0) + 1);
      }
    }
    let best: string | null = null;
    let bestScore = 0;
    for (const [key, n] of [...score].sort((a, b) => a[0].localeCompare(b[0]))) {
      if (n > bestScore) {
        best = key;
        bestScore = n;
      }
    }
    if (!best) continue;
    const list = releaseOf.get(best);
    if (list) list.push(r);
    else releaseOf.set(best, [r]);
  }

  /* ---- velocity windows, from the Part that expresses the whole group ---- */
  const groups = new Map<string, DpfmInstrument>();
  const notes: string[] = [];
  for (const [key, body] of bodyOf) {
    const r = windowsOf.get(key)!;
    const windows = r.windows;
    const windowSource = r.windowSource;
    const velXfade = r.velXfade;
    const groupNotes: string[] = [];

    if (windows) {
      groupNotes.push(
        `Velocity layers were taken from the library's own performance "${windowSource}": ` +
          `${windows.map((w) => `${w.lo}-${w.hi} = "${lib.waveforms[w.waveformIndex].name}"`).join(', ')}. ` +
          `The number of layers, their order and the waveform in each are the library's; only the exact ` +
          `boundary velocities were placed, at the midpoint of each pair of elements that overlapped or ` +
          `stepped, because .jmi velocity layers must tile 1..127 without a gap.`,
      );
      if (velXfade > 0) {
        const used = Math.min(velXfade, MAX_VELOCITY_XFADE);
        groupNotes.push(
          `The source declares a velocity crossfade of ${velXfade} at the loudest of these boundaries` +
            (used === velXfade
              ? ''
              : `, which exceeds the ${MAX_VELOCITY_XFADE} .jmi allows and was written as ${used}`) +
            `. It is recorded as playback.velocityXfade; the engine applies it only under velocityMode ` +
            `"anchored", which this import does not set because it would need a measured level and played ` +
            `velocity per layer that the library does not state.`,
        );
      }
    }
    const rel = releaseOf.get(key) ?? [];
    if (rel.length) {
      groupNotes.push(
        `${rel.map((i) => `"${lib.waveforms[i].name}"`).join(', ')} ` +
          `${rel.length === 1 ? 'is a key-off sample' : 'are key-off samples'} — the library's performances ` +
          `play ${rel.length === 1 ? 'it' : 'them'} on an element whose XA Control is "Key Off" — so ` +
          `${rel.length === 1 ? 'it goes' : 'they go'} into releaseZones (SFZ trigger=release) rather than ` +
          `becoming a bundle of ${rel.length === 1 ? 'its' : 'their'} own. rtDecayDb is 0: the format needs ` +
          `a value, the source states none, and 0 is the neutral one.`,
      );
    }
    groups.set(key, {
      key,
      body,
      release: rel,
      windows,
      windowSource,
      velXfade: Math.min(velXfade, MAX_VELOCITY_XFADE),
      notes: groupNotes,
    });
  }

  const grouped = [...keyOf.keys()].length;
  notes.push(
    `Grouping for ${grouped} of ${lib.waveforms.length} waveform(s) came from the library's own DPFM ` +
      `performance block rather than from their names: ${groups.size} instrument(s), ` +
      `${[...groups.values()].filter((g) => g.windows).length} of them with the velocity split the ` +
      `library itself declares.`,
  );
  for (const line of lib.dpfm.corroboration) notes.push(line);

  return { keyOf, groups, notes };
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

  // The library's own performances outrank the name heuristic. Read first,
  // because a waveform the DPFM identifies as a key-off sample is not an
  // instrument at all: it belongs in another bundle's releaseZones and must
  // not become a unit of its own here.
  const dpfm = dpfmPlan(lib);
  if (dpfm) notes.push(...dpfm.notes);
  const asRelease = new Set<number>();
  for (const g of dpfm?.groups.values() ?? []) for (const i of g.release) asRelease.add(i);

  /* ---- 1. merge key-split partners into units ---- */
  const splitGroups = findKeySplitGroups(lib.waveforms).filter((g) => !g.some((w) => asRelease.has(w.index)));
  const claimed = new Set<number>(asRelease);
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

  /* ---- 2. group units into instruments ---- */
  // A unit the DPFM accounts for is keyed by its DPFM group; anything the
  // DPFM did not mention — or a group this planner declined — still falls back
  // to key map plus name stem, so a library whose DPFM is unreadable converts
  // exactly as it did before.
  const dpfmKeyFor = (u: Unit): string | null => {
    if (!dpfm) return null;
    const keys = new Set<string>();
    for (const m of u.members) {
      const k = dpfm.keyOf.get(m.index);
      if (k) keys.add(k);
    }
    // A unit whose members the DPFM splits across instruments is a conflict
    // between two pieces of evidence; the name heuristic decides instead.
    return keys.size === 1 ? [...keys][0] : null;
  };

  // Names ending in each layer's top velocity ("EP2C Velocity 15" … "127")
  // group as one instrument even without a DPFM. Left to the stem rule they
  // would not: it strips a one- or two-digit tail but not "111" or "127", so
  // the eight would come out as three instruments.
  const namedKey = new Map<Unit, string>();
  {
    const byBase = new Map<string, Unit[]>();
    for (const u of units) {
      if (u.members.length !== 1 || dpfmKeyFor(u)) continue;
      const m = /^(.*?)\s*\d{1,3}$/.exec(u.members[0].name);
      if (!m) continue;
      const k = `${u.signature}||${m[1].trim().toLowerCase()}`;
      const list = byBase.get(k);
      if (list) list.push(u);
      else byBase.set(k, [u]);
    }
    for (const [k, list] of byBase) {
      if (namedVelocityBounds(list)) for (const u of list) namedKey.set(u, `named:${k}`);
    }
  }

  const groups = new Map<string, Unit[]>();
  const groupDpfm = new Map<string, DpfmInstrument>();
  for (const u of units) {
    const dk = dpfmKeyFor(u);
    const key = dk ?? namedKey.get(u) ?? `${u.signature}||${u.stem.toLowerCase()}`;
    if (dk) {
      const g = dpfm?.groups.get(dk);
      if (g) groupDpfm.set(key, g);
    }
    const list = groups.get(key);
    if (list) list.push(u);
    else groups.set(key, [u]);
  }

  const instruments: X7lInstrumentPlan[] = [];
  const skipped: X7lSkippedInstrument[] = [];
  let index = 0;
  const usedNames = new Set<string>();

  // One instrument that cannot be laid out does not sink the library: it is
  // reported and the rest still plan. The 2026 "Full Expansion" library would
  // otherwise have lost 26 waveforms to one unplannable eight-layer set.
  for (const [key, unitGroup] of groups) {
    try {
      const plan = planInstrument(unitGroup, levelsDb, index, usedNames, opts, lib, groupDpfm.get(key) ?? null);
      instruments.push(plan);
      index++;
    } catch (e) {
      const name = [...new Set(unitGroup.map((u) => u.stem || u.members[0].name))].join(' + ');
      skipped.push({ name, reason: (e as Error).message });
    }
  }

  return { version: lib.version, instruments, skipped, totalPcmBytes: lib.totalPcmBytes, notes };
}

/**
 * Velocity windows stated by the waveform names. Every unit must be a single
 * waveform whose name ends in an integer; the integers must be distinct, lie in
 * 1..127, and the largest must be 127 (the top window always reaches the top).
 * Sorted ascending they are the windows' upper bounds; each lower bound is the
 * previous upper bound plus one. Two to sixteen layers (the schema's limit).
 */
export function namedVelocityBounds(units: Unit[]): { order: Unit[]; windows: { lo: number; hi: number }[] } | null {
  if (units.length < 2 || units.length > 16) return null;
  const tagged: { unit: Unit; hi: number }[] = [];
  for (const u of units) {
    if (u.members.length !== 1) return null;
    const m = /(\d{1,3})\s*$/.exec(u.members[0].name);
    if (!m) return null;
    const hi = Number(m[1]);
    if (hi < 1 || hi > 127) return null;
    tagged.push({ unit: u, hi });
  }
  tagged.sort((a, b) => a.hi - b.hi);
  if (new Set(tagged.map((t) => t.hi)).size !== tagged.length) return null;
  if (tagged[tagged.length - 1].hi !== 127) return null;
  const windows: { lo: number; hi: number }[] = [];
  let lo = 1;
  for (const t of tagged) {
    windows.push({ lo, hi: t.hi });
    lo = t.hi + 1;
  }
  return { order: tagged.map((t) => t.unit), windows };
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
  lib: X7lLibrary,
  dpfm: DpfmInstrument | null,
): X7lInstrumentPlan {
  const perInstrumentNotes: string[] = [];
  if (dpfm) {
    perInstrumentNotes.push(
      `The library's own performance block groups these waveforms into one instrument: ` +
        `${dpfm.body.map((i) => `"${lib.waveforms[i].name}"`).join(', ')}` +
        `${dpfm.release.length ? `, with ${dpfm.release.map((i) => `"${lib.waveforms[i].name}"`).join(', ')} as key-off sample(s)` : ''}. ` +
        `That is the library author's own grouping, not an inference from the waveform names.`,
    );
    perInstrumentNotes.push(...dpfm.notes);
  }

  const unitLevel = (u: Unit): number =>
    u.members.reduce((best, w) => Math.max(best, levelsDb[w.index]), -Infinity);

  // When the DPFM gave the velocity split, its order is the layer order: the
  // library says which window each waveform fills, so there is nothing left
  // for a level measurement to decide.
  const unitFor = (waveformIndex: number): Unit | null =>
    unitGroup.find((u) => u.members.some((m) => m.index === waveformIndex)) ?? null;
  const dpfmUnits = dpfm?.windows
    ? dpfm.windows.map((w) => unitFor(w.waveformIndex))
    : null;
  const dpfmOrdered: Unit[] | null =
    dpfmUnits &&
    dpfmUnits.every((u): u is Unit => u !== null) &&
    new Set(dpfmUnits).size === dpfmUnits.length
      ? (dpfmUnits as Unit[])
      : null;

  // Failing a DPFM split, the waveform NAMES may state the windows: "EP2C
  // Velocity 15" .. "EP2C Velocity 127" is eight layers whose names end in each
  // layer's top velocity. Those numbers are the library author's windows, not
  // an inference, so they are used as given and the four-window cap on
  // invented bands does not apply.
  const named = !dpfmOrdered ? namedVelocityBounds(unitGroup) : null;

  // Units in a group are velocity layers of one another; order them by the
  // measured level of their loudest member, quietest first.
  const byLevel = dpfmOrdered ?? named?.order ?? [...unitGroup].sort((a, b) => unitLevel(a) - unitLevel(b));
  if (!dpfmOrdered && !named && byLevel.some((u, i) => u !== unitGroup[i])) {
    perInstrumentNotes.push(
      'Velocity layer order was taken from the measured level of the audio, not the library catalogue order.',
    );
  }
  if (named) {
    perInstrumentNotes.push(
      `Velocity windows were taken from the waveform names, which end in each layer's top velocity ` +
        `(${named.windows.map((w) => w.hi).join(', ')}): ${named.windows.map((w) => `${w.lo}-${w.hi}`).join(', ')}.`,
    );
    const measured = [...named.order].sort((a, b) => unitLevel(a) - unitLevel(b));
    if (measured.some((u, i) => u !== named.order[i])) {
      perInstrumentNotes.push(
        'The measured peak levels of these waveforms do not rise in the order their names put them in. ' +
          'The named order was kept: it is what the library author built.',
      );
    }
  }
  if (dpfmOrdered) {
    const measured = [...new Set(dpfmOrdered)].sort((a, b) => unitLevel(a) - unitLevel(b));
    const declared = [...new Set(dpfmOrdered)];
    if (measured.some((u, i) => u !== declared[i])) {
      perInstrumentNotes.push(
        "The measured peak levels of these waveforms do not rise in the order the library's performance " +
          'puts them in. The declared order was kept: it is what the instrument plays on the synth, and an ' +
          'element can be quieter than the one below it and still sit higher in the velocity range.',
      );
    }
  }

  const allWaveforms = [...new Set(byLevel.flatMap((u) => u.members))];
  const template = byLevel[0];
  const sourceSplits = velocitySplits(template.members[0]);

  // Verify the source's own velocity windows tile 1..127 before trusting them.
  // A DPFM split is only accepted for waveforms with one window of their own
  // (see `dpfmPlan`), so the two can never both apply.
  let splitsUsable = !dpfmOrdered && sourceSplits.length > 1;
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
    : dpfmOrdered && dpfm?.windows
      ? dpfmOrdered.map((u, i) => ({
          unit: u,
          vel: null,
          label: `${lib.waveforms[dpfm.windows![i].waveformIndex].name} vel ${dpfm.windows![i].lo}-${dpfm.windows![i].hi}`,
        }))
      : named
        ? named.order.map((u, i) => ({
            unit: u,
            vel: null,
            label: `${unitLabel(u)} vel ${named.windows[i].lo}-${named.windows[i].hi}`,
          }))
        : byLevel.map((u) => ({ unit: u, vel: null, label: unitLabel(u) }));

  /* ---- name ---- */
  const stemName = dpfm
    ? groupedInstrumentName([...new Set(layers.map((l) => unitLabel(l.unit)))])
    : template.stem || commonInstrumentName(allWaveforms.map((w) => w.name));
  let name = stemName;
  let suffix = 2;
  while (usedNames.has(name.toLowerCase())) name = `${stemName} ${suffix++}`;
  usedNames.add(name.toLowerCase());

  /* ---- velocity windows ---- */
  const layerCount = layers.length;
  const mergeOptions: MergeOption[] = [];
  if (!splitsUsable && !dpfmOrdered) {
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

  if (dpfmOrdered && dpfm?.windows) {
    rawWindows = dpfm.windows.map((w, i) => ({ lo: w.lo, hi: w.hi, sources: [i] }));
  } else if (named) {
    rawWindows = named.windows.map((w, i) => ({ lo: w.lo, hi: w.hi, sources: [i] }));
  } else if (splitsUsable) {
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

  /* ---- the key map, per layer where the layers have their own ---- */
  // With DPFM windows every window is filled by exactly one unit, so each one
  // can be built from that unit's own key map. That is what lets
  // "RomancePiano Low" (41 root notes) and "Romance Piano High" (15) be two
  // velocity layers of one instrument: each layer answers from its own ladder,
  // which is what the synth does too. Without DPFM windows a window may draw
  // on several units at once and one shared grid is unavoidable.
  const slotCache = new Map<Unit, Slot[]>();
  const slotsFor = (u: Unit): Slot[] => {
    const hit = slotCache.get(u);
    if (hit) return hit;
    const made = mergedSlots(u, perInstrumentNotes);
    slotCache.set(u, made);
    return made;
  };
  const slots = slotsFor(template);
  if (dpfmOrdered) {
    const ladders = new Set(dpfmOrdered.map((u) => slotsFor(u).length));
    if (ladders.size > 1) {
      perInstrumentNotes.push(
        `These velocity layers are sampled at different root notes ` +
          `(${dpfmOrdered.map((u, i) => `${layers[i].label.replace(/ vel .*/, '')}: ${slotsFor(u).length} note(s)`).join(', ')}), ` +
          `which is how the library has them. Each layer keeps its own key map rather than being forced onto ` +
          `one shared grid, so no sample is dropped and none is stretched further than the source stretches it.`,
      );
    }
  }
  const allSlots = dpfmOrdered ? [...new Set(dpfmOrdered)].flatMap((u) => slotsFor(u)) : slots;

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
    const winSlots = dpfmOrdered ? slotsFor(layers[win.sources[0]].unit) : slots;
    for (const slot of winSlots) {
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

  /* ---- key-off samples ---- */
  // A release waveform is not a velocity layer of the body, so it does not go
  // through the window machinery at all: its own key map is carried over zone
  // for zone into `releaseZones`. The `rel_` prefix is the name the .jmi
  // schema already reserves for these files.
  const releaseZones: PlannedZone[] = [];
  const releaseWaveforms = (dpfm?.release ?? []).map((i) => lib.waveforms[i]);
  const relRr = new Map<number, number>();
  for (const wf of releaseWaveforms) {
    for (const kz of wf.keyzones) {
      const n = (relRr.get(kz.rootNote) ?? 0) + 1;
      relRr.set(kz.rootNote, n);
      releaseZones.push({
        rootNote: kz.rootNote,
        keyRangeLo: kz.keyRangeLo,
        keyRangeHi: kz.keyRangeHi,
        velRangeLo: kz.velRangeLo,
        velRangeHi: kz.velRangeHi,
        samples: [
          {
            file: `samples/rel_${sampleFileName(kz.rootNote, 1, n)}`,
            waveformIndex: wf.index,
            keyzoneIndex: kz.index,
            layer: 1,
            rr: n,
          },
        ],
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
  for (const wf of [...allWaveforms, ...releaseWaveforms]) {
    totalPcmBytes += wf.totalPcmBytes;
    for (const z of wf.keyzones) totalFrames += z.lengthSamples;
  }
  for (const wf of releaseWaveforms) {
    if (wf.channels !== channels) {
      throw new Error(
        `Key-off waveform "${wf.name}" is ${wf.channels === 2 ? 'stereo' : 'mono'} but "${name}" is ` +
          `${channels === 2 ? 'stereo' : 'mono'}; a .jmi bundle has one channel count for every sample in it.`,
      );
    }
    for (const z of wf.keyzones) {
      if (nominalSampleRate(z.sampleRate) !== nominalSampleRate(template.members[0].keyzones[0].sampleRate)) {
        throw new Error(
          `Key-off waveform "${wf.name}" is ${z.sampleRate} Hz but "${name}" is ` +
            `${template.members[0].keyzones[0].sampleRate} Hz; nothing is resampled on this path.`,
        );
      }
    }
  }

  // The bundle's rate is the schema rate nearest the source's. Keyzones tuned
  // by rate (see plausibleSampleRate in x7l.ts) keep their true rate in their
  // own WAV, which the player honours.
  const sampleRate = nominalSampleRate(template.members[0].keyzones[0].sampleRate);
  {
    const tuned: number[] = [];
    for (const wf of [...allWaveforms, ...releaseWaveforms])
      for (const z of wf.keyzones) if (z.sampleRate !== sampleRate) tuned.push(z.sampleRate);
    if (tuned.length) {
      const lo = Math.min(...tuned);
      const hi = Math.max(...tuned);
      const cents = 1200 * Math.log2(Math.max(hi, sampleRate) / Math.min(lo, sampleRate));
      perInstrumentNotes.push(
        `${tuned.length} sample(s) are tuned by rate in the source (${lo}..${hi} Hz, within ${cents.toFixed(1)} ` +
          `cents of ${sampleRate}); each keeps its true rate in its own WAV, which the player honours, and ` +
          `the bundle is declared at the nominal rate.`,
      );
    }
  }
  return {
    index,
    name,
    bundleName: bundleFolderName(name),
    world: inferWorld(name),
    soundType: sustaining ? 'sustaining' : 'decaying',
    waveformIndices: allWaveforms.map((w) => w.index),
    layerNames: layers.map((l) => l.label),
    layerPeakDb: layers.map((l) => round2(unitLevel(l.unit))),
    keyzoneCount: new Set(allSlots.map((s) => s.rootNote)).size,
    keyRangeLo: Math.min(...allSlots.map((s) => s.keyRangeLo)),
    keyRangeHi: Math.max(...allSlots.map((s) => s.keyRangeHi)),
    sampleRate,
    channels,
    windows,
    zones,
    mergeAt,
    mergeOptions,
    sampleCount:
      zones.reduce((n, z) => n + z.samples.length, 0) +
      releaseZones.reduce((n, z) => n + z.samples.length, 0),
    totalPcmBytes,
    totalDurationSec: round2(totalFrames / (sampleRate || 44100)),
    notes: perInstrumentNotes,
    releaseZones,
    velXfade: dpfmOrdered ? (dpfm?.velXfade ?? 0) : 0,
    fromDpfm: dpfmOrdered !== null,
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

  const releaseZones: ReleaseZone[] = plan.releaseZones.map((z) => ({
    rootNote: z.rootNote,
    keyRangeLo: z.keyRangeLo,
    keyRangeHi: z.keyRangeHi,
    velRangeLo: z.velRangeLo,
    velRangeHi: z.velRangeHi,
    // The source states no attenuation-by-hold-time, and 0 is the format's
    // neutral value, so nothing is invented here.
    rtDecayDb: IMPORT_RT_DECAY_DB,
    roundRobins: z.samples.map((s) => {
      const f = facts[s.file];
      if (!f) throw new Error(`No measurement for ${s.file}`);
      return { file: s.file, peakDb: f.peakDb, lengthMs: f.lengthMs, loop: f.loop };
    }),
  }));

  // An instrument sustains if, and only if, something in it actually loops.
  //
  // The planner's guess comes from the source's loop FIELDS, which every piano
  // and electric piano in the corpus fills in — they declare a loop over their
  // decay tail, to be read under a falling envelope. The converter drops those
  // (see loopSeam.ts), so the fields are not the last word; the loops that
  // survive into `facts` are. Deciding here, from the zones as written, keeps
  // "sustaining" from meaning one thing in the plan and another in the bundle.
  const anyLoop = [...zones, ...releaseZones].some((z) => z.roundRobins.some((rr) => rr.loop));
  const soundType: SoundType = anyLoop ? plan.soundType : 'decaying';

  // `velocityXfade` is written because the library states it; it is inert
  // unless the player is in velocityMode "anchored", which this import does
  // not claim — see the note the planner adds.
  const playback: PlaybackOptions | undefined = plan.velXfade > 0 ? { velocityXfade: plan.velXfade } : undefined;

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
    soundType,
    zones,
    velocityMap: plan.windows.map((w) => ({ layer: w.layer, name: w.name, lo: w.lo, hi: w.hi })),
    ...(releaseZones.length ? { releaseZones } : {}),
    ...(playback ? { playback } : {}),
    reserved: { releaseSamples: [], pedalDownZones: [] },
  };
}

function round2(n: number): number {
  return Math.round(n * 100) / 100;
}
