import { describe, expect, it } from 'vitest';
import { DEFAULT_VELOCITY_MAP } from '@jm/jmi-format';
import {
  centerVelocity,
  expectedPeakRange,
  judgeTake,
  layerForVelocity,
  rrConsistencyOutliers,
  windowForLayer,
} from '../src/engine/velocity';

describe('velocity windowing (§3.2)', () => {
  it('maps velocities to the four default layers', () => {
    expect(layerForVelocity(1)?.layer).toBe(1);
    expect(layerForVelocity(45)?.layer).toBe(1);
    expect(layerForVelocity(46)?.layer).toBe(2);
    expect(layerForVelocity(80)?.layer).toBe(2);
    expect(layerForVelocity(81)?.layer).toBe(3);
    expect(layerForVelocity(110)?.layer).toBe(3);
    expect(layerForVelocity(111)?.layer).toBe(4);
    expect(layerForVelocity(127)?.layer).toBe(4);
    expect(layerForVelocity(0)).toBeNull();
    expect(layerForVelocity(128)).toBeNull();
  });

  it('center velocities match the Auto Mode targets 23/63/95/119 (§3.4)', () => {
    expect([1, 2, 3, 4].map((l) => centerVelocity(l))).toEqual([23, 63, 95, 119]);
  });

  it('windowForLayer returns the exact windows', () => {
    expect(windowForLayer(2)).toMatchObject({ lo: 46, hi: 80 });
    expect(() => windowForLayer(5)).toThrow();
  });
});

describe('take verdict (§3.3)', () => {
  it('accepts inside-window velocity', () => {
    expect(judgeTake(60, 2)).toEqual({ accepted: true });
  });

  it('coaches PLAY HARDER below the window and PLAY SOFTER above', () => {
    expect(judgeTake(30, 2)).toEqual({ accepted: false, coaching: 'PLAY HARDER' });
    expect(judgeTake(90, 2)).toEqual({ accepted: false, coaching: 'PLAY SOFTER' });
  });

  it('rejects peaks outside the expected range from sibling takes', () => {
    const expected = expectedPeakRange([-12, -12.5]);
    expect(judgeTake(60, 2, DEFAULT_VELOCITY_MAP, { peakDb: -20, expected })).toEqual({
      accepted: false,
      coaching: 'PLAY HARDER',
    });
    expect(judgeTake(60, 2, DEFAULT_VELOCITY_MAP, { peakDb: -6, expected })).toEqual({
      accepted: false,
      coaching: 'PLAY SOFTER',
    });
    expect(judgeTake(60, 2, DEFAULT_VELOCITY_MAP, { peakDb: -12.6, expected })).toEqual({ accepted: true });
  });

  it('first take of a cell has no peak reference', () => {
    expect(expectedPeakRange([])).toBeNull();
  });
});

describe('RR consistency (§3.3, 3 dB spread)', () => {
  it('accepts takes within 3 dB', () => {
    expect(rrConsistencyOutliers([-12, -13.5, -11.2])).toEqual([]);
  });

  it('flags the outlier when spread exceeds 3 dB', () => {
    const outliers = rrConsistencyOutliers([-12, -12.4, -18]);
    expect(outliers).toContain(2);
  });
});
