/**
 * Simulated instrument for headless Auto Mode tests (§7): a virtual-time
 * MIDI-driven tone source implementing the engine's MidiOut / CapturePort /
 * ClockPort ports. No Electron, no audio hardware.
 */

import type { CapturePort, ClockPort, MidiOut } from '../src/engine/autoMode';

interface NoteEvent {
  tSec: number;
  type: 'on' | 'off';
  note: number;
  velocity: number;
  /** transpose in force when this note was struck (patch-dependent) */
  transpose: number;
  /** level scale in force when this note was struck */
  level: number;
  /** the source was still loading a patch — it makes no sound at all */
  muted: boolean;
}

/** What a program number sounds like, for patch-queue tests. */
export interface SimPatch {
  transposeSemitones?: number;
  /** multiplies the rendered amplitude (0 = silent source) */
  levelScale?: number;
}

/** A raw MIDI message the instrument received, with the virtual time it arrived. */
export interface SimMessage {
  tSec: number;
  bytes: number[];
}

export interface SimOptions {
  sampleRate?: number;
  /** 'decaying' tone dies with tau; 'sustaining' holds until note-off */
  mode?: 'decaying' | 'sustaining';
  decayTau?: number;
  /** per-note-on random detune/level jitter (breaks the smart-RR null test) */
  jitter?: boolean;
  channels?: number;
  latencySec?: number;
  /** the rig is transposed: every returned note sounds this many semitones off */
  transposeSemitones?: number;
  /** global amplitude scale — 0 models a source that returns nothing at all */
  levelScale?: number;
  /**
   * Per-program patch table. A Program Change selects one of these, exactly as
   * a plugin host would; unlisted programs fall back to the global options.
   */
  patches?: Record<number, SimPatch>;
  /**
   * How long the source takes to LOAD a patch after a program change. Notes
   * struck inside that window make no sound — which is precisely the failure
   * the queue's settle delay exists to prevent, so tests can prove it.
   */
  patchLoadSec?: number;
  /** a stuck note: the source ignores note-off and never decays */
  ignoreNoteOff?: boolean;
}

export class SimulatedInstrument implements MidiOut, CapturePort, ClockPort {
  readonly sampleRate: number;
  private tSec = 0;
  private events: NoteEvent[] = [];
  private pending: { start: number; seconds: number; resolve: (v: Float32Array[]) => void }[] = [];
  private opts: Required<SimOptions>;
  private rng = 12345;

  constructor(opts: SimOptions = {}) {
    this.opts = {
      sampleRate: opts.sampleRate ?? 8000,
      mode: opts.mode ?? 'decaying',
      decayTau: opts.decayTau ?? 0.35,
      jitter: opts.jitter ?? false,
      channels: opts.channels ?? 2,
      latencySec: opts.latencySec ?? 0,
      transposeSemitones: opts.transposeSemitones ?? 0,
      levelScale: opts.levelScale ?? 1,
      patches: opts.patches ?? {},
      patchLoadSec: opts.patchLoadSec ?? 0,
      ignoreNoteOff: opts.ignoreNoteOff ?? false,
    };
    this.sampleRate = this.opts.sampleRate;
  }

  /* ---- raw MIDI: Bank Select + Program Change (patch queue) ---- */

  /** Every raw message received, in order, with the virtual time it arrived. */
  readonly messages: SimMessage[] = [];
  /** Program numbers selected, in order. */
  readonly programs: number[] = [];
  private bankMsb: number | null = null;
  private bankLsb: number | null = null;
  /** bank in force when each program change landed */
  readonly banks: { msb: number | null; lsb: number | null }[] = [];
  private patch: SimPatch | null = null;
  private patchReadyAt = 0;

  send(bytes: number[]): void {
    this.messages.push({ tSec: this.tSec, bytes: [...bytes] });
    const status = bytes[0] & 0xf0;
    if (status === 0xb0 && bytes[1] === 0) this.bankMsb = bytes[2];
    else if (status === 0xb0 && bytes[1] === 32) this.bankLsb = bytes[2];
    else if (status === 0xc0) {
      this.programs.push(bytes[1]);
      this.banks.push({ msb: this.bankMsb, lsb: this.bankLsb });
      this.patch = this.opts.patches[bytes[1]] ?? null;
      // the source goes quiet while it loads the patch
      this.patchReadyAt = this.tSec + this.opts.patchLoadSec;
    }
  }

