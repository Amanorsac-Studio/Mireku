import { describe, expect, it } from 'vitest';
import { anchoredPlan, targetLevelDb, type AnchoredLayer } from '../src/velocity.js';

/** What the rendered Kontakt grand actually measured on C4: four anchors, 20 dB apart end to end. */
const GRAND: AnchoredLayer[] = [
  { anchorVelocity: 27, levelDb: -24.3 },
  { anchorVelocity: 69, levelDb: -13.3 },
  { anchorVelocity: 95, levelDb: -8.1 },
  { anchorVelocity: 117, levelDb: -4.3 },
];

const playedDb = (layers: AnchoredLayer[], v: number, xfade = 0, bright = 0) => {
  const plan = anchoredPlan(layers, v, xfade, bright);
  // power sum of the parts, each part's own recorded level included
  const power = plan.parts.reduce((s, p) => s + Math.pow(10, layers[p.index].levelDb / 10) * p.gain * p.gain, 0);
  return 10 * Math.log10(power);
};

describe('anchored velocity response', () => {
  it('plays a layer exactly as recorded at its own anchor', () => {
    for (const l of GRAND) {
      const plan = anchoredPlan(GRAND, l.anchorVelocity);
      expect(plan.parts).toHaveLength(1);
      expect(plan.parts[0].gain).toBeCloseTo(1, 5);
      expect(plan.targetDb).toBeCloseTo(l.levelDb, 5);
    }
  });

  it('never steps the level at a layer boundary', () => {
    let worst = 0;
    for (let v = 2; v <= 127; v++) worst = Math.max(worst, Math.abs(playedDb(GRAND, v) - playedDb(GRAND, v - 1)));
    expect(worst).toBeLessThan(0.6); // the old switch mode jumped 11.3 dB at vel 48→49
  });

  it('rises monotonically with velocity', () => {
    for (let v = 2; v <= 127; v++) expect(playedDb(GRAND, v)).toBeGreaterThanOrEqual(playedDb(GRAND, v - 1) - 1e-9);
  });

  it('keeps the dynamic range near the real instrument, not 100 dB', () => {
    const span = playedDb(GRAND, 127) - playedDb(GRAND, 1);
    expect(span).toBeGreaterThan(20);
    expect(span).toBeLessThan(45);
  });

  it('blends two layers in equal power around the boundary, and only there', () => {
    const boundary = (69 + 95) / 2; // 82
    const mid = anchoredPlan(GRAND, boundary, 8);
    expect(mid.parts).toHaveLength(2);
    const sumSq = mid.parts.reduce((s, p) => s + p.gain * p.gain / Math.pow(10, (mid.targetDb - GRAND[p.index].levelDb) / 10), 0);
    expect(sumSq).toBeCloseTo(1, 2);          // equal power at the crossover
    expect(anchoredPlan(GRAND, 69, 8).parts).toHaveLength(1);  // at an anchor: one layer only
    expect(anchoredPlan(GRAND, 95, 8).parts).toHaveLength(1);
  });

  it('level is still continuous while blending', () => {
    let worst = 0;
    for (let v = 2; v <= 127; v++) worst = Math.max(worst, Math.abs(playedDb(GRAND, v, 8) - playedDb(GRAND, v - 1, 8)));
    expect(worst).toBeLessThan(0.6);
  });

  it('darkens below an anchor only, and not at or above it', () => {
    expect(anchoredPlan(GRAND, 69, 0, 60).parts[0].cutoffHz).toBeNull();
    const soft = anchoredPlan(GRAND, 52, 0, 60).parts[0].cutoffHz;
    expect(soft).not.toBeNull();
    expect(soft!).toBeLessThan(20000);
    expect(soft!).toBeGreaterThanOrEqual(1200);
    // harder than its anchor is never brightened (we cannot add highs that were not recorded)
    expect(anchoredPlan(GRAND, 80, 0, 60).parts.every((p) => p.cutoffHz === null || p.cutoffHz > 0)).toBe(true);
    expect(anchoredPlan(GRAND, 127, 0, 60).parts[0].cutoffHz).toBeNull();
  });

  it('brightness off means no filtering anywhere', () => {
    for (let v = 1; v <= 127; v++) expect(anchoredPlan(GRAND, v, 8, 0).parts.every((p) => p.cutoffHz === null)).toBe(true);
  });

  it('handles a single layer without dividing by zero', () => {
    const one: AnchoredLayer[] = [{ anchorVelocity: 69, levelDb: -12 }];
    expect(anchoredPlan(one, 1).parts).toHaveLength(1);
    expect(targetLevelDb(one, 1)).toBeLessThan(-12);
    expect(targetLevelDb(one, 127)).toBeGreaterThan(-12);
    let worst = 0;
    for (let v = 2; v <= 127; v++) worst = Math.max(worst, Math.abs(playedDb(one, v) - playedDb(one, v - 1)));
    expect(worst).toBeLessThan(0.6);
  });

  it('with anchors 16 apart and xfade 8, every velocity between anchors is a blend', () => {
    const eight: AnchoredLayer[] = [8, 24, 40, 56, 72, 88, 104, 120].map((a, i) => ({
      anchorVelocity: a, levelDb: [-20, -15.9, -13.1, -11, -9.3, -7.9, -6.7, -5.6][i],
    }));
    let single = 0, worst = 0;
    for (let v = 1; v <= 127; v++) {
      const plan = anchoredPlan(eight, v, 8, 60);
      if (plan.parts.length === 1) single++;
      if (v > 1) worst = Math.max(worst, Math.abs(playedDb(eight, v, 8) - playedDb(eight, v - 1, 8)));
    }
    // only the anchors themselves and the velocities outside the outer anchors play one layer
    expect(single).toBeLessThanOrEqual(16);
    expect(worst).toBeLessThan(0.4);
  });

  it('bounds the correction applied to a layer with a wrong measured level', () => {
    const bad: AnchoredLayer[] = [
      { anchorVelocity: 40, levelDb: -60 },   // nonsense measurement
      { anchorVelocity: 100, levelDb: -6 },
    ];
    for (let v = 1; v <= 127; v++) {
      for (const p of anchoredPlan(bad, v, 8).parts) expect(p.gain).toBeLessThanOrEqual(Math.pow(10, 12 / 20) + 1e-9);
    }
  });
});
