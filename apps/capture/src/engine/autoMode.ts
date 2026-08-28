/**
 * Auto Mode sequencing engine (§3.4) — pure TS, Node-testable.
 *
 * The engine talks only to abstract ports (MIDI out, capture source, clock),
 * so the same code runs against Web MIDI + Web Audio in the app and against
 * a simulated instrument in tests.
 */

import type { SoundType } from '@jm/jmi-format/web';
import { AUTO_MODE_VELOCITIES } from '@jm/jmi-format/web';
import { nullTestDb } from './dsp/level';
import {
  DEFAULT_PITCH_TOLERANCE_CENTS,
  pitchTrend,
  verifyTakePitch,
  type PitchObservation,
  type PitchTrend,
  type PitchVerdict,
} from './dsp/pitch';
import { findSilenceEnd, HARD_CAP_S } from './dsp/silence';
import { detectSoundType, processCapture, SUSTAIN_HOLD_S, type ProcessedTake } from './processing';
import type { SessionPosition, SessionState, TakeRecord } from './session';
import {
  acceptTake,
  fileNameFor,
  markSmartRrReduced,
  nextPosition,
  requiredRRs,
  takeKey,
} from './session';
import { centerVelocity } from './velocity';

/** MIDI output port. */
export interface MidiOut {
  noteOn(note: number, velocity: number): void;
  noteOff(note: number): void;
}

/**
 * Audio capture port. `record(seconds)` captures for the given duration of
 * engine time and resolves with the raw channels. Implementations may run in
 * real time (Web Audio) or instantly (simulation).
 */
export interface CapturePort {
  record(seconds: number): Promise<Float32Array[]>;
  readonly sampleRate: number;
}

/** Clock port — real time in app, virtual (instant) in tests. */
export interface ClockPort {
  wait(ms: number): Promise<void>;
}

/**
 * Auto Mode's pitch alarm. The app sends A3; if the hardware synth is
 * transposed (or the wrong patch is up) EVERY returned sample is wrong by a
 * fixed interval — and an unattended two-hour run would find out far too late.
 * The first few takes are verified and the result surfaced; the run is never
 * stopped by the engine itself, the operator decides (§3.4).
 */
export interface AutoPitchAlert {
  verdict: PitchVerdict;
  trend: PitchTrend;
  position: SessionPosition;
  /** how many of the checked takes have come back off-pitch */
  offCount: number;
  checked: number;
}

export interface AutoModeCallbacks {
  onPosition?(pos: SessionPosition): void;
  onTake?(take: TakeRecord, processed: ProcessedTake): void;
  onSoundTypeDetected?(t: SoundType): void;
  onNote?(msg: string): void;
  /** persist snapshot after every accepted take */
  onSnapshot?(state: SessionState): void;
  /** a verified take did not sound like the note that was sent */
  onPitchAlert?(alert: AutoPitchAlert): void;
}

export interface AutoModeOptions {
  /** measured round-trip latency (ms) — trims capture starts (§3.4) */
  latencyMs?: number;
  /** null-test threshold for smart-RR (dBFS) */
  smartRrThresholdDb?: number;
  /** hold after note-off for decaying captures before silence scan (s) */
  decayingNoteOffAfterS?: number;
  settleMs?: number;
  /** cap for each raw capture (s) */
  captureCapS?: number;
  /** verify the returned audio's pitch on the first N takes (0 disables) */
  pitchCheckTakes?: number;
  /** deviation from the sent note tolerated before alerting (cents) */
  pitchToleranceCents?: number;
}

export const SETTLE_MS = 500;
export const DECAYING_NOTE_OFF_S = 2;
/** Enough takes to tell a transposed rig from one odd unpitched sample. */
export const AUTO_PITCH_CHECK_TAKES = 4;

/**
 * Measure round-trip latency: the caller sends a ping (short MIDI note or
 * test tone) and this measures the delay to the first non-silent sample.
 */
