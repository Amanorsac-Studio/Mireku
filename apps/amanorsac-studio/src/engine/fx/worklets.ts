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

/**
 * Master safety limiter — the port of Sanctuary's jm::MasterLimiter.
 *
 * Without it the output bus is a plain gain into the device: samples that peak at -1 dBFS sum on
 * a chord to well past 0 dBFS, and the device hard-clips. That clipping is what made the sampler
 * sound distorted where Sanctuary (which has this limiter) stayed clean.
 *
 * Soft-knee brick wall at a -0.3 dBFS ceiling: 2 ms lookahead so gain is already down when the
 * peak lands, a sliding minimum over the lookahead window (no overshoot), a fast attack and a
 * program-dependent release (fast 60 ms / slow 550 ms limbs, minimum taken) so transients recover
 * without pumping, and a tanh soft ceiling that is identity below 90 % of the ceiling — so quiet
 * playing is bit-for-bit untouched and only real overs are shaped.
 */
class JmLimiter extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ceiling = Math.pow(10, -0.3 / 20);
    this.knee = 3;
    this.look = Math.max(8, Math.round(sampleRate * 0.002));
    this.delay = new Float32Array(this.look * 2);
    this.win = new Float32Array(this.look + 1).fill(1);
    this.dqIdx = new Int32Array(this.look + 1);
    this.dqHead = 0; this.dqTail = 0; this.dqCount = 0;
    this.t = 0; this.dPos = 0; this.winMin = 1;
    this.gEnv = 1; this.gFast = 1; this.gSlow = 1;
    this.aAtk = Math.exp(-1 / ((0.002 / 3) * sampleRate));
    this.aRelF = Math.exp(-1 / (0.060 * sampleRate));
    this.aRelS = Math.exp(-1 / (0.550 * sampleRate));
  }
  /** sliding minimum of the last "look" targets, monotonic deque */
  pushWindow(v) {
    const cap = this.look + 1;
    this.win[this.t % cap] = v;
    while (this.dqCount > 0 && this.win[this.dqIdx[(this.dqTail - 1 + cap) % cap] % cap] >= v) {
      this.dqTail = (this.dqTail - 1 + cap) % cap;
      this.dqCount--;
    }
    this.dqIdx[this.dqTail] = this.t;
    this.dqTail = (this.dqTail + 1) % cap;
    this.dqCount++;
    while (this.dqCount > 0 && this.dqIdx[this.dqHead] <= this.t - this.look) {
      this.dqHead = (this.dqHead + 1) % cap;
      this.dqCount--;
    }
    this.winMin = this.win[this.dqIdx[this.dqHead] % cap];
    this.t++;
  }
  /** identity below 90 % of the ceiling, C1-continuous tanh bend above, strictly bounded */
  shape(x) {
    const c = this.ceiling, lin = c * 0.9;
    const a = Math.abs(x);
    if (a <= lin) return x;
    const over = (a - lin) / (c - lin);
    const y = lin + (c - lin) * Math.tanh(over);
    return x < 0 ? -y : y;
  }
  process(inputs, outputs) {
    const input = inputs[0], output = outputs[0];
    if (!output || output.length === 0) return true;
    const n = output[0].length;
    const inL = input && input[0] ? input[0] : null;
    const inR = input && input.length > 1 && input[1] ? input[1] : inL;
    for (let i = 0; i < n; i++) {
      const l = inL ? inL[i] : 0;
      const r = inR ? inR[i] : l;
      const peak = Math.max(Math.abs(l), Math.abs(r));
      let target = 1;
      if (peak > 1e-9) {
        const overDb = 20 * Math.log10(peak) - (-0.3);
        let redDb = 0;
        if (overDb >= this.knee * 0.5) redDb = overDb;
        else if (overDb > -this.knee * 0.5) {
          const k = overDb + this.knee * 0.5;
          redDb = (k * k) / (2 * this.knee);
        }
        if (redDb > 0) target = Math.pow(10, -redDb / 20);
      }
      this.pushWindow(target);
      const wMin = this.winMin;
      if (wMin < this.gEnv) {
        this.gEnv = wMin + (this.gEnv - wMin) * this.aAtk;
        this.gFast = this.gEnv; this.gSlow = this.gEnv;
      } else {
        this.gFast = wMin + (this.gFast - wMin) * this.aRelF;
        this.gSlow = wMin + (this.gSlow - wMin) * this.aRelS;
        this.gEnv = Math.min(this.gFast, this.gSlow);
      }
      if (!Number.isFinite(this.gEnv)) this.gEnv = this.gFast = this.gSlow = 1;
      this.gEnv = Math.min(1, Math.max(1e-4, this.gEnv));
      const d = this.dPos * 2;
      const dL = this.delay[d], dR = this.delay[d + 1];
      this.delay[d] = l; this.delay[d + 1] = r;
      this.dPos = (this.dPos + 1) % this.look;
      const oL = this.shape(dL * this.gEnv), oR = this.shape(dR * this.gEnv);
      output[0][i] = oL;
      if (output.length > 1) output[1][i] = oR;
    }
    return true;
  }
}
registerProcessor('as-limiter', JmLimiter);
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
