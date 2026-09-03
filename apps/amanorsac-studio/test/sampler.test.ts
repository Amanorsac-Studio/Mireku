import { describe, expect, it } from 'vitest';
import type { Instrument } from '@jm/jmi-format';
import { VoiceManager, ZoneSelector, type VoiceSink, type ZonePick } from '../src/engine/sampler/samplerCore';

function makeInstrument(): Instrument {
  const zones: Instrument['zones'] = [];
  for (const [root, lo, hi] of [
    [60, 0, 61],
    [63, 62, 127],
  ] as const) {
    for (const [vlo, vhi] of [
      [1, 45],
      [46, 80],
      [81, 110],
      [111, 127],
    ] as const) {
      zones.push({
        rootNote: root,
        keyRangeLo: lo,
        keyRangeHi: hi,
        velRangeLo: vlo,
        velRangeHi: vhi,
        roundRobins: [
          { file: `samples/${root}_${vlo}_rr1.wav`, peakDb: -10, lengthMs: 500, loop: null },
          { file: `samples/${root}_${vlo}_rr2.wav`, peakDb: -10.5, lengthMs: 500, loop: null },
        ],
      });
    }
  }
  return {
    formatVersion: 1,
    meta: {
      name: 'T',
      world: 'rhodes-ep',
      author: 'a',
      createdAt: '2026-01-01T00:00:00Z',
      captureMode: 'auto',
      channels: 'stereo',
      sampleRate: 48000,
      bitDepth: 24,
    },
    soundType: 'decaying',
    zones,
    velocityMap: [
      { layer: 1, name: 'Soft', lo: 1, hi: 45 },
      { layer: 2, name: 'Medium', lo: 46, hi: 80 },
      { layer: 3, name: 'Hard', lo: 81, hi: 110 },
      { layer: 4, name: 'Very Hard', lo: 111, hi: 127 },
    ],
    reserved: { releaseSamples: [], pedalDownZones: [] },
  };
}

class MockSink implements VoiceSink {
  nextId = 1;
  started: { id: number; pick: ZonePick; note: number }[] = [];
  released: number[] = [];
  start(pick: ZonePick, note: number): number {
    const id = this.nextId++;
    this.started.push({ id, pick, note });
    return id;
  }
  release(id: number): void {
    this.released.push(id);
  }
}

describe('zone selection + RR cycling (§3.6)', () => {
  it('selects by key range and velocity window', () => {
    const sel = new ZoneSelector(makeInstrument());
    const p = sel.pick(60, 60)!;
    expect(p.zone.rootNote).toBe(60);
    expect(p.zone.velRangeLo).toBe(46);
    expect(sel.pick(64, 120)!.zone.rootNote).toBe(63);
    expect(sel.pick(60, 0)).toBeNull();
  });

  it('cycles round robins per zone', () => {
    const sel = new ZoneSelector(makeInstrument());
    expect(sel.pick(60, 60)!.rrIndex).toBe(0);
    expect(sel.pick(60, 60)!.rrIndex).toBe(1);
    expect(sel.pick(60, 60)!.rrIndex).toBe(0);
    // independent counter per zone
    expect(sel.pick(60, 120)!.rrIndex).toBe(0);
  });

  it('repitches unsampled keys via playbackRate (±1 st)', () => {
    const sel = new ZoneSelector(makeInstrument());
    expect(sel.pick(61, 60)!.playbackRate).toBeCloseTo(Math.pow(2, 1 / 12));
    expect(sel.pick(62, 60)!.playbackRate).toBeCloseTo(Math.pow(2, -1 / 12));
    expect(sel.pick(60, 60)!.playbackRate).toBe(1);
  });
});

describe('CC64 sustain logic (§5.4)', () => {
  it('defers note-offs while the pedal is down and releases on pedal up', () => {
    const sink = new MockSink();
    const vm = new VoiceManager(new ZoneSelector(makeInstrument()), sink);
    vm.controlChange(64, 127); // pedal down
    const id = vm.noteOn(60, 60)!;
    vm.noteOff(60);
    expect(sink.released).toEqual([]); // deferred
    vm.controlChange(64, 0); // pedal up
    expect(sink.released).toEqual([id]);
  });

  it('re-strike while held spawns a new voice and keeps the old ringing', () => {
    const sink = new MockSink();
    const vm = new VoiceManager(new ZoneSelector(makeInstrument()), sink);
    vm.controlChange(64, 100);
    vm.noteOn(60, 60);
    vm.noteOff(60);
    vm.noteOn(60, 60); // re-strike
    expect(sink.started.length).toBe(2);
    expect(sink.released).toEqual([]);
    expect(vm.activeCount).toBe(2);
    vm.controlChange(64, 10); // pedal up releases the deferred one only
    expect(sink.released.length).toBe(1);
    expect(vm.activeCount).toBe(1);
  });

  it('releases immediately when the pedal is up', () => {
    const sink = new MockSink();
    const vm = new VoiceManager(new ZoneSelector(makeInstrument()), sink);
    const id = vm.noteOn(60, 60)!;
    vm.noteOff(60);
    expect(sink.released).toEqual([id]);
  });

  it('steals voices at the polyphony limit', () => {
    const sink = new MockSink();
    const vm = new VoiceManager(new ZoneSelector(makeInstrument()), sink, 4);
    for (let i = 0; i < 5; i++) vm.noteOn(60, 60);
    expect(vm.activeCount).toBe(4);
    expect(sink.released.length).toBe(1);
  });
});
