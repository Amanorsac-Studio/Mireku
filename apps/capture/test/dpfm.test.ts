/**
 * DPFM (performance block) decoding, and the grouping it gives the importer.
 *
 * Two halves:
 *
 *  • Synthetic libraries, so every field and every refusal is testable without
 *    a 363 MB file. The builder writes the container exactly as the real files
 *    do — chunk lists, the bare part count at +737, the `u16 BE` 8 in front of
 *    each eight-element run — so a change that breaks the real decode breaks
 *    these too.
 *
 *  • One assertion against the client's real `Montage Triton Library.X7L`,
 *    reproducing the whole of Part 1 of "Romance Piano TRFM" as it reads on
 *    his MODX: eight elements, their waveforms, velocity limits, note limits,
 *    crossfades and XA controls. That is the ground truth the layout was
 *    decoded from, and it skips rather than fails when the file is absent.
 */

import { beforeAll, describe, expect, it } from 'vitest';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { validateBundle } from '@jm/jmi-format';
import { measureKeyzone, parseX7l, representativeKeyzone, type X7lLibrary } from '../src/engine/import/x7l';
import { dpfmUsage, decodeDpfm, type DpfmLayer } from '../src/engine/import/dpfm';
import {
  buildImportInstrument,
  dpfmPlan,
  dpfmVelocityWindows,
  groupedInstrumentName,
  planImport,
} from '../src/engine/import/x7lPlan';
import { convertInstrument, type ImportFs } from '../src/engine/import/x7lConvert';

const REAL_TRITON = 'C:\\Users\\amano\\Downloads\\JM Sounds\\Montage Triton Library.X7L';

/* ------------------------------------------------------------------ */
/* a synthetic library with a DPFM block                               */
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

const ELEMENT_PAYLOAD = 149;

interface FakeElement {
  /** element switch; default on */
  on?: boolean;
  /** 1 = this library's waveform (default), 0 = a preset waveform */
  bank?: number;
  /** waveform catalogue id, or preset waveform number */
  wave: number;
  noteLo?: number;
  noteHi?: number;
  velLo?: number;
  velHi?: number;
  xfade?: number;
  /** XA control: 0 normal (default), 2 key off */
  xa?: number;
  group?: number;
}

interface FakePart {
  elements: FakeElement[];
}

/** One element chunk: `[u32 BE 149][149-byte payload]`. */
function elementChunk(e: FakeElement): number[] {
  const p = new Array<number>(ELEMENT_PAYLOAD).fill(0);
  p[0] = e.on === false ? 0 : 1;
  p[1] = e.bank ?? 1;
  p[2] = e.group ?? 0;
  p[3] = 1;
  p[4] = 1;
  p[10] = e.xa ?? 0;
  p[11] = e.noteLo ?? 0;
  p[12] = e.noteHi ?? 127;
  p[13] = e.velLo ?? 1;
  p[14] = e.velHi ?? 127;
  p[15] = e.xfade ?? 0;
  p[146] = 0;
  p[147] = e.wave & 255;
  p[148] = (e.wave >> 8) & 255;
  return [...be32(ELEMENT_PAYLOAD), ...p];
}

/**
 * One DPFM record: filler up to the bare part count at +737, then one block
 * per part — a little filler, the `u16 BE` element count, and eight element
 * chunks. The filler is zeroes, which can never be read as a chunk header.
 */
function dpfmRecord(parts: FakePart[], opts: { declaredParts?: number } = {}): number[] {
  const out = new Array<number>(737).fill(0);
  out.push(...be32(opts.declaredParts ?? parts.length));
  for (const part of parts) {
    out.push(0, 0, 0, 0); // a little filler between parts, as the real files have
    out.push(0, 8); // u16 BE element count
    const elements = [...part.elements];
    while (elements.length < 8) elements.push({ on: false, bank: 0, wave: 1 });
    for (const e of elements.slice(0, 8)) out.push(...elementChunk(e));
  }
  return out;
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
  amp: number;
}

