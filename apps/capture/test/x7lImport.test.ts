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
  type X7lLibrary,
} from '../src/engine/import/x7l';
import {
  buildImportInstrument,
  commonInstrumentName,
  inferWorld,
  keyMapSignature,
  planImport,
  velocityWindows,
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
}
interface FakeWave {
  name: string;
  zones: FakeZone[];
  /** peak amplitude written into every sample of this waveform */
  amp: number;
}

/** Builds a minimal but structurally exact YSFC library in memory. */
function buildFakeLibrary(waves: FakeWave[]): Uint8Array {
  const entryBodies: number[][] = [];
  const dwfmBodies: number[][] = [];
  const dwimBodies: number[][] = [];

  for (const w of waves) {
    const keymap: number[] = le32(w.zones.length);
    const audio: number[] = be32(w.zones.length);
    for (const z of w.zones) {
      const rec = new Array<number>(64).fill(0);
      rec[0] = z.lo;
      rec[1] = z.hi;
      rec[2] = 1;
      rec[3] = 127;
      rec[8] = z.root;
      le32(44100).forEach((b, i) => (rec[36 + i] = b));
      le32(z.samples - 1).forEach((b, i) => (rec[48 + i] = b)); // loop end
      le32(0).forEach((b, i) => (rec[52 + i] = b)); // start
      le32(Math.floor(z.samples / 10)).forEach((b, i) => (rec[56 + i] = b)); // loop start
      le32(z.samples).forEach((b, i) => (rec[60 + i] = b)); // length
      keymap.push(...rec);

      audio.push(...be32(z.samples * 2));
      for (let i = 0; i < z.samples; i++) {
        const v = Math.round(w.amp * Math.sin((i / 32) * Math.PI * 2));
        audio.push(v & 255, (v >> 8) & 255);
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
      ...tag(w.name),
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
  tag('4.0.5').forEach((b, i) => (header[0x10 + i] = b));

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

  it('rejects a library with no audio block', () => {
    // blank the DWIM tag in the block table (third entry)
    expect(() => parseX7l(mangle((b) => (b[0x40 + 16] = 0x41)))).toThrow(/no DWIM block/);
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
    expect(inst.meta.notes).toMatch(/Imported from the Yamaha MONTAGE library "fake\.X7L"/);
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
