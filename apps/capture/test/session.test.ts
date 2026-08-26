import { describe, expect, it } from 'vitest';
import {
  acceptTake,
  cellProgress,
  clearCell,
  createSession,
  defaultSamplingPlan,
  deserializeSession,
  isComplete,
  markSmartRrReduced,
  nextPosition,
  planFromRange,
  serializeSession,
  sessionStats,
  type SessionConfig,
  type TakeRecord,
} from '../src/engine/session';

function makeConfig(overrides: Partial<SessionConfig> = {}): SessionConfig {
  return {
    id: 'test-session',
    instrumentName: 'Test Rhodes',
    world: 'rhodes-ep',
    soundType: 'decaying',
    channels: 'stereo',
    captureMode: 'guided',
    sampleRate: 48000,
    bitDepth: 24,
    audioInputId: null,
    audioInputChannels: [0, 1],
    midiInputId: null,
    midiOutputId: null,
    plan: planFromRange(60, 66, 3, 2),
    createdAt: new Date().toISOString(),
    ...overrides,
  };
}

function take(note: number, layer: number, rr: number): TakeRecord {
  return {
    note,
    layer,
    rr,
    file: `${note}_${layer}_${rr}.wav`,
    peakDb: -12,
    lengthMs: 1000,
    velocity: 60,
    loop: null,
    acceptedAt: new Date().toISOString(),
  };
}

describe('sampling plan defaults (§3.2)', () => {
  it('default plan is 30 notes, 4 layers, 3 RR', () => {
    const p = defaultSamplingPlan();
    expect(p.notes.length).toBe(30);
    expect(p.notes[0]).toBe(21);
    expect(p.notes[p.notes.length - 1]).toBe(108);
    expect(p.velocityMap.length).toBe(4);
    expect(p.roundRobins).toBe(3);
  });
});

describe('session progression and resume (§3.3)', () => {
  it('nextPosition walks note-major, layer, then RR', () => {
    let s = createSession(makeConfig());
    expect(nextPosition(s)).toEqual({ note: 60, layer: 1, rr: 1 });
    s = acceptTake(s, take(60, 1, 1));
    expect(nextPosition(s)).toEqual({ note: 60, layer: 1, rr: 2 });
    s = acceptTake(s, take(60, 1, 2));
    expect(nextPosition(s)).toEqual({ note: 60, layer: 2, rr: 1 });
  });

  it('serialization round-trips and resumes at the exact same position', () => {
    let s = createSession(makeConfig());
    s = acceptTake(s, take(60, 1, 1));
    s = acceptTake(s, take(60, 1, 2));
    s = acceptTake(s, take(60, 2, 1));
    const restored = deserializeSession(serializeSession(s));
    expect(nextPosition(restored)).toEqual(nextPosition(s));
    expect(nextPosition(restored)).toEqual({ note: 60, layer: 2, rr: 2 });
    expect(sessionStats(restored)).toEqual(sessionStats(s));
  });

  it('rejects corrupt or wrong-version snapshots', () => {
    expect(() => deserializeSession('{"formatVersion":2}')).toThrow();
    expect(() => deserializeSession('{"formatVersion":1}')).toThrow();
  });

  it('clearCell reopens a completed cell (click-to-redo)', () => {
    let s = createSession(makeConfig());
    s = acceptTake(s, take(60, 1, 1));
    s = acceptTake(s, take(60, 1, 2));
    expect(cellProgress(s, 60, 1).complete).toBe(true);
    s = clearCell(s, 60, 1);
    expect(cellProgress(s, 60, 1).done).toBe(0);
    expect(nextPosition(s)).toEqual({ note: 60, layer: 1, rr: 1 });
  });

  it('smart-RR reduction shrinks the required takes of a cell', () => {
    let s = createSession(makeConfig());
    s = acceptTake(s, take(60, 1, 1));
    s = markSmartRrReduced(s, 60, 1, 'null test passed');
    expect(cellProgress(s, 60, 1)).toMatchObject({ done: 1, required: 1, complete: true });
    expect(s.sessionNotes).toContain('null test passed');
  });

  it('isComplete only when every cell is filled', () => {
    let s = createSession(makeConfig());
    expect(isComplete(s)).toBe(false);
    for (const note of s.config.plan.notes) {
      for (const l of s.config.plan.velocityMap) {
        for (let rr = 1; rr <= s.config.plan.roundRobins; rr++) {
          s = acceptTake(s, take(note, l.layer, rr));
        }
      }
    }
    expect(isComplete(s)).toBe(true);
    expect(sessionStats(s).percent).toBe(100);
  });
});