export async function measureLatencyMs(
  sendPing: () => void,
  capture: CapturePort,
  probeSeconds = 2,
): Promise<number> {
  const recording = capture.record(probeSeconds);
  sendPing();
  const raw = await recording;
  const threshold = 0.001; // −60 dBFS
  const mono = raw[0];
  for (let i = 0; i < mono.length; i++) {
    if (Math.abs(mono[i]) >= threshold) return (i / capture.sampleRate) * 1000;
  }
  return 0;
}

export class AutoModeEngine {
  private cancelled = false;
  private _running = false;

  constructor(
    private midi: MidiOut,
    private capture: CapturePort,
    private clock: ClockPort,
    private opts: AutoModeOptions = {},
    private cb: AutoModeCallbacks = {},
  ) {}

  get running(): boolean {
    return this._running;
  }

  cancel(): void {
    this.cancelled = true;
  }

  /**
   * Run (or resume) the session. Iterates nextPosition() so a cancelled or
   * crashed run resumes exactly where it left off. Returns the final state.
   */
  async run(state: SessionState): Promise<SessionState> {
    this.cancelled = false;
    this._running = true;
    try {
      let s = state;

      // Resolve sound type first if set to auto-detect (§3.4 detection probe).
      if (s.resolvedSoundType === null) {
        s = await this.detectSoundTypeProbe(s);
      }

      for (;;) {
        if (this.cancelled) return s;
        const pos = nextPosition(s);
        if (!pos) return s;
        this.cb.onPosition?.(pos);

        const { take, processed } = await this.captureOne(s, pos);
        s = acceptTake(s, take);
        this.cb.onTake?.(take, processed);
        this.cb.onSnapshot?.(s);
        this.verifyPitch(processed.channels, pos);

        // Smart RR: after the last planned RR of a cell, null-test the takes.
        s = await this.maybeSmartRr(s, pos, processed);

        await this.clock.wait(this.opts.settleMs ?? SETTLE_MS);
      }
    } finally {
      this._running = false;
    }
  }

  private async detectSoundTypeProbe(s: SessionState): Promise<SessionState> {
    const note = s.config.plan.notes[Math.floor(s.config.plan.notes.length / 2)];
    const vel = centerVelocity(3, s.config.plan.velocityMap);
    const probeS = 9;
    const rec = this.capture.record(probeS);
    this.midi.noteOn(note, vel);
    await this.clock.wait(8500);
    this.midi.noteOff(note);
    const raw = await rec;
    const t = detectSoundType(raw, this.capture.sampleRate);
    this.cb.onSoundTypeDetected?.(t);
    this.cb.onNote?.(`Auto-detected sound type: ${t}`);
    await this.clock.wait(1500); // let the probe ring out
    return { ...s, resolvedSoundType: t, updatedAt: new Date().toISOString() };
  }

  private async captureOne(
    s: SessionState,
    pos: SessionPosition,
  ): Promise<{ take: TakeRecord; processed: ProcessedTake }> {
    const soundType = s.resolvedSoundType ?? 'decaying';
    const velocity = centerVelocity(pos.layer, s.config.plan.velocityMap);
    const sr = this.capture.sampleRate;
    const capS = this.opts.captureCapS ?? HARD_CAP_S;

    let raw: Float32Array[];
    if (soundType === 'sustaining') {
      const holdS = Math.min(SUSTAIN_HOLD_S, capS);
      const tailS = 2;
      const rec = this.capture.record(Math.min(holdS + tailS, capS));
      this.midi.noteOn(pos.note, velocity);
      await this.clock.wait(holdS * 1000);
      this.midi.noteOff(pos.note);
      await this.clock.wait(tailS * 1000);
      raw = await rec;
    } else {
      const noteOffS = this.opts.decayingNoteOffAfterS ?? DECAYING_NOTE_OFF_S;
      const rec = this.capture.record(capS);
      this.midi.noteOn(pos.note, velocity);
      await this.clock.wait(noteOffS * 1000);
      this.midi.noteOff(pos.note);
      raw = await rec;
      // Trim at first sustained silence after note-off (implementations that
      // record the full cap are shortened here rather than in real time).
      const end = findSilenceEnd(raw, sr);
      if (end !== null) raw = raw.map((ch) => ch.slice(0, end));
    }

    // Latency compensation: drop the measured round-trip delay from the front.
    const latencySamples = Math.round(((this.opts.latencyMs ?? 0) / 1000) * sr);
    if (latencySamples > 0 && latencySamples < raw[0].length) {
      raw = raw.map((ch) => ch.slice(latencySamples));
    }

    const processed = processCapture(raw, sr, soundType);
    const take: TakeRecord = {
      note: pos.note,
      layer: pos.layer,
      rr: pos.rr,
      file: fileNameFor(pos),
      peakDb: processed.peakDb,
      lengthMs: processed.lengthMs,
      velocity,
      loop: processed.loop,
      acceptedAt: new Date().toISOString(),
    };
    return { take, processed };
  }

