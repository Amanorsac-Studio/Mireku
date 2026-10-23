/**
 * Drag-and-drop audio import: scanning and conversion planning (pure except
 * for an injected file reader, so the whole flow runs under vitest exactly as
 * it runs in Electron — the same shape as `x7lConvert.ts`).
 *
 * The client's requirement is one sentence: *drop sample files, get a playable
 * instrument, with the audio untouched*. Everything here follows from it.
 *
 *  • **Untouched is the law.** Nothing on the write path decodes audio.
 *    `scanDroppedFiles` reads headers and measures PCM in place; the converter
 *    copies the source byte range after a fresh canonical header. The only
 *    exception is a file the user has explicitly told the importer to convert,
 *    and that file is listed as converted in `meta.notes`.
 *
 *  • **A bundle must be internally consistent.** One sample rate, one bit
 *    depth, one channel count — the validator enforces it and `meta` has to
 *    match. Dropped files routinely are not. `censusFormats` finds the groups,
 *    names the target, and classifies every other file as convertible
 *    (bit depth / channel count) or not (sample rate). Nothing is converted
 *    without appearing in the plan first.
 *
 *  • **Sample rate is never changed.** Bit depth and channel count can be
 *    changed losslessly-ish and reversibly described; resampling cannot, and
 *    resampling is precisely what the client asked not to have. A file at the
 *    wrong rate is therefore *refused*, with the target rate selectable so the
 *    user can choose which half of a mixed drop becomes the bundle.
 *
 *  • **The mapping is shown before it is used.** Names are parsed
 *    (`dropNames.ts`), pitch is detected where the name is silent or
 *    unconfident (`dsp/pitch.ts`), and the result is a table of rows the user
 *    can correct. A mis-mapped sample set is worse than a refusal.
 */

import type { Instrument, LoopRegion, SoundType, VelocityLayer, World, Zone } from '@jm/jmi-format/web';
import { midiToNoteName, sampleFileName } from '@jm/jmi-format/web';
import { bundleFolderName, keyRanges } from '../export';
import { centsBetween, detectPitch, frequencyToMidi, midiToFrequency } from '../dsp/pitch';
import {
  AudioFileError,
  decodeChannels,
  DROPPABLE_EXTENSIONS,
  extensionOf,
  measurePcm,
  readAudioFile,
  REJECTED_EXTENSIONS,
  type AudioFileInfo,
  type PcmEncoding,
  type PcmMeasurement,
} from './audioFile';
import { commonNamePrefix, parseSampleName, type NameParse, type NoteSource } from './dropNames';
import { inferWorld } from './x7lPlan';

/* ------------------------------------------------------------------ */
/* schema limits                                                      */
/* ------------------------------------------------------------------ */

/** `instrument.schema.json` → meta.sampleRate enum. */
export const PERMITTED_SAMPLE_RATES = [44100, 48000, 88200, 96000] as const;
/** `instrument.schema.json` → meta.bitDepth enum. */
export const PERMITTED_BIT_DEPTHS = [16, 24, 32] as const;
/**
 * `instrument.schema.json` → velocityMap.maxItems. NOTE: the X7L importer's
 * `MAX_VELOCITY_LAYERS` says four; the schema in this repo says sixteen and
 * `VelocityLayer.layer` is documented "up to 16". The schema is the authority,
 * so the drop importer allows sixteen — and folds anything above that the same
 * way the X7L importer folds its fifth layer, by pairing the two adjacent
 * layers closest in level as round robins rather than dropping audio.
 */
export const MAX_VELOCITY_LAYERS = 16;

/* ------------------------------------------------------------------ */
/* scanning                                                           */
/* ------------------------------------------------------------------ */

/** Injected file access, so the scan is testable and Node stays out of here. */
export interface DropReadFs {
  readFile(path: string): Uint8Array;
  baseName(path: string): string;
}

export interface DetectedPitch {
  hz: number;
  /** nearest equal-tempered MIDI note */
  note: number;
  /** signed deviation from that note, in cents */
  cents: number;
  confidence: number;
  spreadCents: number;
}

export interface ScannedFile {
  index: number;
  path: string;
  fileName: string;
  parse: NameParse;
  audio: AudioFileInfo | null;
  measurement: PcmMeasurement | null;
  detected: DetectedPitch | null;
  /** null when the file is readable; a plain-English reason when it is not */
  error: string | null;
}

export interface ScanOptions {
  /** seconds of audio decoded for pitch analysis (never written anywhere) */
  pitchSeconds?: number;
  /** below this YIN confidence a detection is discarded as "can't tell" */
  minPitchConfidence?: number;
  /** skip detection entirely (name-only tests) */
  skipPitchDetection?: boolean;
}

/** Lowest / highest fundamental considered — A0 is 27.5 Hz, C8 is 4186 Hz. */
const PITCH_F_MIN = 25;
const PITCH_F_MAX = 2200;

