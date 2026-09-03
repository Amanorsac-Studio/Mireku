/**
 * Guided Mode controller (§3.3) — the auto-arm / capture / verdict state
 * machine. Talks to the audio input and persistence through ports so the
 * logic itself stays headless-testable.
 */

import type { SoundType } from '@jm/jmi-format/web';
import { dbToLin } from './dsp/level';
import { SILENCE_HOLD_MS, SILENCE_THRESHOLD_DB, HARD_CAP_S } from './dsp/silence';
import {
  DEFAULT_PITCH_TOLERANCE_CENTS,
  pitchTrend,
  verifyTakePitch,
  type PitchObservation,
  type PitchTrend,
  type PitchVerdict,
} from './dsp/pitch';
import { processCapture, SUSTAIN_HOLD_S } from './processing';
import type { SessionPosition, SessionState, TakeRecord } from './session';
import { acceptTake, fileNameFor, nextPosition, takeKey } from './session';
import { expectedPeakRange, judgeTake, rrConsistencyOutliers, type TakeVerdict } from './velocity';

export type GuidedPhase = 'waiting' | 'recording' | 'processing' | 'verdict';

/**
 * Plan-note switch gesture (the note gate's smart affordance). A single wrong
 * note NEVER moves the target — it only offers. The offer is taken by
 * repeating the SAME note:
 *   - not sooner than SWITCH_CONFIRM_MIN_MS, so a bouncing key or a doubled
 *     note-on from a stuck contact cannot confirm anything;
 *   - not later than SWITCH_CONFIRM_WINDOW_MS, so an offer never lingers into
 *     an unrelated later mistake;
 *   - and the confirming hit is NOT recorded — it re-targets and re-arms, so
 *     the take that fills a dot is always played deliberately at the new
 *     target with the coaching UI already showing it.
 */
export const SWITCH_CONFIRM_MIN_MS = 250;
export const SWITCH_CONFIRM_WINDOW_MS = 4000;

export interface WrongNoteInfo {
  /** the note that was actually played */
  played: number;
  /** the note the session is asking for */
  expected: number;
  /** the played note is part of this session's sampling plan */
  inPlan: boolean;
  /** the played note is in the plan AND still has takes outstanding */
  switchOffered: boolean;
}

/**
 * Runtime pitch-verification settings, read fresh on every take so the user
 * can turn the check off (or retune it) mid-session.
 */
export interface PitchCheckSettings {
  enabled: boolean;
  toleranceCents: number;
}

export const DEFAULT_PITCH_CHECK: PitchCheckSettings = {
  enabled: true,
  toleranceCents: DEFAULT_PITCH_TOLERANCE_CENTS,
};

export interface PitchReport {
  /** the plan position the verified take was filed under */
  position: SessionPosition;
  verdict: PitchVerdict;
  trend: PitchTrend;
  /** the very first accepted take of the session — check early (§3.5) */
  firstOfSession: boolean;
  /** how many accepted takes exist now, including this one */
  takeIndex: number;
}

export interface GuidedOptions {
  /** injectable clock (tests) */
  now?(): number;
  /** read fresh each take so a runtime toggle takes effect immediately */
  pitchCheck?(): PitchCheckSettings;
}

export interface GuidedInputPort {
  startRecording(): void;
  stopRecording(): Float32Array[];
  recordedSeconds(): number;
  peekTailPeak(seconds: number): number;
  readonly sampleRate: number;
}

export interface GuidedPersistPort {
  /** write the processed take's WAV; resolves when on disk */
  writeTake(take: TakeRecord, channels: Float32Array[]): Promise<void>;
  snapshot(state: SessionState): void;
}

export interface GuidedUiEvents {
  onPhase?(phase: GuidedPhase): void;
  onVerdict?(v: TakeVerdict, take: TakeRecord | null): void;
  onCc64Warning?(): void;
  onRrOutlier?(note: number, layer: number, outlierRr: number): void;
  /** a note-on that is not the note being sampled — nothing was recorded */
  onWrongNote?(info: WrongNoteInfo): void;
  /** the plan-note switch offer was confirmed and the target moved */
  onNoteSwitched?(pos: SessionPosition): void;
  /** pitch verification of an accepted take (warning only, never a block) */
  onPitchReport?(report: PitchReport): void;
  onState?(s: SessionState): void;
  onPosition?(pos: SessionPosition | null): void;
}

export class GuidedController {
  private state: SessionState;
  private position: SessionPosition | null;
  private phase: GuidedPhase = 'waiting';
  private takeVelocity = 0;
  private cc64DuringTake = false;
  private pollTimer: ReturnType<typeof setInterval> | null = null;
  private noteOffSeen = false;
  /** a wrong-but-in-plan note awaiting its confirming repeat */
  private pendingSwitch: { note: number; at: number } | null = null;
  private pitchHistory: PitchObservation[] = [];
  private now: () => number;
  private pitchSettings: () => PitchCheckSettings;