/**
 * A library with EWIM/DWFM/DWIM and, when `perfs` is given, EPFM/DPFM too.
 * Waveform catalogue ids are 1-based in catalogue order, as in the real files.
 */
function buildFakeLibrary(
  waves: FakeWave[],
  perfs: { name: string; parts: FakePart[]; declaredParts?: number }[] = [],
): Uint8Array {
  const entryBodies: number[][] = [];
  const dwfmBodies: number[][] = [];
  const dwimBodies: number[][] = [];
  let handle = 1;

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
      rec[11] = 1;
      rec[15] = 1; // play mode one shot, as every real whole-sample keyzone has
      le32(44100).forEach((b, i) => (rec[36 + i] = b));
      le32(0).forEach((b, i) => (rec[44 + i] = b));
      le32(z.samples - 1).forEach((b, i) => (rec[48 + i] = b));
      le32(0).forEach((b, i) => (rec[52 + i] = b));
      const h = handle++ & 0xffff;
      rec[56] = h & 255;
      rec[57] = (h >> 8) & 255;
      le32(z.samples).forEach((b, i) => (rec[60 + i] = b));
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

  let rel = 12;
  const dwimOffsets: number[] = [];
  for (const body of dwimBodies) {
    dwimOffsets.push(rel);
    rel += 8 + body.length;
  }
  const catalogueEntry = (name: string, id: number, dataLength: number, dataOffset: number): number[] => [
    ...be32(dataLength),
    ...be32(dataOffset),
    0,
    1,
    (id >> 8) & 255,
    id & 255,
    0, 0, 0, 0, 0, 0, 0, 0,
    0x27,
    0x21,
    ...tag(name),
    0,
    0,
  ];
  waves.forEach((w, i) => {
    entryBodies.push(catalogueEntry(w.name, i + 1, dwimBodies[i].length, dwimOffsets[i]));
  });

  const dpfmBodies = perfs.map((p) => dpfmRecord(p.parts, { declaredParts: p.declaredParts }));
  let prel = 12;
  const epfmBodies = perfs.map((p, i) => {
    const body = catalogueEntry(p.name, 0x2000 + i, dpfmBodies[i].length, prel);
    prel += 8 + dpfmBodies[i].length;
    return body;
  });

  const block = (t: string, recTag: string, bodies: number[][]): number[] => {
    let size = 4;
    for (const b of bodies) size += 8 + b.length;
    const out = [...tag(t), ...be32(size), ...be32(bodies.length)];
    for (const b of bodies) out.push(...tag(recTag), ...be32(b.length), ...b);
    return out;
  };

  const blocks: { tag: string; bytes: number[] }[] = [];
  if (perfs.length) {
    blocks.push({ tag: 'EPFM', bytes: block('EPFM', 'Entr', epfmBodies) });
    blocks.push({ tag: 'DPFM', bytes: block('DPFM', 'Data', dpfmBodies) });
  }
  blocks.push({ tag: 'EWIM', bytes: block('EWIM', 'Entr', entryBodies) });
  blocks.push({ tag: 'DWFM', bytes: block('DWFM', 'Data', dwfmBodies) });
  blocks.push({ tag: 'DWIM', bytes: block('DWIM', 'Data', dwimBodies) });

  const header = new Array<number>(0x100).fill(0);
  tag('YAMAHA-YSFC').forEach((b, i) => (header[i] = b));
  tag('4.0.5').forEach((b, i) => (header[0x10 + i] = b));
  let at = header.length;
  blocks.forEach((b, i) => {
    tag(b.tag).forEach((c, k) => (header[0x40 + i * 8 + k] = c));
    be32(at).forEach((c, k) => (header[0x40 + i * 8 + 4 + k] = c));
    at += b.bytes.length;
  });

  return Uint8Array.from([...header, ...blocks.flatMap((b) => b.bytes)]);
}