/**
 * Read every dropped file: header, level, parsed name, and a YIN pitch
 * detection. Unreadable files are recorded with a reason rather than thrown,
 * so one bad file in a folder of three hundred does not lose the drop.
 */
export function scanDroppedFiles(paths: string[], fs: DropReadFs, opts: ScanOptions = {}): ScannedFile[] {
  const out: ScannedFile[] = [];
  paths.forEach((path, index) => {
    const fileName = fs.baseName(path);
    const parse = parseSampleName(fileName);
    const base: ScannedFile = {
      index,
      path,
      fileName,
      parse,
      audio: null,
      measurement: null,
      detected: null,
      error: null,
    };

    const ext = extensionOf(fileName);
    if (!(DROPPABLE_EXTENSIONS as readonly string[]).includes(ext)) {
      out.push({ ...base, error: REJECTED_EXTENSIONS[ext] ?? `"${ext || 'no extension'}" is not an audio file this importer reads.` });
      return;
    }

    let bytes: Uint8Array;
    try {
      bytes = fs.readFile(path);
    } catch (e) {
      out.push({ ...base, error: `Could not be read: ${(e as Error).message}` });
      return;
    }

    let audio: AudioFileInfo;
    try {
      audio = readAudioFile(bytes);
    } catch (e) {
      out.push({ ...base, error: e instanceof AudioFileError ? e.message : `Unreadable: ${(e as Error).message}` });
      return;
    }

    let measurement: PcmMeasurement | null = null;
    try {
      measurement = measurePcm(bytes, audio);
    } catch (e) {
      out.push({ ...base, audio, error: `Audio could not be measured: ${(e as Error).message}` });
      return;
    }

    let detected: DetectedPitch | null = null;
    if (!opts.skipPitchDetection) {
      detected = detectFilePitch(bytes, audio, opts);
    }
    out.push({ ...base, audio, measurement, detected });
  });
  return out;
}

/**
 * Run the existing YIN detector over a prefix of the file. The prefix is
 * deliberate: the detector already skips the attack and stops before the decay
 * tail (12%…75% of what it is given), so a few seconds of a long sample is the
 * stable portion and reading the rest would only cost time.
 */
export function detectFilePitch(bytes: Uint8Array, audio: AudioFileInfo, opts: ScanOptions = {}): DetectedPitch | null {
  const seconds = opts.pitchSeconds ?? 3;
  const maxFrames = Math.max(1, Math.min(audio.frames, Math.round(seconds * audio.sampleRate)));
  let channels: Float32Array[];
  try {
    channels = decodeChannels(bytes, audio, { maxFrames });
  } catch {
    return null;
  }
  const d = detectPitch(channels, audio.sampleRate, { fMin: PITCH_F_MIN, fMax: PITCH_F_MAX, frames: 5 });
  if (d.hz === null || d.confidence < (opts.minPitchConfidence ?? 0.6)) return null;
  const note = Math.round(frequencyToMidi(d.hz));
  if (note < 0 || note > 127) return null;
  return {
    hz: round2(d.hz),
    note,
    cents: round2(centsBetween(d.hz, midiToFrequency(note))),
    confidence: round2(d.confidence),
    spreadCents: round2(d.spreadCents),
  };
}

/* ------------------------------------------------------------------ */
/* format census                                                      */
/* ------------------------------------------------------------------ */

export interface BundleFormat {
  sampleRate: number;
  bitDepth: number;
  channels: number;
  encoding: PcmEncoding;
}

export type FormatVerdict =
  /** identical to the target: the bytes pass straight through */
  | 'passthrough'
  /** bit depth and/or channel count differ — convertible, and said so */
  | 'convertible'
  /** sample rate differs: changing it means resampling, which is refused */
  | 'rate-mismatch';

export interface FormatGroup {
  format: BundleFormat;
  label: string;
  fileIndices: number[];
  /** false when this format cannot appear in a `.jmi` bundle at all */
  schemaLegal: boolean;
  /** why it is not legal, when it is not */
  illegalReason: string | null;
}

export interface FormatCensus {
  groups: FormatGroup[];
  target: BundleFormat;
  targetLabel: string;
  /** true when the drop holds more than one format */
  mixed: boolean;
  verdicts: Record<number, FormatVerdict>;
  notes: string[];
}

export function formatLabel(f: BundleFormat): string {
  const ch = f.channels === 1 ? 'mono' : f.channels === 2 ? 'stereo' : `${f.channels}ch`;
  const depth = f.encoding === 'float' ? `${f.bitDepth}-bit float` : `${f.bitDepth}-bit`;
  return `${(f.sampleRate / 1000).toFixed(f.sampleRate % 1000 === 0 ? 0 : 1)} kHz · ${depth} · ${ch}`;
}

function formatKey(f: BundleFormat): string {
  return `${f.sampleRate}/${f.bitDepth}/${f.channels}/${f.encoding}`;
}

