/**
 * Web Audio VoiceSink — renders ZonePicks with AudioBufferSourceNodes.
 * Loop playback uses the stored loop points; the equal-power crossfade is
 * pre-rendered into the AudioBuffer's loop region on load so native looping
 * is seam-free. Amp envelope release on voice release.
 */

import type { Instrument, LoopRegion } from '@jm/jmi-format/web';
import type { VoiceManager, VoiceSink, ZonePick } from './samplerCore';

export interface LoadedSample {
  buffer: AudioBuffer;
  loop: LoopRegion | null;
}

export class WebAudioVoiceSink implements VoiceSink {
  private nextId = 1;
  private nodes = new Map<number, { src: AudioBufferSourceNode; gain: GainNode }>();
  private samples = new Map<string, LoadedSample>();
  manager: VoiceManager | null = null;
  releaseMs = 250;

  constructor(
    private ctx: BaseAudioContext,
    public readonly output: GainNode = ctx.createGain(),
  ) {}

  /** Register a decoded sample under its bundle-relative file path. */
  setSample(file: string, sample: LoadedSample): void {
    this.samples.set(file, sample);
  }

  hasSample(file: string): boolean {
    return this.samples.has(file);
  }

  clear(): void {
    for (const [id] of this.nodes) this.release(id);
    this.samples.clear();
  }

  start(pick: ZonePick, _note: number, _velocity: number): number {
    const sample = this.samples.get(pick.rr.file);
    const id = this.nextId++;
    if (!sample) return id;
    const src = this.ctx.createBufferSource();
    src.buffer = sample.buffer;
    src.playbackRate.value = pick.playbackRate;
    if (sample.loop) {
      src.loop = true;
      src.loopStart = sample.loop.startSample / sample.buffer.sampleRate;
      src.loopEnd = sample.loop.endSample / sample.buffer.sampleRate;
    }
    const gain = this.ctx.createGain();
    gain.gain.value = 1; // levels live in the samples themselves — never normalize
    src.connect(gain).connect(this.output);
    src.onended = () => {
      this.nodes.delete(id);
      this.manager?.voiceEnded(id);
      try {
        gain.disconnect();
      } catch {
        /* already disconnected */
      }
    };
    src.start();
    this.nodes.set(id, { src, gain });
    return id;
  }

  release(voiceId: number): void {
    const v = this.nodes.get(voiceId);
    if (!v) return;
    const t = this.ctx.currentTime;
    const relS = this.releaseMs / 1000;
    v.gain.gain.setValueAtTime(v.gain.gain.value, t);
    v.gain.gain.linearRampToValueAtTime(0, t + relS);
    v.src.stop(t + relS + 0.01);
    this.nodes.delete(voiceId);
  }
}

/**
 * Pre-render the equal-power loop crossfade into an AudioBuffer so that
 * native AudioBufferSourceNode looping is click-free: the tail of the loop
 * region is blended with the audio just before loopStart.
 */
export function bakeLoopCrossfade(buffer: AudioBuffer, loop: LoopRegion): void {
  const { startSample, endSample, crossfadeSamples } = loop;
  if (crossfadeSamples <= 0) return;
  for (let c = 0; c < buffer.numberOfChannels; c++) {
    const data = buffer.getChannelData(c);
    for (let i = 0; i < crossfadeSamples; i++) {
      const t = (i + 1) / crossfadeSamples;
      const gIn = Math.sin((t * Math.PI) / 2);
      const gOut = Math.cos((t * Math.PI) / 2);
      const tailIdx = endSample - crossfadeSamples + i;
      const preIdx = startSample - crossfadeSamples + i;
      if (tailIdx < 0 || tailIdx >= data.length) continue;
      const pre = preIdx >= 0 ? data[preIdx] : 0;
      data[tailIdx] = data[tailIdx] * gOut + pre * gIn;
    }
  }
}

/** Build an AudioBuffer from Float32 channel data. */
export function audioBufferFrom(
  ctx: BaseAudioContext,
  channels: Float32Array[],
  sampleRate: number,
): AudioBuffer {
  const buf = ctx.createBuffer(channels.length, channels[0].length, sampleRate);
  channels.forEach((ch, i) => buf.copyToChannel(ch as Float32Array<ArrayBuffer>, i));
  return buf;
}

/** Load every sample of an instrument into a sink from raw Float32 data. */
export function loadInstrumentSamples(
  ctx: BaseAudioContext,
  sink: WebAudioVoiceSink,
  instrument: Instrument,
  fetchSample: (file: string) => { channels: Float32Array[]; sampleRate: number } | null,
): void {
  for (const zone of instrument.zones) {
    for (const rr of zone.roundRobins) {
      if (sink.hasSample(rr.file)) continue;
      const data = fetchSample(rr.file);
      if (!data) continue;
      const buffer = audioBufferFrom(ctx, data.channels, data.sampleRate);
      if (rr.loop) bakeLoopCrossfade(buffer, rr.loop);
      sink.setSample(rr.file, { buffer, loop: rr.loop });
    }
  }
}