/** Two waveforms with *different* key ladders and unrelated names. */
const DENSE: FakeZone[] = [
  { lo: 21, hi: 44, root: 36, samples: 256 },
  { lo: 45, hi: 68, root: 60, samples: 256 },
  { lo: 69, hi: 108, root: 84, samples: 256 },
];
const SPARSE: FakeZone[] = [{ lo: 21, hi: 108, root: 60, samples: 256 }];

function twoLayerLibrary(parts: FakePart[]): Uint8Array {
  return buildFakeLibrary(
    [
      { name: '1:Quiet Thing', zones: DENSE, amp: 2000 },
      { name: '1:Unrelated Name', zones: SPARSE, amp: 9000 },
    ],
    [{ name: '0:One Instrument', parts }],
  );
}

function levelsFor(lib: X7lLibrary, bytes: Uint8Array): number[] {
  return lib.waveforms.map((wf) => measureKeyzone(bytes, representativeKeyzone(wf)).peakDb);
}

function layer(p: Partial<DpfmLayer> & { waveformIndex: number; velLo: number; velHi: number }): DpfmLayer {
  return {
    noteLo: 0,
    noteHi: 127,
    velXfade: 0,
    release: false,
    group: 0,
    slot: 0,
    ...p,
  };
}

/* ------------------------------------------------------------------ */

describe('DPFM element decoding', () => {
  it('reads every element field from a part', () => {
    const bytes = twoLayerLibrary([
      {
        elements: [
          { wave: 1, velLo: 1, velHi: 90, xfade: 0 },
          { wave: 2, velLo: 65, velHi: 127, xfade: 15 },
          { wave: 157, bank: 0, on: false, velLo: 1, velHi: 127, noteHi: 91, xa: 2 },
          { wave: 3, bank: 0, on: true, velLo: 40, velHi: 60, noteLo: 20, noteHi: 90, group: 2 },
        ],
      },
    ]);
    const lib = parseX7l(bytes);
    expect(lib.dpfm).not.toBeNull();
    const part = lib.dpfm!.performances[0].parts[0];
    expect(part.elements).toHaveLength(8);
    expect(part.elements[0]).toMatchObject({
      on: true,
      libraryWaveform: true,
      waveformNumber: 1,
      waveformIndex: 0,
      velLo: 1,
      velHi: 90,
      noteLo: 0,
      noteHi: 127,
      velXfade: 0,
      xa: 0,
      release: false,
      group: 0,
    });
    expect(part.elements[1]).toMatchObject({ waveformIndex: 1, velLo: 65, velHi: 127, velXfade: 15 });
    // bank 0 means a preset waveform: the number must NOT be resolved against
    // this library's catalogue, where 157 does not exist at all.
    expect(part.elements[2]).toMatchObject({
      on: false,
      libraryWaveform: false,
      waveformNumber: 157,
      waveformIndex: null,
      xa: 2,
      release: true,
      noteHi: 91,
    });
    expect(part.elements[3]).toMatchObject({ libraryWaveform: false, group: 2, noteLo: 20, noteHi: 90 });
    // the filler slots the builder pads with are switched off
    expect(part.elements.slice(4).every((e) => !e.on)).toBe(true);
  });

  it('reads a waveform number above 255 as the little-endian half-word at +147', () => {
    const bytes = twoLayerLibrary([{ elements: [{ wave: 2461, bank: 0 }, { wave: 1 }] }]);
    const lib = parseX7l(bytes);
    expect(lib.dpfm!.performances[0].parts[0].elements[0].waveformNumber).toBe(2461);
  });

  it('counts a part per element list and reports both to the import', () => {
    const bytes = twoLayerLibrary([{ elements: [{ wave: 1 }] }, { elements: [{ wave: 2 }] }]);
    const lib = parseX7l(bytes);
    expect(lib.dpfm!.performances[0].parts).toHaveLength(2);
    expect(lib.dpfm!.corroboration.join(' ')).toContain('2 AWM2 part(s)');
  });
});