/** Why a source format cannot be written into a bundle as-is, or null. */
export function schemaObjection(f: BundleFormat): string | null {
  if (!(PERMITTED_SAMPLE_RATES as readonly number[]).includes(f.sampleRate)) {
    return `${f.sampleRate} Hz is not one of the rates the .jmi format permits (${PERMITTED_SAMPLE_RATES.join(', ')}).`;
  }
  if (!(PERMITTED_BIT_DEPTHS as readonly number[]).includes(f.bitDepth)) {
    return `${f.bitDepth}-bit is not one of the depths the .jmi format permits (${PERMITTED_BIT_DEPTHS.join(', ')}).`;
  }
  if (f.encoding === 'float') {
    return 'Floating-point WAV is rejected by the bundle validator (it requires integer PCM), so it cannot pass through untouched.';
  }
  if (f.channels > 2) return `${f.channels} channels — a bundle is mono or stereo.`;
  return null;
}

/** The legal format closest to a source format, when the source is illegal. */
function nearestLegalFormat(f: BundleFormat): BundleFormat {
  const rate = (PERMITTED_SAMPLE_RATES as readonly number[]).includes(f.sampleRate)
    ? f.sampleRate
    : [...PERMITTED_SAMPLE_RATES].sort((a, b) => Math.abs(a - f.sampleRate) - Math.abs(b - f.sampleRate))[0];
  const depth = (PERMITTED_BIT_DEPTHS as readonly number[]).includes(f.bitDepth) && f.encoding === 'int'
    ? f.bitDepth
    : f.bitDepth <= 16
      ? 16
      : 24;
  return { sampleRate: rate, bitDepth: depth, channels: Math.min(2, Math.max(1, f.channels)), encoding: 'int' };
}

/**
 * Group the readable files by format, pick the bundle's format, and classify
 * every file against it.
 *
 * The target is the largest schema-legal group (ties broken towards the higher
 * rate, then the higher depth, then stereo), or — when nothing is legal — the
 * nearest legal format to the largest group. `targetOverride` lets the user
 * pick a different group in the UI, which is the proper answer to a drop that
 * is half 44.1 kHz and half 48 kHz.
 */
export function censusFormats(files: ScannedFile[], targetOverride?: BundleFormat): FormatCensus {
  const byKey = new Map<string, FormatGroup>();
  for (const f of files) {
    if (!f.audio || f.error) continue;
    const format: BundleFormat = {
      sampleRate: f.audio.sampleRate,
      bitDepth: f.audio.bitDepth,
      channels: f.audio.channels,
      encoding: f.audio.encoding,
    };
    const key = formatKey(format);
    const existing = byKey.get(key);
    if (existing) existing.fileIndices.push(f.index);
    else {
      const illegalReason = schemaObjection(format);
      byKey.set(key, {
        format,
        label: formatLabel(format),
        fileIndices: [f.index],
        schemaLegal: illegalReason === null,
        illegalReason,
      });
    }
  }

  const groups = [...byKey.values()].sort((a, b) => b.fileIndices.length - a.fileIndices.length);
  const notes: string[] = [];

  let target: BundleFormat;
  if (targetOverride) {
    target = targetOverride;
  } else {
    const legal = groups.filter((g) => g.schemaLegal);
    const pool = legal.length > 0 ? legal : groups;
    const best = [...pool].sort(
      (a, b) =>
        b.fileIndices.length - a.fileIndices.length ||
        b.format.sampleRate - a.format.sampleRate ||
        b.format.bitDepth - a.format.bitDepth ||
        b.format.channels - a.format.channels,
    )[0];
    if (!best) {
      target = { sampleRate: 44100, bitDepth: 24, channels: 1, encoding: 'int' };
    } else {
      target = legal.length > 0 ? best.format : nearestLegalFormat(best.format);
      if (legal.length === 0) {
        notes.push(
          `None of the dropped files is in a format the .jmi schema permits. The closest permitted format is ${formatLabel(target)}, and every file would have to be converted to reach it.`,
        );
      }
    }
  }

  const verdicts: Record<number, FormatVerdict> = {};
  for (const g of groups) {
    const v: FormatVerdict =
      formatKey(g.format) === formatKey(target)
        ? 'passthrough'
        : g.format.sampleRate !== target.sampleRate
          ? 'rate-mismatch'
          : 'convertible';
    for (const i of g.fileIndices) verdicts[i] = v;
  }

  const mixed = groups.length > 1;
  if (mixed) {
    notes.push(
      `The drop holds ${groups.length} different audio formats: ${groups.map((g) => `${g.label} (${g.fileIndices.length})`).join(', ')}. A bundle can only hold one, so ${formatLabel(target)} was chosen as the bundle's format.`,
    );
    const rateOutliers = groups.filter((g) => g.format.sampleRate !== target.sampleRate);
    if (rateOutliers.length > 0) {
      notes.push(
        `Files at ${rateOutliers.map((g) => `${g.format.sampleRate} Hz`).join(' / ')} cannot join a ${target.sampleRate} Hz bundle without resampling, which this importer does not do. They are left out — choose their format as the bundle's instead if they are the ones you want.`,
      );
    }
  }

  return { groups, target, targetLabel: formatLabel(target), mixed, verdicts, notes };
}

