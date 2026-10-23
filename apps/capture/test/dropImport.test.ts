/**
 * Drag-and-drop audio importer (§3.7 import path, second source).
 *
 * The things that can actually go wrong, in order:
 *   • a name is parsed wrong, so a sample is filed under the wrong note;
 *   • a name carries no pitch and the detector has to find it;
 *   • a mixed-format drop is silently coerced into one format;
 *   • the audio is not passed through byte-for-byte;
 *   • the bundle does not validate.
 *
 * Each has a block below. The last block converts the client's own library
 * samples end to end and round-trips them — when that material is not present
 * the heavy tests skip rather than fail, so CI stays green.
 */

import { describe, expect, it } from 'vitest';
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { validateBundle } from '@jm/jmi-format';
import { midiToNoteName } from '@jm/jmi-format/web';
import {
  AudioFileError,
  decodeChannels,
  measurePcm,
  pcmWavHeader,
  readAudioFile,
} from '../src/engine/import/audioFile';
import { commonNamePrefix, parseNoteToken, parseSampleName, tokenise } from '../src/engine/import/dropNames';
import {
  censusFormats,
  foldLayers,
  planDrop,
  scanDroppedFiles,
  schemaObjection,
  suggestOffset,
  windowsForGroups,
  type DropReadFs,
} from '../src/engine/import/dropPlan';
import { convertDrop, encodeIntPcm, type DropConvertFs } from '../src/engine/import/dropConvert';

/** The client's own library — the real-audio proof at the end. */
const CLIENT_LIBRARY = 'C:\\Users\\amano\\Documents\\JM Signature\\Library';

/* ------------------------------------------------------------------ */
/* harness                                                             */
/* ------------------------------------------------------------------ */

const nodeReadFs: DropReadFs = {
  readFile: (p) => new Uint8Array(readFileSync(p)),
  baseName: (p) => basename(p),
};

const nodeConvertFs: DropConvertFs = {
  mkdir: (d) => mkdirSync(d, { recursive: true }),
  writeFile: (p, data) => writeFileSync(p, typeof data === 'string' ? data : Buffer.from(data)),
  join: (...parts) => join(...parts),
  readFile: (p) => new Uint8Array(readFileSync(p)),
};

function scratch(tag: string): string {
  return mkdtempSync(join(tmpdir(), `jm-drop-${tag}-`));
}

/**
 * A decaying harmonic stack with a fade to digital silence — what a real
 * one-shot looks like, and what the sound-type rule is measuring. The client's
 * own samples measure -84 dB to -270 dB RMS over their final 5%, so a test
 * signal that is still ringing at the end would be testing the wrong thing.
 */
function tone(hz: number, seconds: number, sampleRate: number, amp = 0.6): Float32Array {
  const n = Math.round(seconds * sampleRate);
  const tau = seconds / 5;
  const fadeFrom = Math.floor(n * 0.9);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const t = i / sampleRate;
    const fade = i < fadeFrom ? 1 : 1 - (i - fadeFrom) / (n - fadeFrom);
    const env = Math.exp(-t / tau) * Math.min(1, t / 0.005) * fade;
    let s = 0;
    for (let h = 1; h <= 5; h++) {
      if (hz * h > sampleRate / 2.2) break;
      s += Math.sin(2 * Math.PI * hz * h * t) / h;
    }
    out[i] = amp * env * s * 0.6;
  }
  return out;
}

/** A sustaining tone that does not decay — the loop-detection case. */
function sustained(hz: number, seconds: number, sampleRate: number): Float32Array {
  const n = Math.round(seconds * sampleRate);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const t = i / sampleRate;
    out[i] = 0.5 * Math.min(1, t / 0.01) * (Math.sin(2 * Math.PI * hz * t) + 0.4 * Math.sin(4 * Math.PI * hz * t));
  }
  return out;
}

function midiHz(midi: number): number {
  return 440 * Math.pow(2, (midi - 69) / 12);
}

interface WavOpts {
  sampleRate?: number;
  bitDepth?: number;
  channels?: number;
  float?: boolean;
}

/** Write a WAV, in whatever format the test needs — including illegal ones. */
function writeWav(dir: string, name: string, signal: Float32Array, opts: WavOpts = {}): string {
  const sampleRate = opts.sampleRate ?? 44100;
  const bitDepth = opts.bitDepth ?? 16;
  const channels = opts.channels ?? 1;
  const path = join(dir, name);
  mkdirSync(dirname(path), { recursive: true });

  if (opts.float) {
    const dataBytes = signal.length * channels * 4;
    const h = pcmWavHeader(dataBytes, sampleRate, channels, 32);
    new DataView(h.buffer).setUint16(20, 3, true); // IEEE float
    const out = new Uint8Array(h.length + dataBytes);
    out.set(h, 0);
    const dv = new DataView(out.buffer, h.length, dataBytes);
    let at = 0;
    for (let i = 0; i < signal.length; i++) {
      for (let c = 0; c < channels; c++) {
        dv.setFloat32(at, signal[i], true);
        at += 4;
      }
    }
    writeFileSync(path, Buffer.from(out));
    return path;
  }

  const chans: Float32Array[] = [];
  for (let c = 0; c < channels; c++) chans.push(c === 0 ? signal : signal.map((v) => v * 0.9));
  writeFileSync(path, Buffer.from(encodeIntPcm(chans, sampleRate, bitDepth)));
  return path;
}

function listWavs(dir: string): string[] {
  return readdirSync(dir)
    .filter((f) => f.toLowerCase().endsWith('.wav'))
    .sort()
    .map((f) => join(dir, f));
}