  constructor(
    initial: SessionState,
    private input: GuidedInputPort,
    private persist: GuidedPersistPort,
    private ui: GuidedUiEvents = {},
    opts: GuidedOptions = {},
  ) {
    this.state = initial;
    this.position = nextPosition(initial);
    this.now = opts.now ?? (() => Date.now());
    this.pitchSettings = opts.pitchCheck ?? (() => DEFAULT_PITCH_CHECK);
  }

  getState(): SessionState {
    return this.state;
  }

  getPosition(): SessionPosition | null {
    return this.position;
  }

  getPhase(): GuidedPhase {
    return this.phase;
  }

  /** progress-map click-to-redo: retarget a specific cell/rr */
  setPosition(pos: SessionPosition): void {
    this.position = pos;
    this.pendingSwitch = null;
    this.ui.onPosition?.(pos);
  }

  /** The pending plan-note switch offer, if one is live (UI + tests). */
  getPendingSwitch(): number | null {
    return this.pendingSwitch?.note ?? null;
  }

  /** Confident pitch observations gathered so far this session. */
  getPitchHistory(): readonly PitchObservation[] {
    return this.pitchHistory;
  }

  /** First outstanding (layer, rr) at `note`, or null when it is complete. */
  private positionForNote(note: number): SessionPosition | null {
    if (!this.state.config.plan.notes.includes(note)) return null;
    for (const layerDef of this.state.config.plan.velocityMap) {
      const required = this.state.smartRrReduced[`${note}_${layerDef.layer}`]
        ? 1
        : this.state.config.plan.roundRobins;
      for (let rr = 1; rr <= required; rr++) {
        if (!this.state.takes[takeKey(note, layerDef.layer, rr)]) {
          return { note, layer: layerDef.layer, rr };
        }
      }
    }
    return null;
  }

  /**
   * MIDI note gate. A note that is not the note being sampled is never
   * recorded: the take would be filed under the WRONG file name and accepted
   * on velocity alone, and the error stays invisible until the instrument is
   * played back in JM Signature.
   */
  private handleWrongNote(played: number, expected: number): void {
    const now = this.now();
    const target = this.positionForNote(played);
    const inPlan = this.state.config.plan.notes.includes(played);
    const pending = this.pendingSwitch;

    if (
      target &&
      pending &&
      pending.note === played &&
      now - pending.at >= SWITCH_CONFIRM_MIN_MS &&
      now - pending.at <= SWITCH_CONFIRM_WINDOW_MS
    ) {
      // Confirmed: move the target, re-arm, record nothing.
      this.pendingSwitch = null;
      this.position = target;
      this.ui.onPosition?.(target);
      this.ui.onNoteSwitched?.(target);
      return;
    }

    this.pendingSwitch = target ? { note: played, at: now } : null;
    this.ui.onWrongNote?.({ played, expected, inPlan, switchOffered: target !== null });
  }

  private setPhase(p: GuidedPhase): void {
    this.phase = p;
    this.ui.onPhase?.(p);
  }

  /** MIDI note-on: auto-arm (§3.3 — no record button). */
  noteOn(note: number, velocity: number): void {
    if (this.phase !== 'waiting' || !this.position) return;
    if (note !== this.position.note) {
      this.handleWrongNote(note, this.position.note);
      return;
    }
    this.pendingSwitch = null;
    this.takeVelocity = velocity;
    this.cc64DuringTake = false;
    this.noteOffSeen = false;
    this.input.startRecording();
    this.setPhase('recording');
    this.startPolling();
  }

  noteOff(_note: number): void {
    this.noteOffSeen = true;
  }

  controlChange(cc: number, value: number): void {
    if (cc === 64 && value >= 64 && this.phase === 'recording') {
      this.cc64DuringTake = true;
      this.ui.onCc64Warning?.();
    }
  }

  dispose(): void {
    this.stopPolling();
  }

  private startPolling(): void {
    this.stopPolling();
    const soundType = this.state.resolvedSoundType ?? 'decaying';
    this.pollTimer = setInterval(() => this.pollCapture(soundType), 100);
  }

  private stopPolling(): void {
    if (this.pollTimer) clearInterval(this.pollTimer);
    this.pollTimer = null;
  }