/* ------------------------------------------------------------------ */
/* velocity layers                                                    */
/* ------------------------------------------------------------------ */

export interface VelocityGroup {
  /** sort key from the parsed names; null = no velocity information at all */
  order: number | null;
  label: string;
  fileIndices: number[];
  /** mean body level of the group, for the fold decision and for display */
  levelDb: number;
}

export interface PlannedWindow {
  layer: VelocityLayer['layer'];
  name: string;
  lo: number;
  hi: number;
  /** indices into the sorted velocity-group list feeding this window */
  sources: number[];
  sourceLabels: string[];
}

const WINDOW_NAMES = ['Soft', 'Medium', 'Hard', 'Very Hard'];

/** Split 1…127 into one contiguous window per group of source layers. */
export function windowsForGroups(groups: number[][]): { lo: number; hi: number; sources: number[] }[] {
  const count = groups.length;
  const edges: number[] = [];
  for (let i = 0; i <= count; i++) edges.push(1 + Math.round((i * 127) / count));
  return groups.map((sources, i) => ({
    lo: edges[i],
    hi: i === count - 1 ? 127 : edges[i + 1] - 1,
    sources,
  }));
}

/**
 * Fold a layer count down to the schema's maximum by repeatedly pairing the
 * two adjacent layers closest in level — the same rule, and the same promise
 * (no audio is dropped), as the X7L importer's five-into-four fold.
 */
export function foldLayers(levelsDb: number[], max = MAX_VELOCITY_LAYERS): { groups: number[][]; folds: string[] } {
  const groups: number[][] = levelsDb.map((_, i) => [i]);
  const folds: string[] = [];
  const levelOf = (g: number[]) => g.reduce((a, i) => a + levelsDb[i], 0) / g.length;
  while (groups.length > max) {
    let bestAt = 0;
    let bestGap = Infinity;
    for (let i = 0; i + 1 < groups.length; i++) {
      const gap = Math.abs(levelOf(groups[i + 1]) - levelOf(groups[i]));
      if (gap < bestGap) {
        bestGap = gap;
        bestAt = i;
      }
    }
    folds.push(
      `Layers ${bestAt + 1} and ${bestAt + 2} (${round2(bestGap)} dB apart) share one velocity window as round robins.`,
    );
    groups.splice(bestAt, 2, [...groups[bestAt], ...groups[bestAt + 1]]);
  }
  return { groups, folds };
}

/* ------------------------------------------------------------------ */
/* plan                                                               */
/* ------------------------------------------------------------------ */

/** A per-file override the user has made in the mapping table. */
export interface FileOverride {
  /** replace the root note */
  note?: number;
  /** move this file to the n-th velocity group (1-based over the sorted groups) */
  layer?: number;
  /** leave this file out */
  exclude?: boolean;
}

export interface PlanDropOptions {
  /** instrument name; defaults to the dropped folder or the common name prefix */
  name?: string;
  /** dropped folder name, used for the default instrument name */
  folderName?: string;
  world?: World;
  /** semitones added to every note parsed from a FILE NAME (C3=60 sets: +12) */
  semitoneOffset?: number;
  /** pick a different format group as the bundle's format */
  targetFormat?: BundleFormat;
  /** convert the convertible outliers instead of leaving them out */
  convertOutliers?: boolean;
  overrides?: Record<number, FileOverride>;
  soundType?: SoundType;
}

/** One row of the mapping table the user confirms before converting. */
export interface MappingRow {
  fileIndex: number;
  fileName: string;
  /** the root note this file will be filed under, or null when unusable */
  note: number | null;
  noteName: string;
  noteSource: NoteSource;
  /** 1-based velocity layer after folding, or null when the file is excluded */
  layer: number | null;
  layerLabel: string;
  roundRobin: number | null;
  /** destination file name inside the bundle */
  destFile: string | null;
  format: string;
  verdict: FormatVerdict | 'unreadable';
  /** what the parser matched, plus the detector's verdict */
  explanation: string;
  /** set when this file will not be converted */
  excludedReason: string | null;
  /** set when the file will be format-converted rather than copied */
  convertReason: string | null;
  peakDb: number | null;
  lengthMs: number | null;
  detectedNoteName: string | null;
  detectedCents: number | null;
}

export interface PlannedDropSample {
  /** bundle-relative path, e.g. "samples/C3_v2_rr1.wav" */
  file: string;
  fileIndex: number;
  layer: number;
  rr: number;
  /** true when the PCM is copied byte-for-byte */
  passthrough: boolean;
}

export interface PlannedDropZone {
  rootNote: number;
  keyRangeLo: number;
  keyRangeHi: number;
  velRangeLo: number;
  velRangeHi: number;
  samples: PlannedDropSample[];
}

