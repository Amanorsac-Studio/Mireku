/**
 * X7L importer (§3.7 import path).
 *
 * Structure, rejection of malformed input, and the corroboration that matters:
 * the key map and the audio table must describe the same samples. The final
 * block converts the client's real library end to end — when that file is not
 * present the heavy tests skip rather than fail, so CI stays green.
 */

import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { validateBundle } from '@jm/jmi-format';
import {
  measureKeyzone,
  parseX7l,
  pcm16MonoWavHeader,
  representativeKeyzone,
  X7lFormatError,
  SUPPORTED_EXTENSIONS,
  UNVERIFIED_EXTENSIONS,
  X7lNoAudioError,
  X7lTruncatedError,
  keyzoneWav,
  type X7lLibrary,
} from '../src/engine/import/x7l';
import {
  buildImportInstrument,
  commonInstrumentName,
  inferWorld,
  findKeySplitGroups,
  foldSourceWindows,
  keyMapSignature,
  planImport,
  velocitySplits,
  velocityWindows,
  waveformStem,
} from '../src/engine/import/x7lPlan';
import { convertInstrument, type ImportFs } from '../src/engine/import/x7lConvert';

const REAL_LIBRARY = 'C:\\Users\\amano\\Downloads\\MKS20 EP1 EP2.X7L';

/* ------------------------------------------------------------------ */
/* a synthetic library, so structure is testable without the real file  */
/* ------------------------------------------------------------------ */

