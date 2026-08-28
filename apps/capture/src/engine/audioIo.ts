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

/**
 * Why a scan failed, so the UI can say something useful instead of showing an
 * empty dropdown: `denied` = the OS or the shell refused the microphone,
 * `unsupported` = no `navigator.mediaDevices` at all (non-secure context).
 */
export type AudioPermission = 'granted' | 'denied' | 'unsupported';

export interface AudioInputScan {
  devices: AudioDeviceInfo[];
  permission: AudioPermission;
}

/** Selected capture channels within the device: [L, R] or [mono]. */
export type InputChannels = readonly [number] | readonly [number, number];

const DEFAULT_STEREO: InputChannels = [0, 1];

function sameChannels(a: InputChannels | null, b: InputChannels | null): boolean {
  if (!a || !b) return a === b;
  return a.length === b.length && a.every((c, i) => c === b[i]);
}

export class AudioIo {
  ctx: AudioContext | null = null;
  private stream: MediaStream | null = null;
  private sourceNode: MediaStreamAudioSourceNode | null = null;
  private captureNode: AudioWorkletNode | null = null;
  private analyser: AnalyserNode | null = null;
  /** channel splitter/merger inserted only for non-default channel picks */
  private routing: AudioNode[] = [];
  /** what the currently open input actually is (so redundant reopens are skipped) */
  openDeviceId: string | null = null;
  openChannels: InputChannels | null = null;
  /** blocks accumulated while recording: [block][channel] */
  private blocks: Float32Array[][] = [];
  private recording = false;
  monitorTap: GainNode | null = null;

  /**
   * Enumerate audio inputs, reporting *why* the list is empty when it is.
   * A `getUserMedia` round trip runs first because Chromium withholds device
   * labels until the page has held a microphone stream at least once.
   */
  async scanInputs(): Promise<AudioInputScan> {
    if (typeof navigator === 'undefined' || !navigator.mediaDevices) {
      return { devices: [], permission: 'unsupported' };
    }
    let permission: AudioPermission = 'granted';
    let raw = (await navigator.mediaDevices.enumerateDevices()).filter((d) => d.kind === 'audioinput');

    // Only pay for a getUserMedia round trip when the labels are still
    // withheld. Opening a stream can itself make Windows emit `devicechange`,
    // so an unconditional call here would make a live picker re-scan forever.
    if (raw.length === 0 || raw.every((d) => !d.label)) {
      try {
        const s = await navigator.mediaDevices.getUserMedia({ audio: true });
        s.getTracks().forEach((t) => t.stop());
      } catch {
        permission = 'denied';
      }
      raw = (await navigator.mediaDevices.enumerateDevices()).filter((d) => d.kind === 'audioinput');
    }

    const devices = raw.map((d) => ({
      id: d.deviceId,
      label: d.label || `Input ${d.deviceId.slice(0, 6)}`,
    }));
    return { devices, permission };
  }

  async listInputs(): Promise<AudioDeviceInfo[]> {
    return (await this.scanInputs()).devices;
  }

  /**
   * Subscribe to OS device hot-plug. Returns an unsubscribe function, so a
   * picker can keep its list live without ever leaking a listener.
   */
  onDeviceChange(cb: () => void): () => void {
    if (typeof navigator === 'undefined' || !navigator.mediaDevices) return () => {};
    navigator.mediaDevices.addEventListener('devicechange', cb);
    return () => navigator.mediaDevices.removeEventListener('devicechange', cb);
  }

  /**
   * How many capture channels a device actually offers, so the channel-pair
   * picker only offers pairs that exist.
   *
   * Note for Windows: Chromium's shared-mode WASAPI capture caps every device
   * at 2 channels (`capabilities.channelCount.max === 2`), even for a 4-in
   * interface. Multi-channel interfaces expose their higher inputs as separate
   * *devices* ("IN 3-4", "IN 3", "IN 4"), which the device picker lists — so
   * that, not this pair list, is how the user reaches channels 3+.
   */
  async probeChannelCount(deviceId: string | null): Promise<number> {
    if (typeof navigator === 'undefined' || !navigator.mediaDevices) return 2;
    try {
      const s = await navigator.mediaDevices.getUserMedia({
        audio: {
          deviceId: deviceId ? { exact: deviceId } : undefined,
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false,
          channelCount: { ideal: 32 },
        },
      });
      const track = s.getAudioTracks()[0];
      const capped = track?.getCapabilities?.().channelCount?.max;
      const n = capped ?? track?.getSettings().channelCount ?? 2;
      s.getTracks().forEach((t) => t.stop());
      return Math.max(1, n);
    } catch {
      return 2;
    }
  }

  get inputOpen(): boolean {
    return this.stream !== null;
  }

  /**
   * Open a capture input. `channels` picks which channels of the device feed
   * the recorder — the default [0, 1] keeps the graph byte-for-byte what it
   * has always been (source → worklet), and the splitter/merger is inserted
   * only when a different pair is asked for.
   */
  async openInput(deviceId: string | null, channels: InputChannels = DEFAULT_STEREO): Promise<void> {
    this.closeInput();
    this.ctx ??= new AudioContext({ sampleRate: 48000 });
    await registerJmWorklets(this.ctx);
    const highest = Math.max(...channels);
    this.stream = await navigator.mediaDevices.getUserMedia({
      audio: {
        deviceId: deviceId ? { exact: deviceId } : undefined,
        echoCancellation: false,
        noiseSuppression: false,
        autoGainControl: false,
        channelCount: Math.max(2, highest + 1),
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

    let head: AudioNode = this.sourceNode;
    if (!sameChannels(channels, DEFAULT_STEREO)) {
      const splitter = this.ctx.createChannelSplitter(Math.max(2, highest + 1));
      const merger = this.ctx.createChannelMerger(channels.length);
      this.sourceNode.connect(splitter);
      channels.forEach((src, dest) => splitter.connect(merger, src, dest));
      this.routing = [splitter, merger];
      head = merger;
    }
    head.connect(this.captureNode);
    head.connect(this.analyser);

    this.openDeviceId = deviceId;
    this.openChannels = channels;
    // keep worklet pulled without audible monitoring
    const sink = this.ctx.createGain();
    sink.gain.value = 0;
    this.captureNode.connect(sink).connect(this.ctx.destination);
  }

  /** True when the open input already matches this selection. */
  matchesInput(deviceId: string | null, channels: InputChannels): boolean {
    return this.inputOpen && this.openDeviceId === deviceId && sameChannels(this.openChannels, channels);
  }

  closeInput(): void {
    this.stream?.getTracks().forEach((t) => t.stop());
    this.stream = null;
    this.sourceNode?.disconnect();
    this.sourceNode = null;
    this.captureNode?.disconnect();
    this.captureNode = null;
    this.analyser?.disconnect();
    this.analyser = null;
    this.routing.forEach((n) => n.disconnect());
    this.routing = [];
    this.openDeviceId = null;
    this.openChannels = null;
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