/** The `data` chunk bytes of a WAV on disk — what "bit-perfect" is about. */
function pcmPayload(path: string): Buffer {
  const bytes = new Uint8Array(readFileSync(path));
  const info = readAudioFile(bytes);
  return Buffer.from(bytes.subarray(info.dataOffset, info.dataOffset + info.dataBytes));
}

/* ================================================================== */
/* 1. file names                                                       */
/* ================================================================== */

describe('dropNames — real-world sample naming conventions', () => {
  it('parses a note name, an octave and the C4=60 convention', () => {
    expect(parseNoteToken('C4')).toBe(60);
    expect(parseNoteToken('C3')).toBe(48);
    expect(parseNoteToken('A0')).toBe(21);
    expect(parseNoteToken('C8')).toBe(108);
    expect(parseNoteToken('A#2')).toBe(46);
    expect(parseNoteToken('Db4')).toBe(61);
    expect(parseNoteToken('Bb1')).toBe(34);
    expect(parseNoteToken('Cs3')).toBe(49);
    expect(parseNoteToken('c4')).toBe(60);
    expect(parseNoteToken('C-1')).toBe(0);
  });

  it('rejects tokens that merely start with a note letter', () => {
    for (const t of ['EP2', 'Piano', 'FM', 'B', 'Eb', 'G99', 'H3', 'Rhodes']) {
      expect(parseNoteToken(t), t).toBeNull();
    }
  });

  it('is generous about separators and ordering', () => {
    expect(tokenise('Piano_C3_mf.wav')).toEqual(['Piano', 'C3', 'mf']);
    expect(tokenise('EP-60-velo2.wav')).toEqual(['EP', '60', 'velo2']);
    expect(tokenise('Wurli A#2 (pp) [rr2].aiff')).toEqual(['Wurli', 'A#2', 'pp', 'rr2']);
  });

  const cases: {
    file: string;
    note: number | null;
    source: string;
    velocity: string | null;
    rr: number | null;
  }[] = [
    { file: 'Piano_C3_mf.wav', note: 48, source: 'note-name', velocity: 'mf', rr: null },
    { file: 'EP-60-velo2.wav', note: 60, source: 'midi-number', velocity: 'layer 2', rr: null },
    { file: 'Strings_A#2_v3_rr1.wav', note: 46, source: 'note-name', velocity: 'layer 3', rr: 1 },
    { file: 'C3_v2_rr1.wav', note: 48, source: 'note-name', velocity: 'layer 2', rr: 1 },
    { file: 'Rhodes-Db4-ff-2.wav', note: 61, source: 'note-name', velocity: 'ff', rr: 2 },
    { file: 'Wurli Bb1 pp.wav', note: 34, source: 'note-name', velocity: 'pp', rr: null },
    { file: 'MKS20_F#5_vel100_take3.wav', note: 78, source: 'note-name', velocity: 'velocity 100', rr: 3 },
    { file: 'Pad_c4_soft.wav', note: 60, source: 'note-name', velocity: 'soft', rr: null },
    { file: 'Harp_A0_v1.wav', note: 21, source: 'note-name', velocity: 'layer 1', rr: null },
    { file: 'Choir_layer4_G2.wav', note: 43, source: 'note-name', velocity: 'layer 4', rr: null },
    { file: 'kick.wav', note: null, source: 'none', velocity: null, rr: null },
    { file: 'take_01.wav', note: null, source: 'none', velocity: null, rr: 1 },
  ];

  for (const c of cases) {
    it(`reads "${c.file}"`, () => {
      const p = parseSampleName(c.file);
      expect(p.note, 'note').toBe(c.note);
      expect(p.noteSource, 'noteSource').toBe(c.source);
      expect(p.velocity?.label ?? null, 'velocity').toBe(c.velocity);
      expect(p.roundRobin, 'rr').toBe(c.rr);
    });
  }

  it('takes the trailing note token and flags the ambiguity', () => {
    const p = parseSampleName('Model-D5-C3-mf.wav');
    expect(p.note).toBe(48); // C3, not D5
    expect(p.noteAmbiguous).toBe(true);
    expect(p.noteConfidence).toBe('low');
  });

  it('reads a bare letter as a dynamic but a letter+digit as a note', () => {
    expect(parseSampleName('Pno_F3_f.wav').note).toBe(53);
    expect(parseSampleName('Pno_F3_f.wav').velocity?.label).toBe('f');
    // the only token here is the note, so there is no dynamic to find
    expect(parseSampleName('Pno_f3.wav').note).toBe(53);
  });

  it('treats a velocity number above the 16-layer cap as a velocity, not a layer', () => {
    expect(parseSampleName('x_C3_v3.wav').velocity).toMatchObject({ kind: 'layer', order: 3 });
    expect(parseSampleName('x_C3_v100.wav').velocity).toMatchObject({ kind: 'velocity', order: 100 });
  });

  it('records what it matched and what it did not', () => {
    const p = parseSampleName('JM Grand Piano_C3_mf_rr2.wav');
    expect(p.matched).toEqual(['C3', 'mf', 'rr2']);
    expect(p.leftover).toEqual(['JM', 'Grand', 'Piano']);
  });

  it('finds the common prefix of a set for the instrument name', () => {
    expect(commonNamePrefix(['Rhodes_C3_mf.wav', 'Rhodes_E3_mf.wav', 'Rhodes_G3_f.wav'])).toBe('Rhodes');
    expect(commonNamePrefix(['a.wav', 'b.wav'])).toBe('');
  });
});

/* ================================================================== */
/* 2. untrusted input                                                  */
/* ================================================================== */