describe('DPFM — what the importer is given', () => {
  it('ignores switched-off slots and preset waveforms', () => {
    const bytes = twoLayerLibrary([
      {
        elements: [
          { wave: 1, velLo: 1, velHi: 80 },
          { wave: 2, velLo: 81, velHi: 127 },
          { wave: 2, on: false, velLo: 1, velHi: 127 },
          { wave: 300, bank: 0, on: true },
        ],
      },
    ]);
    const lib = parseX7l(bytes);
    const [usage] = dpfmUsage(lib.dpfm!);
    expect(usage.body.map((l) => l.waveformIndex)).toEqual([0, 1]);
    expect(usage.usesPresetWaveforms).toBe(true);
    expect(usage.release).toHaveLength(0);
  });

  it('separates key-off elements from the body', () => {
    const bytes = buildFakeLibrary(
      [
        { name: '1:Body', zones: SPARSE, amp: 9000 },
        { name: '1:Tail', zones: SPARSE, amp: 2000 },
      ],
      [{ name: '0:With Key Off', parts: [{ elements: [{ wave: 1 }, { wave: 2, xa: 2 }] }] }],
    );
    const lib = parseX7l(bytes);
    const [usage] = dpfmUsage(lib.dpfm!);
    expect(usage.body.map((l) => l.waveformIndex)).toEqual([0]);
    expect(usage.release.map((l) => l.waveformIndex)).toEqual([1]);
  });
});

describe('DPFM — refusals', () => {
  const skipReason = (bytes: Uint8Array): string => {
    const lib = parseX7l(bytes);
    const decode =
      lib.dpfm ??
      // when every record is skipped the library reports no DPFM at all, so
      // decode the block directly to read the reasons
      (() => {
        const at = lib.blocks.find((b) => b.tag === 'DPFM')!.offset;
        const size = (bytes[at + 4] << 24) + (bytes[at + 5] << 16) + (bytes[at + 6] << 8) + bytes[at + 7];
        const count = (bytes[at + 8] << 24) + (bytes[at + 9] << 16) + (bytes[at + 10] << 8) + bytes[at + 11];
        const records: { offset: number; length: number }[] = [];
        let p = at + 12;
        for (let i = 0; i < count; i++) {
          const len = (bytes[p + 4] << 24) + (bytes[p + 5] << 16) + (bytes[p + 6] << 8) + bytes[p + 7];
          records.push({ offset: p + 8, length: len });
          p += 8 + len;
        }
        void size;
        return decodeDpfm(
          bytes,
          records,
          lib.performances,
          lib.waveforms.map((w) => w.id),
        );
      })();
    return decode.skipped.map((s) => s.reason).join(' | ');
  };

  it('declines a record whose part count does not match the element lists it holds', () => {
    const bytes = twoLayerLibrary([{ elements: [{ wave: 1 }] }]);
    // claim three parts while writing one
    const lib0 = parseX7l(bytes);
    expect(lib0.dpfm).not.toBeNull();
    const liar = buildFakeLibrary(
      [
        { name: '1:A', zones: SPARSE, amp: 9000 },
        { name: '1:B', zones: SPARSE, amp: 2000 },
      ],
      [{ name: '0:Liar', parts: [{ elements: [{ wave: 1 }] }], declaredParts: 3 }],
    );
    expect(skipReason(liar)).toContain('declares 3 parts');
    expect(parseX7l(liar).dpfm).toBeNull();
  });

  it('declines a record with an impossible velocity limit', () => {
    const bytes = buildFakeLibrary(
      [{ name: '1:A', zones: SPARSE, amp: 9000 }],
      [{ name: '0:Bad', parts: [{ elements: [{ wave: 1, velLo: 90, velHi: 10 }] }] }],
    );
    expect(skipReason(bytes)).toContain('velocity limit 90..10');
  });

  it('declines a record with an impossible note limit', () => {
    const bytes = buildFakeLibrary(
      [{ name: '1:A', zones: SPARSE, amp: 9000 }],
      [{ name: '0:Bad', parts: [{ elements: [{ wave: 1, noteLo: 100, noteHi: 40 }] }] }],
    );
    expect(skipReason(bytes)).toContain('note limit 100..40');
  });

  it('declines a switched-on element whose waveform bank byte it does not know', () => {
    const bytes = buildFakeLibrary(
      [{ name: '1:A', zones: SPARSE, amp: 9000 }],
      [{ name: '0:Bad', parts: [{ elements: [{ wave: 1, bank: 2 }] }] }],
    );
    expect(skipReason(bytes)).toContain('waveform bank byte is 2');
  });

  it('declines a library waveform reference that is not in the catalogue', () => {
    const bytes = buildFakeLibrary(
      [{ name: '1:A', zones: SPARSE, amp: 9000 }],
      [{ name: '0:Bad', parts: [{ elements: [{ wave: 9, bank: 1 }] }] }],
    );
    expect(skipReason(bytes)).toContain('claims library waveform 9');
  });

  it('declines an XA control value the synth does not define', () => {
    const bytes = buildFakeLibrary(
      [{ name: '1:A', zones: SPARSE, amp: 9000 }],
      [{ name: '0:Bad', parts: [{ elements: [{ wave: 1, xa: 9 }] }] }],
    );
    expect(skipReason(bytes)).toContain('XA control 9');
  });
});

