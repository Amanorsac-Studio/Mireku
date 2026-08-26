/** Velocity windowing helpers (pure, Node-safe). Layer windows come from the
 * session's velocityMap (defaults from @jm/jmi-format, spec §3.2). */

import type { VelocityLayer } from '@jm/jmi-format/web';
import { DEFAULT_VELOCITY_MAP } from '@jm/jmi-format/web';

export { DEFAULT_VELOCITY_MAP };

/** Which layer (1–4) a MIDI velocity falls into, or null if v out of range. */
export function layerForVelocity(
  velocity: number,
  map: VelocityLayer[] = DEFAULT_VELOCITY_MAP,
): VelocityLayer | null {
  for (const l of map) {
    if (velocity >= l.lo && velocity <= l.hi) return l;
  }
  return null;
}

export function windowForLayer(
  layer: number,
  map: VelocityLayer[] = DEFAULT_VELOCITY_MAP,
): VelocityLayer {
  const found = map.find((l) => l.layer === layer);
  if (!found) throw new Error(`No velocity layer ${layer}`);
  return found;
}

/** Center-of-window velocity for a layer (Auto Mode targets, §3.4). */
export function centerVelocity(layer: number, map: VelocityLayer[] = DEFAULT_VELOCITY_MAP): number {
  const w = windowForLayer(layer, map);
  // floor: (81+110)/2 → 95, matching the spec targets 23/63/95/119
  return Math.floor((w.lo + w.hi) / 2);
}

export type TakeVerdict =
  | { accepted: true }
  | { accepted: false; coaching: 'PLAY HARDER' | 'PLAY SOFTER' };

/**
 * Guided Mode take verdict (§3.3): velocity must land inside the current
 * layer's window. peakDb is additionally checked against the expected range
 * for the layer when reference peaks exist (see expectedPeakRange).
 */
export function judgeTake(
  velocity: number,
  targetLayer: number,
  map: VelocityLayer[] = DEFAULT_VELOCITY_MAP,
  peak?: { peakDb: number; expected: { lo: number; hi: number } | null },
): TakeVerdict {
  const w = windowForLayer(targetLayer, map);
  if (velocity < w.lo) return { accepted: false, coaching: 'PLAY HARDER' };
  if (velocity > w.hi) return { accepted: false, coaching: 'PLAY SOFTER' };
  if (peak && peak.expected) {
    if (peak.peakDb < peak.expected.lo) return { accepted: false, coaching: 'PLAY HARDER' };
    if (peak.peakDb > peak.expected.hi) return { accepted: false, coaching: 'PLAY SOFTER' };
  }
  return { accepted: true };
}

/**
 * Expected peak range for a layer given previously accepted peaks of the same
 * (note, layer): mean ±3 dB (matches the RR consistency requirement).
 * Null when no reference exists yet (first take of a cell — velocity rules alone).
 */
export function expectedPeakRange(acceptedPeaksDb: number[]): { lo: number; hi: number } | null {
  if (acceptedPeaksDb.length === 0) return null;
  const mean = acceptedPeaksDb.reduce((a, b) => a + b, 0) / acceptedPeaksDb.length;
  return { lo: mean - 3, hi: mean + 3 };
}

/**
 * RR consistency check (§3.3): all round robins of a (note, layer) must sit
 * within a 3 dB peak spread. Returns indices of outliers to retake
 * (farthest-from-median first), or [] if consistent.
 */
export function rrConsistencyOutliers(peaksDb: number[], maxSpreadDb = 3): number[] {
  if (peaksDb.length < 2) return [];
  const spread = Math.max(...peaksDb) - Math.min(...peaksDb);
  if (spread <= maxSpreadDb) return [];
  const sorted = [...peaksDb].sort((a, b) => a - b);
  const median = sorted[Math.floor(sorted.length / 2)];
  return peaksDb
    .map((p, i) => ({ i, d: Math.abs(p - median) }))
    .sort((a, b) => b.d - a.d)
    .filter((x) => x.d > maxSpreadDb / 2)
    .map((x) => x.i);
}
