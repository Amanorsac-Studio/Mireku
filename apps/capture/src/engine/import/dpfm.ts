/**
 * YSFC **DPFM** (performance) block reader — the library's own account of which
 * waveforms belong together.
 *
 * The importer used to infer that from waveform *names* (`MK 80 p` + `MK 80 f`
 * → one instrument). That is guesswork, and it was visibly wrong:
 * `Montage Triton Library.X7L` fell into 13 separate bundles because its
 * waveforms share one key map but have unrelated names. The truth is in DPFM:
 * every performance says, per Part, exactly which waveforms its elements play,
 * over which velocity and key ranges, with which crossfade, and which element
 * is a key-off (release) sample.
 *
 * Pure `Uint8Array` in, structures out. Nothing here throws on an
 * unrecognised performance: the decode is *advisory*. A record this reader does
 * not fully understand is reported in `skipped` and the importer falls back to
 * the name heuristic, because a confidently wrong grouping that silently mixes
 * unrelated sounds is worse than a cautious one.
 *
 * ── How the record is shaped ─────────────────────────────────────────────────
 * A `DPFM` record is a flat list of length-prefixed chunks:
 *
 *     [u32 BE payloadBytes][payload …]  repeated
 *
 * For `0:Romance Piano TRFM` (`Montage Triton Library.X7L`, record 0, 6746
 * bytes) that list begins 512, 61, 63, 18, 63 — performance-common data, the
 * performance name sitting at +4 inside the first chunk — and the fifth chunk
 * ends exactly on byte 737, where a bare `u32 BE` gives the **part count** (1).
 * Each part then contributes a 762-byte part-common chunk (763 in the YSFC
 * 5.0.1 `.X8L`), its name again at +4, followed further on by its element
 * list. The record sizes confirm the arithmetic: in this library a
 * one-part performance is 6746 bytes, two parts 10463, three 14180, four
 * 17897 — a constant 3717 per AWM2 part.
 *
 * ── The element list ────────────────────────────────────────────────────────
 * Each part's elements are a run of **exactly 8** chunks of **exactly 149
 * payload bytes**, immediately preceded by a `u16 BE` holding the count 8.
 * A MONTAGE AWM2 part always has 8 element slots whether or not they are
 * switched on, which is why the count is always 8 and the run length fixed.
 *
 * This locator is checked, not assumed. Across the ten parseable libraries in
 * the client's corpus (115 performance records, 1 088 elements) **every**
 * 149-byte run is exactly 8 long and is preceded by that `u16 BE` 8, and
 *
 *     (runs of 8 × 149-byte chunks) + (runs of 8 × 51-byte chunks)
 *       === Σ part counts over all records
 *
 * holds in all ten files with no slack: 32 + 129 = 161 in `Crystal Rhodes`,
 * 19 + 6 = 25 in `JM Dyno Piano XL`, 26 + 1 = 27 in `Montage Triton Library`,
 * and so on. The 51-byte runs are the other part type's eight slots (FM-X
 * operators — they appear exactly in the libraries that carry an `AS1`/FM
 * part and never carry a waveform reference), so the two run kinds together
 * account for every part in every performance. That is the corroboration: a
 * stray `149` in some unrelated payload cannot form a run of eight with a
 * matching count in front of it without breaking that identity.
 *
 * ── Element payload (149 bytes) ─────────────────────────────────────────────
 *   +0    u8   element switch        1 = on, 0 = off
 *   +1    u8   waveform bank         1 = this library's own waveform,
 *                                    0 = a preset (ROM) waveform the synth has
 *   +2    u8   element group         0-based (the synth displays +1)
 *   +10   u8   XA control            0 = Normal, 2 = Key Off (release sample)
 *   +11   u8   note limit low        MIDI 0..127
 *   +12   u8   note limit high
 *   +13   u8   velocity limit low    1..127
 *   +14   u8   velocity limit high
 *   +15   u8   velocity crossfade    0..n, the synth's "Vel Cross Fade"
 *   +147  u16 LE waveform number     catalogue id when bank === 1
 *
 * Proof of each field, read off the client's MODX for Part 1 of
 * `0:Romance Piano TRFM` and reproduced byte for byte by this reader:
 *
 *  • **note/velocity limits (+11…+14)** — the eight elements' velocity limits
 *    are 1–90, 65–127, 60–92, 93–125, 126–127, 2–59, 60–127, 1–127 and their
 *    note limits C-2–G8, C-2–G8, C-2–G5, C-2–G5, C-2–G5, G#5–G8, G#5–G8,
 *    C-2–G5. As MIDI numbers that is the byte sequence
 *    `00 7F 01 5A | 00 7F 41 7F | 00 5B 3C 5C | 00 5B 5D 7D | 00 5B 7E 7F |
 *     5C 7F 02 3B | 5C 7F 3C 7F | 00 5B 01 7F`, and it occurs in the record
 *    exactly once, at +11 of each of the eight element payloads, in order.
 *    Every one of the 1 088 elements in the corpus satisfies
 *    `0 ≤ noteLo ≤ noteHi ≤ 127` and `1 ≤ velLo ≤ velHi ≤ 127`.
 *  • **velocity crossfade (+15)** — the MODX shows Vel Cross Fade 0 on element
 *    1 and 15 on element 2; the bytes are 0 and 15.
 *  • **waveform number (+147, u16 LE)** — elements 1 and 2 are "RomancePiano
 *    Low" (Number 1) and "Romance Piano High" (Number 2) and hold 1 and 2.
 *    Elements 4 and 7 are both "CFX fff St" and both hold 155 — the same
 *    waveform name giving the same number is a check the velocity limits
 *    cannot provide. +146 is zero in all 1 088 elements, so the number is
 *    unambiguously the little-endian half-word at +147.
 *  • **waveform bank (+1)** — elements 1 and 2 reference library waveforms and
 *    hold 1; elements 3–8 reference MONTAGE's preset CFX piano waveforms
 *    (numbers 152–157, which this 13-waveform library does not contain) and
 *    hold 0. Corpus-wide, **all 246** elements with bank 1 name a waveform
 *    number that exists in that library's EWIM catalogue, and none misses.
 *    Without this byte the reference is unusable: preset numbers collide with
 *    library ids in the low range.
 *  • **element switch (+0)** — 1 on elements 1 and 2, 0 on 3–8. The six
 *    switched-off slots are the CFX layering of the MONTAGE factory
 *    performance this one was built from; the library author replaced it with
 *    his own two samples and left the slots parked. `kt76 grandpiano
 *    ensoniq.X7L` makes the reading plain: its parts have element 1 on and
 *    slots 3–8 off, every one of them holding the identical factory default
 *    (waveform 1, note 0–127, velocity 1–127).
 *  • **element group (+2)** — 0 on all eight elements of Romance Piano, where
 *    the MODX reads Elem Group 1 for all eight; and 0, 1, 2, 3 on the four
 *    elements of `145:Analog Pad TRFM`, which is the only shape a 0-based
 *    group field can take there. Range is 0..3 corpus-wide.
 *  • **XA control (+10)** — 0 on elements 1–7 (MODX: "Normal") and 2 on
 *    element 8 (MODX: "Key Off"), matching MONTAGE's XA Control enum.
 *    Corpus-wide the byte is only ever 0, 2, 5, 6 or 7, never above 7.
 *
 * Bytes +3, +4 and +5 are 1, 1 and 0 in all 1 088 elements. Being constant
 * they cannot be identified, so this reader does not name them. Nothing is
 * claimed about element level: the MODX capture recorded Part Volume, not a
 * per-element level, so no byte could be tied to one.
 */

