/**
 * The 12 JM FX units (§4) — Web Audio implementations. All parameter ids,
 * ranges and defaults come from FX_REGISTRY (@jm/jmi-format); these classes
 * only map registry values onto audio graphs.
 */

import type { FxType } from '@jm/jmi-format/web';
import { BaseFxUnit, BUTTERWORTH_Q_DB, divisionToMs, feedbackGain, makeLfo, type ParamValue } from './fxTypes';

/* ------------------------------------------------------------------ */
/* 1. JM Chorus (BBD-style)                                            */
/* ------------------------------------------------------------------ */
class JmChorus extends BaseFxUnit {
  private dry = this.ctx.createGain();
  private wet = this.ctx.createGain();
  private delayL = this.ctx.createDelay(0.05);
  private delayR = this.ctx.createDelay(0.05);
  private lfoL = makeLfo(this.ctx, this.delayL.delayTime, 0.5, 0.002);
  private lfoR = makeLfo(this.ctx, this.delayR.delayTime, 0.5, 0.002);
  private merger = this.ctx.createChannelMerger(2);
  private widthPanL = this.ctx.createStereoPanner();
  private widthPanR = this.ctx.createStereoPanner();

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'jmChorus');
    this.delayL.delayTime.value = 0.007;
    this.delayR.delayTime.value = 0.009;
    // BBD darkening
    const lpL = ctx.createBiquadFilter();
    const lpR = ctx.createBiquadFilter();
    lpL.type = lpR.type = 'lowpass';
    lpL.frequency.value = lpR.frequency.value = 8000;
    lpL.Q.value = lpR.Q.value = BUTTERWORTH_Q_DB;
    this.wetIn.connect(this.dry).connect(this.wetOut);
    this.wetIn.connect(this.delayL).connect(lpL).connect(this.widthPanL).connect(this.wet);
    this.wetIn.connect(this.delayR).connect(lpR).connect(this.widthPanR).connect(this.wet);
    this.wet.connect(this.wetOut);
    void this.merger;
    this.applyAll();
  }

  protected applyParam(id: string, value: ParamValue): void {
    const p = this.params;
    switch (id) {
      case 'mode': {
        // Juno-style: I = slow/shallow, II = faster/deeper, I+II = both LFOs beat
        const mode = String(value);
        const base = Number(p.rate);
        const rateMul = mode === 'II' ? 1.6 : 1;
        this.setAP(this.lfoL.osc.frequency, base * rateMul);
        this.setAP(this.lfoR.osc.frequency, mode === 'I+II' ? base * 1.6 : base * rateMul);
        break;
      }
      case 'rate':
        this.applyParam('mode', p.mode);
        break;
      case 'depth': {
        const d = (Number(value) / 100) * 0.0035;
        this.setAP(this.lfoL.depthGain.gain, d);
        this.setAP(this.lfoR.depthGain.gain, d);
        break;
      }
      case 'width': {
        const w = Number(value) / 100;
        this.setAP(this.widthPanL.pan, -w);
        this.setAP(this.widthPanR.pan, w);
        break;
      }
      case 'mix': {
        const m = Number(value) / 100;
        this.setAP(this.wet.gain, m);
        this.setAP(this.dry.gain, 1 - m * 0.5);
        break;
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* 2. Tremolo / Auto-Pan                                               */
/* ------------------------------------------------------------------ */
class TremoloAutoPan extends BaseFxUnit {
  private splitter = this.ctx.createChannelSplitter(2);
  private merger = this.ctx.createChannelMerger(2);
  private gL = this.ctx.createGain();
  private gR = this.ctx.createGain();
  private lfo = this.ctx.createOscillator();
  private depthL = this.ctx.createGain();
  private depthR = this.ctx.createGain();

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'tremoloAutoPan');
    this.gL.gain.value = 1;
    this.gR.gain.value = 1;
    this.wetIn.connect(this.splitter);
    this.splitter.connect(this.gL, 0);
    this.splitter.connect(this.gR, 1);
    this.gL.connect(this.merger, 0, 0);
    this.gR.connect(this.merger, 0, 1);
    this.merger.connect(this.wetOut);
    this.lfo.type = 'sine';
    this.lfo.frequency.value = 4.5;
    this.lfo.connect(this.depthL).connect(this.gL.gain);
    this.lfo.connect(this.depthR).connect(this.gR.gain);
    this.lfo.start();
    this.applyAll();
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'rate':
        this.setAP(this.lfo.frequency, Number(value));
        break;
      case 'shape':
        this.lfo.type = value === 'tri' ? 'triangle' : (String(value) as OscillatorType);
        break;
      case 'depth':
      case 'spread': {
        const depth = (Number(this.params.depth) / 100) * 0.5;
        const spread = Number(this.params.spread) / 100; // 0 = tremolo, 1 = full pan
        // L modulates with +depth, R between +depth (tremolo) and −depth (pan)
        this.setAP(this.depthL.gain, depth);
        this.setAP(this.depthR.gain, depth * (1 - 2 * spread));
        break;
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* 3. Phaser                                                           */
/* ------------------------------------------------------------------ */
class Phaser extends BaseFxUnit {
  private allpass: BiquadFilterNode[] = [];
  private lfo = this.ctx.createOscillator();
  private lfoDepth = this.ctx.createGain();
  private fb = this.ctx.createGain();
  private wet = this.ctx.createGain();
  private dry = this.ctx.createGain();
  private stageIn = this.ctx.createGain();
  /**
   * BUG FOUND WHILE WIRING MIX — the feedback loop was SILENT, and it took the
   * whole wet path with it.
   *
   * `stageIn -> allpass... -> fb -> stageIn` is a cycle, and the Web Audio
   * spec only allows a cycle that contains at least one DelayNode: an
   * implementation MUST mute any cycle without one. Chrome and
   * node-web-audio-api both do, and because the muted sub-graph is the wet
   * chain itself, the Capture phaser rendered its DRY INPUT and nothing else —
   * rate, depth, feedback and stages all did nothing audible. Measured: wet
   * peak 0.0 before, 0.82 after, with the identical graph plus this node.
   *
   * One DelayNode in the loop makes the cycle legal. `delayTime` 0 is the
   * shortest the spec allows inside a cycle (it is rounded up to one render
   * quantum, 128 frames), so the loop is a short comb rather than the plugin's
   * per-sample recursion — but it is the tightest Web Audio permits, and a
   * phaser that phases beats one that does not. The loop gain is still exactly
   * `feedbackGain(feedback)` (< 1 everywhere, and the allpass chain is
   * magnitude-flat), so stability is unchanged — fxStability covers it.
   */
  private fbDelay = this.ctx.createDelay(0.05);

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'phaser');
    for (let i = 0; i < 6; i++) {
      const ap = ctx.createBiquadFilter();
      ap.type = 'allpass';
      ap.frequency.value = 800 + i * 200;
      ap.Q.value = 0.6;
      this.allpass.push(ap);
    }
    this.lfo.frequency.value = 0.3;
    this.lfoDepth.gain.value = 500;
    this.lfo.connect(this.lfoDepth);
    for (const ap of this.allpass) this.lfoDepth.connect(ap.frequency);
    this.lfo.start();
    // CLIENT REQUEST — Mix (wet/dry), appended to the shared FX registry. The
    // dry path was a bare unity wire summed against a wet gain of 1; it is a
    // true crossfade now, so mix 50 is the 50/50 blend the sum already had.
    this.wetIn.connect(this.dry).connect(this.wetOut);
    this.wetIn.connect(this.stageIn);
    this.rebuildChain(4);
    this.applyAll();
  }

  private rebuildChain(stages: number): void {
    this.stageIn.disconnect();
    for (const ap of this.allpass) ap.disconnect();
    this.fb.disconnect();
    this.fbDelay.disconnect();
    this.wet.disconnect();
    let node: AudioNode = this.stageIn;
    for (let i = 0; i < stages; i++) {
      node.connect(this.allpass[i]);
      node = this.allpass[i];
    }
    node.connect(this.wet).connect(this.wetOut);
    node.connect(this.fb).connect(this.fbDelay).connect(this.stageIn);
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'rate':
        this.setAP(this.lfo.frequency, Number(value));
        break;
      case 'depth':
        this.setAP(this.lfoDepth.gain, (Number(value) / 100) * 900);
        break;
      case 'feedback':
        // the allpass chain is magnitude-flat (|H| = 1 at every frequency), so
        // the loop gain is exactly this coefficient
        this.setAP(this.fb.gain, feedbackGain(Number(value)));
        break;
      case 'stages':
        this.rebuildChain(Number(value));
        break;
      case 'mix': {
        const m = Number(value) / 100;
        this.setAP(this.wet.gain, m);
        this.setAP(this.dry.gain, 1 - m);
        break;
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* 4. Flanger                                                          */
/* ------------------------------------------------------------------ */
class Flanger extends BaseFxUnit {
  private delay = this.ctx.createDelay(0.05);
  private lfo = makeLfo(this.ctx, this.delay.delayTime, 0.2, 0.001);
  private fb = this.ctx.createGain();
  private wet = this.ctx.createGain();
  private dry = this.ctx.createGain();

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'flanger');
    this.delay.delayTime.value = 0.002;
    // CLIENT REQUEST — Mix (wet/dry). Was hard-wired at dry 1.0 / wet 0.7.
    this.wetIn.connect(this.dry).connect(this.wetOut);
    this.wetIn.connect(this.delay).connect(this.wet).connect(this.wetOut);
    this.delay.connect(this.fb).connect(this.delay);
    this.applyAll();
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'rate':
        this.setAP(this.lfo.osc.frequency, Number(value));
        break;
      case 'depth':
        this.setAP(this.lfo.depthGain.gain, (Number(value) / 100) * 0.002);
        break;
      case 'feedback':
        this.setAP(this.fb.gain, feedbackGain(Number(value))); // sign preserved
        break;
      case 'manual':
        this.setAP(this.delay.delayTime, Number(value) / 1000);
        break;
      case 'mix': {
        const m = Number(value) / 100;
        this.setAP(this.wet.gain, m);
        this.setAP(this.dry.gain, 1 - m);
        break;
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* 5. EP Preamp / Drive (soft-clip waveshaper + tilt filter)           */
/* ------------------------------------------------------------------ */
class EpPreamp extends BaseFxUnit {
  private pre = this.ctx.createGain();
  private shaper = this.ctx.createWaveShaper();
  private tiltLow = this.ctx.createBiquadFilter();
  private tiltHigh = this.ctx.createBiquadFilter();
  private post = this.ctx.createGain();

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'epPreamp');
    this.tiltLow.type = 'lowshelf';
    this.tiltLow.frequency.value = 300;
    this.tiltHigh.type = 'highshelf';
    this.tiltHigh.frequency.value = 2400;
    this.shaper.oversample = '4x';
    this.wetIn.connect(this.pre).connect(this.shaper).connect(this.tiltLow).connect(this.tiltHigh).connect(this.post).connect(this.wetOut);
    this.applyAll();
  }

  /** coefficient the shaper curve currently holds, so a knob drag does not
   * rebuild an identical 2048-point table on every frame */
  private curveK = Number.NaN;

  private setCurve(drive: number): void {
    const k = 1 + (drive / 100) * 24;
    if (k === this.curveK) return;
    try {
      this.shaper.curve = this.makeCurve(drive);
      this.curveK = k;
    } catch {
      // Some strictly-spec'd engines refuse to re-assign an already-set curve.
      // Keep the curve we have: this stage has no feedback path, so the worst
      // case is a slightly different drive character, never an instability.
    }
  }

  private makeCurve(drive: number): Float32Array<ArrayBuffer> {
    const k = 1 + (drive / 100) * 24;
    const n = 2048;
    const curve = new Float32Array(n);
    for (let i = 0; i < n; i++) {
      const x = (i / (n - 1)) * 2 - 1;
      curve[i] = Math.tanh(k * x) / Math.tanh(k);
    }
    return curve;
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'drive': {
        const d = Number(value);
        this.setCurve(d);
        this.setAP(this.pre.gain, 1 + (d / 100) * 2);
        // rough drive compensation (auto-trim, not normalization of samples)
        this.setAP(this.post.gain, Math.pow(10, Number(this.params.output) / 20) / (1 + (d / 100) * 1.2));
        break;
      }
      case 'tone': {
        const t = Number(value) / 100; // −1 dark … +1 bright
        this.setAP(this.tiltLow.gain, -t * 6);
        this.setAP(this.tiltHigh.gain, t * 6);
        break;
      }
      case 'output':
        this.applyParam('drive', this.params.drive);
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* 6. Vintage EQ                                                       */
/* ------------------------------------------------------------------ */
class VintageEq extends BaseFxUnit {
  private lowCut = this.ctx.createBiquadFilter();
  private bass = this.ctx.createBiquadFilter();
  private mid = this.ctx.createBiquadFilter();
  private treble = this.ctx.createBiquadFilter();

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'vintageEq');
    this.lowCut.type = 'highpass';
    this.lowCut.frequency.value = 5;
    this.lowCut.Q.value = BUTTERWORTH_Q_DB;
    this.bass.type = 'lowshelf';
    this.bass.frequency.value = 100;
    this.mid.type = 'peaking';
    this.mid.Q.value = 0.9;
    this.treble.type = 'highshelf';
    this.treble.frequency.value = 8000;
    this.wetIn.connect(this.lowCut).connect(this.bass).connect(this.mid).connect(this.treble).connect(this.wetOut);
    this.applyAll();
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'bass':
        this.setAP(this.bass.gain, Number(value));
        break;
      case 'midFreq':
        this.setAP(this.mid.frequency, Number(value));
        break;
      case 'midGain':
        this.setAP(this.mid.gain, Number(value));
        break;
      case 'treble':
        this.setAP(this.treble.gain, Number(value));
        break;
      case 'lowCut':
        this.setAP(this.lowCut.frequency, value === 'off' ? 5 : Number(value));
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* 7. Compressor (amount macro, auto-makeup)                           */
/* ------------------------------------------------------------------ */
class Compressor extends BaseFxUnit {
  private comp = this.ctx.createDynamicsCompressor();
  private makeup = this.ctx.createGain();

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'compressor');
    this.comp.knee.value = 6;
    this.wetIn.connect(this.comp).connect(this.makeup).connect(this.wetOut);
    this.applyAll();
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'amount': {
        const a = Number(value) / 100;
        const threshold = -a * 32; // 0 → 0 dB, 100 → −32 dB
        const ratio = 1 + a * 7; // 1:1 → 8:1
        this.setAP(this.comp.threshold, threshold);
        this.setAP(this.comp.ratio, ratio);
        // auto-makeup: half of the estimated gain reduction at threshold
        const makeupDb = Math.max(0, -threshold * (1 - 1 / ratio) * 0.5);
        this.setAP(this.makeup.gain, Math.pow(10, makeupDb / 20));
        break;
      }
      case 'attack':
        this.setAP(this.comp.attack, Number(value) / 1000);
        break;
      case 'release':
        this.setAP(this.comp.release, Number(value) / 1000);
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* 8. Tape Delay                                                       */
/* ------------------------------------------------------------------ */
class TapeDelay extends BaseFxUnit {
  private dL = this.ctx.createDelay(3);
  private dR = this.ctx.createDelay(3);
  private fbL = this.ctx.createGain();
  private fbR = this.ctx.createGain();
  private toneL = this.ctx.createBiquadFilter();
  private toneR = this.ctx.createBiquadFilter();
  private wet = this.ctx.createGain();
  private wow = makeLfo(this.ctx, this.dL.delayTime, 0.7, 0.001);
  private flutter = makeLfo(this.ctx, this.dR.delayTime, 6.3, 0.0003);
  private pingpong = false;
  private merger = this.ctx.createChannelMerger(2);

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'tapeDelay');
    this.toneL.type = this.toneR.type = 'lowpass';
    // the tone filter sits *inside* the repeat loop, so it must be flat: any
    // passband resonance multiplies the feedback coefficient every pass
    this.toneL.Q.value = this.toneR.Q.value = BUTTERWORTH_Q_DB;
    this.wetIn.connect(this.wetOut); // dry
    this.wetIn.connect(this.dL);
    this.wetIn.connect(this.dR);
    this.dL.connect(this.toneL);
    this.dR.connect(this.toneR);
    this.toneL.connect(this.merger, 0, 0);
    this.toneR.connect(this.merger, 0, 1);
    this.merger.connect(this.wet).connect(this.wetOut);
    this.routeFeedback(false);
    this.applyAll();
  }

  private routeFeedback(pingpong: boolean): void {
    this.fbL.disconnect();
    this.fbR.disconnect();
    this.toneL.connect(this.fbL);
    this.toneR.connect(this.fbR);
    if (pingpong) {
      this.fbL.connect(this.dR);
      this.fbR.connect(this.dL);
    } else {
      this.fbL.connect(this.dL);
      this.fbR.connect(this.dR);
    }
    this.pingpong = pingpong;
  }

  private currentTimeMs(): number {
    if (this.params.sync === true) return divisionToMs(this.tempo, String(this.params.syncDivision));
    return Number(this.params.time);
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'time':
      case 'sync':
      case 'syncDivision': {
        const ms = this.currentTimeMs();
        this.setAP(this.dL.delayTime, ms / 1000, 0.05);
        this.setAP(this.dR.delayTime, (ms / 1000) * (this.pingpong ? 1 : 1.02), 0.05);
        break;
      }
      case 'feedback': {
        const f = feedbackGain(Number(value));
        this.setAP(this.fbL.gain, f);
        this.setAP(this.fbR.gain, f);
        break;
      }
      case 'tone':
        this.setAP(this.toneL.frequency, Number(value));
        this.setAP(this.toneR.frequency, Number(value));
        break;
      case 'wowFlutter': {
        const a = Number(value) / 100;
        this.setAP(this.wow.depthGain.gain, a * 0.002);
        this.setAP(this.flutter.depthGain.gain, a * 0.0006);
        break;
      }
      case 'pingpong':
        this.routeFeedback(value === true);
        this.applyParam('time', this.params.time);
        break;
      case 'mix':
        this.setAP(this.wet.gain, Number(value) / 100);
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* 9. Digital Delay                                                    */
/* ------------------------------------------------------------------ */
class DigitalDelay extends BaseFxUnit {
  private dL = this.ctx.createDelay(4);
  private dR = this.ctx.createDelay(4);
  private fbL = this.ctx.createGain();
  private fbR = this.ctx.createGain();
  private hp = this.ctx.createBiquadFilter();
  private lp = this.ctx.createBiquadFilter();
  private wet = this.ctx.createGain();
  private merger = this.ctx.createChannelMerger(2);

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'digitalDelay');
    this.hp.type = 'highpass';
    this.lp.type = 'lowpass';
    this.hp.Q.value = this.lp.Q.value = BUTTERWORTH_Q_DB;
    this.wetIn.connect(this.wetOut);
    this.wetIn.connect(this.dL);
    this.wetIn.connect(this.dR);
    this.dL.connect(this.fbL).connect(this.dL);
    this.dR.connect(this.fbR).connect(this.dR);
    this.dL.connect(this.merger, 0, 0);
    this.dR.connect(this.merger, 0, 1);
    this.merger.connect(this.hp).connect(this.lp).connect(this.wet).connect(this.wetOut);
    this.applyAll();
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'timeL':
      case 'timeR':
      case 'sync':
      case 'syncDivisionL':
      case 'syncDivisionR': {
        const sync = this.params.sync === true;
        const msL = sync ? divisionToMs(this.tempo, String(this.params.syncDivisionL)) : Number(this.params.timeL);
        const msR = sync ? divisionToMs(this.tempo, String(this.params.syncDivisionR)) : Number(this.params.timeR);
        this.setAP(this.dL.delayTime, msL / 1000, 0.05);
        this.setAP(this.dR.delayTime, msR / 1000, 0.05);
        break;
      }
      case 'feedback': {
        const f = feedbackGain(Number(value));
        this.setAP(this.fbL.gain, f);
        this.setAP(this.fbR.gain, f);
        break;
      }
      case 'filterHp':
        this.setAP(this.hp.frequency, Number(value));
        break;
      case 'filterLp':
        this.setAP(this.lp.frequency, Number(value));
        break;
      case 'mix':
        this.setAP(this.wet.gain, Number(value) / 100);
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Freeverb-style reverb core (combs + feedback damping), shared by     */
/* Plate and Hall.                                                     */
/* ------------------------------------------------------------------ */
/**
 * Ceiling on a comb's per-pass gain. The damping filter is Butterworth
 * (|H| ≤ 1), so the loop gain *is* this number; the remaining 4 % is headroom
 * for the time-varying delay length (size changes, Hall's modulation), which
 * momentarily raises the effective loop gain while the delay line shortens.
 * A comb's tail is still ~150 loop-times long here, so long decays keep their
 * tail — the ceiling only bites on the shortest combs at the longest decays,
 * where the longer combs carry the tail anyway.
 */
const MAX_COMB_FEEDBACK = 0.96;

class ReverbCore {
  readonly input: GainNode;
  readonly output: GainNode;
  private combs: { delay: DelayNode; fb: GainNode; damp: BiquadFilterNode; baseMs: number; loopS: number }[] = [];
  private modLfos: { osc: OscillatorNode; depth: GainNode }[] = [];
  /** false until the owning unit has applied its initial parameters: while
   * false every coefficient is written directly instead of glided, so the
   * combs never run at a GainNode's default gain of 1.0 (an exactly-unity
   * loop) for the half second a setTargetAtTime ramp would take. */
  private primed = false;

  constructor(
    private ctx: BaseAudioContext,
    baseDelaysMs: number[],
    private mod = false,
  ) {
    this.input = ctx.createGain();
    this.output = ctx.createGain();
    const sum = ctx.createGain();
    sum.gain.value = 1 / baseDelaysMs.length;
    for (const [i, ms] of baseDelaysMs.entries()) {
      const delay = ctx.createDelay(1);
      delay.delayTime.value = ms / 1000;
      const fb = ctx.createGain();
      fb.gain.value = 0; // silent until applyDecay() computes a real coefficient
      const damp = ctx.createBiquadFilter();
      damp.type = 'lowpass';
      damp.frequency.value = 6000;
      // flat, not the node's default +1 dB of resonance: this filter is inside
      // the comb's feedback path, so any peak above 1.0 is added to the loop
      // gain on every single pass
      damp.Q.value = BUTTERWORTH_Q_DB;
      this.input.connect(delay);
      delay.connect(damp).connect(fb).connect(delay);
      damp.connect(sum);
      this.combs.push({ delay, fb, damp, baseMs: ms, loopS: ms / 1000 });
      if (mod && i % 2 === 0) {
        const { osc, depthGain } = makeLfo(ctx, delay.delayTime, 0.2 + i * 0.07, 0);
        this.modLfos.push({ osc, depth: depthGain });
      }
    }
    // simple diffusion: two allpass biquads (magnitude-flat by construction,
    // and outside the comb loops in any case)
    const ap1 = ctx.createBiquadFilter();
    const ap2 = ctx.createBiquadFilter();
    ap1.type = ap2.type = 'allpass';
    ap1.frequency.value = 900;
    ap2.frequency.value = 2400;
    sum.connect(ap1).connect(ap2).connect(this.output);
  }

  /** Called once by the owning unit after its initial applyAll(). */
  prime(): void {
    this.primed = true;
  }

  private write(p: AudioParam, v: number, tau: number): void {
    if (this.primed) p.setTargetAtTime(v, this.ctx.currentTime, tau);
    else p.value = v;
  }

  setSize(pct: number): void {
    const scale = 0.5 + (pct / 100) * 1.2;
    for (const c of this.combs) {
      // remember the *target* loop time; delayTime.value still reads the old
      // one while a glide is in flight, and computing the decay coefficient
      // from a stale (shorter) loop time over-estimates it
      c.loopS = (c.baseMs * scale) / 1000;
      this.write(c.delay.delayTime, c.loopS, 0.1);
    }
    this.applyDecay();
  }

  private decayS = 2.2;

  setDecay(seconds: number): void {
    this.decayS = seconds;
    this.applyDecay();
  }

  private applyDecay(): void {
    for (const c of this.combs) {
      // gain per pass for −60 dB in decayS
      const g = Math.pow(10, (-3 * c.loopS) / this.decayS);
      this.write(c.fb.gain, Math.min(MAX_COMB_FEEDBACK, g), 0.1);
    }
  }

  setDamp(pct: number): void {
    const f = 12000 - (pct / 100) * 10500; // 0 → 12 kHz, 100 → 1.5 kHz
    for (const c of this.combs) this.write(c.damp.frequency, f, 0.05);
  }

  setMod(pct: number): void {
    if (!this.mod) return;
    for (const l of this.modLfos) this.write(l.depth.gain, (pct / 100) * 0.0015, 0.05);
  }
}

/* ------------------------------------------------------------------ */
/* 10. Plate Reverb                                                    */
/* ------------------------------------------------------------------ */
class PlateReverb extends BaseFxUnit {
  private predelay = this.ctx.createDelay(0.25);
  private core = new ReverbCore(this.ctx, [29.7, 37.1, 41.1, 43.7, 50.0, 56.3, 61.1, 68.3]);
  private wet = this.ctx.createGain();

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'plateReverb');
    this.wetIn.connect(this.wetOut);
    this.wetIn.connect(this.predelay).connect(this.core.input);
    this.core.output.connect(this.wet).connect(this.wetOut);
    this.applyAll();
    this.core.prime();
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'size':
        this.core.setSize(Number(value));
        break;
      case 'decay':
        this.core.setDecay(Number(value));
        break;
      case 'predelay':
        this.setAP(this.predelay.delayTime, Number(value) / 1000);
        break;
      case 'damp':
        this.core.setDamp(Number(value));
        break;
      case 'mix':
        this.setAP(this.wet.gain, Number(value) / 100);
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* 11. Hall / Shimmer (+12 st pitch feed via as-pitch-shift worklet)    */
/* ------------------------------------------------------------------ */
/** Send gain at shimmer = 100. See the loop-gain note in the constructor. */
const MAX_SHIMMER_SEND = 0.22;

class HallShimmer extends BaseFxUnit {
  private core = new ReverbCore(this.ctx, [50.1, 56.9, 63.7, 71.3, 79.9, 86.2, 94.1, 103.7], true);
  private wet = this.ctx.createGain();
  private shimmerGain = this.ctx.createGain();
  private shimmerHp = this.ctx.createBiquadFilter();
  private shimmerLp = this.ctx.createBiquadFilter();
  private shifter: AudioWorkletNode | null = null;

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'hallShimmer');
    this.wetIn.connect(this.wetOut);
    this.wetIn.connect(this.core.input);
    this.core.output.connect(this.wet).connect(this.wetOut);
    this.shimmerGain.gain.value = 0;
    // Shimmer: reverb output → +12 st → back into the reverb. This is a second
    // feedback loop wrapped *around* the comb bank, so its gain is the send
    // gain times the core's own resonant magnitude (up to ~1/(1−0.96) per comb
    // before the 1/8 sum). Two things keep it below unity: the send tops out at
    // MAX_SHIMMER_SEND, and the return is band-limited — the octave-up copy is
    // rolled off so the loop cannot regenerate the band where the combs
    // resonate hardest. Both are also what the effect wants musically: a soft
    // octave halo rather than a full-bandwidth re-injection.
    this.shimmerLp.type = 'lowpass';
    this.shimmerLp.frequency.value = 5000;
    this.shimmerLp.Q.value = BUTTERWORTH_Q_DB;
    this.shimmerHp.type = 'highpass';
    this.shimmerHp.frequency.value = 300;
    this.shimmerHp.Q.value = BUTTERWORTH_Q_DB;
    try {
      this.shifter = new AudioWorkletNode(ctx as AudioContext, 'as-pitch-shift');
      this.core.output
        .connect(this.shimmerGain)
        .connect(this.shifter)
        .connect(this.shimmerHp)
        .connect(this.shimmerLp)
        .connect(this.core.input);
    } catch {
      // worklet not registered (headless / tests) — shimmer silently disabled
      this.shifter = null;
    }
    this.applyAll();
    this.core.prime();
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'size':
        this.core.setSize(Number(value));
        break;
      case 'decay':
        this.core.setDecay(Number(value));
        break;
      case 'shimmer':
        this.setAP(this.shimmerGain.gain, (Number(value) / 100) * MAX_SHIMMER_SEND);
        break;
      case 'mod':
        this.core.setMod(Number(value));
        break;
      case 'mix':
        this.setAP(this.wet.gain, Number(value) / 100);
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* 12. Widener / Ensemble (mid-side + correlation meter)               */
/* ------------------------------------------------------------------ */
class Widener extends BaseFxUnit {
  private splitter = this.ctx.createChannelSplitter(2);
  private merger = this.ctx.createChannelMerger(2);
  private sideGain = this.ctx.createGain();
  private ensembleDelay = this.ctx.createDelay(0.05);
  private ensembleLfo = makeLfo(this.ctx, this.ensembleDelay.delayTime, 0.6, 0);
  private ensembleMix = this.ctx.createGain();
  readonly analyserL = this.ctx.createAnalyser();
  readonly analyserR = this.ctx.createAnalyser();

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'widener');
    // M/S encode-decode with side gain = width
    const inv = ctx.createGain();
    inv.gain.value = -1;
    const mid = ctx.createGain();
    mid.gain.value = 0.5;
    const side = ctx.createGain();
    side.gain.value = 0.5;
    this.wetIn.connect(this.splitter);
    // mid = (L+R)/2
    this.splitter.connect(mid, 0);
    this.splitter.connect(mid, 1);
    // side = (L−R)/2
    this.splitter.connect(side, 0);
    this.splitter.connect(inv, 1);
    inv.connect(side);
    side.connect(this.sideGain);
    // ensemble: modulated delay on the side signal
    this.ensembleDelay.delayTime.value = 0.012;
    side.connect(this.ensembleDelay).connect(this.ensembleMix).connect(this.sideGain);
    this.ensembleMix.gain.value = 0;
    // decode: L = M + S, R = M − S
    const sInv = ctx.createGain();
    sInv.gain.value = -1;
    mid.connect(this.merger, 0, 0);
    this.sideGain.connect(this.merger, 0, 0);
    mid.connect(this.merger, 0, 1);
    this.sideGain.connect(sInv).connect(this.merger, 0, 1);
    this.merger.connect(this.wetOut);
    // correlation meter taps
    const outSplit = ctx.createChannelSplitter(2);
    this.wetOut.connect(outSplit);
    outSplit.connect(this.analyserL, 0);
    outSplit.connect(this.analyserR, 1);
    this.applyAll();
  }

  /** correlation −1..1 for the mono-safe indicator */
  correlation(): number {
    const n = this.analyserL.fftSize;
    const a = new Float32Array(n);
    const b = new Float32Array(n);
    this.analyserL.getFloatTimeDomainData(a);
    this.analyserR.getFloatTimeDomainData(b);
    let num = 0;
    let ea = 0;
    let eb = 0;
    for (let i = 0; i < n; i++) {
      num += a[i] * b[i];
      ea += a[i] * a[i];
      eb += b[i] * b[i];
    }
    const d = Math.sqrt(ea * eb);
    return d > 0 ? num / d : 1;
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'width':
        this.setAP(this.sideGain.gain, Number(value) / 100);
        break;
      case 'ensemble': {
        const e = Number(value) / 100;
        this.setAP(this.ensembleMix.gain, e * 0.8);
        this.setAP(this.ensembleLfo.depthGain.gain, e * 0.004);
        break;
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* 13. Filter (resonant SVF-style: biquad + drive + mix)                */
/* ------------------------------------------------------------------ */
class SvFilter extends BaseFxUnit {
  private drive = this.ctx.createWaveShaper();
  private filt = this.ctx.createBiquadFilter();
  private wet = this.ctx.createGain();
  private dry = this.ctx.createGain();
  private curves = new Map<number, Float32Array<ArrayBuffer>>();

  constructor(ctx: BaseAudioContext) {
    super(ctx, 'svFilter');
    this.drive.oversample = '2x';
    this.filt.type = 'lowpass';
    this.wetIn.connect(this.drive).connect(this.filt).connect(this.wet).connect(this.wetOut);
    this.wetIn.connect(this.dry).connect(this.wetOut);
    this.applyAll();
  }

  private curveFor(amount: number): Float32Array<ArrayBuffer> {
    const key = Math.round(amount * 20);
    let c = this.curves.get(key);
    if (c) return c;
    const pre = 1 + amount * 9, post = 1 / Math.sqrt(pre);
    c = new Float32Array(new ArrayBuffer(1024 * 4));
    for (let i = 0; i < c.length; i++) {
      const x = (i / (c.length - 1)) * 2 - 1;
      c[i] = amount > 0 ? Math.tanh(x * pre) * post : x;
    }
    this.curves.set(key, c);
    return c;
  }

  protected applyParam(id: string, value: ParamValue): void {
    switch (id) {
      case 'mode':
        this.filt.type = value === 'highpass' ? 'highpass' : value === 'bandpass' ? 'bandpass' : 'lowpass';
        this.applyParam('resonance', this.params.resonance);
        break;
      case 'cutoff':
        this.setAP(this.filt.frequency, Number(value));
        break;
      case 'resonance': {
        const r = Number(value) / 100;
        // lowpass/highpass Q is in dB (flat at −3 dB); bandpass Q is linear
        this.setAP(this.filt.Q, this.filt.type === 'bandpass' ? 0.7 + r * 11 : BUTTERWORTH_Q_DB + r * 27);
        break;
      }
      case 'drive':
        this.drive.curve = this.curveFor(Number(value) / 100);
        break;
      case 'mix': {
        const m = Number(value) / 100;
        this.setAP(this.wet.gain, m);
        this.setAP(this.dry.gain, 1 - m);
        break;
      }
    }
  }
}

/* ------------------------------------------------------------------ */

export function createFxUnit(ctx: BaseAudioContext, type: FxType): BaseFxUnit {
  switch (type) {
    case 'jmChorus':
      return new JmChorus(ctx);
    case 'tremoloAutoPan':
      return new TremoloAutoPan(ctx);
    case 'phaser':
      return new Phaser(ctx);
    case 'flanger':
      return new Flanger(ctx);
    case 'epPreamp':
      return new EpPreamp(ctx);
    case 'vintageEq':
      return new VintageEq(ctx);
    case 'compressor':
      return new Compressor(ctx);
    case 'tapeDelay':
      return new TapeDelay(ctx);
    case 'digitalDelay':
      return new DigitalDelay(ctx);
    case 'plateReverb':
      return new PlateReverb(ctx);
    case 'hallShimmer':
      return new HallShimmer(ctx);
    case 'widener':
      return new Widener(ctx);
    case 'svFilter':
      return new SvFilter(ctx);
  }
}

export { Widener };
