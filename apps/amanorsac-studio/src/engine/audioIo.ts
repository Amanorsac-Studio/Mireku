/**
 * Renderer audio I/O: device enumeration, input capture via the as-capture
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

/** Same shape for playback devices — the panel treats both lists identically. */
export type AudioOutputScan = AudioInputScan;

/**
 * How the audible bus actually reaches the chosen playback device.
 * `context` = `AudioContext.setSinkId()` (Chromium 110+, what Electron 33 has);
 * `element` = the portable fallback, `MediaStreamAudioDestinationNode` piped
 * into an `HTMLAudioElement` whose `setSinkId()` does the routing;
 * `default`  = no explicit device chosen, straight out of `ctx.destination`.
 */
export type SinkRouting = 'context' | 'element' | 'default';

/** Selected capture channels within the device: [L, R] or [mono]. */
export type InputChannels = readonly [number] | readonly [number, number];

const DEFAULT_STEREO: InputChannels = [0, 1];

/** Monitor gain changes are ramped, never stepped — a step is a click. */
const MONITOR_RAMP_S = 0.02;

/** Test-tone envelope: short fades so the ping can never click or thump. */
const TONE_FADE_S = 0.015;

/**
 * Device names that mean "this is a loop back into the machine, not a speaker".
 * Used only to *warn*; nothing is ever silently disabled on the strength of a
 * string match.
 */
const LOOPBACK_HINTS = [
  'cable',
  'vb-audio',
  'voicemeeter',
  'blackhole',
  'loopback',
  'soundflower',
  'virtual',
  'stereo mix',
  'what u hear',
  'wave out mix',
];

/** Which hint words a device label contains (lower-cased, de-duplicated). */
function hintsIn(label: string): string[] {
  const l = label.toLowerCase();
  return LOOPBACK_HINTS.filter((h) => l.includes(h));
}

/**
 * Feedback risk for a monitoring path, as a pure function so it can be reasoned
 * about (and tested) without an AudioContext.
 *
 * - `loop`    — input and output name the *same* virtual routing device, so
 *               monitoring would feed the capture straight back into itself.
 * - `virtual` — the input is a loopback/virtual cable: whatever is monitored
 *               out can find its way back in through the host's routing.
 * - `acoustic`— an ordinary microphone-style input with an ordinary output:
 *               the classic speaker-into-mic howl.
 */
export type FeedbackRisk = 'loop' | 'virtual' | 'acoustic' | null;

