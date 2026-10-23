/**
 * Session model — the capture session's plan, progress, and persistence
 * (pure, Node-safe). A session serializes to JSON after every accepted take
 * and resumes exactly where it left off (§3.3).
 */

import type { Category } from '@amanorsac/aofas-format/catalog';
import type { CaptureMode, Channels, SoundType, VelocityLayer, World } from '@jm/jmi-format/web';
import { DEFAULT_VELOCITY_MAP, defaultPlanNotes, midiToNoteName, sampleFileName } from '@jm/jmi-format/web';
import type { PatchQueueState } from './patchQueue';

export interface SamplingPlan {
  /** MIDI note numbers to sample, ascending */
  notes: number[];
  velocityMap: VelocityLayer[];
  roundRobins: number;
}

export function defaultSamplingPlan(): SamplingPlan {
  return {
    notes: defaultPlanNotes(),
    velocityMap: DEFAULT_VELOCITY_MAP.map((l) => ({ ...l })),
    roundRobins: 3,
  };
}

/** Build a plan from key range + interval (wizard editing, §3.1). */
export function planFromRange(lo: number, hi: number, intervalSemitones: number, roundRobins = 3): SamplingPlan {
  const notes: number[] = [];
  for (let n = lo; n <= hi; n += intervalSemitones) notes.push(n);
  return { notes, velocityMap: DEFAULT_VELOCITY_MAP.map((l) => ({ ...l })), roundRobins };
}

export interface TakeRecord {
  note: number;
  layer: number;
  rr: number;
  file: string;
  peakDb: number;
  lengthMs: number;
  velocity: number;
  loop: { startSample: number; endSample: number; crossfadeSamples: number } | null;
  acceptedAt: string;
}

export interface SessionConfig {
  id: string;
  instrumentName: string;
  /**
   * The .jmi world. It drives FX defaults and cloth, and it belongs to the JM
   * format — four values, no piano among them. Left exactly as it was.
   */
  world: World;
  /**
   * The Amanorsac catalogue shelf, written into the sealed .aofas so every
   * Amanorsac app can file this sound the same way. Independent of `world`
   * on purpose: the JM format must not move to fit our taxonomy.
   */
  category?: Category;
  soundType: SoundType | 'auto-detect';
  channels: Channels;
  captureMode: CaptureMode;
  sampleRate: number;
  bitDepth: 16 | 24;
  audioInputId: string | null;
  audioInputChannels: [number, number] | [number];
  midiInputId: string | null;
  midiOutputId: string | null;
  plan: SamplingPlan;
  createdAt: string;
  notes?: string;
  /** stereo capture confirmed effectively mono — fold at export (§3.5) */
  foldToMono?: boolean;
}

export interface SessionState {
  formatVersion: 1;
  config: SessionConfig;
  /** detected sound type once known (auto detection or user choice) */
  resolvedSoundType: SoundType | null;
  /** accepted takes keyed by `${note}_${layer}_${rr}` */
  takes: Record<string, TakeRecord>;
  /** zones auto-reduced to 1 RR by the smart-RR null test, keyed `${note}_${layer}` */
  smartRrReduced: Record<string, true>;
  sessionNotes: string[];
  /**
   * Multi-patch sampling queue (§3.4). Lives on the *base* session created by
   * the wizard; each entry's takes go into their own derived session, so a
   * queue resumes at both levels. Absent for ordinary single-run sessions,
   * whose path is completely unchanged.
   */
  patchQueue?: PatchQueueState;
  updatedAt: string;
}

export function takeKey(note: number, layer: number, rr: number): string {
  return `${note}_${layer}_${rr}`;
}

export function cellKey(note: number, layer: number): string {
  return `${note}_${layer}`;
}

export function createSession(config: SessionConfig): SessionState {
  return {
    formatVersion: 1,
    config,
    resolvedSoundType: config.soundType === 'auto-detect' ? null : config.soundType,
    takes: {},
    smartRrReduced: {},
    sessionNotes: [],
    updatedAt: new Date().toISOString(),
  };
}

export interface CellProgress {
  note: number;
  layer: number;
  /** takes accepted / required (accounting for smart-RR reduction) */
  done: number;
  required: number;
  complete: boolean;
}

