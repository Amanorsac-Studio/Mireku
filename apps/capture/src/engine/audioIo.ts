/**
 * Renderer audio I/O: device enumeration, input capture via the jm-capture
 * AudioWorklet, live peak metering, and the Web Audio CapturePort used by
 * Auto Mode. Recording stays Float32 in the renderer; finished takes are
 * shipped to the main process for 24-bit WAV writing (see DECISIONS-capture.md).
 */

import type { CapturePort } from './autoMode';
import { registerJmWorklets } from './fx/worklets';

export interface AudioDeviceInfo {
  id: string;
  label: string;
}

export class AudioIo {
  ctx: AudioContext | null = null;
  private stream: MediaStream | null = null;
  private sourceNode: MediaStreamAudioSourceNode | null = null;
  private captureNode: AudioWorkletNode | null = null;
  private analyser: AnalyserNode | null = null;
  /** blocks accumulated while recording: [block][channel] */
  private blocks: Float32Array[][] = [];
  private recording = false;
  monitorTap: GainNode | null = null;

  async listInputs(): Promise<AudioDeviceInfo[]> {
    // getUserMedia once so labels populate
    try {
      const s = await navigator.mediaDevices.getUserMedia({ audio: true });
      s.getTracks().forEach((t) => t.stop());
    } catch {
      /* permission denied — labels may be empty */
    }
    const devices = await navigator.mediaDevices.enumerateDevices();
    return devices
      .filter((d) => d.kind === 'audioinput')
      .map((d) => ({ id: d.deviceId, label: d.label || `Input ${d.deviceId.slice(0, 6)}` }));
  }

  async openInput(deviceId: string | null): Promise<void> {
    this.closeInput();
    this.ctx ??= new AudioContext({ sampleRate: 48000 });
    await registerJmWorklets(this.ctx);
    this.stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        deviceId: deviceId ? { exact: deviceId } : undefined,
        echoCancellation: false,
        noiseSuppression: false,
        autoGainControl: false,
        channelCount: 2,
      },
    });
    this.sourceNode = this.ctx.createMediaStreamSource(this.stream);
    this.captureNode = new AudioWorkletNode(this.ctx, 'jm-capture', {
      numberOfInputs: 1,
      numberOfOutputs: 1,
      channelCount: 2,
    });
    this.captureNode.port.onmessage = (e) => {
      if (e.data?.type === 'block' && this.recording) {
        this.blocks.push(e.data.channels as Float32Array[]);
      }
    };
    this.analyser = this.ctx.createAnalyser();
    this.analyser.fftSize = 2048;
    this.sourceNode.connect(this.captureNode);
    this.sourceNode.connect(this.analyser);
    // keep worklet pulled without audible monitoring
    const sink = this.ctx.createGain();
    sink.gain.value = 0;
    this.captureNode.connect(sink).connect(this.ctx.destination);
  }

  closeInput(): void {
    this.stream?.getTracks().forEach((t) => t.stop());
    this.stream = null;
    this.sourceNode?.disconnect();
    this.sourceNode = null;
    this.captureNode?.disconnect();
    this.captureNode = null;
  }

  get sampleRate(): number {
    return this.ctx?.sampleRate ?? 48000;
  }

  /** Live input peak 0..1 for the guided meter needle. */
  livePeak(): number {
    if (!this.analyser) return 0;
    const buf = new Float32Array(this.analyser.fftSize);
    this.analyser.getFloatTimeDomainData(buf);
    let peak = 0;
    for (let i = 0; i < buf.length; i++) {
      const a = Math.abs(buf[i]);
      if (a > peak) peak = a;
    }
    return peak;
  }

  startRecording(): void {
    this.blocks = [];
    this.recording = true;
    this.captureNode?.port.postMessage('start');
  }

  stopRecording(): Float32Array[] {
    this.recording = false;
    this.captureNode?.port.postMessage('stop');
    return this.collect();
  }

  isRecording(): boolean {
    return this.recording;
  }

  /** current recorded duration in seconds */
  recordedSeconds(): number {
    let n = 0;
    for (const b of this.blocks) n += b[0]?.length ?? 0;
    return n / this.sampleRate;
  }

  /** peek at the recorded audio without stopping (for live silence tracking) */
  peekTailPeak(seconds: number): number {
    const need = Math.round(seconds * this.sampleRate);
    let have = 0;
    let peak = 0;
    for (let i = this.blocks.length - 1; i >= 0 && have < need; i--) {
      for (const ch of this.blocks[i]) {
        for (let j = 0; j < ch.length; j++) {
          const a = Math.abs(ch[j]);
          if (a > peak) peak = a;
        }
      }
      have += this.blocks[i][0]?.length ?? 0;
    }
    return peak;
  }

  private collect(): Float32Array[] {
    if (this.blocks.length === 0) return [new Float32Array(0), new Float32Array(0)];
    const numCh = this.blocks[0].length;
    let total = 0;
    for (const b of this.blocks) total += b[0].length;
    const out = Array.from({ length: numCh }, () => new Float32Array(total));
    let pos = 0;
    for (const b of this.blocks) {
      for (let c = 0; c < numCh; c++) out[Math.min(c, numCh - 1)].set(b[c] ?? b[0], pos);
      pos += b[0].length;
    }
    this.blocks = [];
    return out;
  }

  /** Play a 1 kHz test tone burst (loopback helper verification, §3.4). */
  playTestTone(durationS = 1, freq = 1000): void {
    if (!this.ctx) this.ctx = new AudioContext({ sampleRate: 48000 });
    const osc = this.ctx.createOscillator();
    osc.frequency.value = freq;
    const g = this.ctx.createGain();
    g.gain.value = 0.5;
    osc.connect(g).connect(this.ctx.destination);
    const t = this.ctx.currentTime;
    osc.start(t);
    g.gain.setValueAtTime(0.5, t + durationS - 0.02);
    g.gain.linearRampToValueAtTime(0, t + durationS);
    osc.stop(t + durationS);
  }
}

export const audioIo = new AudioIo();

/** Real-time CapturePort adapter for Auto Mode. */
export class WebAudioCapturePort implements CapturePort {
  constructor(private io: AudioIo) {}

  get sampleRate(): number {
    return this.io.sampleRate;
  }

  record(seconds: number): Promise<Float32Array[]> {
    this.io.startRecording();
    return new Promise((resolve) => {
      setTimeout(() => resolve(this.io.stopRecording()), seconds * 1000);
    });
  }
}

/** Real-time clock for Auto Mode. */
export const realClock = {
  wait: (ms: number) => new Promise<void>((r) => setTimeout(r, ms)),
};