describe('audioFile — malformed input is refused, not half-read', () => {
  const dir = scratch('bad');

  const bad: { name: string; bytes: Buffer; match: RegExp }[] = (() => {
    const good = Buffer.from(encodeIntPcm([tone(440, 0.2, 44100)], 44100, 16));
    const noFmt = Buffer.concat([good.subarray(0, 12), good.subarray(36)]);
    const compressed = Buffer.from(good);
    compressed.writeUInt16LE(0x0011, 20); // IMA ADPCM
    const hugeChunk = Buffer.from(good);
    hugeChunk.writeUInt32LE(0x7fffffff, 16); // fmt chunk claims 2 GB
    const zeroRate = Buffer.from(good);
    zeroRate.writeUInt32LE(0, 24);
    const oddDepth = Buffer.from(good);
    oddDepth.writeUInt16LE(13, 34);
    const emptyData = Buffer.from(good.subarray(0, 44));
    emptyData.writeUInt32LE(0, 40);
    return [
      { name: 'not-riff.wav', bytes: Buffer.alloc(200, 7), match: /Not a RIFF/ },
      { name: 'too-short.wav', bytes: Buffer.from('RIFF'), match: /too short/ },
      { name: 'no-fmt.wav', bytes: noFmt, match: /no fmt chunk/ },
      { name: 'compressed.wav', bytes: compressed, match: /compressed/ },
      { name: 'huge-chunk.wav', bytes: hugeChunk, match: /claims \d+ bytes/ },
      { name: 'zero-rate.wav', bytes: zeroRate, match: /sample rate of 0/ },
      { name: 'odd-depth.wav', bytes: oddDepth, match: /13-bit/ },
      { name: 'empty-data.wav', bytes: emptyData, match: /no audio/ },
    ];
  })();

  for (const b of bad) {
    it(`refuses ${b.name}`, () => {
      expect(() => readAudioFile(new Uint8Array(b.bytes))).toThrow(AudioFileError);
      expect(() => readAudioFile(new Uint8Array(b.bytes))).toThrow(b.match);
    });
  }

  it('reports an unreadable file in the scan instead of losing the whole drop', () => {
    writeWav(dir, 'Good_C3_mf.wav', tone(midiHz(48), 1, 44100));
    writeFileSync(join(dir, 'Broken_E3_mf.wav'), Buffer.alloc(300, 9));
    writeFileSync(join(dir, 'Lossy_G3_mf.mp3'), Buffer.alloc(300, 9));

    const scanned = scanDroppedFiles(listWavs(dir).concat(join(dir, 'Lossy_G3_mf.mp3')), nodeReadFs, {
      skipPitchDetection: true,
    });
    expect(scanned).toHaveLength(3);
    expect(scanned.find((f) => f.fileName.startsWith('Good'))!.error).toBeNull();
    expect(scanned.find((f) => f.fileName.startsWith('Broken'))!.error).toMatch(/Not a RIFF/);
    expect(scanned.find((f) => f.fileName.endsWith('.mp3'))!.error).toMatch(/lossy/i);

    // the readable file still converts
    const plan = planDrop(scanned);
    expect(plan.usable).toBe(true);
    expect(plan.sampleCount).toBe(1);
    expect(plan.excludedCount).toBe(2);
  });

  it('accepts WAVE_FORMAT_EXTENSIBLE as the PCM it is', () => {
    const good = Buffer.from(encodeIntPcm([tone(440, 0.2, 44100)], 44100, 24));
    // rebuild as extensible: 40-byte fmt chunk with a PCM SubFormat GUID
    const fmtBody = Buffer.alloc(40);
    good.copy(fmtBody, 0, 20, 36);
    fmtBody.writeUInt16LE(0xfffe, 0);
    fmtBody.writeUInt16LE(22, 16); // cbSize
    fmtBody.writeUInt16LE(24, 18); // valid bits
    fmtBody.writeUInt16LE(1, 24); // SubFormat GUID: PCM
    const out = Buffer.concat([
      good.subarray(0, 12),
      Buffer.from('fmt '),
      (() => {
        const b = Buffer.alloc(4);
        b.writeUInt32LE(40);
        return b;
      })(),
      fmtBody,
      good.subarray(36),
    ]);
    out.writeUInt32LE(out.length - 8, 4);
    const info = readAudioFile(new Uint8Array(out));
    expect(info.encoding).toBe('int');
    expect(info.bitDepth).toBe(24);
    expect(info.sampleRate).toBe(44100);
  });
});

/* ================================================================== */
/* 3. pitch-detection fallback                                         */
/* ================================================================== */