  private pitchChecked = 0;
  private pitchOff = 0;
  private pitchHistory: PitchObservation[] = [];

  /** Confident pitch observations from the verified head of the run. */
  getPitchHistory(): readonly PitchObservation[] {
    return this.pitchHistory;
  }

  /**
   * Verify the first few returned takes against the note that was SENT.
   * Analysis only — it never alters the audio, and it never stops the run.
   */
  private verifyPitch(channels: Float32Array[], pos: SessionPosition): void {
    const budget = this.opts.pitchCheckTakes ?? AUTO_PITCH_CHECK_TAKES;
    if (budget <= 0 || this.pitchChecked >= budget) return;
    this.pitchChecked++;
    let verdict: PitchVerdict;
    try {
      verdict = verifyTakePitch(channels, this.capture.sampleRate, pos.note, {
        toleranceCents: this.opts.pitchToleranceCents ?? DEFAULT_PITCH_TOLERANCE_CENTS,
      });
    } catch {
      return;
    }
    if (verdict.status === 'unknown') return; // unpitched source — say nothing
    this.pitchHistory.push({
      note: pos.note,
      semitones: verdict.semitones ?? 0,
      cents: verdict.cents ?? 0,
      confidence: verdict.confidence,
    });
    if (verdict.status !== 'off') return;
    this.pitchOff++;
    const trend = pitchTrend(this.pitchHistory);
    this.cb.onNote?.(verdict.message);
    this.cb.onPitchAlert?.({
      verdict,
      trend,
      position: pos,
      offCount: this.pitchOff,
      checked: this.pitchChecked,
    });
  }

  private lastProcessed = new Map<string, ProcessedTake>();

  private async maybeSmartRr(
    s: SessionState,
    pos: SessionPosition,
    processed: ProcessedTake,
  ): Promise<SessionState> {
    this.lastProcessed.set(takeKey(pos.note, pos.layer, pos.rr), processed);
    const required = requiredRRs(s, pos.note, pos.layer);
    if (pos.rr !== required || required < 2) return s;

    const first = this.lastProcessed.get(takeKey(pos.note, pos.layer, 1));
    if (!first) return s;
    const threshold = this.opts.smartRrThresholdDb ?? -60;
    for (let rr = 2; rr <= required; rr++) {
      const p = this.lastProcessed.get(takeKey(pos.note, pos.layer, rr));
      if (!p) return s;
      if (nullTestDb(first.channels, p.channels) >= threshold) return s; // real variation — keep all
    }
    // Near-identical digital source: keep RR1 only.
    let out = s;
    const msg = `Smart RR: note ${pos.note} layer ${pos.layer} takes null below ${threshold} dB — reduced to 1 round robin`;
    out = markSmartRrReduced(out, pos.note, pos.layer, msg);
    for (let rr = 2; rr <= required; rr++) {
      const takes = { ...out.takes };
      delete takes[takeKey(pos.note, pos.layer, rr)];
      out = { ...out, takes };
    }
    this.cb.onNote?.(msg);
    this.cb.onSnapshot?.(out);
    return out;
  }
}

export { AUTO_MODE_VELOCITIES };