describe('dpfmVelocityWindows', () => {
  it('places an overlapping boundary at the midpoint of the overlap', () => {
    const w = dpfmVelocityWindows([
      layer({ waveformIndex: 0, velLo: 1, velHi: 90 }),
      layer({ waveformIndex: 1, velLo: 65, velHi: 127 }),
    ]);
    expect(w).toEqual([
      { lo: 1, hi: 77, waveformIndex: 0 },
      { lo: 78, hi: 127, waveformIndex: 1 },
    ]);
  });

  it('closes a gap between two elements at its midpoint', () => {
    // `Init Normal (AWM2)` in New York Grand steps from 58 to 60; velocity 59
    // belonged to neither element, and the midpoint rule gives it to the upper.
    const w = dpfmVelocityWindows([
      layer({ waveformIndex: 0, velLo: 1, velHi: 58 }),
      layer({ waveformIndex: 1, velLo: 60, velHi: 127 }),
    ]);
    expect(w).toEqual([
      { lo: 1, hi: 58, waveformIndex: 0 },
      { lo: 59, hi: 127, waveformIndex: 1 },
    ]);
  });

  it('merges adjacent windows on the same waveform into one layer', () => {
    const w = dpfmVelocityWindows([
      layer({ waveformIndex: 0, velLo: 1, velHi: 31 }),
      layer({ waveformIndex: 0, velLo: 32, velHi: 49 }),
      layer({ waveformIndex: 1, velLo: 50, velHi: 127 }),
    ]);
    expect(w).toEqual([
      { lo: 1, hi: 49, waveformIndex: 0 },
      { lo: 50, hi: 127, waveformIndex: 1 },
    ]);
  });

  it('refuses two elements stacked over the same range', () => {
    expect(
      dpfmVelocityWindows([
        layer({ waveformIndex: 0, velLo: 1, velHi: 127 }),
        layer({ waveformIndex: 1, velLo: 1, velHi: 127 }),
      ]),
    ).toBeNull();
  });

  it('refuses elements whose velocity order and loudness order disagree', () => {
    // one element wholly inside another: no boundary can separate them
    expect(
      dpfmVelocityWindows([
        layer({ waveformIndex: 0, velLo: 1, velHi: 127 }),
        layer({ waveformIndex: 1, velLo: 40, velHi: 60 }),
      ]),
    ).toBeNull();
  });

  it('refuses a split that is also a key split', () => {
    expect(
      dpfmVelocityWindows([
        layer({ waveformIndex: 0, velLo: 1, velHi: 60, noteHi: 91 }),
        layer({ waveformIndex: 1, velLo: 61, velHi: 127, noteHi: 127 }),
      ]),
    ).toBeNull();
  });

  it('turns a single element into the whole range', () => {
    expect(dpfmVelocityWindows([layer({ waveformIndex: 0, velLo: 40, velHi: 80 })])).toEqual([
      { lo: 1, hi: 127, waveformIndex: 0 },
    ]);
  });
});