export interface DropImportPlan {
  name: string;
  bundleName: string;
  world: World;
  soundType: SoundType;
  soundTypeReason: string;
  format: BundleFormat;
  formatLabel: string;
  census: FormatCensus;
  velocityGroups: VelocityGroup[];
  windows: PlannedWindow[];
  zones: PlannedDropZone[];
  rows: MappingRow[];
  /** file index → absolute source path, so the converter needs nothing else */
  sourcePaths: Record<number, string>;
  roots: number[];
  keyRangeLo: number;
  keyRangeHi: number;
  sampleCount: number;
  /** files whose PCM is copied unchanged */
  passthroughCount: number;
  /** files that will be format-converted (always listed, never silent) */
  convertCount: number;
  excludedCount: number;
  /** how many roots came from the pitch detector rather than a file name */
  pitchFallbackCount: number;
  totalPcmBytes: number;
  totalDurationSec: number;
  /** a non-zero multiple of 12 when names and detected pitch disagree uniformly */
  suggestedSemitoneOffset: number | null;
  notes: string[];
  warnings: string[];
  /** true when there is something convertible here */
  usable: boolean;
}

/**
 * Build the conversion plan from a scan. Pure: the same scan and options
 * always give the same plan, so the UI can re-plan on every keystroke.
 */