  /** Virtual time of the first note-on after the given time, or null. */
  firstNoteOnAfter(tSec: number): number | null {
    const ev = this.events.find((e) => e.type === 'on' && e.tSec >= tSec);
    return ev ? ev.tSec : null;
  }

  private random(): number {
    // deterministic LCG
    this.rng = (this.rng * 1103515245 + 12345) & 0x7fffffff;
    return this.rng / 0x7fffffff;
  }

  noteOn(note: number, velocity: number): void {
    // The patch in force is resolved at strike time, so a note played during a
    // patch load is silent exactly the way the real source would be.
    this.events.push({
      tSec: this.tSec,
      type: 'on',
      note,
      velocity,
      transpose: this.patch?.transposeSemitones ?? this.opts.transposeSemitones,
      level: (this.patch?.levelScale ?? 1) * this.opts.levelScale,
      muted: this.tSec < this.patchReadyAt,
    });
  }

  noteOff(note: number): void {
    if (this.opts.ignoreNoteOff) return;
    this.events.push({ tSec: this.tSec, type: 'off', note, velocity: 0, transpose: 0, level: 0, muted: false });
  }

  record(seconds: number): Promise<Float32Array[]> {
    const start = this.tSec;
    return new Promise((resolve) => {
      this.pending.push({ start, seconds, resolve });
      // If the engine awaits this recording without advancing the clock far
      // enough, jump virtual time forward on the next macrotask.
      setTimeout(() => this.flush(start + seconds), 0);
    });
  }

  async wait(ms: number): Promise<void> {
    this.tSec += ms / 1000;
    this.flush(this.tSec);
    await Promise.resolve();
  }

  private flush(uptoSec: number): void {
    if (uptoSec > this.tSec) this.tSec = uptoSec;
    const ready = this.pending.filter((p) => p.start + p.seconds <= this.tSec);
    this.pending = this.pending.filter((p) => p.start + p.seconds > this.tSec);
    for (const p of ready) p.resolve(this.render(p.start, p.seconds));
  }

  private render(startSec: number, seconds: number): Float32Array[] {
    const sr = this.sampleRate;
    const n = Math.round(seconds * sr);
    const chans = Array.from({ length: this.opts.channels }, () => new Float32Array(n));
    const lat = this.opts.latencySec;

    // pair note-ons with their offs
    for (let ei = 0; ei < this.events.length; ei++) {
      const ev = this.events[ei];
      if (ev.type !== 'on') continue;
      let offSec = Infinity;
      for (let j = ei + 1; j < this.events.length; j++) {
        if (this.events[j].type === 'off' && this.events[j].note === ev.note) {
          offSec = this.events[j].tSec;
          break;
        }
      }
      const detune = this.opts.jitter ? 1 + (this.random() - 0.5) * 0.006 : 1;
      const gainJit = this.opts.jitter ? 1 + (this.random() - 0.5) * 0.12 : 1;
      if (ev.muted || ev.level === 0) continue;
      const sounding = ev.note + ev.transpose;
      const freq = 440 * Math.pow(2, (sounding - 69) / 12) * detune;
      const amp = (ev.velocity / 127) * 0.8 * gainJit * ev.level;
      const noteStart = ev.tSec + lat;

      for (let i = 0; i < n; i++) {
        const t = startSec + i / sr;
        if (t < noteStart) continue;
        const dt = t - noteStart;
        let env: number;
        if (this.opts.mode === 'decaying') {
          env = Math.exp(-dt / this.opts.decayTau);
        } else {
          // sustaining: full level while held, fast release after note-off
          env = t <= offSec + lat ? 1 : Math.exp(-(t - (offSec + lat)) / 0.15);
        }
        if (env < 1e-6) continue;
        const s = amp * env * Math.sin(2 * Math.PI * freq * dt);
        for (let c = 0; c < chans.length; c++) chans[c][i] += s;
      }
    }
    return chans;
  }
}
