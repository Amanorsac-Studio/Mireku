/**
 * Anchored velocity response — the shared model for sampled acoustic instruments.
 *
 * A sampled instrument holds a handful of recordings per note, each played at one velocity (its
 * "anchor"), and each already carrying the real instrument's loudness at that velocity. Applying a
 * velocity curve on top of that, restarted inside every layer, double-counts the dynamics: the
 * level jumps at each layer boundary and the overall range balloons far past a real instrument's.
 *
 * Instead: level follows ONE continuous curve that passes exactly through the measured level of
 * each anchor, and a layer change only alters timbre. Optionally two neighbouring layers blend in
 * equal power across a narrow band around the boundary (level-matched first, which is what keeps
 * two different recordings from phasing audibly), and a note played below its layer's anchor is
 * darkened slightly, standing in for the dynamics between the recorded layers.
 */

export interface AnchoredLayer {
  /** velocity this layer was recorded at */
  anchorVelocity: number;
  /** measured loudness of the layer's sample, dBFS */
  levelDb: number;
}

export interface VelocityPart {
  /** index into the layers array as passed in */
  index: number;
  /** linear gain to apply to that sample */
  gain: number;
  /** low-pass cutoff in Hz, or null for no filtering */
  cutoffHz: number | null;
}

export interface VelocityPlan {
  /** the level the note should come out at, dBFS */
  targetDb: number;
  /** one part per voice to start (two while blending across a boundary) */
  parts: VelocityPart[];
}

/** How far below its softest anchor the curve may fall, and how far above its loudest it may rise. */
const SOFT_FLOOR_DB = 24;
const LOUD_CEILING_DB = 3;
/** Level correction applied to a layer is bounded, so a mis-measured level cannot blow up. */
const MAX_TRIM_DB = 12;
/** Fallback slope when there is only one layer to go on (dB per velocity step). */
const LONE_SLOPE_DB = 0.25;
const FILTER_MAX_HZ = 20000;
const FILTER_MIN_HZ = 1200;
/** A note this many velocity steps under its anchor gets the full brightness tilt. */
const BRIGHT_SPAN = 32;
const BRIGHT_OCTAVES = 1.5;

const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

/** The continuous level curve through the anchors, in dBFS. */
export function targetLevelDb(sorted: AnchoredLayer[], velocity: number): number {
  const v = clamp(velocity, 1, 127);
  if (sorted.length === 0) return 0;
  const first = sorted[0], last = sorted[sorted.length - 1];
  if (sorted.length === 1) {
    const db = first.levelDb + LONE_SLOPE_DB * (v - first.anchorVelocity);
    return clamp(db, first.levelDb - SOFT_FLOOR_DB, first.levelDb + LOUD_CEILING_DB);
  }
  if (v <= first.anchorVelocity) {
    const next = sorted[1];
    const slope = (next.levelDb - first.levelDb) / Math.max(1, next.anchorVelocity - first.anchorVelocity);
    return Math.max(first.levelDb - SOFT_FLOOR_DB, first.levelDb + slope * (v - first.anchorVelocity));
  }
  if (v >= last.anchorVelocity) {
    const prev = sorted[sorted.length - 2];
    const slope = (last.levelDb - prev.levelDb) / Math.max(1, last.anchorVelocity - prev.anchorVelocity);
    return Math.min(last.levelDb + LOUD_CEILING_DB, last.levelDb + slope * (v - last.anchorVelocity));
  }
  for (let i = 0; i + 1 < sorted.length; i++) {
    const a = sorted[i], b = sorted[i + 1];
    if (v >= a.anchorVelocity && v <= b.anchorVelocity) {
      const t = (v - a.anchorVelocity) / Math.max(1, b.anchorVelocity - a.anchorVelocity);
      return a.levelDb + t * (b.levelDb - a.levelDb);
    }
  }
  return last.levelDb;
}

/**
 * Which layers to play at this velocity, and at what gain.
 *
 * @param layers    one entry per velocity layer available for the note (any order)
 * @param velocity  1..127
 * @param xfade     half-width in velocity steps of the blend around a boundary; 0 = hard switch
 * @param brightness 0..100 — darkening applied below a layer's anchor; 0 = off
 */
export function anchoredPlan(
  layers: AnchoredLayer[],
  velocity: number,
  xfade = 0,
  brightness = 0,
): VelocityPlan {
  const order = layers.map((l, index) => ({ ...l, index })).sort((a, b) => a.anchorVelocity - b.anchorVelocity);
  if (order.length === 0) return { targetDb: 0, parts: [] };
  const v = clamp(velocity, 1, 127);
  const targetDb = targetLevelDb(order, v);

  const tilt = (anchor: number): number | null => {
    if (brightness <= 0) return null;
    const under = Math.min(0, v - anchor);                 // only ever darker, never brighter
    if (under === 0) return null;
    const oct = (under / BRIGHT_SPAN) * (brightness / 100) * BRIGHT_OCTAVES;
    const hz = clamp(FILTER_MAX_HZ * Math.pow(2, oct), FILTER_MIN_HZ, FILTER_MAX_HZ);
    return hz >= FILTER_MAX_HZ * 0.98 ? null : hz;
  };
  const partFor = (l: { index: number; anchorVelocity: number; levelDb: number }, weight: number): VelocityPart => ({
    index: l.index,
    gain: weight * Math.pow(10, clamp(targetDb - l.levelDb, -MAX_TRIM_DB, MAX_TRIM_DB) / 20),
    cutoffHz: tilt(l.anchorVelocity),
  });

  // nearest anchor carries the timbre
  let k = 0;
  for (let i = 1; i < order.length; i++) {
    if (Math.abs(order[i].anchorVelocity - v) < Math.abs(order[k].anchorVelocity - v)) k = i;
  }

  if (xfade > 0 && order.length > 1) {
    // blend across the boundary (the midpoint between two anchors) when we are inside the band
    for (let i = 0; i + 1 < order.length; i++) {
      const a = order[i], b = order[i + 1];
      if (v < a.anchorVelocity || v > b.anchorVelocity) continue;
      const boundary = (a.anchorVelocity + b.anchorVelocity) / 2;
      const half = Math.min(xfade, (b.anchorVelocity - a.anchorVelocity) / 2);
      if (Math.abs(v - boundary) > half || half <= 0) break;
      const u = clamp((v - (boundary - half)) / (2 * half), 0, 1);
      const wLow = Math.cos((u * Math.PI) / 2), wHigh = Math.sin((u * Math.PI) / 2);
      return { targetDb, parts: [partFor(a, wLow), partFor(b, wHigh)] };
    }
  }
  return { targetDb, parts: [partFor(order[k], 1)] };
}