describe('pitch fallback — files with no pitch in the name', () => {
  it('derives every root note from the audio and gets it right', () => {
    const dir = scratch('pitch');
    const notes = [36, 43, 48, 55, 60, 67, 72, 79];
    notes.forEach((n, i) => writeWav(dir, `untitled_take${String(i + 1).padStart(2, '0')}.wav`, tone(midiHz(n), 1.5, 44100)));

    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs);
    expect(scanned.every((f) => f.error === null)).toBe(true);
    // nothing in these names is a pitch
    expect(scanned.every((f) => f.parse.note === null)).toBe(true);

    const detected = scanned.map((f) => f.detected);
    expect(detected.every((d) => d !== null)).toBe(true);
    expect(detected.map((d) => d!.note)).toEqual(notes);
    // the detector is accurate to a fraction of a cent on clean material
    for (const d of detected) expect(Math.abs(d!.cents)).toBeLessThan(5);

    const plan = planDrop(scanned);
    expect(plan.pitchFallbackCount).toBe(notes.length);
    expect(plan.roots).toEqual(notes);
    expect(plan.rows.every((r) => r.noteSource === 'pitch-detect')).toBe(true);
    expect(plan.notes.join(' ')).toMatch(/root notes came from the pitch detector/);
  });

  it('leaves out a file with neither a name pitch nor a detectable one', () => {
    const dir = scratch('noise');
    writeWav(dir, 'Kit_C3_mf.wav', tone(midiHz(48), 1, 44100));
    // white noise: no stable fundamental anywhere in it
    const n = 44100;
    const noise = new Float32Array(n);
    let seed = 12345;
    for (let i = 0; i < n; i++) {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff;
      noise[i] = (seed / 0x7fffffff - 0.5) * 0.5;
    }
    writeWav(dir, 'crash.wav', noise);

    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs);
    const plan = planDrop(scanned);
    const crash = plan.rows.find((r) => r.fileName === 'crash.wav')!;
    expect(crash.note).toBeNull();
    expect(crash.excludedReason).toMatch(/no stable pitch in the audio/);
    expect(plan.usable).toBe(true); // the other file still converts
  });

  it('lets the audio overrule a bare MIDI number the name guessed at', () => {
    const dir = scratch('override');
    // the name says 60, the audio is unmistakably C3 (48)
    writeWav(dir, 'EP-60-velo2.wav', tone(midiHz(48), 1.5, 44100));
    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs);
    expect(scanned[0].parse.noteSource).toBe('midi-number');
    expect(scanned[0].detected!.note).toBe(48);
    const plan = planDrop(scanned);
    expect(plan.rows[0].note).toBe(48);
    expect(plan.rows[0].noteSource).toBe('pitch-detect');
  });

  it('spots a whole set written to the C3=60 convention', () => {
    const dir = scratch('octave');
    // named an octave below what they sound: "C3" holding middle C
    const named = [
      ['C3', 60],
      ['E3', 64],
      ['G3', 67],
      ['C4', 72],
    ] as const;
    for (const [label, sounding] of named) writeWav(dir, `Yam_${label}_mf.wav`, tone(midiHz(sounding), 1.5, 44100));

    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs);
    expect(suggestOffset(scanned, 0)).toBe(12);

    const plan = planDrop(scanned);
    expect(plan.suggestedSemitoneOffset).toBe(12);
    expect(plan.warnings.join(' ')).toMatch(/octave/);
    // applying it files them under what they sound
    const shifted = planDrop(scanned, { semitoneOffset: 12 });
    expect(shifted.roots).toEqual([60, 64, 67, 72]);
    expect(shifted.suggestedSemitoneOffset).toBeNull();
  });

  it('says nothing about the octave when the names and the audio agree', () => {
    const dir = scratch('agree');
    for (const n of [48, 52, 55, 60]) writeWav(dir, `Ok_${midiToNoteName(n)}_mf.wav`, tone(midiHz(n), 1.5, 44100));
    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs);
    expect(suggestOffset(scanned, 0)).toBeNull();
    expect(planDrop(scanned).pitchFallbackCount).toBe(0);
  });
});

/* ================================================================== */
/* 4. mixed formats                                                    */
/* ================================================================== */

