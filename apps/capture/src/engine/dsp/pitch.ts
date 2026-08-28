/**
 * Pitch verification for captured takes (pure, Node-safe).
 *
 * Why this exists: MIDI only tells us which key was pressed. It cannot see a
 * transposed instrument, an octave switch left engaged, or the wrong patch
 * loaded — errors that make EVERY take of a session wrong by a fixed interval
 * and are invisible until the .jmi is loaded in JM Signature.
 *
 * Algorithm: YIN (de Cheveigné & Kawahara 2002) — the cumulative mean
 * normalized difference function — run on a decimated copy of the take's
 * STABLE portion (attack skipped), then refined at the full sample rate around
 * the coarse lag. The existing loop detector (dsp/loop.ts) uses plain
 * normalized autocorrelation; YIN is used here instead because autocorrelation
 * biases towards the sub-octave (it peaks at multiples of the true period),
 * which is exactly the error class this check exists to catch.
 *
 * This module is ANALYSIS ONLY. It never touches the audio that gets written.
 */

import { mixToMono, rms } from './level';

export const A4_HZ = 440;

/** Default deviation tolerance before a take is flagged (cents). */
export const DEFAULT_PITCH_TOLERANCE_CENTS = 50;
/** Below this confidence the source is treated as "can't tell", never wrong. */
export const DEFAULT_MIN_CONFIDENCE = 0.6;
/** YIN absolute threshold — the classic 0.1–0.15 band. */
const YIN_THRESHOLD = 0.15;
/** Analysis is decimated to at least this many samples per period at fMax. */
const SAMPLES_PER_PERIOD = 8;
/** Below this RMS the window is silence, not an unpitched sound. */
const SILENCE_RMS = 1e-4;

export function midiToFrequency(midi: number, a4 = A4_HZ): number {
  return a4 * Math.pow(2, (midi - 69) / 12);
}

export function frequencyToMidi(hz: number, a4 = A4_HZ): number {
  return 69 + 12 * Math.log2(hz / a4);
}

/** Signed cents from `refHz` to `hz` (positive = sharp). */
export function centsBetween(hz: number, refHz: number): number {
  return 1200 * Math.log2(hz / refHz);
}

/* ------------------------------------------------------------------ */
/* Core detection                                                      */
/* ------------------------------------------------------------------ */

export interface PitchDetectOptions {
  /** lowest fundamental to consider (Hz) */
  fMin?: number;
  /** highest fundamental to consider (Hz) */
  fMax?: number;
  /** number of analysis frames spread over the stable portion */
  frames?: number;
  /** fraction of the signal to skip before analysing (skips the attack) */
  startFraction?: number;
  /** fraction of the signal after which analysis stops (skips the decay tail) */
  endFraction?: number;
}

export interface PitchDetection {
  /** median fundamental of the voiced frames, or null when none were voiced */
  hz: number | null;
  /** 0–1; 1 - the YIN aperiodicity of the median frame */
  confidence: number;
  framesAnalysed: number;
  framesVoiced: number;
  /** max |cents| spread between voiced frames — high means unstable pitch */
  spreadCents: number;
}

/** Box-average decimation (cheap anti-alias ahead of the lag search). */
function decimate(x: Float32Array, factor: number): Float32Array {
  if (factor <= 1) return x;
  const n = Math.floor(x.length / factor);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    let sum = 0;
    for (let k = 0; k < factor; k++) sum += x[i * factor + k];
    out[i] = sum / factor;
  }
  return out;
}

/** Squared-difference function d(tau) over a window (YIN step 1). */
function differenceFunction(
  x: Float32Array,
  offset: number,
  windowSize: number,
  tauMax: number,
): Float32Array {
  const d = new Float32Array(tauMax + 1);
  for (let tau = 1; tau <= tauMax; tau++) {
    let sum = 0;
    for (let i = 0; i < windowSize; i++) {
      const delta = x[offset + i] - x[offset + i + tau];
      sum += delta * delta;
    }
    d[tau] = sum;
  }
  return d;
}

/** Cumulative mean normalized difference (YIN step 2). */
function cmndf(d: Float32Array): Float32Array {
  const out = new Float32Array(d.length);
  out[0] = 1;
  let running = 0;
  for (let tau = 1; tau < d.length; tau++) {
    running += d[tau];
    out[tau] = running > 0 ? (d[tau] * tau) / running : 1;
  }
  return out;
}

/** Parabolic interpolation of the minimum at `tau` in `y`. */
function parabolicMin(y: Float32Array | number[], tau: number): number {
  const a = y[tau - 1];
  const b = y[tau];
  const c = y[tau + 1];
  if (!Number.isFinite(a) || !Number.isFinite(b) || !Number.isFinite(c)) return tau;
  const denom = a - 2 * b + c;
  if (denom === 0) return tau;
  const shift = (0.5 * (a - c)) / denom;
  return Number.isFinite(shift) && Math.abs(shift) < 1 ? tau + shift : tau;
}

