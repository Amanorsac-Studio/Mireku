/**
 * FX rack (§4): up to 6 slots in series, reorderable, per-unit bypass.
 * Serializes to/from the shared FxUnitState[] preset shape.
 */

import type { FxType, FxUnitState } from '@jm/jmi-format/web';
import type { BaseFxUnit } from './fxTypes';
import { createFxUnit } from './units';

export const MAX_SLOTS = 6;

export class FxRack {
  readonly input: GainNode;
  readonly output: GainNode;
  private units: BaseFxUnit[] = [];
  private tempo = 120;
  /**
   * Defensive guard, not a fix: `jm-safety` replaces NaN/Infinity with silence
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
    try {
      this.safety = new AudioWorkletNode(ctx as AudioContext, 'jm-safety');
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
    if (index === undefined || index >= this.units.length) this.units.push(unit);
    else this.units.splice(index, 0, unit);
    this.reconnect();
    return unit;
  }

  removeUnit(index: number): void {
    const [unit] = this.units.splice(index, 1);
    unit?.dispose();
    this.reconnect();
  }

  moveUnit(from: number, to: number): void {
    if (from < 0 || from >= this.units.length) return;
    const [unit] = this.units.splice(from, 1);
    this.units.splice(Math.max(0, Math.min(to, this.units.length)), 0, unit);
    this.reconnect();
  }

  private reconnect(): void {
    this.input.disconnect();
    for (const u of this.units) u.output.disconnect();
    let node: AudioNode = this.input;
    for (const u of this.units) {
      node.connect(u.input);
      node = u.output;
    }
    node.connect(this.safety ?? this.output);
  }

  serialize(): FxUnitState[] {
    return this.units.map((u) => ({
      type: u.type,
      bypassed: u.isBypassed(),
      params: u.getParams(),
    }));
  }

  load(states: FxUnitState[]): void {
    for (const u of this.units) u.dispose();
    this.units = [];
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
    this.units = [];
  }
}