describe('mixed-rate / mixed-depth drops', () => {
  it('names every format, picks one, and never converts without saying so', () => {
    const dir = scratch('mixed');
    writeWav(dir, 'Mix_C3_mf.wav', tone(midiHz(48), 1, 44100), { sampleRate: 44100, bitDepth: 16 });
    writeWav(dir, 'Mix_E3_mf.wav', tone(midiHz(52), 1, 44100), { sampleRate: 44100, bitDepth: 16 });
    writeWav(dir, 'Mix_G3_mf.wav', tone(midiHz(55), 1, 44100), { sampleRate: 44100, bitDepth: 16 });
    writeWav(dir, 'Mix_A3_mf.wav', tone(midiHz(57), 1, 48000), { sampleRate: 48000, bitDepth: 16 });
    writeWav(dir, 'Mix_C4_mf.wav', tone(midiHz(60), 1, 44100), { sampleRate: 44100, bitDepth: 24 });
    writeWav(dir, 'Mix_E4_mf.wav', tone(midiHz(64), 1, 44100), { sampleRate: 44100, bitDepth: 16, channels: 2 });

    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true });
    const census = censusFormats(scanned);
    expect(census.mixed).toBe(true);
    expect(census.groups).toHaveLength(4);
    // the majority group wins
    expect(census.target).toMatchObject({ sampleRate: 44100, bitDepth: 16, channels: 1, encoding: 'int' });
    expect(census.notes.join(' ')).toMatch(/4 different audio formats/);
    expect(census.notes.join(' ')).toMatch(/cannot join a 44100 Hz bundle without resampling/);

    const byName = (n: string) => scanned.find((f) => f.fileName === n)!.index;
    expect(census.verdicts[byName('Mix_C3_mf.wav')]).toBe('passthrough');
    expect(census.verdicts[byName('Mix_A3_mf.wav')]).toBe('rate-mismatch');
    expect(census.verdicts[byName('Mix_C4_mf.wav')]).toBe('convertible');
    expect(census.verdicts[byName('Mix_E4_mf.wav')]).toBe('convertible');

    // by default nothing is converted: the odd ones out are left out, stated
    const strict = planDrop(scanned);
    expect(strict.convertCount).toBe(0);
    expect(strict.passthroughCount).toBe(3);
    expect(strict.excludedCount).toBe(3);
    const rateRow = strict.rows.find((r) => r.fileName === 'Mix_A3_mf.wav')!;
    expect(rateRow.excludedReason).toMatch(/resampling it, which this importer will not do/);

    // opting in converts the depth/channel outliers and says so per file
    const loose = planDrop(scanned, { convertOutliers: true });
    expect(loose.convertCount).toBe(2);
    expect(loose.passthroughCount).toBe(3);
    expect(loose.rows.find((r) => r.fileName === 'Mix_C4_mf.wav')!.convertReason).toMatch(
      /NOT a bit-perfect passthrough/,
    );
    // the sample-rate outlier is still refused, even when converting is on
    expect(loose.rows.find((r) => r.fileName === 'Mix_A3_mf.wav')!.excludedReason).toMatch(/resampling/);
  });

  it('lets the user choose the other half of a split drop', () => {
    const dir = scratch('pick');
    writeWav(dir, 'S_C3_mf.wav', tone(midiHz(48), 1, 44100), { sampleRate: 44100 });
    writeWav(dir, 'S_E3_mf.wav', tone(midiHz(52), 1, 96000), { sampleRate: 96000 });
    writeWav(dir, 'S_G3_mf.wav', tone(midiHz(55), 1, 96000), { sampleRate: 96000 });

    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true });
    expect(planDrop(scanned).format.sampleRate).toBe(96000); // the majority
    const picked = planDrop(scanned, {
      targetFormat: { sampleRate: 44100, bitDepth: 16, channels: 1, encoding: 'int' },
    });
    expect(picked.format.sampleRate).toBe(44100);
    expect(picked.sampleCount).toBe(1);
  });

  it('refuses rates and depths the schema does not permit, and says which', () => {
    expect(schemaObjection({ sampleRate: 22050, bitDepth: 16, channels: 1, encoding: 'int' })).toMatch(
      /22050 Hz is not one of the rates/,
    );
    expect(schemaObjection({ sampleRate: 44100, bitDepth: 8, channels: 1, encoding: 'int' })).toMatch(
      /8-bit is not one of the depths/,
    );
    expect(schemaObjection({ sampleRate: 44100, bitDepth: 32, channels: 1, encoding: 'float' })).toMatch(
      /Floating-point WAV is rejected/,
    );
    expect(schemaObjection({ sampleRate: 44100, bitDepth: 24, channels: 4, encoding: 'int' })).toMatch(
      /4 channels/,
    );
    expect(schemaObjection({ sampleRate: 96000, bitDepth: 24, channels: 2, encoding: 'int' })).toBeNull();
  });

  it('flags a 22.05 kHz drop as impossible to pass through untouched', () => {
    const dir = scratch('lowrate');
    writeWav(dir, 'Old_C3_mf.wav', tone(midiHz(48), 1, 22050), { sampleRate: 22050 });
    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true });
    const census = censusFormats(scanned);
    expect(census.groups[0].schemaLegal).toBe(false);
    expect(census.notes.join(' ')).toMatch(/None of the dropped files is in a format the .jmi schema permits/);
    expect(planDrop(scanned).usable).toBe(false);
  });

  it('converts a 32-bit float file to integer PCM when asked, never silently', () => {
    const dir = scratch('float');
    writeWav(dir, 'F_C3_mf.wav', tone(midiHz(48), 0.5, 48000), { sampleRate: 48000, float: true });
    writeWav(dir, 'F_E3_mf.wav', tone(midiHz(52), 0.5, 48000), { sampleRate: 48000, bitDepth: 24 });
    writeWav(dir, 'F_G3_mf.wav', tone(midiHz(55), 0.5, 48000), { sampleRate: 48000, bitDepth: 24 });

    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true });
    expect(scanned.find((f) => f.fileName === 'F_C3_mf.wav')!.audio!.encoding).toBe('float');
    const plan = planDrop(scanned, { convertOutliers: true });
    expect(plan.format.encoding).toBe('int');
    expect(plan.rows.find((r) => r.fileName === 'F_C3_mf.wav')!.convertReason).toMatch(/NOT a bit-perfect/);

    const out = scratch('float-out');
    const bundle = convertDrop(plan, out, nodeConvertFs, { author: 'Test', sourceLabel: '3 audio files' });
    expect(bundle.convertedCount).toBe(1);
    expect(bundle.passthroughCount).toBe(2);
    const result = validateBundle(bundle.bundlePath);
    expect(result.issues.filter((i) => i.level === 'error')).toEqual([]);
    expect(result.valid).toBe(true);
  });
});

/* ================================================================== */
/* 5. velocity layers                                                  */
/* ================================================================== */

