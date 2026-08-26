import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import {
  validateBundle, validateInstrumentJson, validatePresetJson,
  encodeWav, decodeWav,
  midiToNoteName, noteNameToMidi, sampleFileName, defaultPlanNotes,
  DEFAULT_VELOCITY_MAP, buildDefaultRack, defaultEngineParams,
  type Instrument, type Preset,
} from '../src/index.js';

let dir: string;

function sine(freq: number, seconds: number, sr = 48000, gain = 0.5): Float32Array {
  const n = Math.floor(seconds * sr);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = gain * Math.sin((2 * Math.PI * freq * i) / sr) * Math.exp(-i / (sr * 0.7));
  return out;
}

function makeBundle(root: string, name: string): string {
  const bundle = join(root, `${name}.jmi`);
  mkdirSync(join(bundle, 'samples'), { recursive: true });
  mkdirSync(join(bundle, 'presets'), { recursive: true });

  const notes = [60, 63];
  const zones: Instrument['zones'] = [];
  for (const note of notes) {
    for (const layer of DEFAULT_VELOCITY_MAP) {
      const rrs = [];
      for (let rr = 1; rr <= 2; rr++) {
        const file = `samples/${sampleFileName(note, layer.layer, rr)}`;
        const mono = sine(440 * Math.pow(2, (note - 69) / 12), 0.5);
        writeFileSync(join(bundle, file), encodeWav({ sampleRate: 48000, bitDepth: 24, channels: [mono, mono] }));
        rrs.push({ file, peakDb: -6, lengthMs: 500, loop: null });
      }
      zones.push({
        rootNote: note, keyRangeLo: note - 1, keyRangeHi: note + 1,
        velRangeLo: layer.lo, velRangeHi: layer.hi, roundRobins: rrs,
      });
    }
  }

  const inst: Instrument = {
    formatVersion: 1,
    meta: {
      name, world: 'rhodes-ep', author: 'test', createdAt: new Date().toISOString(),
      captureMode: 'guided', channels: 'stereo', sampleRate: 48000, bitDepth: 24,
    },
    soundType: 'decaying',
    zones,
    velocityMap: DEFAULT_VELOCITY_MAP,
    reserved: { releaseSamples: [], pedalDownZones: [] },
  };
  writeFileSync(join(bundle, 'instrument.json'), JSON.stringify(inst, null, 2));

  const preset: Preset = {
    formatVersion: 1,
    name: 'Test Preset',
    tags: ['test'],
    engine: defaultEngineParams('rhodes-ep'),
    fxRack: buildDefaultRack('rhodes-ep'),
    ampEnvelope: { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 },
  };
  writeFileSync(join(bundle, 'presets', 'Test Preset.json'), JSON.stringify(preset, null, 2));
  return bundle;
}

beforeAll(() => { dir = mkdtempSync(join(tmpdir(), 'jmi-test-')); });
afterAll(() => { rmSync(dir, { recursive: true, force: true }); });

describe('note name helpers', () => {
  it('maps MIDI to names and back', () => {
    expect(midiToNoteName(60)).toBe('C4');
    expect(midiToNoteName(21)).toBe('A0');
    expect(midiToNoteName(108)).toBe('C8');
    expect(noteNameToMidi('C4')).toBe(60);
    expect(noteNameToMidi('A#3')).toBe(58);
  });
  it('default plan is A0..C8 every 3 st = 30 notes', () => {
    const notes = defaultPlanNotes();
    expect(notes.length).toBe(30);
    expect(notes[0]).toBe(21);
    expect(notes[notes.length - 1]).toBe(108);
  });
});

describe('wav round-trip', () => {
  it('24-bit PCM encodes and decodes losslessly enough', () => {
    const src = sine(440, 0.1);
    const wav = encodeWav({ sampleRate: 48000, bitDepth: 24, channels: [src] });
    const dec = decodeWav(wav);
    expect(dec.sampleRate).toBe(48000);
    expect(dec.bitDepth).toBe(24);
    expect(dec.channels[0].length).toBe(src.length);
    let maxErr = 0;
    for (let i = 0; i < src.length; i++) maxErr = Math.max(maxErr, Math.abs(src[i] - dec.channels[0][i]));
    expect(maxErr).toBeLessThan(2 / 8388608);
  });
});

describe('bundle round-trip validation', () => {
  it('a Capture-style written bundle validates clean', () => {
    const bundle = makeBundle(dir, 'JM_Test_Rhodes');
    const res = validateBundle(bundle);
    expect(res.issues.filter((i) => i.level === 'error')).toEqual([]);
    expect(res.valid).toBe(true);
    expect(res.stats?.sampleCount).toBe(16);
    expect(res.stats?.presetCount).toBe(1);
  });

  it('rejects missing sample files', () => {
    const bundle = makeBundle(dir, 'JM_Broken');
    rmSync(join(bundle, 'samples', 'C4_v1_rr1.wav'));
    const res = validateBundle(bundle);
    expect(res.valid).toBe(false);
    expect(res.issues.some((i) => i.message.includes('Missing sample file'))).toBe(true);
  });

  it('rejects schema violations', () => {
    const issues = validateInstrumentJson({ formatVersion: 2 });
    expect(issues.length).toBeGreaterThan(0);
  });

  it('validates preset schema', () => {
    expect(validatePresetJson({
      formatVersion: 1, name: 'X', engine: {}, fxRack: [],
      ampEnvelope: { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 },
    })).toEqual([]);
    expect(validatePresetJson({ formatVersion: 1 }).length).toBeGreaterThan(0);
  });
});