/** One AWM2 element slot of one Part. */
export interface DpfmElement {
  /** 0-based slot, 0..7 */
  slot: number;
  /** element switch: false = the slot is parked and makes no sound */
  on: boolean;
  /** true when `waveformNumber` names a waveform in *this* library */
  libraryWaveform: boolean;
  /** waveform catalogue id (library) or preset waveform number */
  waveformNumber: number;
  /** index into `X7lLibrary.waveforms`, or null for a preset waveform */
  waveformIndex: number | null;
  /** 0-based element group (the synth displays this + 1) */
  group: number;
  /** raw XA control byte */
  xa: number;
  /** XA control 2 — a key-off (release) sample */
  release: boolean;
  noteLo: number;
  noteHi: number;
  velLo: number;
  velHi: number;
  /** the synth's Vel Cross Fade for this element */
  velXfade: number;
}

/** One Part of a performance: 8 element slots in order. */
export interface DpfmPart {
  /** 0-based part ordinal within the performance */
  index: number;
  elements: DpfmElement[];
}

export interface DpfmPerformance {
  /** DPFM record index, which is also the EPFM catalogue order */
  index: number;
  /** name from the EPFM catalogue, when one was available */
  name: string;
  /** part count declared by the record */
  declaredParts: number;
  /** the parts whose element list this reader found and understood */
  parts: DpfmPart[];
}