  private pollCapture(soundType: SoundType): void {
    if (this.phase !== 'recording') return;
    const secs = this.input.recordedSeconds();
    if (soundType === 'sustaining') {
      // capture the full hold + tail, then finish once released and silent
      if (secs >= SUSTAIN_HOLD_S + 2 || (this.noteOffSeen && this.tailSilent())) {
        void this.finishTake();
      }
      return;
    }
    if (secs >= HARD_CAP_S || (secs > 0.6 && this.tailSilent())) {
      void this.finishTake();
    }
  }

  private tailSilent(): boolean {
    return this.input.peekTailPeak(SILENCE_HOLD_MS / 1000) < dbToLin(SILENCE_THRESHOLD_DB);
  }

  private async finishTake(): Promise<void> {
    this.stopPolling();
    this.setPhase('processing');
    const raw = this.input.stopRecording();
    const pos = this.position;
    if (!pos) {
      this.setPhase('waiting');
      return;
    }
    const soundType = this.state.resolvedSoundType ?? 'decaying';
    const processed = processCapture(raw, this.input.sampleRate, soundType);

    // expected peak range from other accepted takes of the same cell
    const siblingPeaks: number[] = [];
    for (let rr = 1; rr <= this.state.config.plan.roundRobins; rr++) {
      if (rr === pos.rr) continue;
      const t = this.state.takes[takeKey(pos.note, pos.layer, rr)];
      if (t) siblingPeaks.push(t.peakDb);
    }
    const verdict = judgeTake(this.takeVelocity, pos.layer, this.state.config.plan.velocityMap, {
      peakDb: processed.peakDb,
      expected: expectedPeakRange(siblingPeaks),
    });

    if (!verdict.accepted) {
      this.setPhase('verdict');
      this.ui.onVerdict?.(verdict, null);
      // instant re-arm (§3.3)
      this.setPhase('waiting');
      return;
    }

    const take: TakeRecord = {
      note: pos.note,
      layer: pos.layer,
      rr: pos.rr,
      file: fileNameFor(pos),
      peakDb: processed.peakDb,
      lengthMs: processed.lengthMs,
      velocity: this.takeVelocity,
      loop: processed.loop,
      acceptedAt: new Date().toISOString(),
    };
    const takeIndex = Object.keys(this.state.takes).length + 1;
    await this.persist.writeTake(take, processed.channels);
    this.state = acceptTake(this.state, take);
    this.persist.snapshot(this.state); // snapshot after EVERY accepted take (§3.3)
    this.ui.onState?.(this.state);
    this.setPhase('verdict');
    this.ui.onVerdict?.(verdict, take);
    if (this.cc64DuringTake) this.ui.onCc64Warning?.();

    // Layer 2 — audio pitch verification. Analysis only: it runs on a copy of
    // the processed take AFTER it is on disk and can never change what was
    // written. A warning, never a block (§3.5).
    this.verifyPitch(processed.channels, pos, takeIndex);

    // RR consistency (§3.3): once the cell is complete, check 3 dB spread.
    const peaks: number[] = [];
    for (let rr = 1; rr <= this.state.config.plan.roundRobins; rr++) {
      const t = this.state.takes[takeKey(pos.note, pos.layer, rr)];
      if (t) peaks.push(t.peakDb);
    }
    if (peaks.length >= this.state.config.plan.roundRobins) {
      const outliers = rrConsistencyOutliers(peaks);
      if (outliers.length > 0) {
        this.ui.onRrOutlier?.(pos.note, pos.layer, outliers[0] + 1);
      }
    }

    this.position = nextPosition(this.state);
    this.ui.onPosition?.(this.position);
    this.setPhase('waiting');
  }

  /**
   * Compare the captured audio to the note it was filed under. MIDI cannot see
   * a transposed instrument, a latched octave switch or the wrong patch — this
   * can, and it matters most on the FIRST take, before 359 more join it.
   */
  private verifyPitch(channels: Float32Array[], pos: SessionPosition, takeIndex: number): void {
    const settings = this.pitchSettings();
    if (!settings.enabled) return;
    let verdict: PitchVerdict;
    try {
      verdict = verifyTakePitch(channels, this.input.sampleRate, pos.note, {
        toleranceCents: settings.toleranceCents,
      });
    } catch {
      return; // analysis must never take the capture path down with it
    }
    if (verdict.status !== 'unknown') {
      this.pitchHistory.push({
        note: pos.note,
        semitones: verdict.semitones ?? 0,
        cents: verdict.cents ?? 0,
        confidence: verdict.confidence,
      });
    }
    const trend = pitchTrend(this.pitchHistory);
    if (verdict.status === 'off' || trend.message !== null) {
      this.ui.onPitchReport?.({
        position: pos,
        verdict,
        trend,
        firstOfSession: takeIndex === 1,
        takeIndex,
      });
    }
  }
}
