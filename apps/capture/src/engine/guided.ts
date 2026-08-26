/**
 * Guided Mode controller (§3.3) — the auto-arm / capture / verdict state
 * machine. Talks to the audio input and persistence through ports so the
 * logic itself stays headless-testable.
 */

import type { SoundType } from '@jm/jmi-format/web';
import { dbToLin } from './dsp/level';
import { SILENCE_HOLD_MS, SILENCE_THRESHOLD_DB, HARD_CAP_S } from './dsp/silence';
import { processCapture, SUSTAIN_HOLD_S } from './processing';
import type { SessionPosition, SessionState, TakeRecord } from './session';
import { acceptTake, fileNameFor, nextPosition, takeKey } from './session';
import { expectedPeakRange, judgeTake, rrConsistencyOutliers, type TakeVerdict } from './velocity';

export type GuidedPhase = 'waiting' | 'recording' | 'processing' | 'verdict';

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

  constructor(
    initial: SessionState,
    private input: GuidedInputPort,
    private persist: GuidedPersistPort,
    private ui: GuidedUiEvents = {},
  ) {
    this.state = initial;
    this.position = nextPosition(initial);
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
    this.ui.onPosition?.(pos);
  }

  private setPhase(p: GuidedPhase): void {
    this.phase = p;
    this.ui.onPhase?.(p);
  }

  /** MIDI note-on: auto-arm (§3.3 — no record button). */
  noteOn(_note: number, velocity: number): void {
    if (this.phase !== 'waiting' || !this.position) return;
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
    await this.persist.writeTake(take, processed.channels);
    this.state = acceptTake(this.state, take);
    this.persist.snapshot(this.state); // snapshot after EVERY accepted take (§3.3)
    this.ui.onState?.(this.state);
    this.setPhase('verdict');
    this.ui.onVerdict?.(verdict, take);
    if (this.cc64DuringTake) this.ui.onCc64Warning?.();

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
}