export interface DpfmDecode {
  performances: DpfmPerformance[];
  /** records the reader declined, with the reason — surfaced, never silent */
  skipped: { index: number; name: string; reason: string }[];
  /** cross-checks that passed, for the import report */
  corroboration: string[];
}

/* ------------------------------------------------------------------ */

/** Element payload size. Fixed at 149 in every YSFC 4.x and 5.x file seen. */
const ELEMENT_PAYLOAD = 149;
/** Chunk overhead: the u32 BE length prefix. */
const CHUNK_HEADER = 4;
const ELEMENT_STRIDE = CHUNK_HEADER + ELEMENT_PAYLOAD;
/** A MONTAGE AWM2 part always has exactly this many element slots. */
const ELEMENTS_PER_PART = 8;
/** The other part type's slot size — FM-X operators. Counted, never decoded. */
const OPERATOR_PAYLOAD = 51;
/** Offset of the bare `u32 BE` part count, after the five common chunks. */
const PART_COUNT_OFFSET = 737;

const E = {
  switch: 0,
  bank: 1,
  group: 2,
  xa: 10,
  noteLo: 11,
  noteHi: 12,
  velLo: 13,
  velHi: 14,
  velXfade: 15,
  waveformNumberHigh: 146,
  waveformNumber: 147,
} as const;

/** XA control value for a key-off sample. */
const XA_KEY_OFF = 2;
const XA_MAX = 7;

/** Waveform bank values this reader understands. */
const BANK_PRESET = 0;
const BANK_LIBRARY = 1;

function u32be(b: Uint8Array, at: number): number {
  return ((b[at] << 24) >>> 0) + (b[at + 1] << 16) + (b[at + 2] << 8) + b[at + 3];
}

function u16be(b: Uint8Array, at: number): number {
  return (b[at] << 8) + b[at + 1];
}

function u16le(b: Uint8Array, at: number): number {
  return b[at] + (b[at + 1] << 8);
}

/**
 * Offsets of every run of exactly `count` chunks whose payload is `payload`
 * bytes, each run preceded by a `u16 BE` equal to `count`.
 *
 * The count prefix is what makes this safe. A `149` that happens to fall in
 * some other field cannot start a run of eight *and* be preceded by an 8.
 */