export function requiredRRs(s: SessionState, note: number, layer: number): number {
  return s.smartRrReduced[cellKey(note, layer)] ? 1 : s.config.plan.roundRobins;
}

export function cellProgress(s: SessionState, note: number, layer: number): CellProgress {
  const required = requiredRRs(s, note, layer);
  let done = 0;
  for (let rr = 1; rr <= required; rr++) {
    if (s.takes[takeKey(note, layer, rr)]) done++;
  }
  return { note, layer, done, required, complete: done >= required };
}

export interface SessionPosition {
  note: number;
  layer: number;
  rr: number;
}

/**
 * The next take to capture: first incomplete (note, layer, rr) in note-major
 * order (all layers/RRs of a note before moving to the next note).
 * Null when the whole session is complete. Resume logic uses this (§3.3).
 */
export function nextPosition(s: SessionState): SessionPosition | null {
  for (const note of s.config.plan.notes) {
    for (const layerDef of s.config.plan.velocityMap) {
      const required = requiredRRs(s, note, layerDef.layer);
      for (let rr = 1; rr <= required; rr++) {
        if (!s.takes[takeKey(note, layerDef.layer, rr)]) {
          return { note, layer: layerDef.layer, rr };
        }
      }
    }
  }
  return null;
}

export function acceptTake(s: SessionState, take: TakeRecord): SessionState {
  return {
    ...s,
    takes: { ...s.takes, [takeKey(take.note, take.layer, take.rr)]: take },
    updatedAt: new Date().toISOString(),
  };
}

/** Remove a take (progress-map click-to-redo, or RR outlier retake). */
export function clearTake(s: SessionState, note: number, layer: number, rr: number): SessionState {
  const takes = { ...s.takes };
  delete takes[takeKey(note, layer, rr)];
  return { ...s, takes, updatedAt: new Date().toISOString() };
}

export function clearCell(s: SessionState, note: number, layer: number): SessionState {
  const takes = { ...s.takes };
  for (let rr = 1; rr <= s.config.plan.roundRobins; rr++) delete takes[takeKey(note, layer, rr)];
  const smartRrReduced = { ...s.smartRrReduced };
  delete smartRrReduced[cellKey(note, layer)];
  return { ...s, takes, smartRrReduced, updatedAt: new Date().toISOString() };
}

export function markSmartRrReduced(s: SessionState, note: number, layer: number, note_text: string): SessionState {
  return {
    ...s,
    smartRrReduced: { ...s.smartRrReduced, [cellKey(note, layer)]: true },
    sessionNotes: [...s.sessionNotes, note_text],
    updatedAt: new Date().toISOString(),
  };
}

export function sessionStats(s: SessionState): { done: number; total: number; percent: number } {
  let done = 0;
  let total = 0;
  for (const note of s.config.plan.notes) {
    for (const l of s.config.plan.velocityMap) {
      const req = requiredRRs(s, note, l.layer);
      total += req;
      for (let rr = 1; rr <= req; rr++) if (s.takes[takeKey(note, l.layer, rr)]) done++;
    }
  }
  return { done, total, percent: total ? Math.round((done / total) * 100) : 0 };
}

export function isComplete(s: SessionState): boolean {
  return nextPosition(s) === null;
}

/** Canonical WAV file name for a plan position (delegates to @jm/jmi-format). */
export function fileNameFor(pos: SessionPosition): string {
  return sampleFileName(pos.note, pos.layer, pos.rr);
}

export function describePosition(pos: SessionPosition, map: VelocityLayer[]): string {
  const layerName = map.find((l) => l.layer === pos.layer)?.name ?? `Layer ${pos.layer}`;
  return `${midiToNoteName(pos.note)} · Layer ${pos.layer} · ${layerName} · Take ${pos.rr}`;
}

/** Serialize / restore — snapshots are plain JSON (§3.3 persistence). */
export function serializeSession(s: SessionState): string {
  return JSON.stringify(s, null, 2);
}

export function deserializeSession(json: string): SessionState {
  const s = JSON.parse(json) as SessionState;
  if (s.formatVersion !== 1) throw new Error(`Unsupported session formatVersion ${s.formatVersion}`);
  if (!s.config?.plan?.notes?.length) throw new Error('Corrupt session: missing plan');
  return s;
}
