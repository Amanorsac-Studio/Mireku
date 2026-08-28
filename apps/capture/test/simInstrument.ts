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
    };
    this.sampleRate = this.opts.sampleRate;
  }

  private random(): number {
    // deterministic LCG
    this.rng = (this.rng * 1103515245 + 12345) & 0x7fffffff;
    return this.rng / 0x7fffffff;
  }

  noteOn(note: number, velocity: number): void {
    this.events.push({ tSec: this.tSec, type: 'on', note, velocity });
  }

  noteOff(note: number): void {
    this.events.push({ tSec: this.tSec, type: 'off', note, velocity: 0 });
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
      const sounding = ev.note + this.opts.transposeSemitones;
      const freq = 440 * Math.pow(2, (sounding - 69) / 12) * detune;
      const amp = (ev.velocity / 127) * 0.8 * gainJit;
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