describe('the planner uses the DPFM grouping', () => {
  const PARTS: FakePart[] = [
    {
      elements: [
        { wave: 1, velLo: 1, velHi: 90, xfade: 0 },
        { wave: 2, velLo: 65, velHi: 127, xfade: 15 },
      ],
    },
  ];

  it('merges two waveforms with unrelated names and different key maps into one instrument', () => {
    const bytes = twoLayerLibrary(PARTS);
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    expect(plan.instruments).toHaveLength(1);
    const inst = plan.instruments[0];
    expect(inst.fromDpfm).toBe(true);
    expect(inst.windows.map((w) => [w.lo, w.hi])).toEqual([
      [1, 77],
      [78, 127],
    ]);
    expect(inst.velXfade).toBe(15);
    expect(inst.waveformIndices.sort()).toEqual([0, 1]);
    // each layer keeps its own key ladder: three zones for the dense waveform,
    // one for the sparse one
    const perLayer = new Map<number, number>();
    for (const z of inst.zones) perLayer.set(z.velRangeLo, (perLayer.get(z.velRangeLo) ?? 0) + 1);
    expect(perLayer.get(1)).toBe(3);
    expect(perLayer.get(78)).toBe(1);
    expect(inst.notes.join(' ')).toContain('performance block groups these waveforms');
  });

  it('falls back to the name heuristic when the same library has no DPFM', () => {
    const bytes = buildFakeLibrary([
      { name: '1:Quiet Thing', zones: DENSE, amp: 2000 },
      { name: '1:Unrelated Name', zones: SPARSE, amp: 9000 },
    ]);
    const lib = parseX7l(bytes);
    expect(lib.dpfm).toBeNull();
    const plan = planImport(lib, levelsFor(lib, bytes));
    expect(plan.instruments).toHaveLength(2);
    expect(plan.instruments.every((i) => !i.fromDpfm)).toBe(true);
  });

  it('keeps waveforms apart when no performance plays them together', () => {
    const bytes = buildFakeLibrary(
      [
        { name: '1:Alike 1', zones: SPARSE, amp: 2000 },
        { name: '1:Alike 2', zones: SPARSE, amp: 9000 },
      ],
      [
        { name: '0:First', parts: [{ elements: [{ wave: 1 }] }] },
        { name: '0:Second', parts: [{ elements: [{ wave: 2 }] }] },
      ],
    );
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    // the name heuristic would merge "Alike 1" and "Alike 2" on their shared
    // stem; the library's own performances say they are separate instruments
    expect(plan.instruments).toHaveLength(2);
  });

  it('files a key-off waveform as a release zone of the instrument it plays with', () => {
    const bytes = buildFakeLibrary(
      [
        { name: '1:Body', zones: SPARSE, amp: 9000 },
        { name: '1:Note off', zones: SPARSE, amp: 2000 },
      ],
      [{ name: '0:With Key Off', parts: [{ elements: [{ wave: 1 }, { wave: 2, xa: 2 }] }] }],
    );
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    expect(plan.instruments).toHaveLength(1);
    const inst = plan.instruments[0];
    expect(inst.releaseZones).toHaveLength(1);
    expect(inst.releaseZones[0].samples[0].file).toMatch(/^samples\/rel_[A-G]#?-?\d+_v1_rr1\.wav$/);
    expect(inst.zones).toHaveLength(1);
    expect(inst.sampleCount).toBe(2);
  });

  it('writes the release zone and the crossfade into a bundle that validates', () => {
    const bytes = buildFakeLibrary(
      [
        { name: '1:Body Soft', zones: SPARSE, amp: 2000 },
        { name: '1:Body Hard', zones: SPARSE, amp: 9000 },
        { name: '1:Note off', zones: SPARSE, amp: 1000 },
      ],
      [
        {
          name: '0:Everything',
          parts: [
            {
              elements: [
                { wave: 1, velLo: 1, velHi: 70, xfade: 8 },
                { wave: 2, velLo: 60, velHi: 127, xfade: 12 },
                { wave: 3, xa: 2 },
              ],
            },
          ],
        },
      ],
    );
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    expect(plan.instruments).toHaveLength(1);

    const dir = mkdtempSync(join(tmpdir(), 'jm-dpfm-'));
    try {
      const fs: ImportFs = {
        mkdir: (d) => mkdirSync(d, { recursive: true }),
        writeFile: (p, data) =>
          // eslint-disable-next-line @typescript-eslint/no-var-requires
          require('node:fs').writeFileSync(p, typeof data === 'string' ? data : Buffer.from(data)),
        join: (...parts) => join(...parts),
      };
      const out = convertInstrument(bytes, lib, plan.instruments[0], dir, fs, {
        author: 'test',
        sourceFileName: 'fake.X7L',
      });
      const result = validateBundle(out.bundlePath);
      expect(result.issues.filter((i) => i.level === 'error')).toEqual([]);
      expect(result.valid).toBe(true);
      const inst = JSON.parse(readFileSync(join(out.bundlePath, 'instrument.json'), 'utf8'));
      expect(inst.releaseZones).toHaveLength(1);
      expect(inst.releaseZones[0].rtDecayDb).toBe(0);
      expect(inst.playback).toEqual({ velocityXfade: 12 });
      expect(inst.reserved).toEqual({ releaseSamples: [], pedalDownZones: [] });
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });

  it('builds an instrument.json whose release zones carry the source key map', () => {
    const bytes = buildFakeLibrary(
      [
        { name: '1:Body', zones: SPARSE, amp: 9000 },
        { name: '1:Tail', zones: DENSE, amp: 2000 },
      ],
      [{ name: '0:P', parts: [{ elements: [{ wave: 1 }, { wave: 2, xa: 2 }] }] }],
    );
    const lib = parseX7l(bytes);
    const plan = planImport(lib, levelsFor(lib, bytes));
    const facts: Record<string, { peakDb: number; lengthMs: number; loop: null }> = {};
    for (const z of [...plan.instruments[0].zones, ...plan.instruments[0].releaseZones]) {
      for (const s of z.samples) facts[s.file] = { peakDb: -6, lengthMs: 10, loop: null };
    }
    const built = buildImportInstrument(plan.instruments[0], facts, {
      author: 'test',
      sourceFileName: 'fake.X7L',
    });
    expect(built.releaseZones).toHaveLength(3);
    expect(built.releaseZones!.map((z) => z.rootNote)).toEqual([36, 60, 84]);
    expect(built.releaseZones!.every((z) => z.rtDecayDb === 0)).toBe(true);
  });
});

describe('groupedInstrumentName', () => {
  it('drops the token that names a position in the instrument', () => {
    expect(groupedInstrumentName(['Stage Ep Low', 'Stage Ep High'])).toBe('Stage Ep');
    expect(groupedInstrumentName(['Brass Low', 'Brass High'])).toBe('Brass');
  });

  it('keeps the first layer name when a shared prefix would be a stub', () => {
    // "String Low" + "Strung High" share only "Str", which is not a name
    expect(groupedInstrumentName(['String Low', 'Strung High'])).toBe('String');
  });

  it('drops a numbered variant and a dynamic marking', () => {
    expect(groupedInstrumentName(['DYNO 1', 'DYNO 2', 'DYNO 5'])).toBe('DYNO');
    expect(groupedInstrumentName(['NY p SE', 'NY mp SE', 'NY mf SE'])).toBe('NY SE');
  });
});

/* ------------------------------------------------------------------ */
/* the ground truth: the client's MODX, read back out of the file      */
/* ------------------------------------------------------------------ */

const haveTriton = existsSync(REAL_TRITON);

describe.skipIf(!haveTriton)('Montage Triton Library.X7L — Romance Piano TRFM', () => {
  let lib: X7lLibrary;
  let bytes: Uint8Array;

  // Read once: the file is 363 MB, and reading it per test is what made this
  // block flaky when the whole suite runs in parallel.
  beforeAll(() => {
    bytes = new Uint8Array(readFileSync(REAL_TRITON));
    lib = parseX7l(bytes);
  }, 300_000);

  it('reproduces all eight elements of Part 1 exactly as the MODX shows them', () => {
    const perf = lib.dpfm!.performances.find((p) => p.name.includes('Romance Piano TRFM'))!;
    expect(perf).toBeDefined();
    expect(perf.declaredParts).toBe(1);
    expect(perf.parts).toHaveLength(1);

    const name = (el: { libraryWaveform: boolean; waveformIndex: number | null; waveformNumber: number }) =>
      el.libraryWaveform && el.waveformIndex !== null
        ? lib.waveforms[el.waveformIndex].name
        : `preset #${el.waveformNumber}`;

    // El | XA        | waveform (bank, number) | velocity | note
    const expected = [
      ['Normal', 'RomancePiano Low', 1, 1, 90, 0, 127, 0],
      ['Normal', 'Romance Piano High', 2, 65, 127, 0, 127, 15],
      ['Normal', 'preset #153', 153, 60, 92, 0, 91, 0],
      ['Normal', 'preset #155', 155, 93, 125, 0, 91, 0],
      ['Normal', 'preset #156', 156, 126, 127, 0, 91, 0],
      ['Normal', 'preset #152', 152, 2, 59, 92, 127, 0],
      ['Normal', 'preset #155', 155, 60, 127, 92, 127, 0],
      ['Key Off', 'preset #157', 157, 1, 127, 0, 91, 0],
    ] as const;

    const els = perf.parts[0].elements;
    expect(els).toHaveLength(8);
    els.forEach((el, i) => {
      const [xa, wname, wnum, vlo, vhi, nlo, nhi, xf] = expected[i];
      expect([i, name(el)]).toEqual([i, wname]);
      expect([i, el.waveformNumber]).toEqual([i, wnum]);
      expect([i, el.velLo, el.velHi]).toEqual([i, vlo, vhi]);
      expect([i, el.noteLo, el.noteHi]).toEqual([i, nlo, nhi]);
      expect([i, el.velXfade]).toEqual([i, xf]);
      expect([i, el.release ? 'Key Off' : 'Normal']).toEqual([i, xa]);
      // the MODX reads Elem Group 1 on all eight; the field is 0-based
      expect([i, el.group]).toEqual([i, 0]);
    });

    // Only the first two are switched on, and they are the only two that name
    // a waveform this library actually contains.
    expect(els.map((e) => e.on)).toEqual([true, true, false, false, false, false, false, false]);
    expect(els.map((e) => e.libraryWaveform)).toEqual([
      true,
      true,
      false,
      false,
      false,
      false,
      false,
      false,
    ]);
  });

  it('groups the 13 waveforms into the 8 instruments its performances declare', () => {
    const grouping = dpfmPlan(lib)!;
    expect(grouping).not.toBeNull();
    const named = [...grouping.groups.values()].map((g) => g.body.map((i) => lib.waveforms[i].name));
    expect(named).toHaveLength(8);
    expect(named).toEqual(
      expect.arrayContaining([
        ['RomancePiano Low', 'Romance Piano High'],
        ['Stage Ep Low', 'Stage EP High'],
        ['Brass Low', 'Brass High'],
        ['analogpad Low', 'analogpad High'],
        ['String Low', 'Strung High'],
        ['VelvetPad'],
        ['M1 Organ'],
        ['Uni CHoir'],
      ]),
    );

    const plan = planImport(lib, levelsFor(lib, bytes));
    expect(plan.instruments).toHaveLength(8);
    const romance = plan.instruments.find((i) => i.name === 'RomancePiano')!;
    expect(romance).toBeDefined();
    expect(romance.fromDpfm).toBe(true);
    expect(romance.windows.map((w) => [w.lo, w.hi])).toEqual([
      [1, 77],
      [78, 127],
    ]);
    expect(romance.velXfade).toBe(15);
  }, 300_000);
});