describe('velocity layers', () => {
  it('tiles 1..127 with no gap for any layer count', () => {
    for (let n = 1; n <= 16; n++) {
      const w = windowsForGroups(Array.from({ length: n }, (_, i) => [i]));
      expect(w[0].lo).toBe(1);
      expect(w[w.length - 1].hi).toBe(127);
      for (let i = 1; i < w.length; i++) expect(w[i].lo).toBe(w[i - 1].hi + 1);
    }
  });

  it('folds above sixteen layers by pairing the closest in level, dropping nothing', () => {
    const levels = [-40, -36, -32, -28, -24, -22, -21.5, -20, -18, -16, -14, -12, -10, -8, -6, -4, -2];
    const { groups, folds } = foldLayers(levels);
    expect(groups).toHaveLength(16);
    expect(folds).toHaveLength(1);
    // -22 and -21.5 are 0.5 dB apart — the closest pair
    expect(folds[0]).toMatch(/0.5 dB apart/);
    expect(groups.flat().sort((a, b) => a - b)).toEqual(levels.map((_, i) => i));
  });

  it('maps dynamics words to ascending layers and keeps round robins', () => {
    const dir = scratch('layers');
    for (const n of [48, 55]) {
      for (const dyn of ['pp', 'mf', 'ff']) {
        for (const rr of [1, 2]) {
          writeWav(dir, `Pno_${midiToNoteName(n)}_${dyn}_rr${rr}.wav`, tone(midiHz(n), 0.6, 44100));
        }
      }
    }
    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true });
    const plan = planDrop(scanned, { name: 'Pno' });
    expect(plan.velocityGroups.map((g) => g.label)).toEqual(['pp', 'mf', 'ff']);
    expect(plan.windows.map((w) => `${w.name} ${w.lo}-${w.hi}`)).toEqual([
      'Soft 1-42',
      'Medium 43-85',
      'Hard 86-127',
    ]);
    expect(plan.zones).toHaveLength(6); // 2 roots x 3 layers
    expect(plan.zones.every((z) => z.samples.length === 2)).toBe(true);
    expect(plan.zones[0].samples.map((s) => s.file)).toEqual([
      'samples/C3_v1_rr1.wav',
      'samples/C3_v1_rr2.wav',
    ]);
  });

  it('is a single full-range layer when nothing carries a velocity', () => {
    const dir = scratch('onelayer');
    for (const n of [48, 60]) writeWav(dir, `Solo_${midiToNoteName(n)}.wav`, tone(midiHz(n), 0.6, 44100));
    const plan = planDrop(scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true }));
    expect(plan.windows).toEqual([
      expect.objectContaining({ layer: 1, lo: 1, hi: 127, sourceLabels: ['Unspecified'] }),
    ]);
    expect(plan.warnings.join(' ')).not.toMatch(/no velocity marking/);
  });

  it('tiles key ranges across the roots that are present', () => {
    const dir = scratch('keys');
    for (const n of [36, 48, 60, 72]) writeWav(dir, `K_${midiToNoteName(n)}_mf.wav`, tone(midiHz(n), 0.5, 44100));
    const plan = planDrop(scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true }));
    expect(plan.zones.map((z) => [z.keyRangeLo, z.rootNote, z.keyRangeHi])).toEqual([
      [0, 36, 41],
      [42, 48, 53],
      [54, 60, 65],
      [66, 72, 127],
    ]);
    // no key is unmapped and no two zones of one layer overlap
    for (let i = 1; i < plan.zones.length; i++) {
      expect(plan.zones[i].keyRangeLo).toBe(plan.zones[i - 1].keyRangeHi + 1);
    }
  });
});

/* ================================================================== */
/* 6. bit-perfect passthrough + a valid bundle                         */
/* ================================================================== */