interface FrameResult {
  hz: number;
  aperiodicity: number;
}

/**
 * One YIN frame at the decimated rate, refined at the original rate.
 * Returns null when the frame is silent or nothing periodic is in range.
 */
function analyseFrame(
  full: Float32Array,
  fullOffset: number,
  sampleRate: number,
  decimated: Float32Array,
  decimFactor: number,
  analysisRate: number,
  fMin: number,
  fMax: number,
): FrameResult | null {
  const tauMin = Math.max(2, Math.floor(analysisRate / fMax));
  const tauMax = Math.ceil(analysisRate / fMin);
  const windowSize = 2 * tauMax;
  const offset = Math.floor(fullOffset / decimFactor);
  if (offset + windowSize + tauMax + 1 > decimated.length) return null;
  if (rms(decimated, offset, offset + windowSize) < SILENCE_RMS) return null;
  if (tauMax <= tauMin + 1) return null;

  const cm = cmndf(differenceFunction(decimated, offset, windowSize, tauMax));

  // YIN step 3/4: first local minimum below the absolute threshold; the FIRST
  // such dip is taken (not the global one) precisely so a strong second
  // harmonic cannot pull the answer down an octave.
  let chosen = -1;
  for (let tau = tauMin; tau < tauMax; tau++) {
    if (cm[tau] < YIN_THRESHOLD) {
      while (tau + 1 < tauMax && cm[tau + 1] < cm[tau]) tau++;
      chosen = tau;
      break;
    }
  }
  if (chosen < 0) {
    let best = tauMin;
    for (let tau = tauMin + 1; tau < tauMax; tau++) if (cm[tau] < cm[best]) best = tau;
    chosen = best;
  }
  const aperiodicity = Math.max(0, Math.min(1, cm[chosen]));
  const coarseTau = parabolicMin(cm, chosen);
  if (coarseTau <= 0) return null;
  const coarseHz = analysisRate / coarseTau;

  // Refinement at the full sample rate: one sample of lag error at the
  // decimated rate is worth tens of cents, which would swamp a 50-cent
  // tolerance. Re-minimise the plain difference function in a ±3 % band.
  const tauFull = sampleRate / coarseHz;
  const lo = Math.max(2, Math.floor(tauFull * 0.97));
  const hi = Math.ceil(tauFull * 1.03);
  const win = Math.min(Math.round(4 * tauFull), full.length - fullOffset - hi - 2);
  if (win < tauFull * 2) return { hz: coarseHz, aperiodicity };

  const span = hi - lo + 2;
  const d: number[] = new Array(span + 2).fill(0);
  for (let k = 0; k <= span; k++) {
    const tau = lo - 1 + k;
    if (tau < 1) {
      d[k] = Number.POSITIVE_INFINITY;
      continue;
    }
    let sum = 0;
    for (let i = 0; i < win; i++) {
      const delta = full[fullOffset + i] - full[fullOffset + i + tau];
      sum += delta * delta;
    }
    d[k] = sum;
  }
  let bestK = 1;
  for (let k = 1; k < span; k++) if (d[k] < d[bestK]) bestK = k;
  const refinedK = parabolicMin(d, bestK);
  const refinedTau = lo - 1 + refinedK;
  if (!(refinedTau > 0)) return { hz: coarseHz, aperiodicity };
  return { hz: sampleRate / refinedTau, aperiodicity };
}

/**
 * Detect the fundamental of a capture's stable portion.
 * Multi-frame with a median vote, so one glitched frame cannot decide.
 */
