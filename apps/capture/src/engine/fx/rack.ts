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

  constructor(private ctx: BaseAudioContext) {
    this.input = ctx.createGain();
    this.output = ctx.createGain();
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
    node.connect(this.output);
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