function elementRuns(body: Uint8Array, payload: number, count: number, countPrefix: boolean): number[] {
  const stride = CHUNK_HEADER + payload;
  const isChunk = (at: number): boolean =>
    at >= 0 && at + CHUNK_HEADER + payload <= body.length && u32be(body, at) === payload;

  const out: number[] = [];
  for (let q = 2; q + count * stride <= body.length; q++) {
    if (countPrefix && u16be(body, q - 2) !== count) continue;
    let ok = true;
    for (let k = 0; k < count; k++) {
      if (!isChunk(q + k * stride)) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;
    // Maximality: a ninth chunk of the same size would mean this is not a
    // part's element list but something this reader has not understood.
    if (isChunk(q + count * stride)) continue;
    out.push(q);
    q += count * stride - 1;
  }
  return out;
}

/**
 * Decode one DPFM record. Returns the parts, or a reason to decline it.
 *
 * `idForWaveformIndex` maps a library waveform index to its catalogue id, so
 * the bank-1 reference can be resolved — and refused when it does not resolve.
 */
function decodeRecord(
  body: Uint8Array,
  index: number,
  name: string,
  idToIndex: Map<number, number>,
): { performance: DpfmPerformance; operatorParts: number } | { reason: string } {
  if (body.length < PART_COUNT_OFFSET + 4) {
    return { reason: `record is only ${body.length} bytes, too short to hold a part count` };
  }
  const declaredParts = u32be(body, PART_COUNT_OFFSET);
  if (declaredParts < 1 || declaredParts > 16) {
    return { reason: `declares ${declaredParts} parts, which is outside the 1..16 a performance can hold` };
  }

  const runs = elementRuns(body, ELEMENT_PAYLOAD, ELEMENTS_PER_PART, true);
  // The other part type's eight slots are only *counted*, never decoded, so
  // they are matched on the run alone. Their own count prefix is 0, not 8 —
  // the `u16 BE` 8 is specific to an AWM2 element list, which is exactly why
  // it is a safe guard for the runs this reader does read.
  const operatorParts = elementRuns(body, OPERATOR_PAYLOAD, ELEMENTS_PER_PART, false).length;

  // The identity that proves the locator found every part and nothing else.
  if (runs.length + operatorParts !== declaredParts) {
    return {
      reason:
        `declares ${declaredParts} parts but the record holds ${runs.length} element list(s) and ` +
        `${operatorParts} operator list(s) (${runs.length + operatorParts} together), so the reader has ` +
        `either missed a part or found something that is not one`,
    };
  }

  const parts: DpfmPart[] = [];
  for (let p = 0; p < runs.length; p++) {
    const elements: DpfmElement[] = [];
    for (let k = 0; k < ELEMENTS_PER_PART; k++) {
      const at = runs[p] + k * ELEMENT_STRIDE + CHUNK_HEADER;
      const noteLo = body[at + E.noteLo];
      const noteHi = body[at + E.noteHi];
      const velLo = body[at + E.velLo];
      const velHi = body[at + E.velHi];
      const bank = body[at + E.bank];
      const xa = body[at + E.xa];
      const on = body[at + E.switch] === 1;
      const where = `part ${p + 1} element ${k + 1}`;

      if (noteLo > noteHi || noteHi > 127) {
        return { reason: `${where} has note limit ${noteLo}..${noteHi}, which is not a MIDI key range` };
      }
      if (velLo < 1 || velHi > 127 || velLo > velHi) {
        return { reason: `${where} has velocity limit ${velLo}..${velHi}, which is not a MIDI velocity range` };
      }
      if (xa > XA_MAX) {
        return { reason: `${where} has XA control ${xa}, outside the 0..${XA_MAX} the synth defines` };
      }
      if (body[at + E.waveformNumberHigh] !== 0) {
        return {
          reason:
            `${where} has a non-zero byte at +${E.waveformNumberHigh}, so the waveform number is not the ` +
            `16-bit value at +${E.waveformNumber} this reader expects`,
        };
      }
      // A switched-on element whose bank byte is neither of the two values
      // this reader has proven is a reference it cannot resolve. Guessing it
      // would either drop a layer or stack a sound the library does not hold.
      if (on && bank !== BANK_PRESET && bank !== BANK_LIBRARY) {
        return {
          reason:
            `${where} is switched on but its waveform bank byte is ${bank}, not 0 (preset) or 1 (library), ` +
            `so which waveform it plays cannot be established`,
        };
      }
      const waveformNumber = u16le(body, at + E.waveformNumber);
      const libraryWaveform = bank === BANK_LIBRARY;
      const waveformIndex = libraryWaveform ? (idToIndex.get(waveformNumber) ?? null) : null;
      if (libraryWaveform && waveformIndex === null) {
        return {
          reason:
            `${where} claims library waveform ${waveformNumber}, which is not in this library's ` +
            `EWIM catalogue`,
        };
      }
      elements.push({
        slot: k,
        on,
        libraryWaveform,
        waveformNumber,
        waveformIndex,
        group: body[at + E.group],
        xa,
        release: xa === XA_KEY_OFF,
        noteLo,
        noteHi,
        velLo,
        velHi,
        velXfade: body[at + E.velXfade],
      });
    }
    parts.push({ index: p, elements });
  }

  return { performance: { index, name, declaredParts, parts }, operatorParts };
}

/**
 * Decode a library's DPFM block.
 *
 * `records` are the raw `Data` record spans of the DPFM block, `names` the
 * EPFM catalogue names in the same order (may be shorter or empty), and
 * `waveformIds` the EWIM catalogue id of each waveform, by index.
 */
export function decodeDpfm(
  bytes: Uint8Array,
  records: { offset: number; length: number }[],
  names: string[],
  waveformIds: number[],
): DpfmDecode {
  const idToIndex = new Map<number, number>();
  waveformIds.forEach((id, i) => {
    // A duplicate id would make the reference ambiguous; the first wins and
    // the ambiguity shows up as a refusal only if a performance uses it.
    if (!idToIndex.has(id)) idToIndex.set(id, i);
  });

  const performances: DpfmPerformance[] = [];
  const skipped: DpfmDecode['skipped'] = [];
  let parts = 0;
  let operators = 0;
  let elements = 0;

  for (let i = 0; i < records.length; i++) {
    const rec = records[i];
    const name = names[i] ?? `Performance ${i + 1}`;
    if (rec.offset + rec.length > bytes.length) {
      skipped.push({ index: i, name, reason: 'record runs past the end of the file' });
      continue;
    }
    const body = bytes.subarray(rec.offset, rec.offset + rec.length);
    const got = decodeRecord(body, i, name, idToIndex);
    if ('reason' in got) {
      skipped.push({ index: i, name, reason: got.reason });
      continue;
    }
    performances.push(got.performance);
    parts += got.performance.parts.length;
    operators += got.operatorParts;
    elements += got.performance.parts.length * ELEMENTS_PER_PART;
  }

  const corroboration: string[] = [];
  if (performances.length) {
    corroboration.push(
      `DPFM: ${performances.length} of ${records.length} performance record(s) decoded, ` +
        `${parts} AWM2 part(s) and ${elements} element slot(s); every record's part count matched the ` +
        `${parts} element list(s) plus ${operators} operator list(s) found in it.`,
    );
  }
  if (skipped.length) {
    corroboration.push(
      `DPFM: ${skipped.length} record(s) were not understood and contribute nothing to grouping ` +
        `(${skipped.map((s) => `"${s.name}": ${s.reason}`).join('; ')}).`,
    );
  }
  return { performances, skipped, corroboration };
}

/* ------------------------------------------------------------------ */
/* what the importer asks of a decode                                  */
/* ------------------------------------------------------------------ */

/** One switched-on element that plays a waveform from *this* library. */
export interface DpfmLayer {
  waveformIndex: number;
  velLo: number;
  velHi: number;
  noteLo: number;
  noteHi: number;
  velXfade: number;
  release: boolean;
  group: number;
  slot: number;
}

/** One Part reduced to the library waveforms it actually sounds. */
export interface DpfmUsage {
  performanceIndex: number;
  performanceName: string;
  partIndex: number;
  /** switched-on, library-owned, non-release elements, in slot order */
  body: DpfmLayer[];
  /** switched-on, library-owned, key-off elements */
  release: DpfmLayer[];
  /** true when the part also sounds waveforms this library does not contain */
  usesPresetWaveforms: boolean;
}

/**
 * Every part in the decode that plays at least one of this library's own
 * waveforms, in performance then part order.
 */
export function dpfmUsage(decode: DpfmDecode): DpfmUsage[] {
  const out: DpfmUsage[] = [];
  for (const perf of decode.performances) {
    for (const part of perf.parts) {
      const body: DpfmLayer[] = [];
      const release: DpfmLayer[] = [];
      let preset = false;
      for (const el of part.elements) {
        if (!el.on) continue;
        if (!el.libraryWaveform || el.waveformIndex === null) {
          preset = true;
          continue;
        }
        const layer: DpfmLayer = {
          waveformIndex: el.waveformIndex,
          velLo: el.velLo,
          velHi: el.velHi,
          noteLo: el.noteLo,
          noteHi: el.noteHi,
          velXfade: el.velXfade,
          release: el.release,
          group: el.group,
          slot: el.slot,
        };
        (el.release ? release : body).push(layer);
      }
      if (body.length === 0 && release.length === 0) continue;
      out.push({
        performanceIndex: perf.index,
        performanceName: perf.name,
        partIndex: part.index,
        body,
        release,
        usesPresetWaveforms: preset,
      });
    }
  }
  return out;
}
