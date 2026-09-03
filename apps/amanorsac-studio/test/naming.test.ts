import { describe, expect, it } from 'vitest';
import { midiToNoteName, noteNameToMidi, sampleFileName } from '@jm/jmi-format';
import { fileNameFor } from '../src/engine/session';
import { bundleFolderName, keyRanges } from '../src/engine/export';

describe('file naming (§3.2: {NoteName}{Octave}_v{layer}_rr{n}.wav)', () => {
  it('names canonical examples correctly', () => {
    expect(sampleFileName(48, 2, 1)).toBe('C3_v2_rr1.wav');
    expect(sampleFileName(21, 1, 3)).toBe('A0_v1_rr3.wav');
    expect(sampleFileName(108, 4, 2)).toBe('C8_v4_rr2.wav');
  });

  it('fileNameFor matches the shared format helper', () => {
    expect(fileNameFor({ note: 61, layer: 3, rr: 2 })).toBe('C#4_v3_rr2.wav');
  });

  it('round-trips note names', () => {
    for (const n of [21, 33, 48, 60, 61, 90, 108]) {
      expect(noteNameToMidi(midiToNoteName(n))).toBe(n);
    }
  });

  it('bundle folder names are sanitized with a .jmi extension', () => {
    expect(bundleFolderName('JM Rhodes Warm')).toBe('JM_Rhodes_Warm.jmi');
    expect(bundleFolderName('  weird/name*? ')).toBe('weirdname.jmi');
    expect(bundleFolderName('')).toBe('JM_Instrument.jmi');
  });
});

describe('key ranges (§3.2: zones fill the gaps)', () => {
  it('default 3-semitone plan gives root ±1 with full 0–127 coverage', () => {
    const notes = [60, 63, 66];
    const r = keyRanges(notes);
    expect(r[1]).toEqual({ lo: 62, hi: 64 });
    expect(r[0].lo).toBe(0);
    expect(r[2].hi).toBe(127);
    // contiguity
    expect(r[1].lo).toBe(r[0].hi + 1);
    expect(r[2].lo).toBe(r[1].hi + 1);
  });
});
