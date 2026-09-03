/**
 * FX rack (§4): up to 6 slots in series, reorderable, per-unit bypass.
 * Serializes to/from the shared FxUnitState[] preset shape.
 */

import type { FxType, FxUnitState } from '@jm/jmi-format/web';
import type { BaseFxUnit } from './fxTypes';
import { createFxUnit } from './units';

export const MAX_SLOTS = 6;

/**
 * Seconds of dry crossfade wrapped around a STRUCTURAL change (remove, insert,
 * reorder). See `restructure()`.
 */
export const RESTRUCTURE_FADE_S = 0.012;

/**
 * Seconds the rewired chain is left running, still muted, before it is faded
 * back in.
 *
 * Rewiring is a step at an INTERNAL junction: the unit that suddenly finds a
 * different signal on its input rings — its biquads, its shaper, its
 * compressor envelope all react to an edge that was never in the music. Fading
 * the chain's output back in immediately would simply fade that ringing in.
 * Holding the mute for a settling window lets the transient die inside the
 * chain, where nobody hears it. 30 ms is several time constants of every
 * filter in the rack and still shorter than a semiquaver at any tempo.
 */
export const RESTRUCTURE_SETTLE_S = 0.03;

export class FxRack {
  readonly input: GainNode;
  readonly output: GainNode;
  /**
   * The two ends of the structural crossfade. `wetGate` carries the chain's
   * output, `dryGate` carries the rack input straight to the output; they always
   * sum to 1. Both are downstream of the units, so the chain keeps *receiving*
   * signal while it is muted and delay/reverb tails are not starved.
   */
  private readonly wetGate: GainNode;
  private readonly dryGate: GainNode;
  private units: BaseFxUnit[] = [];
  private tempo = 120;
  /** units spliced out of the chain, disposed once the graph has been rewired */
  private retiring: BaseFxUnit[] = [];
  /** a structural crossfade is in flight; further edits ride along with it */
  private restructurePending = false;
  /**
   * How the second half of a structural change is scheduled. Overridable so an
   * OfflineAudioContext render can drive it sample-deterministically through
   * `suspend()` instead of the wall clock.
   */
  scheduleRestructure: (fn: () => void, seconds: number) => void = (fn, seconds) => {
    setTimeout(fn, seconds * 1000 + 2);
  };
  /**
   * Defensive guard, not a fix: `as-safety` replaces NaN/Infinity with silence
   * and clamps at +12 dBFS. It belongs here rather than in a unit because the
   * rack output is the one node every processed sample passes through on its
   * way to the output bus — downstream of all twelve units and of every
   * feedback loop, upstream of the speakers. Null when the worklet has not
   * been registered (offline renders, tests), in which case the rack is simply
   * wired straight through.
   */
  private safety: AudioWorkletNode | null = null;
  /** last non-finite / clamped-sample report from the safety worklet */
  unsafeReport: { nonFinite: number; clamped: number; at: number } | null = null;

  constructor(private ctx: BaseAudioContext) {
    this.input = ctx.createGain();
    this.output = ctx.createGain();
    this.wetGate = ctx.createGain();
    this.dryGate = ctx.createGain();
    this.wetGate.gain.value = 1;
    this.dryGate.gain.value = 0;
    this.input.connect(this.dryGate);
    try {
      this.safety = new AudioWorkletNode(ctx as AudioContext, 'as-safety');
      this.safety.port.onmessage = (e: MessageEvent) => {
        const d = e.data as { type?: string; nonFinite?: number; clamped?: number };
        if (d?.type !== 'fx-unsafe') return;
        this.unsafeReport = { nonFinite: d.nonFinite ?? 0, clamped: d.clamped ?? 0, at: Date.now() };
        console.warn('[fx] safety guard engaged', this.unsafeReport);
      };
      this.safety.connect(this.output);
    } catch {
      this.safety = null;
    }
    this.reconnect();
  }

  get slots(): readonly BaseFxUnit[] {
    return this.units;
  }

  setTempo(bpm: number): void {
    this.tempo = bpm;
    for (const u of this.units) u.setTempo(bpm);
  }

  getTempo(): number {
    return this.tempo;
  }

  addUnit(type: FxType, index?: number): BaseFxUnit | null {
    if (this.units.length >= MAX_SLOTS) return null;
    const unit = createFxUnit(this.ctx, type);
    unit.setTempo(this.tempo);
    this.restructure(() => {
      if (index === undefined || index >= this.units.length) this.units.push(unit);
      else this.units.splice(index, 0, unit);
    });
    return unit;
  }

  removeUnit(index: number): void {
    if (index < 0 || index >= this.units.length) return;
    this.restructure(() => {
      const [unit] = this.units.splice(index, 1);
      // NOT disposed here: dispose() disconnects, which is the very edit the
      // crossfade exists to hide. It is disposed by `reconnect()` once the
      // chain is silent.
      if (unit) this.retiring.push(unit);
    });
  }