export function planDrop(files: ScannedFile[], opts: PlanDropOptions = {}): DropImportPlan {
  const census = censusFormats(files, opts.targetFormat);
  const overrides = opts.overrides ?? {};
  const offset = opts.semitoneOffset ?? 0;
  const notes: string[] = [...census.notes];
  const warnings: string[] = [];

  /* ---- which files are in, and why the others are not -------------- */
  interface Usable {
    file: ScannedFile;
    note: number;
    noteSource: NoteSource;
    verdict: FormatVerdict;
  }
  const usable: Usable[] = [];
  const excluded = new Map<number, string>();

  for (const f of files) {
    const ov = overrides[f.index] ?? {};
    if (f.error || !f.audio || !f.measurement) {
      excluded.set(f.index, f.error ?? 'Unreadable audio.');
      continue;
    }
    if (ov.exclude) {
      excluded.set(f.index, 'Left out by you.');
      continue;
    }
    const verdict = census.verdicts[f.index] ?? 'rate-mismatch';
    if (verdict === 'rate-mismatch') {
      excluded.set(
        f.index,
        `${f.audio.sampleRate} Hz — the bundle is ${census.target.sampleRate} Hz, and changing a file's sample rate means resampling it, which this importer will not do.`,
      );
      continue;
    }
    if (verdict === 'convertible' && !opts.convertOutliers) {
      excluded.set(
        f.index,
        `${formatLabel({ sampleRate: f.audio.sampleRate, bitDepth: f.audio.bitDepth, channels: f.audio.channels, encoding: f.audio.encoding })} — the bundle is ${census.targetLabel}. Turn on converting the odd files out if you want it included.`,
      );
      continue;
    }

    // Root note: an override wins, then the file name, then the detector.
    let note: number | null = null;
    let noteSource: NoteSource = 'none';
    if (ov.note !== undefined) {
      note = ov.note;
      noteSource = 'manual';
    } else if (f.parse.note !== null && f.parse.noteSource === 'note-name') {
      note = clampNote(f.parse.note + offset);
      noteSource = 'note-name';
    } else if (f.parse.note !== null && f.detected && Math.abs(f.detected.note - (f.parse.note + offset)) > 1) {
      // A low-confidence name (a bare MIDI number) the audio contradicts: the
      // audio wins, exactly as the X7L importer lets a level measurement
      // override the catalogue's layer order.
      note = f.detected.note;
      noteSource = 'pitch-detect';
    } else if (f.parse.note !== null) {
      note = clampNote(f.parse.note + offset);
      noteSource = f.parse.noteSource;
    } else if (f.detected) {
      note = f.detected.note;
      noteSource = 'pitch-detect';
    }

    if (note === null) {
      excluded.set(
        f.index,
        'No pitch in the file name and no stable pitch in the audio — set its root note by hand or leave it out.',
      );
      continue;
    }
    usable.push({ file: f, note, noteSource, verdict });
  }

  /* ---- does the detector think the names are an octave out? -------- */
  const suggestedSemitoneOffset = suggestOffset(files, offset);
  if (suggestedSemitoneOffset !== null) {
    warnings.push(
      `Every file name that carries a note is ${suggestedSemitoneOffset > 0 ? '' : '-'}${Math.abs(suggestedSemitoneOffset) / 12} octave${Math.abs(suggestedSemitoneOffset) === 12 ? '' : 's'} away from the pitch actually in the audio — the set is probably written to the C3=60 convention. Applying the offset is one control away.`,
    );
  }

  /* ---- velocity groups -------------------------------------------- */
  const groupMap = new Map<string, VelocityGroup>();
  for (const u of usable) {
    const v = u.file.parse.velocity;
    const order = v ? v.order : null;
    const key = v ? `${v.kind}:${order}` : 'none';
    const label = v ? v.label : 'Unspecified';
    const g = groupMap.get(key);
    if (g) g.fileIndices.push(u.file.index);
    else groupMap.set(key, { order, label, fileIndices: [u.file.index], levelDb: 0 });
  }
  const velocityGroups = [...groupMap.values()].sort((a, b) => {
    if (a.order === null) return -1;
    if (b.order === null) return 1;
    return a.order - b.order;
  });
  const byIndex = new Map(files.map((f) => [f.index, f]));
  for (const g of velocityGroups) {
    const levels = g.fileIndices.map((i) => byIndex.get(i)?.measurement?.bodyRmsDb ?? -144);
    g.levelDb = round2(levels.reduce((a, b) => a + b, 0) / Math.max(1, levels.length));
  }
  if (velocityGroups.length > 1 && velocityGroups.some((g) => g.order === null)) {
    warnings.push(
      `${velocityGroups.find((g) => g.order === null)?.fileIndices.length} file(s) carry no velocity marking while the rest do. They have been put in the softest window — set their layer by hand if that is wrong.`,
    );
  }

  const { groups: folded, folds } = foldLayers(velocityGroups.map((g) => g.levelDb));
  if (folds.length > 0) {
    notes.push(
      `The drop holds ${velocityGroups.length} velocity layers and the .jmi format allows ${MAX_VELOCITY_LAYERS}. ${folds.join(' ')} No audio is dropped.`,
    );
  }

  const windows: PlannedWindow[] = windowsForGroups(folded).map((w, i) => ({
    layer: (i + 1) as VelocityLayer['layer'],
    name: WINDOW_NAMES[i] ?? `Layer ${i + 1}`,
    lo: w.lo,
    hi: w.hi,
    sources: w.sources,
    sourceLabels: w.sources.map((s) => velocityGroups[s].label),
  }));

  /** velocity-group index → window (1-based layer) */
  const groupToLayer = new Map<number, number>();
  windows.forEach((w) => w.sources.forEach((s) => groupToLayer.set(s, w.layer)));
  const groupOfFile = new Map<number, number>();
  velocityGroups.forEach((g, gi) => g.fileIndices.forEach((i) => groupOfFile.set(i, gi)));

  /* ---- zones ------------------------------------------------------- */
  const roots = [...new Set(usable.map((u) => u.note))].sort((a, b) => a - b);
  const ranges = keyRanges(roots);
  const rangeOf = new Map(roots.map((r, i) => [r, ranges[i]]));

  // (root, layer) → the files that land there, in a stable order
  const cells = new Map<string, Usable[]>();
  for (const u of usable) {
    const ov = overrides[u.file.index] ?? {};
    const gi = groupOfFile.get(u.file.index) ?? 0;
    const layer = ov.layer ?? groupToLayer.get(gi) ?? 1;
    const key = `${u.note}/${layer}`;
    const list = cells.get(key);
    if (list) list.push(u);
    else cells.set(key, [u]);
  }

  const rowOf = new Map<number, { layer: number; rr: number; destFile: string }>();
  const zones: PlannedDropZone[] = [];
  let totalPcmBytes = 0;
  let totalFrames = 0;
  let passthroughCount = 0;
  let convertCount = 0;

  for (const root of roots) {
    for (const win of windows) {
      const members = cells.get(`${root}/${win.layer}`);
      if (!members || members.length === 0) continue;
      // Round robins: the parsed index first, then the file name, so the order
      // is stable and a missing rr marker never collides.
      const ordered = [...members].sort(
        (a, b) =>
          (a.file.parse.roundRobin ?? 999) - (b.file.parse.roundRobin ?? 999) ||
          a.file.fileName.localeCompare(b.file.fileName),
      );
      const samples: PlannedDropSample[] = ordered.map((u, i) => {
        const file = `samples/${sampleFileName(root, win.layer, i + 1)}`;
        const passthrough = u.verdict === 'passthrough';
        if (passthrough) passthroughCount++;
        else convertCount++;
        totalPcmBytes += u.file.audio?.dataBytes ?? 0;
        totalFrames += u.file.audio?.frames ?? 0;
        rowOf.set(u.file.index, { layer: win.layer, rr: i + 1, destFile: file });
        return { file, fileIndex: u.file.index, layer: win.layer, rr: i + 1, passthrough };
      });
      const range = rangeOf.get(root)!;
      zones.push({
        rootNote: root,
        keyRangeLo: range.lo,
        keyRangeHi: range.hi,
        velRangeLo: win.lo,
        velRangeHi: win.hi,
        samples,
      });
    }
  }

  /* ---- sound type -------------------------------------------------- */
  const { soundType, reason } = opts.soundType
    ? { soundType: opts.soundType, reason: 'Set by you.' }
    : decideSoundType(usable.map((u) => u.file));
  if (soundType === 'sustaining') {
    notes.push(
      'The material sustains, so loop points are searched for in each sample as it is written. A sample with no stable loop keeps `loop: null` rather than being given a fabricated one.',
    );
  }

  /* ---- mapping rows ------------------------------------------------ */
  const usableByIndex = new Map(usable.map((u) => [u.file.index, u]));
  const rows: MappingRow[] = files.map((f) => {
    const u = usableByIndex.get(f.index);
    const placed = rowOf.get(f.index);
    const fmt = f.audio
      ? formatLabel({
          sampleRate: f.audio.sampleRate,
          bitDepth: f.audio.bitDepth,
          channels: f.audio.channels,
          encoding: f.audio.encoding,
        })
      : '—';
    const verdict: MappingRow['verdict'] = f.error || !f.audio ? 'unreadable' : (census.verdicts[f.index] ?? 'rate-mismatch');
    const detectedBit = f.detected
      ? `detector heard ${midiToNoteName(f.detected.note)}${f.detected.cents >= 0 ? '+' : ''}${f.detected.cents.toFixed(1)}¢ (confidence ${f.detected.confidence.toFixed(2)})`
      : 'detector found no stable pitch';
    const nameBit = describeRowParse(f.parse);
    return {
      fileIndex: f.index,
      fileName: f.fileName,
      note: u?.note ?? null,
      noteName: u ? midiToNoteName(u.note) : '—',
      noteSource: u?.noteSource ?? f.parse.noteSource,
      layer: placed?.layer ?? null,
      layerLabel: placed ? (windows.find((w) => w.layer === placed.layer)?.name ?? `Layer ${placed.layer}`) : '—',
      roundRobin: placed?.rr ?? null,
      destFile: placed?.destFile ?? null,
      format: fmt,
      verdict,
      explanation: `${nameBit}; ${detectedBit}`,
      excludedReason: excluded.get(f.index) ?? null,
      convertReason:
        u && u.verdict === 'convertible'
          ? `Converted from ${fmt} to ${census.targetLabel} — this file is NOT a bit-perfect passthrough.`
          : null,
      peakDb: f.measurement?.peakDb ?? null,
      lengthMs: f.audio ? round2(f.audio.lengthMs) : null,
      detectedNoteName: f.detected ? midiToNoteName(f.detected.note) : null,
      detectedCents: f.detected?.cents ?? null,
    };
  });

  const pitchFallbackCount = usable.filter((u) => u.noteSource === 'pitch-detect').length;
  if (pitchFallbackCount > 0) {
    notes.push(
      `${pitchFallbackCount} of ${usable.length} root notes came from the pitch detector because the file name did not carry a usable one.`,
    );
  }
  if (convertCount > 0) {
    notes.push(
      `${convertCount} file(s) are converted to ${census.targetLabel} instead of being copied unchanged. Every one is listed in the mapping table and in the bundle's meta.notes.`,
    );
  }

  const name = (opts.name || defaultName(opts.folderName, files)).trim() || 'Imported Instrument';
  const world = opts.world ?? inferWorld(name);

  return {
    name,
    bundleName: bundleFolderName(name),
    world,
    soundType,
    soundTypeReason: reason,
    format: census.target,
    formatLabel: census.targetLabel,
    census,
    velocityGroups,
    windows,
    zones,
    rows,
    sourcePaths: Object.fromEntries(files.map((f) => [f.index, f.path])),
    roots,
    keyRangeLo: roots.length ? ranges[0].lo : 0,
    keyRangeHi: roots.length ? ranges[ranges.length - 1].hi : 127,
    sampleCount: passthroughCount + convertCount,
    passthroughCount,
    convertCount,
    excludedCount: excluded.size,
    pitchFallbackCount,
    totalPcmBytes,
    totalDurationSec: round2(totalFrames / Math.max(1, census.target.sampleRate)),
    suggestedSemitoneOffset,
    notes,
    warnings,
    usable: zones.length > 0,
  };
}