describe('conversion — bit-perfect, and it validates', () => {
  it('writes PCM byte-identical to the source, with a canonical header', () => {
    const dir = scratch('bitperfect');
    const sources: string[] = [];
    for (const n of [40, 48, 56, 64]) {
      for (const dyn of ['p', 'f']) {
        sources.push(writeWav(dir, `BP_${midiToNoteName(n)}_${dyn}.wav`, tone(midiHz(n), 0.7, 48000), {
          sampleRate: 48000,
          bitDepth: 24,
          channels: 2,
        }));
      }
    }
    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true });
    const plan = planDrop(scanned, { name: 'BitPerfect' });
    expect(plan.passthroughCount).toBe(8);
    expect(plan.convertCount).toBe(0);
    expect(plan.format).toMatchObject({ sampleRate: 48000, bitDepth: 24, channels: 2, encoding: 'int' });

    const out = scratch('bitperfect-out');
    const bundle = convertDrop(plan, out, nodeConvertFs, {
      author: 'JM Studios',
      sourceLabel: '8 audio files',
    });
    expect(bundle.passthroughCount).toBe(8);

    // THE assertion: every written payload equals its source payload, byte for byte
    let compared = 0;
    for (const z of plan.zones) {
      for (const s of z.samples) {
        const written = pcmPayload(join(bundle.bundlePath, ...s.file.split('/')));
        const source = pcmPayload(plan.sourcePaths[s.fileIndex]);
        expect(written.length).toBe(source.length);
        expect(written.equals(source)).toBe(true);
        compared++;
      }
    }
    expect(compared).toBe(8);

    // and the header is the canonical 44 bytes, nothing else
    const first = new Uint8Array(readFileSync(join(bundle.bundlePath, 'samples', 'E2_v1_rr1.wav')));
    expect(readAudioFile(first).dataOffset).toBe(44);

    const result = validateBundle(bundle.bundlePath);
    expect(result.issues.filter((i) => i.level === 'error')).toEqual([]);
    expect(result.valid).toBe(true);
    expect(result.stats!.sampleCount).toBe(8);
    expect(result.stats!.presetCount).toBe(2);

    const inst = JSON.parse(readFileSync(join(bundle.bundlePath, 'instrument.json'), 'utf8'));
    expect(inst.meta).toMatchObject({
      name: 'BitPerfect',
      captureMode: 'auto',
      channels: 'stereo',
      sampleRate: 48000,
      bitDepth: 24,
    });
    expect(inst.meta.notes).toMatch(/copied through unchanged/);
    expect(inst.meta.notes).toMatch(/bit-perfect/);
    expect(inst.meta.notes).toMatch(/this bundle was not recorded/);
    expect(inst.soundType).toBe('decaying');
    expect(inst.zones.every((z: { roundRobins: { loop: unknown }[] }) => z.roundRobins.every((r) => r.loop === null))).toBe(true);
    expect(sources.every((p) => existsSync(p))).toBe(true); // sources untouched
  });

  it('measures peakDb and lengthMs off the real audio', () => {
    const dir = scratch('facts');
    const signal = tone(midiHz(60), 1.234, 44100, 0.5);
    writeWav(dir, 'M_C4_mf.wav', signal);
    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true });
    const plan = planDrop(scanned);
    const out = scratch('facts-out');
    const bundle = convertDrop(plan, out, nodeConvertFs, { author: 'T', sourceLabel: '1 audio file' });

    const inst = JSON.parse(readFileSync(join(bundle.bundlePath, 'instrument.json'), 'utf8'));
    const rr = inst.zones[0].roundRobins[0];
    const bytes = new Uint8Array(readFileSync(join(bundle.bundlePath, ...rr.file.split('/'))));
    const info = readAudioFile(bytes);
    expect(rr.lengthMs).toBeCloseTo(1234, 0);
    expect(rr.peakDb).toBeCloseTo(measurePcm(bytes, info).peakDb, 5);
    expect(rr.peakDb).toBeLessThan(0);
    expect(rr.peakDb).toBeGreaterThan(-20);
  });

  it('finds loop points for sustaining material and fabricates none for one-shots', () => {
    const dir = scratch('sustain');
    for (const n of [48, 55]) writeWav(dir, `Pad_${midiToNoteName(n)}_mf.wav`, sustained(midiHz(n), 6, 44100));
    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true });
    const plan = planDrop(scanned, { name: 'Pad' });
    expect(plan.soundType).toBe('sustaining');
    expect(plan.soundTypeReason).toMatch(/still sounding at the end/);

    const out = scratch('sustain-out');
    const bundle = convertDrop(plan, out, nodeConvertFs, { author: 'T', sourceLabel: '2 audio files' });
    expect(bundle.loopedCount).toBe(2);
    const inst = JSON.parse(readFileSync(join(bundle.bundlePath, 'instrument.json'), 'utf8'));
    for (const z of inst.zones) {
      for (const rr of z.roundRobins) {
        expect(rr.loop).not.toBeNull();
        expect(rr.loop.endSample).toBeGreaterThan(rr.loop.startSample);
      }
    }
    const result = validateBundle(bundle.bundlePath);
    expect(result.issues.filter((i) => i.level === 'error')).toEqual([]);

    // and the same files, forced to decaying, get no loops at all
    const oneShot = planDrop(scanned, { name: 'Pad', soundType: 'decaying' });
    const out2 = scratch('oneshot-out');
    const b2 = convertDrop(oneShot, out2, nodeConvertFs, { author: 'T', sourceLabel: '2 audio files' });
    expect(b2.loopedCount).toBe(0);
    const inst2 = JSON.parse(readFileSync(join(b2.bundlePath, 'instrument.json'), 'utf8'));
    expect(inst2.zones.every((z: { roundRobins: { loop: unknown }[] }) => z.roundRobins.every((r) => r.loop === null))).toBe(true);
  });

  it('honours a per-file correction made in the mapping table', () => {
    const dir = scratch('override2');
    for (const n of [48, 55, 60]) writeWav(dir, `O_${midiToNoteName(n)}_mf.wav`, tone(midiHz(n), 0.5, 44100));
    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true });
    const byName = (n: string) => scanned.find((f) => f.fileName === n)!.index;

    const plan = planDrop(scanned, {
      overrides: {
        [byName('O_G3_mf.wav')]: { note: 53 },
        [byName('O_C4_mf.wav')]: { exclude: true },
      },
    });
    expect(plan.roots).toEqual([48, 53]);
    expect(plan.rows.find((r) => r.fileName === 'O_G3_mf.wav')!.noteSource).toBe('manual');
    expect(plan.rows.find((r) => r.fileName === 'O_C4_mf.wav')!.excludedReason).toBe('Left out by you.');
    expect(plan.sampleCount).toBe(2);
  });

  it('never collides two samples on one destination name', () => {
    const dir = scratch('collide');
    // three files that all infer the same note and the same layer
    for (const tag of ['a', 'b', 'c']) writeWav(dir, `X_C3_mf_${tag}.wav`, tone(midiHz(48), 0.4, 44100));
    const plan = planDrop(scanDroppedFiles(listWavs(dir), nodeReadFs, { skipPitchDetection: true }));
    const files = plan.zones.flatMap((z) => z.samples.map((s) => s.file));
    expect(new Set(files).size).toBe(files.length);
    expect(files).toEqual(['samples/C3_v1_rr1.wav', 'samples/C3_v1_rr2.wav', 'samples/C3_v1_rr3.wav']);
  });
});

/* ================================================================== */
/* 7. the client's real audio                                          */
/* ================================================================== */

