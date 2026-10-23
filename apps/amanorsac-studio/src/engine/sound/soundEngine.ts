/**
 * Sound engine — plays a layered Sound in Web Audio: one sampler + FX rack per
 * part, each with its own key/velocity window, octave shift, level and pan,
 * summed into the app's output bus. Samples are shared across parts that use
 * the same bundle.
 */

import type { Instrument } from '@jm/jmi-format/web';
import { audioIo } from '../audioIo';
import { FxRack } from '../fx/rack';
import { VoiceManager, ZoneSelector } from '../sampler/samplerCore';
import { audioBufferFrom, bakeLoopCrossfade, WebAudioVoiceSink, type LoadedSample } from '../sampler/samplerWebAudio';
import { dbToGain, type Part } from './soundModel';

export interface PartRuntime {
  part: Part;
  sink: WebAudioVoiceSink;
  voices: VoiceManager;
  rack: FxRack;
  gain: GainNode;
  panner: StereoPannerNode;
  ready: boolean;
}

type ReadSample = (dir: string, file: string) => Promise<{ sampleRate: number; channels: ArrayBuffer[] } | null>;

export class SoundEngine {
  private ctx: AudioContext | null = null;
  private master: GainNode | null = null;
  private runtimes = new Map<string, PartRuntime>();
  private cache = new Map<string, LoadedSample>();
  private held = new Map<number, number[]>(); // incoming note → per-part transposed notes
  tempo = 120;

  constructor(private readSample: ReadSample) {}

  async ensure(ctx: AudioContext): Promise<void> {
    if (this.ctx) return;
    this.ctx = ctx;
    this.master = ctx.createGain();
    this.master.connect(audioIo.output);
  }

  get parts(): PartRuntime[] {
    return [...this.runtimes.values()];
  }
  runtime(id: string): PartRuntime | undefined {
    return this.runtimes.get(id);
  }

  async addPart(part: Part, instrument: Instrument): Promise<PartRuntime> {
    if (!this.ctx || !this.master) throw new Error('SoundEngine not initialised');
    this.removePart(part.id);
    const ctx = this.ctx;
    const sink = new WebAudioVoiceSink(ctx);
    const voices = new VoiceManager(new ZoneSelector(instrument), sink);
    sink.manager = voices;
    const rack = new FxRack(ctx);
    rack.setTempo(this.tempo);
    const gain = ctx.createGain();
    const panner = ctx.createStereoPanner();
    sink.output.connect(rack.input);
    rack.output.connect(gain).connect(panner).connect(this.master);
    const rt: PartRuntime = { part, sink, voices, rack, gain, panner, ready: false };
    this.runtimes.set(part.id, rt);
    this.applyPart(rt, part);
    rack.load(part.fxRack);
    for (const zone of [...instrument.zones, ...(instrument.releaseZones ?? [])]) {
      for (const rr of zone.roundRobins) {
        const key = `${part.bundleDir}|${rr.file}`;
        let sample = this.cache.get(key);
        if (!sample) {
          const d = await this.readSample(part.bundleDir, rr.file);
          if (!d) continue;
          const buffer = audioBufferFrom(ctx, d.channels.map((b) => new Float32Array(b)), d.sampleRate);
          if (rr.loop) bakeLoopCrossfade(buffer, rr.loop);
          sample = { buffer, loop: rr.loop };
          this.cache.set(key, sample);
        }
        sink.setSample(rr.file, sample);
      }
    }
    rt.ready = true;
    return rt;
  }

  /** Push the part's mix / window settings into its runtime (cheap, call on every edit). */
  updatePart(part: Part): void {
    const rt = this.runtimes.get(part.id);
    if (!rt) return;
    rt.part = part;
    this.applyPart(rt, part);
  }

  private applyPart(rt: PartRuntime, part: Part): void {
    const t = this.ctx?.currentTime ?? 0;
    rt.gain.gain.setTargetAtTime(part.enabled ? dbToGain(part.volumeDb) : 0, t, 0.015);
    rt.panner.pan.setTargetAtTime(Math.max(-1, Math.min(1, part.pan)), t, 0.015);
    rt.sink.releaseMs = part.ampEnvelope.releaseMs;
  }

  removePart(id: string): void {
    const rt = this.runtimes.get(id);
    if (!rt) return;
    rt.voices.allNotesOff();
    rt.sink.clear();
    rt.rack.dispose?.();
    try {
      rt.panner.disconnect();
    } catch {
      /* already gone */
    }
    this.runtimes.delete(id);
  }

  clear(): void {
    for (const id of [...this.runtimes.keys()]) this.removePart(id);
    this.held.clear();
  }

  setTempo(bpm: number): void {
    this.tempo = bpm;
    for (const rt of this.runtimes.values()) rt.rack.setTempo(bpm);
  }

  /** Serialise every part's live rack back into its Part (call before saving). */
  captureRacks(parts: Part[]): Part[] {
    return parts.map((p) => {
      const rt = this.runtimes.get(p.id);
      return rt ? { ...p, fxRack: rt.rack.serialize() } : p;
    });
  }

  noteOn(note: number, velocity: number): void {
    const sent: number[] = [];
    for (const rt of this.runtimes.values()) {
      const p = rt.part;
      if (!rt.ready || !p.enabled) { sent.push(-1); continue; }
      if (note < p.keyLo || note > p.keyHi || velocity < p.velLo || velocity > p.velHi) { sent.push(-1); continue; }
      const n = Math.max(0, Math.min(127, note + 12 * p.octaveShift));
      rt.voices.noteOn(n, velocity);
      sent.push(n);
    }
    this.held.set(note, sent);
  }

  noteOff(note: number): void {
    const sent = this.held.get(note);
    this.held.delete(note);
    let i = 0;
    for (const rt of this.runtimes.values()) {
      const n = sent?.[i++] ?? -1;
      if (n >= 0) rt.voices.noteOff(n);
    }
  }

  controlChange(cc: number, value: number): void {
    for (const rt of this.runtimes.values()) rt.voices.controlChange(cc, value);
  }

  allNotesOff(): void {
    for (const rt of this.runtimes.values()) rt.voices.allNotesOff();
    this.held.clear();
  }
}