function clampNote(n: number): number {
  return Math.max(0, Math.min(127, n));
}

function defaultName(folderName: string | undefined, files: ScannedFile[]): string {
  if (folderName && folderName.trim()) return folderName.trim();
  const prefix = commonNamePrefix(files.map((f) => f.fileName));
  return prefix.length >= 2 ? prefix : 'Imported Instrument';
}

function describeRowParse(p: NameParse): string {
  const bits: string[] = [];
  if (p.noteSource === 'note-name') bits.push(`note "${p.noteToken}"`);
  else if (p.noteSource === 'midi-number') bits.push(`MIDI number "${p.noteToken}"`);
  else bits.push('no pitch in the name');
  if (p.velocity) bits.push(`${p.velocity.label} from "${p.velocity.token}"`);
  if (p.roundRobin !== null) bits.push(`round robin ${p.roundRobin}`);
  return `name: ${bits.join(', ')}`;
}

/**
 * A uniform octave disagreement between the names and the audio means the set
 * uses the other octave convention (C3 = 60), not that the files are wrong.
 * Needs at least three corroborating files before it says anything.
 */
export function suggestOffset(files: ScannedFile[], appliedOffset: number): number | null {
  const diffs: number[] = [];
  for (const f of files) {
    if (f.parse.noteSource !== 'note-name' || f.parse.note === null || !f.detected) continue;
    if (f.detected.confidence < 0.7) continue;
    diffs.push(f.detected.note - (f.parse.note + appliedOffset));
  }
  if (diffs.length < 3) return null;
  const sorted = [...diffs].sort((a, b) => a - b);
  const median = sorted[Math.floor(sorted.length / 2)];
  if (median === 0 || median % 12 !== 0) return null;
  // Every corroborating file must agree, or this is not a convention problem.
  if (!diffs.every((d) => d === median)) return null;
  return median;
}

