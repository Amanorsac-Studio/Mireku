/**
 * AudioWorklet processors, registered from inline source via blob URLs so no
 * separate asset pipeline is needed. Two processors:
 *  - as-pitch-shift: granular +N semitone shifter (Hall/Shimmer feed)
 *  - as-capture: streams input Float32 blocks to the main thread (recorder)
 */

const WORKLET_SOURCE = `
class JmPitchShift extends AudioWorkletProcessor {
  static get parameterDescriptors() {
    return [{ name: 'semitones', defaultValue: 12, minValue: -24, maxValue: 24 }];
  }
  constructor() {
    super();
    this.bufLen = 8192;
    this.grain = 4096;
    this.bufs = [new Float32Array(this.bufLen), new Float32Array(this.bufLen)];
    this.writePos = 0;
    this.readPos = 0;
  }
  process(inputs, outputs, params) {
    const input = inputs[0];
    const output = outputs[0];
    if (!input || input.length === 0) return true;
    const ratio = Math.pow(2, (params.semitones[0] || 12) / 12);
    const n = input[0].length;
    for (let ch = 0; ch < output.length; ch++) {
      const inCh = input[Math.min(ch, input.length - 1)];
      const buf = this.bufs[Math.min(ch, 1)];
      let wp = this.writePos;
      let rp = this.readPos;
      for (let i = 0; i < n; i++) {
        buf[wp % this.bufLen] = inCh[i];
        wp++;
        // two overlapping grains, half-grain apart, triangular windows
        const g = this.grain;
        const p1 = rp % g;
        const p2 = (rp + g / 2) % g;
        const w1 = 1 - Math.abs(p1 / g - 0.5) * 2;
        const w2 = 1 - Math.abs(p2 / g - 0.5) * 2;
        const base = wp - g;
        const i1 = Math.floor(base + p1) % this.bufLen;
        const i2 = Math.floor(base + p2) % this.bufLen;
        output[ch][i] =
          buf[(i1 + this.bufLen) % this.bufLen] * w1 +
          buf[(i2 + this.bufLen) % this.bufLen] * w2;
        rp += ratio;
      }
      if (ch === output.length - 1) {
        this.writePos = wp;
        this.readPos = rp;
      }
    }
    return true;
  }
}
registerProcessor('as-pitch-shift', JmPitchShift);

class JmCapture extends AudioWorkletProcessor {
  constructor() {
    super();
    this.recording = false;
    this.port.onmessage = (e) => {
      if (e.data === 'start') this.recording = true;
      if (e.data === 'stop') this.recording = false;
    };
  }
  process(inputs) {
    const input = inputs[0];
    if (this.recording && input && input.length > 0 && input[0].length > 0) {
      // copy: worklet buffers are recycled
      const chans = input.map((c) => {
        const out = new Float32Array(c.length);
        out.set(c);
        return out;
      });
      this.port.postMessage({ type: 'block', channels: chans }, chans.map((c) => c.buffer));
    }
    return true;
  }
}
registerProcessor('as-capture', JmCapture);

/**
 * Last line of defence between the FX rack and the user's speakers. It is NOT
 * the stability fix — every unit is stable by construction (see units.ts) —
 * it exists so that a future regression in a feedback path can never put NaN,
 * Infinity or an ear-damaging level onto the output device. It sits at the
 * rack output because that is the single node every sample of processed audio
 * must pass through, downstream of all twelve units and of every feedback
 * loop, and upstream of the output bus. Non-finite samples become silence;
 * anything past the ceiling is clamped. Both are reported once per second so
 * the condition is visible rather than silently masked.
 */
class JmSafety extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ceiling = 4; // +12 dBFS: far above any musical peak, far below harm
    this.nonFinite = 0;
    this.clamped = 0;
    this.lastReport = 0;
  }
  process(inputs, outputs) {
    const input = inputs[0];
    const output = outputs[0];
    if (!input || input.length === 0) return true;
    for (let ch = 0; ch < output.length; ch++) {
      const src = input[Math.min(ch, input.length - 1)];
      const dst = output[ch];
      if (!src) { dst.fill(0); continue; }
      for (let i = 0; i < dst.length; i++) {
        const v = src[i];
        if (!Number.isFinite(v)) { dst[i] = 0; this.nonFinite++; }
        else if (v > this.ceiling) { dst[i] = this.ceiling; this.clamped++; }
        else if (v < -this.ceiling) { dst[i] = -this.ceiling; this.clamped++; }
        else dst[i] = v;
      }
    }
    if ((this.nonFinite || this.clamped) && currentTime - this.lastReport > 1) {
      this.lastReport = currentTime;
      this.port.postMessage({ type: 'fx-unsafe', nonFinite: this.nonFinite, clamped: this.clamped });
      this.nonFinite = 0;
      this.clamped = 0;
    }
    return true;
  }
}
registerProcessor('as-safety', JmSafety);
`;

/** Keyed by context: a module registered on one AudioContext is not available
 * on any other, so memoising globally silently left every later context (an
 * offline render, a rebuilt context after a device change) without the pitch
 * shifter and — more seriously — without the output safety guard. */
const registered = new WeakMap<BaseAudioContext, Promise<void>>();

/** Register the JM worklet processors on a context (idempotent per context). */
export function registerJmWorklets(ctx: BaseAudioContext): Promise<void> {
  let p = registered.get(ctx);
  if (!p) {
    const blob = new Blob([WORKLET_SOURCE], { type: 'application/javascript' });
    const url = URL.createObjectURL(blob);
    p = ctx.audioWorklet.addModule(url).finally(() => URL.revokeObjectURL(url));
    registered.set(ctx, p);
  }
  return p;
}