  moveUnit(from: number, to: number): void {
    if (from < 0 || from >= this.units.length) return;
    if (from === to) return;
    this.restructure(() => {
      const [unit] = this.units.splice(from, 1);
      this.units.splice(Math.max(0, Math.min(to, this.units.length)), 0, unit);
    });
  }

  /**
   * Apply a structural edit CLICKLESSLY.
   *
   * Rewiring a Web Audio graph mid-stream is a step discontinuity: the junction
   * being disconnected is almost never carrying zero at that instant. So the
   * edit is wrapped in a crossfade:
   *
   *   1. the model changes IMMEDIATELY, so `slots` — the array the UI renders
   *      from — is correct the moment the user lets go of the drag;
   *   2. the rack crossfades from the chain to a straight dry feed of its own
   *      input over `RESTRUCTURE_FADE_S`. Audio never stops: what the listener
   *      hears is the effects fading out, not a gap;
   *   3. once the chain's output is silent, and only then, the graph is rewired
   *      and any removed unit disposed — no sample of that edit reaches the
   *      output;
   *   4. the crossfade runs back to the chain over the same window.
   *
   * The units stay connected to the rack input throughout, so tails inside a
   * delay or reverb keep running rather than being cut off and restarted.
   */
  private restructure(mutate: () => void): void {
    mutate();
    // A second edit while the first is still crossfading must NOT start a
    // second fade — the gates would be told to ramp from a value they are not
    // at, which is the click this whole dance exists to avoid. The rewire
    // already in flight reads `this.units` when it runs, so it will pick up
    // this edit too.
    if (this.restructurePending) return;
    this.restructurePending = true;
    this.crossfadeToDry(1);
    this.scheduleRestructure(() => {
      this.reconnect();
      // the return is pure automation scheduled ahead of time — no second timer
      this.crossfadeToDry(0, RESTRUCTURE_SETTLE_S);
      this.restructurePending = false;
    }, RESTRUCTURE_FADE_S);
  }

  /**
   * `dry` 1 = straight through, 0 = the unit chain; linear ramp either way.
   *
   * The ramp start is taken from the KNOWN previous state (the two calls always
   * alternate and the second only runs once the first has finished) rather than
   * from `AudioParam.value`, which is the value last *set*, not the value the
   * automation has reached — reading it back mid-ramp and re-anchoring on it is
   * itself a step.
   */
  private crossfadeToDry(dry: number, delay = 0): void {
    const t = this.ctx.currentTime + delay;
    const end = t + RESTRUCTURE_FADE_S;
    for (const [param, from, to] of [
      [this.wetGate.gain, dry, 1 - dry],
      [this.dryGate.gain, 1 - dry, dry],
    ] as const) {
      param.cancelScheduledValues(t);
      param.setValueAtTime(from, t);
      param.linearRampToValueAtTime(to, end);
    }
  }

  private reconnect(): void {
    for (const u of this.retiring) u.dispose();
    this.retiring = [];
    this.input.disconnect();
    this.input.connect(this.dryGate);
    for (const u of this.units) u.output.disconnect();
    let node: AudioNode = this.input;
    for (const u of this.units) {
      node.connect(u.input);
      node = u.output;
    }
    this.wetGate.disconnect();
    this.dryGate.disconnect();
    node.connect(this.wetGate);
    const sink = this.safety ?? this.output;
    this.wetGate.connect(sink);
    this.dryGate.connect(sink);
  }

  serialize(): FxUnitState[] {
    return this.units.map((u) => ({
      type: u.type,
      bypassed: u.isBypassed(),
      params: u.getParams(),
    }));
  }

  /**
   * Replace the whole rack from a preset. Not a "structural edit" in the sense
   * `restructure()` means — the player is changing preset, not nudging a unit —
   * so it applies immediately, and it cancels any crossfade left in flight.
   */
  load(states: FxUnitState[]): void {
    for (const u of this.units) u.dispose();
    for (const u of this.retiring) u.dispose();
    this.retiring = [];
    this.units = [];
    const t = this.ctx.currentTime;
    for (const [param, v] of [
      [this.wetGate.gain, 1],
      [this.dryGate.gain, 0],
    ] as const) {
      param.cancelScheduledValues(t);
      param.setValueAtTime(v, t);
    }
    for (const s of states.slice(0, MAX_SLOTS)) {
      const unit = createFxUnit(this.ctx, s.type);
      unit.setTempo(this.tempo);
      for (const [id, v] of Object.entries(s.params)) unit.setParam(id, v);
      unit.setBypassed(s.bypassed);
      this.units.push(unit);
    }
    this.reconnect();
  }

  dispose(): void {
    for (const u of this.units) u.dispose();
    for (const u of this.retiring) u.dispose();
    this.units = [];
    this.retiring = [];
    this.wetGate.disconnect();
    this.dryGate.disconnect();
  }
}