function be32(v: number): number[] {
  return [(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255];
}
function le32(v: number): number[] {
  return [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
}
function tag(s: string): number[] {
  return [...s].map((c) => c.charCodeAt(0));
}

interface FakeZone {
  lo: number;
  hi: number;
  root: number;
  samples: number;
  /** velocity window; defaults to the whole range */
  vlo?: number;
  vhi?: number;
  /** loop start (field +44); defaults to 0 = whole-sample loop / one-shot */
  loopStart?: number;
  /** loop end (field +48); defaults to samples - 1 */
  loopEnd?: number;
}
interface FakeWave {
  name: string;
  zones: FakeZone[];
  /** peak amplitude written into every sample of this waveform */
  amp: number;
  /** 1 = mono (default), 2 = stereo (each zone written as an L/R pair) */
  channels?: 1 | 2;
}

/**
 * Builds a minimal but structurally exact YSFC library in memory.
 *
 * Field placement matches the real files: the catalogue name sits at +22 after
 * a 2-byte category code, the loop start is at +44, and +56 holds the 16-bit
 * per-sample handle (NOT a loop point). A stereo waveform writes two
 * byte-identical keyzone records per zone — differing only in that handle —
 * and two consecutive PCM sub-records, left then right.
 */
function buildFakeLibrary(waves: FakeWave[], version = '4.0.5'): Uint8Array {
  const entryBodies: number[][] = [];
  const dwfmBodies: number[][] = [];
  const dwimBodies: number[][] = [];
  let handle = 1;

  for (const w of waves) {
    const ch = w.channels ?? 1;
    const keymap: number[] = le32(w.zones.length * ch);
    const audio: number[] = be32(w.zones.length * ch);
    for (const z of w.zones) {
      for (let c = 0; c < ch; c++) {
        const rec = new Array<number>(64).fill(0);
        rec[0] = z.lo;
        rec[1] = z.hi;
        rec[2] = z.vlo ?? 1;
        rec[3] = z.vhi ?? 127;
        rec[8] = z.root;
        rec[11] = ch;
        le32(44100).forEach((b, i) => (rec[36 + i] = b));
        le32(z.loopStart ?? 0).forEach((b, i) => (rec[44 + i] = b)); // loop start
        le32(z.loopEnd ?? z.samples - 1).forEach((b, i) => (rec[48 + i] = b)); // loop end
        le32(0).forEach((b, i) => (rec[52 + i] = b)); // start
        const h = handle++ & 0xffff;
        rec[56] = h & 255;
        rec[57] = (h >> 8) & 255; // per-sample handle (u16 LE)
        le32(z.samples).forEach((b, i) => (rec[60 + i] = b)); // length
        keymap.push(...rec);

        audio.push(...be32(z.samples * 2));
        for (let i = 0; i < z.samples; i++) {
          const v = Math.round((c === 0 ? w.amp : -w.amp) * Math.sin((i / 32) * Math.PI * 2));
          audio.push(v & 255, (v >> 8) & 255);
        }
      }
    }
    dwfmBodies.push(keymap);
    dwimBodies.push(audio);
  }

  // catalogue entries reference the DWIM records, so offsets must be computed
  // after the data block layout is known
  let rel = 12;
  const dwimOffsets: number[] = [];
  for (const body of dwimBodies) {
    dwimOffsets.push(rel);
    rel += 8 + body.length;
  }
  waves.forEach((w, i) => {
    const body: number[] = [
      ...be32(dwimBodies[i].length),
      ...be32(dwimOffsets[i]),
      0,
      1,
      0,
      i + 1,
      0, 0, 0, 0, 0, 0, 0, 0, // +12: eight zero bytes
      0x27, 0x21, // +20: category code, printable on purpose
      ...tag(w.name), // +22: the name
      0,
      0,
    ];
    entryBodies.push(body);
  });

  const block = (t: string, recTag: string, bodies: number[][]): number[] => {
    let size = 4; // the count field
    for (const b of bodies) size += 8 + b.length;
    const out = [...tag(t), ...be32(size), ...be32(bodies.length)];
    for (const b of bodies) out.push(...tag(recTag), ...be32(b.length), ...b);
    return out;
  };

  const ewim = block('EWIM', 'Entr', entryBodies);
  const dwfm = block('DWFM', 'Data', dwfmBodies);
  const dwim = block('DWIM', 'Data', dwimBodies);

  const header = new Array<number>(0x100).fill(0);
  tag('YAMAHA-YSFC').forEach((b, i) => (header[i] = b));
  tag(version).forEach((b, i) => (header[0x10 + i] = b));

  const ewimAt = header.length;
  const dwfmAt = ewimAt + ewim.length;
  const dwimAt = dwfmAt + dwfm.length;
  [
    ['EWIM', ewimAt],
    ['DWFM', dwfmAt],
    ['DWIM', dwimAt],
  ].forEach(([t, off], i) => {
    tag(t as string).forEach((b, k) => (header[0x40 + i * 8 + k] = b));
    be32(off as number).forEach((b, k) => (header[0x40 + i * 8 + 4 + k] = b));
  });

  return Uint8Array.from([...header, ...ewim, ...dwfm, ...dwim]);
}

const FAKE_ZONES: FakeZone[] = [
  { lo: 21, hi: 59, root: 40, samples: 512 },
  { lo: 60, hi: 108, root: 72, samples: 384 },
];

function fake(layerAmps: number[]): Uint8Array {
  return buildFakeLibrary(
    layerAmps.map((amp, i) => ({ name: `1:Fake EP L${i + 1}`, zones: FAKE_ZONES, amp })),
  );
}

/* ------------------------------------------------------------------ */

describe('X7L parser — structure', () => {
  it('reads magic, version, block table, key map and audio', () => {
    const lib = parseX7l(fake([2000, 9000]));
    expect(lib.version).toBe('4.0.5');
    expect(lib.blocks.map((b) => b.tag)).toEqual(['EWIM', 'DWFM', 'DWIM']);
    expect(lib.waveforms).toHaveLength(2);
    expect(lib.waveforms[0].name).toBe('Fake EP L1');
    expect(lib.waveforms[0].keyzones).toHaveLength(2);
    expect(lib.waveforms[0].keyzones[0]).toMatchObject({
      keyRangeLo: 21,
      keyRangeHi: 59,
      rootNote: 40,
      sampleRate: 44100,
      lengthSamples: 512,
      pcmBytes: 1024,
    });
    // 16-bit MONO: bytes are exactly 2 per declared sample, never 4
    for (const wf of lib.waveforms) {
      for (const z of wf.keyzones) expect(z.pcmBytes).toBe(z.lengthSamples * 2);
    }
  });

  it('treats loop-end-at-final-sample as a one-shot', () => {
    const lib = parseX7l(fake([2000, 9000]));
    expect(lib.waveforms.every((w) => w.oneShot)).toBe(true);
  });

  it('reads the PCM back bit-for-bit at the recorded offset', () => {
    const bytes = fake([9000]);
    const lib = parseX7l(bytes);
    const z = lib.waveforms[0].keyzones[0];
    const view = new DataView(bytes.buffer, bytes.byteOffset + z.pcmOffset, z.pcmBytes);
    for (let i = 0; i < 8; i++) {
      expect(view.getInt16(i * 2, true)).toBe(Math.round(9000 * Math.sin((i / 32) * Math.PI * 2)));
    }
  });
});

describe('X7L parser — rejects malformed input', () => {
  const good = fake([2000, 9000]);

  const mangle = (fn: (b: Uint8Array) => void): Uint8Array => {
    const copy = good.slice();
    fn(copy);
    return copy;
  };

  it('rejects a file that is not YSFC', () => {
    expect(() => parseX7l(mangle((b) => (b[0] = 0x58)))).toThrow(X7lFormatError);
    expect(() => parseX7l(mangle((b) => (b[0] = 0x58)))).toThrow(/YAMAHA-YSFC/);
  });

  it('rejects a truncated file', () => {
    expect(() => parseX7l(good.slice(0, 64))).toThrow(X7lFormatError);
  });

  it('rejects an unsupported version', () => {
    expect(() => parseX7l(mangle((b) => (b[0x10] = 0x33)))).toThrow(/version 3\.0\.5 is not supported/);
  });

  it('rejects a nonsense version string', () => {
    expect(() => parseX7l(mangle((b) => (b[0x10] = 0x5a)))).toThrow(/Unrecognised YSFC version/);
  });

  it('refuses a library with no audio, explaining it must be sampled off the hardware', () => {
    // blank the DWIM tag in the block table (third entry)
    const noAudio = mangle((b) => (b[0x40 + 16] = 0x41));
    expect(() => parseX7l(noAudio)).toThrow(X7lNoAudioError);
    expect(() => parseX7l(noAudio)).toThrow(/holds no sampled audio/);
    expect(() => parseX7l(noAudio)).toThrow(/sampled off the hardware/);
  });

  it('rejects a key map whose size is not a whole number of keyzones', () => {
    const broken = buildFakeLibrary([{ name: '1:Bad', zones: FAKE_ZONES, amp: 100 }]);
    // corrupt the DWFM record length so its body is 63 bytes short of a zone
    const dwfmTag = findTag(broken, 'DWFM');
    const recLenAt = dwfmTag + 12 + 4;
    broken[recLenAt + 3] -= 1;
    expect(() => parseX7l(broken)).toThrow(X7lFormatError);
  });

  it('rejects audio whose size disagrees with the key map', () => {
    const broken = fake([2000]);
    const dwfmTag = findTag(broken, 'DWFM');
    // halve the declared length of the first keyzone → the DWIM sub-record no
    // longer matches, which is exactly the cross-check that must fire
    const zoneAt = dwfmTag + 12 + 8 + 4;
    broken[zoneAt + 60] = 0; // length 256 instead of 512 …
    broken[zoneAt + 61] = 1;
    broken[zoneAt + 48] = 255; // … with the loop pulled inside it, so the
    broken[zoneAt + 49] = 0; //     key-map/audio cross-check is what fires
    broken[zoneAt + 56] = 51;
    broken[zoneAt + 57] = 0;
    expect(() => parseX7l(broken)).toThrow(/the key map says/);
  });

  it('rejects an out-of-range key range', () => {
    const broken = fake([2000]);
    const zoneAt = findTag(broken, 'DWFM') + 12 + 8 + 4;
    broken[zoneAt] = 90; // lo 90 > hi 59
    expect(() => parseX7l(broken)).toThrow(/impossible key range/);
  });

  it('rejects an unsupported sample rate', () => {
    const broken = fake([2000]);
    const zoneAt = findTag(broken, 'DWFM') + 12 + 8 + 4;
    broken[zoneAt + 36] = 0x11;
    expect(() => parseX7l(broken)).toThrow(/unsupported sample rate/);
  });
});

function findTag(b: Uint8Array, t: string): number {
  const want = [...t].map((c) => c.charCodeAt(0));
  for (let i = 0x100; i < b.length - 4; i++) {
    if (b[i] === want[0] && b[i + 1] === want[1] && b[i + 2] === want[2] && b[i + 3] === want[3]) return i;
  }
  throw new Error(`tag ${t} not found`);
}

describe('velocity windows', () => {
  it('tiles 1..127 with no gap or overlap', () => {
    for (const n of [1, 2, 3, 4, 5]) {
      const w = velocityWindows(n, null);
      expect(w[0].lo).toBe(1);
      expect(w[w.length - 1].hi).toBe(127);
      for (let i = 1; i < w.length; i++) expect(w[i].lo).toBe(w[i - 1].hi + 1);
    }
  });

  it('folds five source layers into four windows without dropping any', () => {
    const w = velocityWindows(5, 3);
    expect(w).toHaveLength(4);
    expect(w.flatMap((x) => x.sources).sort()).toEqual([0, 1, 2, 3, 4]);
    expect(w[3]).toMatchObject({ lo: 77, hi: 127, sources: [3, 4] });
  });

  it('refuses an impossible merge', () => {
    expect(() => velocityWindows(4, 3)).toThrow();
  });
});

describe('naming and grouping helpers', () => {
  it('finds the shared instrument name', () => {
    expect(commonInstrumentName(['MKS20 p', 'MKS20 mp', 'MKS20 f'])).toBe('MKS20');
    expect(commonInstrumentName(['EP2 pp', 'EP2 f'])).toBe('EP2');
    expect(commonInstrumentName(['Solo'])).toBe('Solo');
  });

  it('infers a world from the name', () => {
    expect(inferWorld('MKS20')).toBe('mks');
    expect(inferWorld('Rhodes Mk1')).toBe('rhodes-ep');
    expect(inferWorld('DX7 Bells')).toBe('fm');
    expect(inferWorld('Solo Strings')).toBe('strings');
  });

  it('fingerprints key maps so identical maps group together', () => {
    const lib = parseX7l(fake([2000, 9000]));
    expect(keyMapSignature(lib.waveforms[0])).toBe(keyMapSignature(lib.waveforms[1]));
  });
});

/* ------------------------------------------------------------------ */
/* conversion                                                          */
/* ------------------------------------------------------------------ */

interface Written {
  dirs: string[];
  files: Map<string, Uint8Array | string>;
}

function memFs(): { fs: ImportFs; out: Written } {
  const out: Written = { dirs: [], files: new Map() };
  return {
    out,
    fs: {
      mkdir: (d) => out.dirs.push(d),
      writeFile: (p, data) => out.files.set(p, data),
      join: (...parts) => parts.join('/'),
    },
  };
}

function levelsFor(lib: X7lLibrary, bytes: Uint8Array): number[] {
  return lib.waveforms.map((wf) => measureKeyzone(bytes, representativeKeyzone(wf)).peakDb);
}

describe('conversion to .jmi', () => {
  it('plans one bundle per instrument and every source sample lands somewhere', () => {
    const bytes = fake([2000, 6000, 12000, 20000, 28000]);
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    expect(plan.instruments).toHaveLength(1);
    const inst = plan.instruments[0];
    expect(inst.windows).toHaveLength(4);
    expect(inst.sampleCount).toBe(5 * FAKE_ZONES.length);
    // every source waveform is referenced
    const used = new Set(inst.zones.flatMap((z) => z.samples.map((s) => s.waveformIndex)));
    expect([...used].sort()).toEqual([0, 1, 2, 3, 4]);
  });

  it('writes bit-perfect 16-bit mono WAVs', () => {
    const bytes = fake([9000, 20000]);
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    const { fs, out } = memFs();
    const inst = plan.instruments[0];
    convertInstrument(bytes, lib, inst, '/out', fs, {
      author: 'test',
      sourceFileName: 'fake.X7L',
    });
    // the quietest layer's first keyzone — waveform 0, keyzone 0
    const first = inst.zones
      .flatMap((z) => z.samples)
      .find((s) => s.waveformIndex === 0 && s.keyzoneIndex === 0)!;
    const wav = out.files.get(`/out/${inst.bundleName}/${first.file}`) as Uint8Array;
    expect(wav).toBeInstanceOf(Uint8Array);
    const z = lib.waveforms[0].keyzones[0];
    expect(wav.length).toBe(44 + z.pcmBytes);
    expect(wav.subarray(0, 44)).toEqual(pcm16MonoWavHeader(z.pcmBytes, 44100));
    expect(wav.subarray(44)).toEqual(bytes.subarray(z.pcmOffset, z.pcmOffset + z.pcmBytes));
  });

  it('records the true provenance in meta.notes and keeps captureMode legal', () => {
    const bytes = fake([9000, 20000]);
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    const inst = buildImportInstrument(
      plan.instruments[0],
      Object.fromEntries(
        plan.instruments[0].zones.flatMap((z) =>
          z.samples.map((s) => [s.file, { peakDb: -6, lengthMs: 10, loop: null }]),
        ),
      ),
      { author: 'test', sourceFileName: 'fake.X7L', createdAt: '2026-01-01T00:00:00.000Z' },
    );
    expect(inst.meta.captureMode).toBe('auto');
    expect(inst.meta.notes).toMatch(/Imported from the Yamaha YSFC library "fake\.X7L"/);
    expect(inst.meta.notes).toMatch(/bit-perfect/);
    expect(inst.meta.channels).toBe('mono');
    expect(inst.meta.bitDepth).toBe(16);
    expect(inst.meta.sampleRate).toBe(44100);
  });
});

/* ------------------------------------------------------------------ */
/* the client's real library                                           */
/* ------------------------------------------------------------------ */

const haveReal = existsSync(REAL_LIBRARY);
const maybe = haveReal ? describe : describe.skip;

maybe("the client's MKS20 EP1 EP2.X7L", () => {
  let bytes: Uint8Array;
  let lib: X7lLibrary;
  let dir = '';

  beforeAll(() => {
    bytes = new Uint8Array(readFileSync(REAL_LIBRARY));
    lib = parseX7l(bytes);
    dir = mkdtempSync(join(tmpdir(), 'jm-x7l-'));
  }, 120_000);

  afterAll(() => {
    if (dir) rmSync(dir, { recursive: true, force: true });
  });

  it('parses nine waveforms across two instruments', () => {
    expect(lib.version).toBe('4.0.5');
    expect(lib.waveforms.map((w) => w.name)).toEqual([
      'MKS20 p',
      'MKS20 mp',
      'MKS20 mf',
      'MKS20 f',
      'EP2 pp',
      'EP2 p',
      'EP2 mp',
      'EP2 mf',
      'EP2 f',
    ]);
    const sigs = [...new Set(lib.waveforms.map(keyMapSignature))];
    expect(sigs).toHaveLength(2);
  });

  it('accounts for every byte of the file', () => {
    // the parser already asserts each DWIM record is fully consumed; this
    // checks the aggregate against the file on disk
    expect(lib.totalPcmBytes).toBeGreaterThan(178 * 1024 * 1024);
    expect(lib.totalPcmBytes).toBeLessThan(statSync(REAL_LIBRARY).size);
  });

  it('the key map corroborates the audio: zones tile the keyboard with no gap', () => {
    for (const wf of lib.waveforms) {
      const sorted = [...wf.keyzones].sort((a, b) => a.keyRangeLo - b.keyRangeLo);
      for (let i = 1; i < sorted.length; i++) {
        expect(sorted[i].keyRangeLo).toBe(sorted[i - 1].keyRangeHi + 1);
      }
      expect(sorted[0].keyRangeLo).toBe(21);
      expect(sorted[sorted.length - 1].keyRangeHi).toBe(108);
      for (const z of wf.keyzones) {
        expect(z.rootNote).toBeGreaterThanOrEqual(z.keyRangeLo);
        expect(z.rootNote).toBeLessThanOrEqual(z.keyRangeHi);
      }
    }
  });

  it('every keyzone declares a one-shot and its tail is digital silence', () => {
    expect(lib.waveforms.every((w) => w.oneShot)).toBe(true);
    for (const wf of lib.waveforms) {
      const m = measureKeyzone(bytes, representativeKeyzone(wf));
      expect(m.tailRmsDb).toBeLessThan(-70);
      expect(m.peakDb).toBeLessThanOrEqual(0);
    }
  });

  it('converts to two valid .jmi bundles', () => {
    const levels = levelsFor(lib, bytes);
    const plan = planImport(lib, levels);
    expect(plan.instruments.map((i) => i.name)).toEqual(['MKS20', 'EP2']);
    expect(plan.instruments[0].mergeAt).toBeNull();
    expect(plan.instruments[1].mergeAt).toBe(3);

    const nodeFs: ImportFs = {
      mkdir: (d) => mkdirSync(d, { recursive: true }),
      writeFile: (p, data) => {
        mkdirSync(dirname(p), { recursive: true });
        writeFileSync(p, typeof data === 'string' ? data : Buffer.from(data));
      },
      join: (...parts) => join(...parts),
    };

    for (const p of plan.instruments) {
      const r = convertInstrument(bytes, lib, p, dir, nodeFs, {
        author: 'JM Studios',
        sourceFileName: 'MKS20 EP1 EP2.X7L',
      });
      const v = validateBundle(r.bundlePath);
      expect(v.issues.filter((i) => i.level === 'error')).toEqual([]);
      expect(v.valid).toBe(true);
      expect(v.stats?.sampleCount).toBe(p.sampleCount);
    }

    // 4 layers × 24 keyzones, and 5 layers × 22 keyzones with one pair folded
    expect(plan.instruments[0].sampleCount).toBe(96);
    expect(plan.instruments[1].sampleCount).toBe(110);
  }, 600_000);
});

/* ------------------------------------------------------------------ */
/* regression coverage for the batch-run failure classes               */
/* ------------------------------------------------------------------ */

/** The 16-bit PCM frames of a WAV produced by the importer. */
function wavFrames(wav: Uint8Array): { channels: number; frames: number[][] } {
  const dv = new DataView(wav.buffer, wav.byteOffset, wav.byteLength);
  const channels = dv.getUint16(22, true);
  const dataBytes = dv.getUint32(40, true);
  const n = dataBytes / (2 * channels);
  const frames: number[][] = [];
  for (let i = 0; i < n; i++) {
    const f: number[] = [];
    for (let c = 0; c < channels; c++) f.push(dv.getInt16(44 + (i * channels + c) * 2, true));
    frames.push(f);
  }
  return { channels, frames };
}

describe('A — stereo keyzone pairs (the sample-name collision)', () => {
  const stereoLib = () =>
    buildFakeLibrary([{ name: '1:Fake Stereo p', zones: FAKE_ZONES, amp: 9000, channels: 2 }]);

  it('folds each left/right pair into ONE logical keyzone', () => {
    const lib = parseX7l(stereoLib());
    const wf = lib.waveforms[0];
    // two zones in the key map, four keyzone records in the file
    expect(wf.channels).toBe(2);
    expect(wf.keyzones).toHaveLength(FAKE_ZONES.length);
    for (const z of wf.keyzones) {
      expect(z.channels).toBe(2);
      expect(z.pcm).toHaveLength(2);
      expect(z.pcmBytes).toBe(z.lengthSamples * 2);
      expect(z.totalPcmBytes).toBe(z.lengthSamples * 4);
      expect(z.pcm[1].offset).toBeGreaterThan(z.pcm[0].offset);
    }
  });

  it('plans unique sample names — the collision cannot come back', () => {
    const bytes = stereoLib();
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    const inst = plan.instruments[0];
    const files = inst.zones.flatMap((z) => z.samples.map((s) => s.file));
    expect(new Set(files).size).toBe(files.length);
    // one sample per logical keyzone, not two
    expect(inst.sampleCount).toBe(FAKE_ZONES.length);
    expect(inst.channels).toBe(2);
  });

  it('writes a stereo WAV whose frames interleave the source channels bit-perfectly', () => {
    const bytes = stereoLib();
    const lib = parseX7l(bytes);
    const z = lib.waveforms[0].keyzones[0];
    const { channels, frames } = wavFrames(keyzoneWav(bytes, z));
    expect(channels).toBe(2);
    expect(frames).toHaveLength(z.lengthSamples);
    const left = new DataView(bytes.buffer, bytes.byteOffset + z.pcm[0].offset, z.pcm[0].bytes);
    const right = new DataView(bytes.buffer, bytes.byteOffset + z.pcm[1].offset, z.pcm[1].bytes);
    for (let i = 0; i < z.lengthSamples; i++) {
      expect(frames[i][0]).toBe(left.getInt16(i * 2, true));
      expect(frames[i][1]).toBe(right.getInt16(i * 2, true));
    }
    // the fixture's right channel really is different audio, so an interleave
    // that silently duplicated one channel would be caught here
    expect(frames.some((f) => f[0] !== f[1])).toBe(true);
  });

  it('declares the bundle stereo so the validator checks the WAVs', () => {
    const bytes = stereoLib();
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    const inst = buildImportInstrument(
      plan.instruments[0],
      Object.fromEntries(
        plan.instruments[0].zones.flatMap((z) =>
          z.samples.map((s) => [s.file, { peakDb: -6, lengthMs: 10, loop: null }]),
        ),
      ),
      { author: 'test', sourceFileName: 'fake.X7L' },
    );
    expect(inst.meta.channels).toBe('stereo');
  });

  it('refuses a pair marked stereo that does not describe one sample', () => {
    const broken = buildFakeLibrary([{ name: '1:Bad', zones: FAKE_ZONES, amp: 9000, channels: 2 }]);
    // change the right-hand record's root note: no longer the same sample
    const zoneAt = findTag(broken, 'DWFM') + 12 + 8 + 4;
    broken[zoneAt + 64 + 8] = 99;
    expect(() => parseX7l(broken)).toThrow(/left and right channels of one recording/);
  });

  it('refuses two keyzones claiming the same key and velocity slot', () => {
    const dup = buildFakeLibrary([
      { name: '1:Dup', zones: [FAKE_ZONES[0], { ...FAKE_ZONES[0], samples: 300 }], amp: 9000 },
    ]);
    expect(() => parseX7l(dup)).toThrow(/both claim key/);
  });
});

describe('A — velocity windows declared inside one key map', () => {
  /** One instrument, eight velocity windows per note, as Crystal Rhodes has. */
  const EIGHT: FakeZone[] = (
    [
      [1, 16],
      [17, 32],
      [33, 48],
      [49, 64],
      [65, 79],
      [80, 95],
      [96, 111],
      [112, 127],
    ] as [number, number][]
  ).flatMap(([vlo, vhi]) => [
    { lo: 21, hi: 59, root: 40, samples: 256, vlo, vhi },
    { lo: 60, hi: 108, root: 72, samples: 192, vlo, vhi },
  ]);

  const lib8 = () => buildFakeLibrary([{ name: '1:Eight', zones: EIGHT, amp: 9000 }]);

  it('reads the source velocity splits', () => {
    const lib = parseX7l(lib8());
    expect(velocitySplits(lib.waveforms[0]).map((w) => `${w.lo}-${w.hi}`)).toEqual([
      '1-16',
      '17-32',
      '33-48',
      '49-64',
      '65-79',
      '80-95',
      '96-111',
      '112-127',
    ]);
  });

  it('folds eight source windows into four that still tile 1..127', () => {
    const folded = foldSourceWindows([
      { lo: 1, hi: 16 },
      { lo: 17, hi: 32 },
      { lo: 33, hi: 48 },
      { lo: 49, hi: 64 },
      { lo: 65, hi: 79 },
      { lo: 80, hi: 95 },
      { lo: 96, hi: 111 },
      { lo: 112, hi: 127 },
    ]);
    expect(folded).toHaveLength(4);
    expect(folded[0].lo).toBe(1);
    expect(folded[3].hi).toBe(127);
    for (let i = 1; i < folded.length; i++) expect(folded[i].lo).toBe(folded[i - 1].hi + 1);
    // every source window is kept — no audio is dropped
    expect(folded.flatMap((w) => w.sources).sort((a, b) => a - b)).toEqual([0, 1, 2, 3, 4, 5, 6, 7]);
  });

  it('plans four layers, every source keyzone used, every sample name unique', () => {
    const bytes = lib8();
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    const inst = plan.instruments[0];
    expect(inst.windows).toHaveLength(4);
    expect(inst.sampleCount).toBe(EIGHT.length);
    const files = inst.zones.flatMap((z) => z.samples.map((s) => s.file));
    expect(new Set(files).size).toBe(files.length);
    // the zones carry the source's own velocity spans, not an even re-split
    expect(inst.windows.map((w) => `${w.lo}-${w.hi}`)).toEqual(['1-32', '33-64', '65-95', '96-127']);
    // and the two windows sharing a span are round robins of one zone
    expect(inst.zones[0].samples.map((s) => s.rr)).toEqual([1, 2]);
  });

  it('refuses in-map velocity windows that do not tile 1..127', () => {
    const bytes = buildFakeLibrary([
      {
        name: '1:Gappy',
        zones: [
          { lo: 21, hi: 108, root: 60, samples: 256, vlo: 1, vhi: 40 },
          { lo: 21, hi: 108, root: 60, samples: 256, vlo: 60, vhi: 127 },
        ],
        amp: 9000,
      },
    ]);
    const lib = parseX7l(bytes);
    expect(() => planImport(lib, levelsFor(lib, bytes))).toThrow(/do not tile 1\.\.127/);
  });
});

describe('A — grouping waveforms into instruments', () => {
  const ladder = (base: number, step: number, count: number, name: string): FakeWave => ({
    name,
    zones: Array.from({ length: count }, (_, i) => ({
      lo: base + i * step,
      hi: base + i * step + step - 1,
      root: base + i * step,
      samples: 128,
    })),
    amp: 9000,
  });

  it('merges key-split partners into one instrument covering the whole ladder', () => {
    // two waveforms, roots every 6 semitones, offset by 3 → a 3-semitone ladder
    const bytes = buildFakeLibrary([ladder(21, 6, 8, '1:Split A'), ladder(24, 6, 8, '1:Split B')]);
    const lib = parseX7l(bytes);
    expect(findKeySplitGroups(lib.waveforms)).toHaveLength(1);
    const plan = planImport(lib, levelsFor(lib, bytes));
    expect(plan.instruments).toHaveLength(1);
    const inst = plan.instruments[0];
    expect(inst.keyzoneCount).toBe(16);
    expect(inst.windows).toHaveLength(1);
    // the merged zones tile the span with no gap and no overlap
    const zones = [...inst.zones].sort((x, y) => x.keyRangeLo - y.keyRangeLo);
    for (let i = 1; i < zones.length; i++) {
      expect(zones[i].keyRangeLo).toBe(zones[i - 1].keyRangeHi + 1);
    }
    // and both source waveforms are used
    const used = [...new Set(inst.zones.flatMap((z) => z.samples.map((s) => s.waveformIndex)))].sort();
    expect(used).toEqual([0, 1]);
  });

  it('does not merge waveforms whose roots are not an interleaved ladder', () => {
    const bytes = buildFakeLibrary([ladder(21, 6, 8, '1:One'), ladder(22, 6, 8, '1:Two')]);
    const lib = parseX7l(bytes);
    // offset 1 of 6 with two members: the union is not an even 3-semitone ladder
    expect(findKeySplitGroups(lib.waveforms)).toHaveLength(0);
  });

  it('layers waveforms whose names differ only by a variant token', () => {
    const bytes = fake([2000, 9000]);
    const lib = parseX7l(bytes);
    expect(lib.waveforms.map((w) => w.name)).toEqual(['Fake EP L1', 'Fake EP L2']);
    const plan = planImport(lib, levelsFor(lib, bytes));
    expect(plan.instruments).toHaveLength(1);
    expect(plan.instruments[0].windows).toHaveLength(2);
  });

  it('keeps unrelated waveforms that share a key map as separate instruments', () => {
    // exactly the Montage Triton case: one key map, several unrelated names
    const bytes = buildFakeLibrary([
      { name: '1:Brass Low', zones: FAKE_ZONES, amp: 4000 },
      { name: '1:M1 Organ', zones: FAKE_ZONES, amp: 9000 },
      { name: '1:analogpad High', zones: FAKE_ZONES, amp: 14000 },
    ]);
    const lib = parseX7l(bytes);
    expect(keyMapSignature(lib.waveforms[0])).toBe(keyMapSignature(lib.waveforms[1]));
    const plan = planImport(lib, levelsFor(lib, bytes));
    expect(plan.instruments.map((i) => i.name)).toEqual(['Brass Low', 'M1 Organ', 'analogpad High']);
    for (const inst of plan.instruments) expect(inst.windows).toHaveLength(1);
  });

  it('reduces a name to the stem that identifies the instrument', () => {
    expect(waveformStem('MKS20 pn1')).toBe('MKS20');
    expect(waveformStem('MKS20 pn2')).toBe('MKS20');
    expect(waveformStem('MK 80 p')).toBe('MK 80');
    expect(waveformStem('MK 80 f')).toBe('MK 80');
    expect(waveformStem('DYNO 3')).toBe('DYNO');
    expect(waveformStem('Grand Lady D2')).toBe('Grand Lady');
    expect(waveformStem('AZ mp SE')).toBe('AZ SE');
    expect(waveformStem('AZ p SE')).toBe('AZ SE');
    expect(waveformStem('Strings MF')).toBe('Strings');
    // and the ones that must NOT collapse together
    expect(waveformStem('M1 Organ')).toBe('M1 Organ');
    expect(waveformStem('Brass Low')).not.toBe(waveformStem('Brass High'));
    expect(waveformStem('Stac P')).not.toBe(waveformStem('Strings P'));
  });
});

describe('B — a file that stops early', () => {
  it('names the shortfall instead of blaming the block layout', () => {
    const good = fake([2000, 9000]);
    // lop off the tail, exactly as an interrupted download does
    const short = good.slice(0, good.length - 600);
    let caught: unknown;
    try {
      parseX7l(short);
    } catch (e) {
      caught = e;
    }
    expect(caught).toBeInstanceOf(X7lTruncatedError);
    const err = caught as X7lTruncatedError;
    expect(err.message).toMatch(/incomplete/);
    expect(err.message).toMatch(/is missing/);
    expect(err.message).toMatch(/Re-download/);
    expect(err.declaredBytes).toBeGreaterThan(err.actualBytes);
    expect(err.actualBytes).toBe(short.length);
  });
});

describe('C — the field at +56 is a handle, not a loop start', () => {
  /**
   * The shape the batch run reported: a short sample whose whole length is the
   * loop, with a large value sitting at +56. Reading +56 as a 32-bit loop
   * start made it "loop 48016..30463 in 30464 samples" and the file was
   * refused; +44 is the real loop start and says 0.
   */
  const shortOneShot = () => {
    const b = buildFakeLibrary([
      { name: '1:Short', zones: [{ lo: 21, hi: 108, root: 60, samples: 30464 }], amp: 9000 },
    ]);
    const zoneAt = findTag(b, 'DWFM') + 12 + 8 + 4;
    // handle 48016 — larger than the sample is long
    b[zoneAt + 56] = 48016 & 255;
    b[zoneAt + 57] = (48016 >> 8) & 255;
    return b;
  };

  it('parses a whole-sample loop as a one-shot however large the handle is', () => {
    const lib = parseX7l(shortOneShot());
    const z = lib.waveforms[0].keyzones[0];
    expect(z.sampleId).toBe(48016);
    expect(z.loopStartSample).toBe(0);
    expect(z.loopEndSample).toBe(z.lengthSamples - 1);
    expect(lib.waveforms[0].oneShot).toBe(true);
  });

  it('writes loop: null for it', () => {
    const bytes = shortOneShot();
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    const { fs, out } = memFs();
    const inst = plan.instruments[0];
    convertInstrument(bytes, lib, inst, '/out', fs, { author: 't', sourceFileName: 'f.X7L' });
    const json = JSON.parse(out.files.get(`/out/${inst.bundleName}/instrument.json`) as string);
    expect(json.soundType).toBe('decaying');
    expect(json.zones[0].roundRobins[0].loop).toBeNull();
  });

  it('carries a genuine loop region straight over from +44/+48', () => {
    const bytes = buildFakeLibrary([
      {
        name: '1:Looped',
        zones: [{ lo: 21, hi: 108, root: 60, samples: 4000, loopStart: 1000, loopEnd: 3500 }],
        amp: 9000,
      },
    ]);
    const lib = parseX7l(bytes);
    expect(lib.waveforms[0].oneShot).toBe(false);
    const plan = planImport(lib, levelsFor(lib, bytes));
    const { fs, out } = memFs();
    const inst = plan.instruments[0];
    convertInstrument(bytes, lib, inst, '/out', fs, { author: 't', sourceFileName: 'f.X7L' });
    const json = JSON.parse(out.files.get(`/out/${inst.bundleName}/instrument.json`) as string);
    expect(json.soundType).toBe('sustaining');
    expect(json.zones[0].roundRobins[0].loop).toEqual({
      startSample: 1000,
      endSample: 3500,
      crossfadeSamples: 0,
    });
  });

  it('refuses a keyzone whose +58 half-word is used, rather than guessing', () => {
    const b = fake([2000]);
    const zoneAt = findTag(b, 'DWFM') + 12 + 8 + 4;
    b[zoneAt + 58] = 1;
    expect(() => parseX7l(b)).toThrow(/half-word at \+58/);
  });
});

describe('D — YSFC version gate', () => {
  it('accepts 5.0.1, now that a 5.x file has been verified', () => {
    const lib = parseX7l(buildFakeLibrary([{ name: '1:Five', zones: FAKE_ZONES, amp: 9000 }], '5.0.1'));
    expect(lib.version).toBe('5.0.1');
    expect(lib.waveforms).toHaveLength(1);
  });

  it('accepts .X8L as a supported extension', () => {
    expect(SUPPORTED_EXTENSIONS as readonly string[]).toContain('x8l');
    expect(UNVERIFIED_EXTENSIONS as readonly string[]).not.toContain('x8l');
  });

  it('still refuses versions with no verified file', () => {
    for (const v of ['3.0.5', '6.0.0']) {
      expect(() =>
        parseX7l(buildFakeLibrary([{ name: '1:V', zones: FAKE_ZONES, amp: 9000 }], v)),
      ).toThrow(/is not supported/);
    }
  });
});

describe('catalogue names', () => {
  it('reads the name at +22, past the 2-byte category code', () => {
    // the fixture writes a printable code (0x27 0x21) before the name, exactly
    // like the real files whose names came out as "<code>18:DYNO 1"
    const lib = parseX7l(buildFakeLibrary([{ name: '18:DYNO 1', zones: FAKE_ZONES, amp: 9000 }]));
    expect(lib.waveforms[0].rawName).toBe('18:DYNO 1');
    expect(lib.waveforms[0].name).toBe('DYNO 1');
  });
});
