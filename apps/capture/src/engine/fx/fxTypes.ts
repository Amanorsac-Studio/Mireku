/** FX unit plumbing shared by all 12 units. Param ids/ranges/defaults come
 * exclusively from FX_REGISTRY in @jm/jmi-format — units never redefine them. */

import type { FxType } from '@jm/jmi-format/web';
import { defaultParams, FX_REGISTRY } from '@jm/jmi-format/web';

export type ParamValue = number | string | boolean;

export interface FxUnitNode {
  readonly type: FxType;
  readonly input: GainNode;
  readonly output: GainNode;
  setParam(id: string, value: ParamValue): void;
  getParams(): Record<string, ParamValue>;
  setBypassed(bypassed: boolean): void;
  isBypassed(): boolean;
  setTempo(bpm: number): void;
  dispose(): void;
}

/** Base class: dry/bypass routing + param bookkeeping. Subclasses build a wet
 * chain between this.wetIn and this.wetOut and implement applyParam. */
export abstract class BaseFxUnit implements FxUnitNode {
  readonly input: GainNode;
  readonly output: GainNode;
  protected readonly wetIn: GainNode;
  protected readonly wetOut: GainNode;
  private readonly bypassGain: GainNode;
  private bypassed = false;
  protected params: Record<string, ParamValue>;
  protected tempo = 120;

  constructor(
    protected ctx: BaseAudioContext,
    public readonly type: FxType,
  ) {
    this.input = ctx.createGain();
    this.output = ctx.createGain();
    this.wetIn = ctx.createGain();
    this.wetOut = ctx.createGain();
    this.bypassGain = ctx.createGain();
    this.bypassGain.gain.value = 0;
    this.input.connect(this.wetIn);
    this.wetOut.connect(this.output);
    this.input.connect(this.bypassGain).connect(this.output);
    this.params = defaultParams(type);
  }

  /** call from subclass constructor after building the wet chain */
  protected applyAll(): void {
    for (const [id, v] of Object.entries(this.params)) this.applyParam(id, v);
  }

  protected abstract applyParam(id: string, value: ParamValue): void;

  setParam(id: string, value: ParamValue): void {
    const spec = FX_REGISTRY[this.type].params.find((p) => p.id === id);
    if (!spec) return;
    if (spec.kind === 'float' || spec.kind === 'int') {
      let n = Number(value);
      if (spec.min !== undefined) n = Math.max(spec.min, n);
      if (spec.max !== undefined) n = Math.min(spec.max ?? n, n);
      value = n;
    }
    this.params[id] = value;
    this.applyParam(id, value);
  }

  getParams(): Record<string, ParamValue> {
    return { ...this.params };
  }

  setBypassed(b: boolean): void {
    this.bypassed = b;
    const t = this.ctx.currentTime;
    this.bypassGain.gain.setTargetAtTime(b ? 1 : 0, t, 0.01);
    this.wetOut.gain.setTargetAtTime(b ? 0 : 1, t, 0.01);
  }

  isBypassed(): boolean {
    return this.bypassed;
  }

  setTempo(bpm: number): void {
    this.tempo = bpm;
    // re-apply syncable params so division changes take effect
    for (const p of FX_REGISTRY[this.type].params) {
      if (p.syncable) this.applyParam(p.id, this.params[p.id]);
    }
  }

  dispose(): void {
    try {
      this.input.disconnect();
      this.output.disconnect();
    } catch {
      /* already disconnected */
    }
  }

  protected setAP(param: AudioParam, value: number, smooth = 0.02): void {
    param.setTargetAtTime(value, this.ctx.currentTime, smooth);
  }
}

/** Tempo-sync division → milliseconds. */
export function divisionToMs(bpm: number, division: string): number {
  const beatMs = 60000 / bpm; // quarter note
  const map: Record<string, number> = {
    '1/32': 0.125,
    '1/16': 0.25,
    '3/32': 0.375,
    '1/8': 0.5,
    '3/16': 0.75,
    '1/4': 1,
    '1/4d': 1.5,
    '1/2': 2,
  };
  return beatMs * (map[division] ?? 1);
}

/** Helper: create an LFO (osc → depth gain) targeting an AudioParam. */
export function makeLfo(
  ctx: BaseAudioContext,
  target: AudioParam,
  rateHz: number,
  depth: number,
  shape: OscillatorType = 'sine',
): { osc: OscillatorNode; depthGain: GainNode } {
  const osc = ctx.createOscillator();
  osc.type = shape;
  osc.frequency.value = rateHz;
  const depthGain = ctx.createGain();
  depthGain.gain.value = depth;
  osc.connect(depthGain).connect(target);
  osc.start();
  return { osc, depthGain };
}