export function detectPitch(
  channels: Float32Array[],
  sampleRate: number,
  opts: PitchDetectOptions = {},
): PitchDetection {
  const empty: PitchDetection = { hz: null, confidence: 0, framesAnalysed: 0, framesVoiced: 0, spreadCents: 0 };
  if (channels.length === 0 || channels[0].length === 0) return empty;
  const mono = mixToMono(channels);

  const nyquist = sampleRate / 2;
  const fMin = Math.max(20, opts.fMin ?? 40);
  const fMax = Math.min(opts.fMax ?? 2000, nyquist * 0.45);
  if (fMax <= fMin * 1.5) return empty;

  // Decimate to ~SAMPLES_PER_PERIOD samples per period at fMax.
  const targetRate = Math.min(sampleRate, fMax * SAMPLES_PER_PERIOD);
  const decimFactor = Math.max(1, Math.floor(sampleRate / targetRate));
  const decimated = decimate(mono, decimFactor);
  const analysisRate = sampleRate / decimFactor;

  const tauMaxDec = Math.ceil(analysisRate / fMin);
  const frameSpanFull = (3 * tauMaxDec + 2) * decimFactor;
  const startF = opts.startFraction ?? 0.12;
  const endF = opts.endFraction ?? 0.75;
  const from = Math.floor(mono.length * startF);
  const to = Math.max(from, Math.floor(mono.length * endF));
  const frames = Math.max(1, opts.frames ?? 4);

  const results: FrameResult[] = [];
  for (let f = 0; f < frames; f++) {
    const t = frames === 1 ? 0 : f / (frames - 1);
    let offset = Math.round(from + t * Math.max(0, to - from - frameSpanFull));
    if (offset + frameSpanFull > mono.length) offset = mono.length - frameSpanFull;
    if (offset < 0) break;
    const r = analyseFrame(
      mono,
      offset,
      sampleRate,
      decimated,
      decimFactor,
      analysisRate,
      fMin,
      fMax,
    );
    if (r) results.push(r);
  }
  if (results.length === 0) return { ...empty, framesAnalysed: frames };

  const sorted = [...results].sort((a, b) => a.hz - b.hz);
  const median = sorted[Math.floor(sorted.length / 2)];
  const spreadCents =
    sorted.length > 1 ? Math.abs(centsBetween(sorted[sorted.length - 1].hz, sorted[0].hz)) : 0;

  // Frames that agree with the median (within a quarter tone) vote for it.
  const agreeing = results.filter((r) => Math.abs(centsBetween(r.hz, median.hz)) < 50);
  const agreement = agreeing.length / results.length;
  const periodicity = 1 - agreeing.reduce((a, r) => a + r.aperiodicity, 0) / agreeing.length;
  const confidence = Math.max(0, Math.min(1, periodicity * agreement));

  return {
    hz: median.hz,
    confidence,
    framesAnalysed: frames,
    framesVoiced: results.length,
    spreadCents,
  };
}

/* ------------------------------------------------------------------ */
/* Take verification                                                   */
/* ------------------------------------------------------------------ */

export type PitchStatus = 'ok' | 'off' | 'unknown';
export type PitchIssue = 'octave' | 'semitone' | 'interval' | 'detuned';

export interface PitchVerdict {
  status: PitchStatus;
  expectedNote: number;
  expectedHz: number;
  detectedHz: number | null;
  /** signed deviation from the expected fundamental, in cents */
  cents: number | null;
  /** the deviation rounded to whole semitones (0 when merely detuned) */
  semitones: number | null;
  /** what is left after removing the whole semitones */
  residualCents: number | null;
  /** the MIDI note the detected pitch is nearest to */
  nearestNote: number | null;
  confidence: number;
  issue: PitchIssue | null;
  /** short instructive line for the coaching register (never an accusation) */
  message: string;
}

export interface PitchVerifyOptions extends PitchDetectOptions {
  toleranceCents?: number;
  minConfidence?: number;
  /** how far either side of the expected note to search, in octaves */
  searchOctaves?: number;
  a4?: number;
}

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];

/** Local note namer (keeps this module free of app/package imports). */
export function noteName(midi: number): string {
  const m = Math.round(midi);
  return `${NOTE_NAMES[((m % 12) + 12) % 12]}${Math.floor(m / 12) - 1}`;
}

function plural(n: number, word: string): string {
  return `${n} ${word}${Math.abs(n) === 1 ? '' : 'S'}`;
}

/**
 * Compare a captured take against the note it is filed under.
 *
 * A warning, never a block: `status: 'off'` says "this does not sound like the
 * note you asked for", and `'unknown'` says "there is no stable pitch here" —
 * which is the correct answer for percussion, noise and inharmonic sources.
 */
