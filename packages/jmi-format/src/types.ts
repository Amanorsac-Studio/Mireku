/** Shared TypeScript types for the .jmi bundle format (formatVersion 1). */

export type World = 'rhodes-ep' | 'mks' | 'fm' | 'strings';
export type SoundType = 'decaying' | 'sustaining';
export type CaptureMode = 'guided' | 'auto';
export type Channels = 'mono' | 'stereo';

export interface LoopRegion {
  startSample: number;
  endSample: number;
  crossfadeSamples: number;
}

export interface RoundRobin {
  /** POSIX-style path relative to the bundle root, e.g. "samples/C3_v2_rr1.wav" */
  file: string;
  peakDb: number;
  lengthMs: number;
  loop: LoopRegion | null;
}

export interface Zone {
  rootNote: number;
  keyRangeLo: number;
  keyRangeHi: number;
  velRangeLo: number;
  velRangeHi: number;
  roundRobins: RoundRobin[];
  /** amp release for voices from this zone, overriding the preset envelope (SFZ ampeg_release) */
  releaseMs?: number;
  /**
   * The velocity this layer was actually played at. With velocityMode 'anchored' the engine
   * matches levels at the anchors and interpolates between them, so changing layer alters the
   * timbre without stepping the level.
   */
  anchorVelocity?: number;
  /** measured loudness of this zone's samples (dBFS, RMS over the first 300 ms), for level matching */
  levelDb?: number;
}

/** A key-off sample zone (SFZ trigger=release). */
export interface ReleaseZone extends Zone {
  /** attenuation in dB per second the key was held before release (SFZ rt_decay) */
  rtDecayDb: number;
}

/** Optional playback behaviour for sampled acoustic instruments. Absent = the original JM behaviour. */
export interface PlaybackOptions {
  /**
   * 'switch'    — hard velocity-layer switching (pianos); the velocity curve applies on top.
   * 'crossfade' — the JM ±8 equal-power blend.
   * 'anchored'  — layers carry anchorVelocity + levelDb, so level follows one continuous curve
   *               through the anchors and a layer change never steps the level.
   */
  velocityMode?: 'switch' | 'crossfade' | 'anchored';
  /** 0..100 — SFZ amp_veltrack: 100 = amplitude follows (velocity/127)². Ignored when anchored. */
  ampVelTrack?: number;
  /** 'neutral' = play the samples as recorded, without the JM world voicing (bark, tine, swell…) */
  voicing?: 'world' | 'neutral';
  /**
   * 'anchored' only: half-width in velocity steps of the equal-power blend around each layer
   * boundary. 0 = hard switch. Levels are matched first, which is what keeps two different
   * recordings from phasing audibly; 6–10 is the useful range.
   */
  velocityXfade?: number;
  /**
   * 'anchored' only: 0..100 — how far a note played below its layer's anchor is darkened by a
   * low-pass, standing in for the dynamics between the recorded layers. 0 = off.
   */
  velocityBrightness?: number;
}

export interface VelocityLayer {
  /** 1-based velocity layer, softest first (up to 16) */
  layer: number;
  name: string;
  lo: number;
  hi: number;
}

export interface InstrumentMeta {
  /**
   * The uniform gain, in dB, the builder applied to every sample of this instrument. Recorded so a
   * later pass (key-off samples) can match the level the body was built at.
   */
  renderTrimDb?: number;
  name: string;
  world: World;
  author: string;
  createdAt: string;
  captureMode: CaptureMode;
  channels: Channels;
  sampleRate: number;
  bitDepth: number;
  notes?: string;
}

export interface Instrument {
  formatVersion: 1;
  meta: InstrumentMeta;
  soundType: SoundType;
  zones: Zone[];
  velocityMap: VelocityLayer[];
  releaseZones?: ReleaseZone[];
  playback?: PlaybackOptions;
  reserved: {
    releaseSamples: never[];
    pedalDownZones: never[];
  };
}

export type FxType =
  | 'jmChorus'
  | 'tremoloAutoPan'
  | 'phaser'
  | 'flanger'
  | 'epPreamp'
  | 'vintageEq'
  | 'compressor'
  | 'tapeDelay'
  | 'digitalDelay'
  | 'plateReverb'
  | 'hallShimmer'
  | 'widener'
  | 'svFilter'
  | 'tapeMachine'
  | 'ringMod';

export interface FxUnitState {
  type: FxType;
  bypassed: boolean;
  params: Record<string, number | string | boolean>;
}

export interface AmpEnvelope {
  attackMs: number;
  decayMs: number;
  sustain: number;
  releaseMs: number;
}

export interface Preset {
  formatVersion: 1;
  name: string;
  tags?: string[];
  engine: Record<string, number | string | boolean>;
  fxRack: FxUnitState[];
  ampEnvelope: AmpEnvelope;
}

/** Default velocity map per spec §3.2. */
export const DEFAULT_VELOCITY_MAP: VelocityLayer[] = [
  { layer: 1, name: 'Soft', lo: 1, hi: 45 },
  { layer: 2, name: 'Medium', lo: 46, hi: 80 },
  { layer: 3, name: 'Hard', lo: 81, hi: 110 },
  { layer: 4, name: 'Very Hard', lo: 111, hi: 127 },
];

/** Auto Mode center-of-window velocities per spec §3.4. */
export const AUTO_MODE_VELOCITIES = [23, 63, 95, 119] as const;

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

/** MIDI note number → "C3"-style name (C4 = 60, so octave = floor(n/12) - 1). */
export function midiToNoteName(midi: number): string {
  return `${NOTE_NAMES[midi % 12]}${Math.floor(midi / 12) - 1}`;
}

export function noteNameToMidi(name: string): number {
  const m = /^([A-G]#?)(-?\d+)$/.exec(name);
  if (!m) throw new Error(`Bad note name: ${name}`);
  const idx = NOTE_NAMES.indexOf(m[1]);
  if (idx < 0) throw new Error(`Bad note name: ${name}`);
  return idx + (parseInt(m[2], 10) + 1) * 12;
}

/** Canonical sample file name per spec §3.2: {NoteName}{Octave}_v{layer}_rr{n}.wav */
export function sampleFileName(midi: number, layer: number, rr: number): string {
  return `${midiToNoteName(midi)}_v${layer}_rr${rr}.wav`;
}

/** Default sampling plan notes: A0 (21) → C8 (108) every 3 semitones = 30 notes. */
export function defaultPlanNotes(): number[] {
  const notes: number[] = [];
  for (let n = 21; n <= 108; n += 3) notes.push(n);
  return notes;
}
