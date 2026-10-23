import { describe, expect, it } from 'vitest';
import type { Instrument } from '@jm/jmi-format/web';
import { VoiceManager, ZoneSelector, type VoiceSink, type ZonePick } from '../src/engine/sampler/samplerCore';

/** Two-layer piano key at C4 with a key-off release zone, SFZ-style (rt_decay 4 dB/s). */
function piano(): Instrument {
  const rr = (file: string) => ({ file, peakDb: -6, lengthMs: 5000, loop: null });
  return {
    formatVersion: 1,
    meta: { name: 'T', world: 'rhodes-ep', author: '', createdAt: '', captureMode: 'auto', channels: 'stereo', sampleRate: 44100, bitDepth: 24 },
    soundType: 'decaying',
    zones: [
      { rootNote: 60, keyRangeLo: 59, keyRangeHi: 61, velRangeLo: 1, velRangeHi: 83, roundRobins: [rr('samples/C4_v1_rr1.wav')], releaseMs: 400 },
      { rootNote: 60, keyRangeLo: 59, keyRangeHi: 61, velRangeLo: 84, velRangeHi: 127, roundRobins: [rr('samples/C4_v2_rr1.wav')], releaseMs: 400 },
    ],
    releaseZones: [
      { rootNote: 60, keyRangeLo: 59, keyRangeHi: 61, velRangeLo: 1, velRangeHi: 127, roundRobins: [rr('samples/rel_C4_v1_rr1.wav')], rtDecayDb: 4 },
    ],
    velocityMap: [{ layer: 1, name: 'p', lo: 1, hi: 83 }, { layer: 2, name: 'f', lo: 84, hi: 127 }],
    playback: { velocityMode: 'switch', ampVelTrack: 100, voicing: 'neutral' },
    reserved: { releaseSamples: [], pedalDownZones: [] },
  };
}

function rig() {
  let t = 0;
  let next = 1;
  const started: { id: number; pick: ZonePick }[] = [];
  const released: { id: number; ms?: number }[] = [];
  const sink: VoiceSink = {
    start: (pick) => { const id = next++; started.push({ id, pick }); return id; },
    release: (id, ms) => { released.push({ id, ms }); },
  };
  const vm = new VoiceManager(new ZoneSelector(piano()), sink);
  vm.now = () => t;
  return { vm, started, released, advance: (s: number) => { t += s; } };
}
const relStarts = (started: { pick: ZonePick }[]) => started.filter((s) => s.pick.rr.file.includes('rel_'));
const db = (g: number) => 20 * Math.log10(g);

describe('piano playback: velocity curve, key-off releases, sustain pedal', () => {
  it('hard-switches layers and applies the (velocity/127)² curve', () => {
    const { vm, started } = rig();
    vm.noteOn(60, 83);
    vm.noteOn(61, 84);
    expect(started[0].pick.rr.file).toBe('samples/C4_v1_rr1.wav');
    expect(started[1].pick.rr.file).toBe('samples/C4_v2_rr1.wav');
    expect(started[0].pick.gain).toBeCloseTo((83 / 127) ** 2, 6);
    expect(db(new ZoneSelector(piano()).velocityGain(1))).toBeLessThan(-80);
  });

  it('fires a key-off sample on release, attenuated by rt_decay × held seconds, using the zone release time', () => {
    const { vm, started, released, advance } = rig();
    vm.noteOn(60, 100);
    advance(2.5);
    vm.noteOff(60);
    expect(released).toEqual([{ id: 1, ms: 400 }]);
    const rel = relStarts(started);
    expect(rel).toHaveLength(1);
    const bodyGain = (100 / 127) ** 2;
    expect(db(rel[0].pick.gain / bodyGain)).toBeCloseTo(-10, 5); // 4 dB/s × 2.5 s
  });

  it('a staccato note gets an almost full-level release; a long-held note gets none', () => {
    const short = rig();
    short.vm.noteOn(60, 100);
    short.advance(0.1);
    short.vm.noteOff(60);
    expect(db(relStarts(short.started)[0].pick.gain / (100 / 127) ** 2)).toBeGreaterThan(-0.5);

    const long = rig();
    long.vm.noteOn(60, 100);
    long.advance(16); // 64 dB of decay — beyond the −60 dB floor
    long.vm.noteOff(60);
    expect(relStarts(long.started)).toHaveLength(0);
  });

  it('pedal down: key-up rings on with no release; pedal up: dampers fall and one release fires per note', () => {
    const { vm, started, released, advance } = rig();
    vm.controlChange(64, 127);
    vm.noteOn(60, 90);
    vm.noteOn(60, 90); // re-strike while held → second voice, same note
    advance(1);
    vm.noteOff(60);
    expect(released).toHaveLength(0);
    expect(relStarts(started)).toHaveLength(0);
    advance(1);
    vm.controlChange(64, 0);
    expect(released.map((r) => r.id).sort()).toEqual([1, 2]);
    expect(relStarts(started)).toHaveLength(1);
    expect(db(relStarts(started)[0].pick.gain / (90 / 127) ** 2)).toBeCloseTo(-8, 5); // held 2 s
  });

  it('bundles without playback options keep the original unity gain and no releases', () => {
    const inst = piano();
    delete inst.playback;
    delete inst.releaseZones;
    const sel = new ZoneSelector(inst);
    expect(sel.pick(60, 40)?.gain).toBe(1);
    expect(sel.pickRelease(60, 40)).toBeNull();
  });
});