describe.skipIf(!existsSync(CLIENT_LIBRARY))("the client's own samples, end to end", () => {
  /** Copy a bundle's samples out to a scratch folder, as a user would. */
  function stage(bundle: string, tag: string, limit?: number): string {
    const src = join(CLIENT_LIBRARY, bundle, 'samples');
    const dir = scratch(tag);
    const names = readdirSync(src)
      .filter((f) => f.toLowerCase().endsWith('.wav'))
      .sort();
    for (const f of limit ? names.slice(0, limit) : names) cpSync(join(src, f), join(dir, f));
    return dir;
  }

  for (const bundle of ['MKS20.jmi', 'EP2.jmi', 'Piano.jmi']) {
    it.skipIf(!existsSync(join(CLIENT_LIBRARY, bundle)))(`round-trips ${bundle}`, () => {
      const original = JSON.parse(readFileSync(join(CLIENT_LIBRARY, bundle, 'instrument.json'), 'utf8'));
      const dir = stage(bundle, bundle.replace(/\W/g, ''));
      const paths = listWavs(dir);
      expect(paths.length).toBeGreaterThan(0);

      const scanned = scanDroppedFiles(paths, nodeReadFs);
      const unreadable = scanned.filter((f) => f.error);
      expect(unreadable.map((f) => `${f.fileName}: ${f.error}`)).toEqual([]);

      const plan = planDrop(scanned, { name: original.meta.name, folderName: original.meta.name });

      // the format comes back exactly as it went in — nothing converted
      expect(plan.format.sampleRate).toBe(original.meta.sampleRate);
      expect(plan.format.bitDepth).toBe(original.meta.bitDepth);
      expect(plan.format.channels).toBe(original.meta.channels === 'stereo' ? 2 : 1);
      expect(plan.convertCount).toBe(0);
      expect(plan.excludedCount).toBe(0);
      expect(plan.passthroughCount).toBe(paths.length);

      // the `.jmi` names round-trip: same roots, same layers, same zones
      expect(plan.roots).toEqual([...new Set(original.zones.map((z: { rootNote: number }) => z.rootNote))].sort(
        (a, b) => (a as number) - (b as number),
      ));
      expect(plan.windows).toHaveLength(original.velocityMap.length);
      expect(plan.zones).toHaveLength(original.zones.length);
      expect(plan.pitchFallbackCount).toBe(0); // every name carried its note

      const out = scratch(`${bundle.replace(/\W/g, '')}-out`);
      const written = convertDrop(plan, out, nodeConvertFs, {
        author: original.meta.author,
        sourceLabel: `the folder "${original.meta.name}" (${paths.length} audio files)`,
      });

      const result = validateBundle(written.bundlePath);
      expect(result.issues.filter((i) => i.level === 'error')).toEqual([]);
      expect(result.valid).toBe(true);
      expect(result.stats!.sampleCount).toBe(paths.length);

      // bit-perfect against the client's own files
      let bytes = 0;
      for (const z of plan.zones) {
        for (const s of z.samples) {
          const a = pcmPayload(join(written.bundlePath, ...s.file.split('/')));
          const b = pcmPayload(plan.sourcePaths[s.fileIndex]);
          expect(a.equals(b)).toBe(true);
          bytes += a.length;
        }
      }
      expect(bytes).toBeGreaterThan(0);
    });
  }

  it('detects the right pitch on real samples whose names are stripped', () => {
    const src = join(CLIENT_LIBRARY, 'MKS20.jmi', 'samples');
    if (!existsSync(src)) return;
    // the loudest layer of a spread of real notes, renamed to carry no pitch
    const picks = readdirSync(src)
      .filter((f) => /_v4_rr1\.wav$/.test(f))
      .sort();
    const dir = scratch('blind');
    const expected: number[] = [];
    picks.forEach((f, i) => {
      const note = f.replace(/_v\d+_rr\d+\.wav$/, '');
      expected.push(noteNameToMidiLocal(note));
      cpSync(join(src, f), join(dir, `unnamed_take${String(i + 1).padStart(3, '0')}.wav`));
    });

    const scanned = scanDroppedFiles(listWavs(dir), nodeReadFs);
    expect(scanned.every((f) => f.parse.note === null)).toBe(true);

    let right = 0;
    let octaveOut = 0;
    const errors: number[] = [];
    scanned.forEach((f, i) => {
      if (!f.detected) return;
      const d = f.detected.note - expected[i];
      if (d === 0) {
        right++;
        errors.push(Math.abs(f.detected.cents));
      } else if (Math.abs(d) % 12 === 0) octaveOut++;
    });
    const detected = scanned.filter((f) => f.detected).length;
    // eslint-disable-next-line no-console
    console.log(
      `[real-audio pitch] ${detected}/${scanned.length} detected, ${right} exactly right, ${octaveOut} an octave out, mean |cents| on the exact ones ${(errors.reduce((a, b) => a + b, 0) / Math.max(1, errors.length)).toFixed(3)}`,
    );
    expect(detected / scanned.length).toBeGreaterThan(0.7);
    expect((right + octaveOut) / Math.max(1, detected)).toBeGreaterThan(0.85);
  });
});

/** Local note-name reader, so this test does not depend on the parser it tests. */
function noteNameToMidiLocal(name: string): number {
  const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const m = /^([A-G]#?)(-?\d+)$/.exec(name);
  if (!m) throw new Error(`bad note ${name}`);
  return names.indexOf(m[1]) + (parseInt(m[2], 10) + 1) * 12;
}

/* ================================================================== */
/* 8. the decode path is analysis only                                 */
/* ================================================================== */

describe('decode is for analysis only', () => {
  it('round-trips 16 / 24 / 32-bit and float within their own resolution', () => {
    const sig = tone(220, 0.1, 44100, 0.8);
    for (const bitDepth of [16, 24, 32]) {
      const wav = encodeIntPcm([sig], 44100, bitDepth);
      const info = readAudioFile(wav);
      const back = decodeChannels(wav, info)[0];
      let worst = 0;
      for (let i = 0; i < sig.length; i++) worst = Math.max(worst, Math.abs(back[i] - sig[i]));
      expect(worst, `${bitDepth}-bit`).toBeLessThan(2 / Math.pow(2, bitDepth - 1));
    }
  });

  it('stops at maxFrames so a long sample costs nothing extra to analyse', () => {
    const wav = encodeIntPcm([tone(220, 5, 44100)], 44100, 16);
    const info = readAudioFile(wav);
    expect(decodeChannels(wav, info, { maxFrames: 1000 })[0]).toHaveLength(1000);
    expect(decodeChannels(wav, info)[0]).toHaveLength(info.frames);
  });
});