export function verifyTakePitch(
  channels: Float32Array[],
  sampleRate: number,
  expectedNote: number,
  opts: PitchVerifyOptions = {},
): PitchVerdict {
  const a4 = opts.a4 ?? A4_HZ;
  const expectedHz = midiToFrequency(expectedNote, a4);
  const tolerance = opts.toleranceCents ?? DEFAULT_PITCH_TOLERANCE_CENTS;
  const minConfidence = opts.minConfidence ?? DEFAULT_MIN_CONFIDENCE;
  const octaves = opts.searchOctaves ?? 2;

  const base: PitchVerdict = {
    status: 'unknown',
    expectedNote,
    expectedHz,
    detectedHz: null,
    cents: null,
    semitones: null,
    residualCents: null,
    nearestNote: null,
    confidence: 0,
    issue: null,
    message: 'PITCH UNCLEAR — NO STABLE TONE IN THIS TAKE',
  };

  const detection = detectPitch(channels, sampleRate, {
    fMin: opts.fMin ?? Math.max(20, expectedHz / Math.pow(2, octaves) / 1.06),
    fMax: opts.fMax ?? expectedHz * Math.pow(2, octaves) * 1.06,
    frames: opts.frames,
    startFraction: opts.startFraction,
    endFraction: opts.endFraction,
  });

  if (detection.hz === null || detection.confidence < minConfidence) {
    return { ...base, detectedHz: detection.hz, confidence: detection.confidence };
  }

  const cents = centsBetween(detection.hz, expectedHz);
  const semitones = Math.round(cents / 100);
  const residualCents = cents - semitones * 100;
  const nearestNote = Math.round(frequencyToMidi(detection.hz, a4));

  if (Math.abs(cents) <= tolerance) {
    return {
      status: 'ok',
      expectedNote,
      expectedHz,
      detectedHz: detection.hz,
      cents,
      semitones: 0,
      residualCents: cents,
      nearestNote,
      confidence: detection.confidence,
      issue: null,
      message: `PITCH OK — ${noteName(expectedNote)} ${cents >= 0 ? '+' : '−'}${Math.abs(Math.round(cents))} CENTS`,
    };
  }

  const dir = cents > 0 ? 'ABOVE' : 'BELOW';
  const abs = Math.abs(semitones);
  let issue: PitchIssue;
  let message: string;
  if (semitones !== 0 && abs % 12 === 0) {
    issue = 'octave';
    message = `SOUNDS ${plural(abs / 12, 'OCTAVE')} ${dir} ${noteName(expectedNote)} — CHECK THE OCTAVE SWITCH`;
  } else if (abs === 1) {
    issue = 'semitone';
    message = `SOUNDS 1 SEMITONE ${dir} ${noteName(expectedNote)} — HEARD ${noteName(nearestNote)}`;
  } else if (semitones !== 0) {
    issue = 'interval';
    message = `SOUNDS ${plural(abs, 'SEMITONE')} ${dir} ${noteName(expectedNote)} — HEARD ${noteName(nearestNote)}`;
  } else {
    issue = 'detuned';
    message = `${Math.abs(Math.round(cents))} CENTS ${cents > 0 ? 'SHARP' : 'FLAT'} OF ${noteName(expectedNote)}`;
  }

  return {
    status: 'off',
    expectedNote,
    expectedHz,
    detectedHz: detection.hz,
    cents,
    semitones,
    residualCents,
    nearestNote,
    confidence: detection.confidence,
    issue,
    message,
  };
}

/* ------------------------------------------------------------------ */
/* Session-wide trend                                                  */
/* ------------------------------------------------------------------ */

export interface PitchObservation {
  note: number;
  semitones: number;
  cents: number;
  confidence: number;
}

export interface PitchTrend {
  /** the fixed interval every observed take shares, or null */
  consistentSemitones: number | null;
  /** the mean detune when every take is off by the same sub-semitone amount */
  consistentCents: number | null;
  samples: number;
  message: string | null;
}

/**
 * The catastrophic case: a FIXED offset across takes. One odd take is a
 * fingering slip; three takes all +12 is a transposed instrument.
 */
export function pitchTrend(observations: PitchObservation[], minSamples = 2): PitchTrend {
  const none: PitchTrend = {
    consistentSemitones: null,
    consistentCents: null,
    samples: observations.length,
    message: null,
  };
  if (observations.length < minSamples) return none;

  const first = observations[0].semitones;
  if (first !== 0 && observations.every((o) => o.semitones === first)) {
    const dir = first > 0 ? '+' : '−';
    const abs = Math.abs(first);
    const what =
      abs % 12 === 0
        ? `${abs / 12} OCTAVE${abs / 12 === 1 ? '' : 'S'}`
        : `${abs} SEMITONE${abs === 1 ? '' : 'S'}`;
    return {
      consistentSemitones: first,
      consistentCents: null,
      samples: observations.length,
      message: `EVERY TAKE IS ${dir}${what} — IS YOUR INSTRUMENT TRANSPOSED?`,
    };
  }

  if (observations.every((o) => o.semitones === 0)) {
    const mean = observations.reduce((a, o) => a + o.cents, 0) / observations.length;
    const spread = Math.max(...observations.map((o) => Math.abs(o.cents - mean)));
    if (Math.abs(mean) > 20 && spread < 15) {
      return {
        consistentSemitones: 0,
        consistentCents: mean,
        samples: observations.length,
        message: `EVERY TAKE IS ${Math.round(Math.abs(mean))} CENTS ${mean > 0 ? 'SHARP' : 'FLAT'} — CHECK THE INSTRUMENT'S TUNING`,
      };
    }
  }

  return none;
}