/**
 * Decaying or sustaining, decided from the audio.
 *
 * Capture's own `detectSoundType` probes RMS at 3 s and 8 s, which needs a
 * sample at least that long. Dropped one-shots are usually shorter, so the
 * decision here is the tail-against-body level that `measurePcm` already
 * computed: a sample whose last 5% has fallen far below its loudest 200 ms has
 * decayed; one that has not is still sounding at the end, which is what a
 * sustaining sample looks like. Decided over the whole set by majority, with
 * the rule that fired recorded for the user.
 */
export function decideSoundType(files: ScannedFile[]): { soundType: SoundType; reason: string } {
  const DECAY_DB = 35;
  let decaying = 0;
  let sustaining = 0;
  for (const f of files) {
    if (!f.measurement) continue;
    const fall = f.measurement.bodyRmsDb - f.measurement.tailRmsDb;
    if (fall >= DECAY_DB) decaying++;
    else sustaining++;
  }
  if (decaying + sustaining === 0) {
    return { soundType: 'decaying', reason: 'No audio could be measured; defaulted to decaying.' };
  }
  const soundType: SoundType = decaying >= sustaining ? 'decaying' : 'sustaining';
  return {
    soundType,
    reason:
      soundType === 'decaying'
        ? `${decaying} of ${decaying + sustaining} samples fall more than ${DECAY_DB} dB from their loudest 200 ms to their final 5% — the material decays, so no loops are written.`
        : `${sustaining} of ${decaying + sustaining} samples are still sounding at the end (less than ${DECAY_DB} dB below their loudest 200 ms) — the material sustains, so loop points are searched for.`,
  };
}

/* ------------------------------------------------------------------ */
/* instrument.json                                                    */
/* ------------------------------------------------------------------ */

export interface DropSampleFacts {
  peakDb: number;
  lengthMs: number;
  loop: LoopRegion | null;
}

export interface BuildDropInstrumentOptions {
  author: string;
  createdAt?: string;
  /** what the user dropped, for the provenance line */
  sourceLabel: string;
}

/**
 * `meta.captureMode` admits only "guided" | "auto". An import is neither, and
 * widening the shared format would change the schema, the TypeScript union,
 * Signature's C++ reader and the validator for every consumer — so it is NOT
 * changed here. The X7L importer made exactly this call and flagged it for the
 * format owner; the drop importer follows it rather than inventing a second
 * answer, and the real provenance goes in `meta.notes`.
 */
export const DROP_CAPTURE_MODE = 'auto' as const;

export function buildDropInstrument(
  plan: DropImportPlan,
  facts: Record<string, DropSampleFacts>,
  opts: BuildDropInstrumentOptions,
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

  const converted = plan.rows.filter((r) => r.convertReason).map((r) => r.fileName);
  const provenance = [
    `Imported by dropping ${opts.sourceLabel} onto JM Capture — no recording was involved.`,
    plan.convertCount === 0
      ? `Every sample's PCM was copied through unchanged — ${plan.format.sampleRate} Hz, ${plan.format.bitDepth}-bit, ${plan.format.channels === 1 ? 'mono' : 'stereo'}, bit-perfect; nothing was resampled, requantised or normalised.`
      : `${plan.passthroughCount} sample(s) were copied through unchanged (bit-perfect). ${plan.convertCount} were converted to ${plan.formatLabel} so the bundle could be internally consistent: ${converted.join(', ')}. Nothing was resampled and nothing was normalised.`,
    `captureMode is recorded as "auto" because the .jmi format has no value for an import; this bundle was not recorded.`,
    plan.pitchFallbackCount > 0
      ? `${plan.pitchFallbackCount} root note(s) were taken from YIN pitch detection because the file names did not carry one.`
      : 'Every root note came from the file names.',
    plan.soundTypeReason,
    ...plan.notes.filter((n) => !n.startsWith('Imported')),
  ].join(' ');

  return {
    formatVersion: 1,
    meta: {
      name: plan.name,
      world: plan.world,
      author: opts.author,
      createdAt: opts.createdAt ?? new Date().toISOString(),
      captureMode: DROP_CAPTURE_MODE,
      channels: plan.format.channels === 1 ? 'mono' : 'stereo',
      sampleRate: plan.format.sampleRate,
      bitDepth: plan.format.bitDepth,
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