export function feedbackRisk(inputLabel: string, outputLabel: string): FeedbackRisk {
  const inHints = hintsIn(inputLabel);
  const outHints = hintsIn(outputLabel);
  if (inHints.length > 0 && outHints.some((h) => inHints.includes(h))) return 'loop';
  if (inHints.length > 0) return 'virtual';
  if (/mic|microphone|line in|input/i.test(inputLabel)) return 'acoustic';
  return null;
}

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

  /* ---- audible output bus (everything the user hears passes through here) -- */
  /** Master audible bus. Sampler, FX rack, loop audition and test tones join here. */
  private outputBus: GainNode | null = null;
  private outputAnalyser: AnalyserNode | null = null;
  /** master safety limiter between the bus and the device (see installLimiter) */
  private limiter: AudioWorkletNode | null = null;
  /** Fallback sink: bus → MediaStreamDestination → <audio>.setSinkId(device). */
  private elementSink: { dest: MediaStreamAudioDestinationNode; el: HTMLAudioElement } | null = null;
  /** The playback device currently routed to (null = system default). */
  outputDeviceId: string | null = null;
  /** Which mechanism is actually carrying the audio right now. */
  sinkRouting: SinkRouting = 'default';

  /* ---- optional software input monitoring (off unless asked for) ---------- */
  /** Monitor send, tapped off the capture head *after* the recorder's tap. */
  monitorTap: GainNode | null = null;
  private monitorEnabled = false;
  private monitorLevel = 0;

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
   * Enumerate playback devices. Chromium withholds *all* device labels —
   * outputs included — until the page has held a media stream once, so the
   * same one-shot `getUserMedia` unlock as `scanInputs()` applies, and for the
   * same reason it is only paid for when the labels are actually missing
   * (opening a stream makes Windows emit `devicechange`, which would otherwise
   * re-trigger a live picker's scan forever).
   */
  async scanOutputs(): Promise<AudioOutputScan> {
    if (typeof navigator === 'undefined' || !navigator.mediaDevices) {
      return { devices: [], permission: 'unsupported' };
    }
    let permission: AudioPermission = 'granted';
    let raw = (await navigator.mediaDevices.enumerateDevices()).filter((d) => d.kind === 'audiooutput');

    if (raw.length === 0 || raw.every((d) => !d.label)) {
      try {
        const s = await navigator.mediaDevices.getUserMedia({ audio: true });
        s.getTracks().forEach((t) => t.stop());
      } catch {
        permission = 'denied';
      }
      raw = (await navigator.mediaDevices.enumerateDevices()).filter((d) => d.kind === 'audiooutput');
    }

    const devices = raw.map((d) => ({
      id: d.deviceId,
      label: d.label || `Output ${d.deviceId.slice(0, 6)}`,
    }));
    return { devices, permission };
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

  /* ------------------------------------------------------------------ */
  /* Audible output bus                                                  */
  /* ------------------------------------------------------------------ */

  private ensureCtx(): AudioContext {
    this.ctx ??= new AudioContext({ sampleRate: 48000 });
    return this.ctx;
  }

  /**
   * The one node every audible signal connects to. Nothing in the app should
   * touch `ctx.destination` directly any more: routing to a chosen device means
   * re-pointing exactly this bus, and the output meter taps exactly this bus.
   *
   * (The zero-gain "keep the worklet pulled" sink in `openInput` is the single
   * deliberate exception — it is silent by construction and must stay attached
   * to the context's own destination so no device choice can ever stall it.)
   */
  get output(): AudioNode {
    const ctx = this.ensureCtx();
    if (!this.outputBus) {
      this.outputBus = ctx.createGain();
      this.outputBus.gain.value = 1;
      this.outputAnalyser = ctx.createAnalyser();
      this.outputAnalyser.fftSize = 2048;
      this.attachBus();
    }
    return this.outputBus;
  }

  /** (Re)connect the bus to the analyser tap and to the current terminal node. */
  private attachBus(): void {
    if (!this.outputBus || !this.ctx) return;
    this.outputBus.disconnect();
    this.limiter?.disconnect();
    if (this.outputAnalyser) this.outputBus.connect(this.outputAnalyser);
    const terminal = this.elementSink ? this.elementSink.dest : this.ctx.destination;
    // bus → master limiter → device, so a chord of -1 dBFS samples cannot clip the output
    if (this.limiter) {
      this.outputBus.connect(this.limiter);
      this.limiter.connect(terminal);
    } else {
      this.outputBus.connect(terminal);
    }
  }

  /**
   * Insert the master safety limiter (-0.3 dBFS brick wall, 2 ms lookahead) between the output bus
   * and the device. Call once the worklet module is registered; idempotent. Without it, samples
   * that peak at -1 dBFS sum on a chord past 0 dBFS and the device hard-clips — which is exactly
   * what made playback sound distorted next to Sanctuary, which has the same limiter.
   */
  installLimiter(): void {
    const ctx = this.ctx;
    if (!ctx || this.limiter) return;
    void this.output;             // make sure the bus exists first
    try {
      this.limiter = new AudioWorkletNode(ctx, 'as-limiter', {
        numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2,
        channelCountMode: 'explicit', channelInterpretation: 'speakers',
      });
      this.attachBus();
    } catch {
      this.limiter = null;        // module not registered yet: stay unlimited rather than silent
    }
  }

  /** True once the master limiter is in the signal path. */
  get limiterActive(): boolean {
    return this.limiter !== null;
  }

  private teardownElementSink(): void {
    if (!this.elementSink) return;
    this.elementSink.el.pause();
    this.elementSink.el.srcObject = null;
    this.elementSink.dest.disconnect();
    this.elementSink = null;
  }

  /**
   * Route everything audible to a specific playback device.
   *
   * Preference order, decided at runtime rather than by assumption:
   *  1. `AudioContext.setSinkId()` — one call, no extra graph, sample-accurate.
   *     Shipped in Chromium 110; Electron 33 is Chromium 130, so this is the
   *     path that actually runs here (logged as `sinkRouting === 'context'`).
   *  2. `MediaStreamAudioDestinationNode` → `HTMLAudioElement.setSinkId()` —
   *     kept as a real, exercised fallback for any runtime that lacks (1).
   *
   * Returns the mechanism that ended up carrying the audio.
   */
  async setOutputDevice(deviceId: string | null): Promise<SinkRouting> {
    const ctx = this.ensureCtx();
    this.outputDeviceId = deviceId;
    void this.output; // make sure the bus exists before it is re-pointed

    const sinkable = ctx as AudioContext & { setSinkId?(id: string): Promise<void> };
    const report = (how: SinkRouting) => {
      // one diagnostic line, so the mechanism in use is provable in the field
      // (support can read it from the console) rather than assumed
      console.info(`[jm] audio output → ${deviceId ?? 'system default'} via ${how}`);
      return how;
    };
    if (typeof sinkable.setSinkId === 'function') {
      try {
        await sinkable.setSinkId(deviceId ?? '');
        this.teardownElementSink();
        this.attachBus();
        this.sinkRouting = deviceId ? 'context' : 'default';
        return report(this.sinkRouting);
      } catch {
        /* fall through to the element sink — e.g. a device that vanished */
      }
    }

    if (!deviceId) {
      this.teardownElementSink();
      this.attachBus();
      this.sinkRouting = 'default';
      return report(this.sinkRouting);
    }

    if (!this.elementSink) {
      const dest = ctx.createMediaStreamDestination();
      const el = new Audio();
      el.autoplay = true;
      el.srcObject = dest.stream;
      this.elementSink = { dest, el };
    }
    const el = this.elementSink.el as HTMLAudioElement & { setSinkId?(id: string): Promise<void> };
    if (typeof el.setSinkId === 'function') await el.setSinkId(deviceId);
    this.attachBus();
    await el.play().catch(() => undefined);
    this.sinkRouting = 'element';
    return report(this.sinkRouting);
  }

  /** Live peak 0..1 of everything leaving the app, for the output meter. */
  outputPeak(): number {
    return peakOf(this.outputAnalyser);
  }

  /* ------------------------------------------------------------------ */
  /* Input monitoring (opt-in, never on by default)                      */
  /* ------------------------------------------------------------------ */

  /**
   * Enable/disable software monitoring and set its level (linear 0..1).
   * The send is tapped off the same node that feeds the recorder, *after* the
   * capture tap, so nothing about what gets recorded changes.
   */
  setMonitor(enabled: boolean, level: number): void {
    this.monitorEnabled = enabled;
    this.monitorLevel = Math.max(0, Math.min(1, level));
    this.applyMonitorGain();
  }

  get monitoring(): boolean {
    return this.monitorEnabled;
  }

  /** True when monitoring is armed but held silent because a take is running. */
  get monitorHeldForTake(): boolean {
    return this.monitorEnabled && this.recording;
  }

  /**
   * Monitoring is force-muted for the whole of a take. Rationale (documented in
   * DECISIONS-capture.md): the monitor send leaves through the user's speakers
   * or through whatever virtual device is selected as the output, and in either
   * case it can re-enter the take — acoustically through an open microphone, or
   * electrically through the loopback cable Auto mode deliberately uses. The
   * recording must be exactly the instrument, so the monitor is ramped to zero
   * on `startRecording()` and restored on `stopRecording()`.
   */
  private applyMonitorGain(): void {
    if (!this.monitorTap || !this.ctx) return;
    const target = this.monitorEnabled && !this.recording ? this.monitorLevel : 0;
    const g = this.monitorTap.gain;
    const t = this.ctx.currentTime;
    g.cancelScheduledValues(t);
    g.setValueAtTime(g.value, t);
    g.linearRampToValueAtTime(target, t + MONITOR_RAMP_S);
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
    this.captureNode = new AudioWorkletNode(this.ctx, 'as-capture', {
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

    // Optional monitor send: a parallel branch off the capture head, so the
    // recorder's signal path is untouched. Starts at zero and is only opened
    // by applyMonitorGain() if the user actually asked for monitoring.
    this.monitorTap = this.ctx.createGain();
    this.monitorTap.gain.value = 0;
    head.connect(this.monitorTap);
    this.monitorTap.connect(this.output);
    this.applyMonitorGain();
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
    this.monitorTap?.disconnect();
    this.monitorTap = null;
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
    return peakOf(this.analyser);
  }

  startRecording(): void {
    this.blocks = [];
    this.recording = true;
    // hold the monitor silent for the take — see applyMonitorGain()
    this.applyMonitorGain();
    this.captureNode?.port.postMessage('start');
  }

  stopRecording(): Float32Array[] {
    this.recording = false;
    this.applyMonitorGain();
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

  /**
   * Play a sine burst through the **selected output** (the bus, not
   * `ctx.destination`). Defaults are the loopback helper's 1 kHz / −6 dBFS
   * verification tone (§3.4); the Devices panel calls it at −18 dBFS.
   * Fades top and tail, so it can never click.
   */
  playTestTone(durationS = 1, freq = 1000, peak = 0.5): void {
    const ctx = this.ensureCtx();
    void ctx.resume();
    const osc = ctx.createOscillator();
    osc.frequency.value = freq;
    const g = ctx.createGain();
    osc.connect(g).connect(this.output);
    const t = ctx.currentTime;
    const fade = Math.min(TONE_FADE_S, durationS / 4);
    g.gain.setValueAtTime(0, t);
    g.gain.linearRampToValueAtTime(peak, t + fade);
    g.gain.setValueAtTime(peak, t + durationS - fade);
    g.gain.linearRampToValueAtTime(0, t + durationS);
    osc.start(t);
    osc.stop(t + durationS);
  }
}

/** Peak absolute sample 0..1 from an analyser's time-domain window. */
function peakOf(analyser: AnalyserNode | null): number {
  if (!analyser) return 0;
  const buf = new Float32Array(analyser.fftSize);
  analyser.getFloatTimeDomainData(buf);
  let peak = 0;
  for (let i = 0; i < buf.length; i++) {
    const a = Math.abs(buf[i]);
    if (a > peak) peak = a;
  }
  return peak;
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
