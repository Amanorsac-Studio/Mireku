/**
 * Yamaha YSFC library reader (`.X7L` / `.X8L` — MONTAGE / MODX user library).
 *
 * Pure TypeScript: `Uint8Array` in, structures out. No DOM, no `node:fs`, so
 * it runs in the renderer, in the Electron main process and under vitest.
 *
 * Every structural claim below is *asserted* while parsing — the reader
 * refuses a file it does not fully understand rather than emitting
 * plausible-looking garbage.
 *
 * ── Container ───────────────────────────────────────────────────────────────
 *   0x00  "YAMAHA-YSFC"        magic, NUL padded to 16 bytes
 *   0x10  "4.0.5" / "5.0.1"    version string, NUL padded to 16 bytes
 *   0x40  block table          repeating [tag:4 ASCII][offset:u32 BE],
 *                              terminated by the first non-`[A-Z]{4}` tag
 *
 * `E***` blocks are catalogues; `D***` blocks hold the matching payloads.
 * Four matter for the audio: EWFM/DWFM (key maps) and EWIM/DWIM (audio). A
 * fifth, EPFM/DPFM, holds the performances — which waveforms the library
 * itself groups into one instrument. That block is decoded in `dpfm.ts`, which
 * documents its layout and the evidence for every field; it is advisory, so a
 * library whose DPFM cannot be read still converts.
 *
 * ── Catalogue block (`E***`) ────────────────────────────────────────────────
 *   [tag:4][size:u32 BE][count:u32 BE] then `count` × ["Entr"][len:u32 BE][body]
 *   body:
 *     +0   u32 BE  dataLength   (byte length of the paired D*** record)
 *     +4   u32 BE  dataOffset   (relative to the start of the paired D*** block)
 *     +8   u16 BE  always 1
 *     +10  u16 BE  id           (1-based, catalogue order)
 *     +12  8 bytes zero
 *     +20  u16 BE  category/library code
 *     +22  name, NUL-terminated ASCII
 *
 *   The name lives at **+22**, after that 2-byte code. Scanning for printable
 *   runs from +12 instead glues the code's bytes onto the front of the name
 *   whenever they happen to be printable ASCII — which is where names like
 *   `'*0:AZ p SE` and `'"RomancePiano Low` came from.
 *
 * ── Data block (`D***`) ─────────────────────────────────────────────────────
 *   [tag:4][size:u32 BE][count:u32 BE] then `count` × ["Data"][len:u32 BE][body]
 *
 * ── DWFM record body — the key map (little-endian) ──────────────────────────
 *   +0            u32 LE  keyzone-record count
 *   then `count` × 64-byte keyzone records:
 *     +0   u8   keyRangeLo          +1   u8   keyRangeHi
 *     +2   u8   velRangeLo          +3   u8   velRangeHi
 *     +5   u8   pan                 (1 = L63 … 64 = centre … 127 = R63)
 *     +8   u8   rootNote
 *     +11  u8   channels            (1 = mono, 2 = stereo)
 *     +15  u8   play mode           (1 = one shot, 0 = loop; see below)
 *     +36  u32 LE  sampleRate     (the PCM's playback rate; not always a
 *                                  standard one — see plausibleSampleRate)
 *     +44  u32 LE  loopStartSample
 *     +48  u32 LE  loopEndSample  (inclusive; == length-1 for a whole-sample loop)
 *     +52  u32 LE  startSample    (0 in every file seen)
 *     +56  u16 LE  sampleId       (per-sample handle; NOT a loop point)
 *     +60  u32 LE  lengthSamples  (per channel)
 *
 *   `sampleId` at +56 was previously read as a 32-bit loop start. It is not
 *   one: across the whole client corpus (4,000+ keyzones, 12 libraries) the
 *   upper half-word at +58 is always zero while sample lengths reach 790,000,
 *   the value is distinct for every keyzone in every library, and in
 *   `Montage Triton Library.X7L` it enumerates 1…408 strictly sequentially —
 *   a handle, not an offset. The real loop start is at +44, which satisfies
 *   `loopStart <= loopEnd < lengthSamples` for every keyzone in the corpus and
 *   is exactly 0 in every library whose loops span the whole sample.
 *
 *   Byte +15 is the keybank's PLAY MODE. Across the corpus it is 1 for every
 *   keyzone whose loop spans the whole sample and 0 for every sub-region loop,
 *   so the two always agreed. The client's 2026 "Full Expansion" library then
 *   showed a one-shot piano ("Gospel Grand v3") with the flag = 1 AND leftover
 *   loop points — a start of 39,300 on the left channel and 61 on the right of
 *   the same stereo pair, which no intended loop would have. On the synth a
 *   one-shot keybank plays once whatever its loop points say, so that is what
 *   is read here: when the flag says one shot the loop fields are normalised to
 *   the whole sample and the zone is a one-shot. The other disagreement (flag =
 *   loop, points spanning the whole sample) has never been seen and is refused.
 *
 * ── Stereo ──────────────────────────────────────────────────────────────────
 *   When `channels == 2` the keyzone records come in adjacent pairs that are
 *   byte-identical except for `sampleId`, and the DWIM record holds two
 *   consecutive mono sub-records for them: left then right. The two are folded
 *   into ONE logical keyzone here, with one PCM span per channel.
 *
 *   Reading them as two independent mono keyzones is what produced the
 *   duplicate `{Note}_v{layer}_rr{n}` sample names, and filing them as round
 *   robins of each other would alternate the left and right channels of one
 *   recording from note to note. `channels` is the field that settles it: it
 *   is 1 in exactly the two libraries that have no such pairs, and 2 in every
 *   library that has them, where the zone count is always even and the pairing
 *   is exact.
 *
 * ── Stereo by panning ───────────────────────────────────────────────────────
 *   A second way a library stores stereo: two MONO keyzone records on the same
 *   key/velocity slot, identical except for `sampleId` and byte +5, which is 1
 *   (hard left) on one and 127 (hard right) on the other. That is the Montage
 *   "two keybanks panned L63 / R63" idiom, and it is how the client's own 2026
 *   "Full Expansion" library is built — 108 such pairs in its first waveform.
 *   Byte +5 was identified as pan the same way +56 was identified as a handle:
 *   it is 64 (centre) on all ~4,300 keyzones of the verified corpus and on every
 *   unpaired keyzone of that file, and exactly {1, 127} on the paired ones.
 *   The pairs are usually adjacent records but need not be (one waveform puts a
 *   right channel 55 records after its left), so pairing is by slot, not by
 *   position. A pair is folded into ONE stereo keyzone, left = the pan-1
 *   record. A waveform where only SOME slots are doubled, or where a doubled
 *   slot is not one left and one right, is still refused: that is two
 *   different samples in one slot, and guessing which one plays would be
 *   silent and wrong.
 *
 * ── DWIM record body — the audio (big-endian headers) ───────────────────────
 *   +0            u32 BE  sub-record count (== the DWFM keyzone-record count)
 *   then `count` × [byteLength:u32 BE][PCM: byteLength bytes]
 *   PCM is 16-bit signed little-endian, one channel per sub-record.
 *
 * The two tables lock together exactly: `byteLength === 2 * lengthSamples` for
 * every keyzone record in the file, the walk lands precisely on the record
 * end, and within one waveform no two logical keyzones claim the same
 * (key range, velocity range) slot. That is the corroboration the reader
 * enforces — see `verifyLayout` and `readKeyMap` below.
 */

import { decodeDpfm, type DpfmDecode } from './dpfm';

/** Thrown for anything the reader does not fully understand. */
export class X7lFormatError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'X7lFormatError';
  }
}

/** Thrown when the container is well-formed but the file stops early. */
export class X7lTruncatedError extends X7lFormatError {
  constructor(
    message: string,
    readonly declaredBytes: number,
    readonly actualBytes: number,
  ) {
    super(message);
    this.name = 'X7lTruncatedError';
  }
}

/** Thrown when the library holds patch parameters but no sampled audio. */
export class X7lNoAudioError extends X7lFormatError {
  constructor(message: string) {
    super(message);
    this.name = 'X7lNoAudioError';
  }
}

export const X7L_MAGIC = 'YAMAHA-YSFC';

/**
 * Library extensions this reader accepts. `.X7L` (YSFC 4.x, MONTAGE) and
 * `.X8L` (YSFC 5.0.1, MODX/MONTAGE M) have both now been verified against
 * real files — see DECISIONS-capture.md.
 */
export const SUPPORTED_EXTENSIONS = ['x7l', 'x8l'] as const;

/** Extensions that look like YSFC libraries but are not verified. */
export const UNVERIFIED_EXTENSIONS = ['x7u', 'x8u', 'x7a', 'x8a'] as const;

/** YSFC major versions verified against a real file. */
export const SUPPORTED_VERSION_MAJORS = ['4', '5'] as const;

/** One channel's PCM span within the file. */
export interface X7lPcmSpan {
  offset: number;
  bytes: number;
}

export interface X7lKeyzone {
  index: number;
  keyRangeLo: number;
  keyRangeHi: number;
  velRangeLo: number;
  velRangeHi: number;
  rootNote: number;
  /** byte +5: 1 = L63, 64 = centre, 127 = R63 (64 once a hard-panned pair is folded) */
  pan: number;
  sampleRate: number;
  /** 1 = mono, 2 = stereo (two PCM spans, left then right) */
  channels: 1 | 2;
  /** total length of this zone's audio, in sample frames (per channel) */
  lengthSamples: number;
  /** declared playback start (0 in every file seen) */
  startSample: number;
  /** declared loop start (field +44) */
  loopStartSample: number;
  /** declared loop end (inclusive). Equal to lengthSamples-1 for one-shots. */
  loopEndSample: number;
  /** per-sample handle from field +56 — informational, never a loop point */
  sampleId: number;
  /** PCM span per channel, `channels` entries, left first */
  pcm: X7lPcmSpan[];
  /** byte offset of the first channel's PCM within the whole file */
  pcmOffset: number;
  /** first channel's PCM byte count (always 2 * lengthSamples) */
  pcmBytes: number;
  /** PCM bytes across every channel */
  totalPcmBytes: number;
  /** index of the first DWFM keyzone *record* this logical zone came from */
  recordIndex: number;
  /** the DWFM record per channel, left first (`recordIndices[0] === recordIndex`) */
  recordIndices: number[];
}

export interface X7lWaveform {
  /** index within the library (0-based, catalogue order) */
  index: number;
  /** catalogue id from the EWIM entry */
  id: number;
  /** raw catalogue name, e.g. "18:MKS20 mp" */
  rawName: string;
  /** name with the leading "<n>:" catalogue prefix stripped, e.g. "MKS20 mp" */
  name: string;
  /** 1 or 2 — constant across a waveform's keyzones */
  channels: 1 | 2;
  keyzones: X7lKeyzone[];
  totalPcmBytes: number;
  /** true when every declared loop spans the whole sample (nothing to loop) */
  oneShot: boolean;
}

export interface X7lLibrary {
  version: string;
  blocks: { tag: string; offset: number }[];
  /** performance / program names from the EPFM catalogue (informational) */
  performances: string[];
  waveforms: X7lWaveform[];
  totalPcmBytes: number;
  /**
   * The decoded DPFM performance block — which waveforms the library itself
   * groups together, with their velocity and key ranges. `null` when the
   * library has no DPFM block or nothing in it could be read; individual
   * records the reader declined are listed in `dpfm.skipped`. Advisory: the
   * planner falls back to its name heuristic wherever this is silent.
   */
  dpfm: DpfmDecode | null;
}

/* ------------------------------------------------------------------ */
/* primitives                                                          */
/* ------------------------------------------------------------------ */

function u32be(b: Uint8Array, at: number): number {
  need(b, at, 4, 'uint32');
  return ((b[at] << 24) >>> 0) + (b[at + 1] << 16) + (b[at + 2] << 8) + b[at + 3];
}

function u32le(b: Uint8Array, at: number): number {
  need(b, at, 4, 'uint32');
  return (b[at] + (b[at + 1] << 8) + (b[at + 2] << 16) + b[at + 3] * 0x1000000) >>> 0;
}

function u16le(b: Uint8Array, at: number): number {
  need(b, at, 2, 'uint16');
  return b[at] + (b[at + 1] << 8);
}

function need(b: Uint8Array, at: number, len: number, what: string): void {
  if (at < 0 || at + len > b.length) {
    throw new X7lFormatError(
      `Truncated file: needed ${len} bytes for ${what} at offset ${at}, file is ${b.length} bytes`,
    );
  }
}

function ascii(b: Uint8Array, at: number, len: number): string {
  need(b, at, len, 'text');
  let s = '';
  for (let i = 0; i < len; i++) s += String.fromCharCode(b[at + i]);
  return s;
}

/** A NUL-terminated printable string at a known offset inside a record body. */
function cstring(body: Uint8Array, from: number): string {
  let s = '';
  for (let i = from; i < body.length; i++) {
    const c = body[i];
    if (c === 0) break;
    if (c >= 0x20 && c <= 0x7e) s += String.fromCharCode(c);
  }
  return s.trim();
}

/** Human-readable byte size, for refusal messages. */
function mib(n: number): string {
  return `${(n / 1048576).toFixed(1)} MiB`;
}

/* ------------------------------------------------------------------ */
/* blocks                                                              */
/* ------------------------------------------------------------------ */

interface RawRecord {
  /** offset of the record's payload within the file */
  offset: number;
  length: number;
}

function readBlockRecords(b: Uint8Array, blockOffset: number, tag: string, wantTag: string): RawRecord[] {
  const got = ascii(b, blockOffset, 4);
  if (got !== tag) {
    throw new X7lFormatError(`Block ${tag} at 0x${blockOffset.toString(16)} starts with "${got}"`);
  }
  const size = u32be(b, blockOffset + 4);
  const count = u32be(b, blockOffset + 8);
  if (count > 0xffff) throw new X7lFormatError(`Block ${tag} declares an implausible ${count} records`);
  const blockEnd = blockOffset + 8 + size;
  if (blockEnd > b.length) {
    throw new X7lTruncatedError(
      `This library file is incomplete. Its ${tag} block declares data ending at byte ${blockEnd}, ` +
        `but the file is only ${b.length} bytes (${mib(b.length)}) — ${mib(blockEnd - b.length)} is missing. ` +
        `Everything the reader can check inside the file agrees with itself, so the format is understood; ` +
        `the bytes simply are not there. Re-download or re-export the library and try again.`,
      blockEnd,
      b.length,
    );
  }
  const out: RawRecord[] = [];
  let p = blockOffset + 12;
  for (let i = 0; i < count; i++) {
    const rt = ascii(b, p, 4);
    if (rt !== wantTag) {
      throw new X7lFormatError(`Block ${tag} record ${i} has tag "${rt}", expected "${wantTag}"`);
    }
    const len = u32be(b, p + 4);
    need(b, p + 8, len, `${tag} record ${i}`);
    out.push({ offset: p + 8, length: len });
    p += 8 + len;
  }
  if (p !== blockEnd) {
    throw new X7lFormatError(
      `Block ${tag} records end at ${p} but the block header says ${blockEnd} (drift ${p - blockEnd})`,
    );
  }
  return out;
}

interface CatalogueEntry {
  dataLength: number;
  dataOffset: number;
  id: number;
  name: string;
}

/** Offset of the NUL-terminated name inside a catalogue entry body. */
const CATALOGUE_NAME_OFFSET = 22;

function readCatalogue(b: Uint8Array, blockOffset: number, tag: string): CatalogueEntry[] {
  return readBlockRecords(b, blockOffset, tag, 'Entr').map((r, i) => {
    const body = b.subarray(r.offset, r.offset + r.length);
    if (body.length < CATALOGUE_NAME_OFFSET + 1) {
      throw new X7lFormatError(`${tag} entry ${i} is only ${body.length} bytes`);
    }
    return {
      dataLength: u32be(body, 0),
      dataOffset: u32be(body, 4),
      id: (body[10] << 8) + body[11],
      name: cstring(body, CATALOGUE_NAME_OFFSET),
    };
  });
}

/* ------------------------------------------------------------------ */
/* parse                                                               */
/* ------------------------------------------------------------------ */

const KEYZONE_RECORD_BYTES = 64;

/** Sample rates a .jmi bundle may declare (the schema's enum). */
const LEGAL_SAMPLE_RATES = [44100, 48000, 88200, 96000];

/**
 * Rates a keyzone may carry. A keyzone's rate is the rate its PCM plays back
 * at, and it need not be a standard one: the client's 2026 "Full Expansion"
 * library has three Mini Moog waveforms whose keyzones sit at 44,063 .. 44,120
 * Hz, one rate per note — samples TUNED BY RATE, the way Akai-era samplers and
 * their converters do it (44,066 Hz is 44,100 lowered by 1.3 cents). The PCM is
 * still copied bit for bit and the WAV written carries the true rate, which
 * the player honours, so the pitch comes out exactly as the synth plays it.
 * The bundle's nominal rate is nominalSampleRate() of that.
 */
export function plausibleSampleRate(r: number): boolean {
  return r >= 8000 && r <= 192000;
}

/** The schema rate nearest a keyzone's true rate. */
export function nominalSampleRate(r: number): number {
  return LEGAL_SAMPLE_RATES.reduce((best, l) => (Math.abs(l - r) < Math.abs(best - r) ? l : best));
}

export function parseX7l(bytes: Uint8Array): X7lLibrary {
  if (bytes.length < 0x100) throw new X7lFormatError(`File is only ${bytes.length} bytes — not a YSFC library`);

  const magic = ascii(bytes, 0, X7L_MAGIC.length);
  if (magic !== X7L_MAGIC) {
    throw new X7lFormatError(`Not a Yamaha YSFC library: expected "${X7L_MAGIC}", found "${magic}"`);
  }
  const version = ascii(bytes, 0x10, 16).replace(/\0+$/, '');
  if (!/^\d+\.\d+\.\d+$/.test(version)) {
    throw new X7lFormatError(`Unrecognised YSFC version string "${version}"`);
  }
  const major = version.split('.')[0];
  if (!(SUPPORTED_VERSION_MAJORS as readonly string[]).includes(major)) {
    throw new X7lFormatError(
      `YSFC version ${version} is not supported — this reader is verified against version ` +
        `${SUPPORTED_VERSION_MAJORS.join('.x and ')}.x libraries only`,
    );
  }

  // ---- block table ----
  const blocks: { tag: string; offset: number }[] = [];
  for (let p = 0x40; p + 8 <= bytes.length; p += 8) {
    const tag = ascii(bytes, p, 4);
    if (!/^[A-Z]{4}$/.test(tag)) break;
    const offset = u32be(bytes, p + 4);
    if (offset >= bytes.length) {
      throw new X7lFormatError(`Block ${tag} points at offset ${offset}, past the end of the file`);
    }
    // A block table entry must point at its own tag; anything else means the
    // table has already ended and we are reading payload bytes that happen to
    // look like a tag (YSFC 4.0.0 files terminate the table differently).
    if (ascii(bytes, offset, 4) !== tag) break;
    blocks.push({ tag, offset });
  }
  const has = (tag: string): boolean => blocks.some((x) => x.tag === tag);
  const at = (tag: string): number => {
    const hit = blocks.find((x) => x.tag === tag);
    if (!hit) {
      throw new X7lFormatError(
        `Library has no ${tag} block (found: ${blocks.map((x) => x.tag).join(', ') || 'none'})`,
      );
    }
    return hit.offset;
  };

  // ---- a parameter-only library is a clear, specific refusal ----
  if (!has('EWIM') || !has('DWIM') || !has('DWFM')) {
    throw new X7lNoAudioError(
      `This library holds no sampled audio, so there is nothing to import. ` +
        `A YSFC library stores its waveforms in the EWIM/DWIM (audio) and EWFM/DWFM (key map) blocks; ` +
        `this file has only ${blocks.map((x) => x.tag).join(', ') || 'no'} blocks, which carry patch ` +
        `parameters alone. Libraries like this are programs for the synth's own tone generator — the FM ` +
        `patches in a DX7 ROM collection, for example — and they reference sounds the synth already has ` +
        `rather than shipping any audio of their own. ` +
        `To get these sounds into a .jmi bundle they have to be sampled off the hardware: load the library ` +
        `on the instrument and record it with JM Capture's guided or auto sampling mode.`,
    );
  }

  // ---- catalogues ----
  const ewim = readCatalogue(bytes, at('EWIM'), 'EWIM');
  const dwimBase = at('DWIM');
  const dwim = readBlockRecords(bytes, dwimBase, 'DWIM', 'Data');
  const dwfm = readBlockRecords(bytes, at('DWFM'), 'DWFM', 'Data');

  if (ewim.length !== dwim.length || dwim.length !== dwfm.length) {
    throw new X7lFormatError(
      `Catalogue mismatch: EWIM has ${ewim.length} entries, DWIM ${dwim.length} records, DWFM ${dwfm.length} records`,
    );
  }
  if (ewim.length === 0) throw new X7lNoAudioError('Library contains no waveforms');

  let performances: string[] = [];
  try {
    performances = readCatalogue(bytes, at('EPFM'), 'EPFM').map((e) => e.name);
  } catch {
    // EPFM is informational only — a library without it still converts.
    performances = [];
  }

  const waveforms: X7lWaveform[] = [];
  let totalPcmBytes = 0;

  for (let i = 0; i < ewim.length; i++) {
    const entry = ewim[i];
    const audio = dwim[i];
    const map = dwfm[i];

    // The catalogue entry must agree with the record we walked to.
    const expectedOffset = audio.offset - 8 - dwimBase;
    if (entry.dataOffset !== expectedOffset || entry.dataLength !== audio.length) {
      throw new X7lFormatError(
        `EWIM entry ${i} points at +${entry.dataOffset}/${entry.dataLength} bytes but DWIM record ${i} is at +${expectedOffset}/${audio.length}`,
      );
    }

    const rawName = entry.name || `Waveform ${i + 1}`;
    const { keyzones, records } = readKeyMap(bytes, map, i);
    verifyLayout(bytes, audio, keyzones, records, i);

    let bytesHere = 0;
    let oneShot = true;
    for (const z of keyzones) {
      bytesHere += z.totalPcmBytes;
      if (z.loopEndSample !== z.lengthSamples - 1 || z.loopStartSample !== 0 || z.startSample !== 0) {
        oneShot = false;
      }
    }
    totalPcmBytes += bytesHere;

    waveforms.push({
      index: i,
      id: entry.id,
      rawName,
      name: rawName.replace(/^\d+\s*:\s*/, '').replace(/\s+/g, ' ').trim() || rawName,
      channels: keyzones[0].channels,
      keyzones,
      totalPcmBytes: bytesHere,
      oneShot,
    });
  }

  // ---- DPFM: the library's own grouping (advisory) ----
  // Read last, because resolving an element's waveform reference needs the
  // EWIM ids. Any failure here leaves `dpfm` null and the importer falls back
  // to its name heuristic — a library still converts without it.
  let dpfm: DpfmDecode | null = null;
  if (has('DPFM')) {
    try {
      const records = readBlockRecords(bytes, at('DPFM'), 'DPFM', 'Data');
      const decoded = decodeDpfm(
        bytes,
        records,
        performances,
        waveforms.map((w) => w.id),
      );
      dpfm = decoded.performances.length ? decoded : null;
    } catch {
      dpfm = null;
    }
  }

  return { version, blocks, performances, waveforms, totalPcmBytes, dpfm };
}

/**
 * DWFM record → logical keyzones.
 *
 * For a stereo waveform two adjacent keyzone *records* describe one logical
 * keyzone, so the returned `records` count is what the DWIM sub-record walk
 * must match while `keyzones` is what the rest of the importer works with.
 * PCM offsets are filled in by `verifyLayout`.
 */
function readKeyMap(
  bytes: Uint8Array,
  rec: RawRecord,
  wfIndex: number,
): { keyzones: X7lKeyzone[]; records: number } {
  const body = bytes.subarray(rec.offset, rec.offset + rec.length);
  if (body.length < 4 + KEYZONE_RECORD_BYTES || (body.length - 4) % KEYZONE_RECORD_BYTES !== 0) {
    throw new X7lFormatError(
      `DWFM record ${wfIndex} is ${body.length} bytes — not 4 + a whole number of ${KEYZONE_RECORD_BYTES}-byte keyzones`,
    );
  }
  const declared = u32le(body, 0);
  const actual = (body.length - 4) / KEYZONE_RECORD_BYTES;
  if (declared !== actual) {
    throw new X7lFormatError(
      `DWFM record ${wfIndex} declares ${declared} keyzones but its size holds ${actual}`,
    );
  }

  const readRecord = (r: number) => {
    const o = 4 + r * KEYZONE_RECORD_BYTES;
    const where = `DWFM record ${wfIndex} keyzone ${r}`;
    const channels = body[o + 11];
    if (channels !== 1 && channels !== 2) {
      throw new X7lFormatError(
        `${where}: channel count ${channels} is neither 1 (mono) nor 2 (stereo) — this reader is ` +
          `verified against those two only`,
      );
    }
    const z = {
      keyRangeLo: body[o],
      keyRangeHi: body[o + 1],
      velRangeLo: body[o + 2],
      velRangeHi: body[o + 3],
      rootNote: body[o + 8],
      pan: body[o + 5],
      channels: channels as 1 | 2,
      sampleRate: u32le(body, o + 36),
      loopStartSample: u32le(body, o + 44),
      loopEndSample: u32le(body, o + 48),
      startSample: u32le(body, o + 52),
      sampleId: u16le(body, o + 56),
      lengthSamples: u32le(body, o + 60),
    };
    // +56 is a 16-bit handle; if the upper half-word is ever used this reader
    // has the field wrong and must not guess.
    const upper = u16le(body, o + 58);
    if (upper !== 0) {
      throw new X7lFormatError(
        `${where}: the half-word at +58 is ${upper}, not 0 — field +56 is not the 16-bit sample ` +
          `handle this reader expects, so the keyzone layout cannot be trusted`,
      );
    }
    if (z.keyRangeLo > z.keyRangeHi || z.keyRangeHi > 127) {
      throw new X7lFormatError(`${where}: impossible key range ${z.keyRangeLo}..${z.keyRangeHi}`);
    }
    if (z.rootNote > 127) throw new X7lFormatError(`${where}: root note ${z.rootNote} is out of range`);
    if (z.velRangeLo < 1 || z.velRangeHi > 127 || z.velRangeLo > z.velRangeHi) {
      throw new X7lFormatError(`${where}: impossible velocity range ${z.velRangeLo}..${z.velRangeHi}`);
    }
    if (!plausibleSampleRate(z.sampleRate)) {
      throw new X7lFormatError(`${where}: implausible sample rate ${z.sampleRate}`);
    }
    if (z.lengthSamples === 0) throw new X7lFormatError(`${where}: zero-length sample`);
    if (z.loopEndSample >= z.lengthSamples) {
      throw new X7lFormatError(
        `${where}: loop end ${z.loopEndSample} falls outside the ${z.lengthSamples}-sample audio`,
      );
    }
    if (z.loopStartSample > z.loopEndSample) {
      throw new X7lFormatError(
        `${where}: loop ${z.loopStartSample}..${z.loopEndSample} runs backwards`,
      );
    }
    if (z.startSample >= z.lengthSamples) {
      throw new X7lFormatError(`${where}: start offset ${z.startSample} is past the end of the audio`);
    }
    // Byte +15 is the play mode (see the header). One shot wins over leftover
    // loop points: the fields are normalised to the whole sample so every later
    // stage, and the C++ reader, see the same zone. A LOOP flag on points that
    // span the whole sample has never been seen and would be a mis-parse, so it
    // is refused rather than guessed at.
    const oneShotFlag = body[o + 15] !== 0;
    const spansWhole = z.loopStartSample === 0 && z.loopEndSample === z.lengthSamples - 1;
    if (oneShotFlag && !spansWhole) {
      z.loopStartSample = 0;
      z.loopEndSample = z.lengthSamples - 1;
    } else if (!oneShotFlag && spansWhole) {
      throw new X7lFormatError(
        `${where}: the play-mode flag at +15 says looped but the loop points span the whole sample, ` +
          `so this keyzone has been mis-parsed and its audio cannot be mapped safely`,
      );
    }
    return z;
  };

  const channels = readRecord(0).channels;
  if (channels === 2 && actual % 2 !== 0) {
    throw new X7lFormatError(
      `DWFM record ${wfIndex} is stereo but holds an odd number of keyzone records (${actual}), ` +
        `so its left/right pairs do not line up`,
    );
  }

  type Rec = ReturnType<typeof readRecord>;
  // Two records describe one sample when everything but the handle and the
  // pan agrees. Used for both stereo idioms (the channels === 2 flag and the
  // hard-panned mono pair).
  const sameSample = (a: Rec, c: Rec): boolean =>
    c.keyRangeLo === a.keyRangeLo &&
    c.keyRangeHi === a.keyRangeHi &&
    c.velRangeLo === a.velRangeLo &&
    c.velRangeHi === a.velRangeHi &&
    c.rootNote === a.rootNote &&
    c.channels === a.channels &&
    c.sampleRate === a.sampleRate &&
    c.lengthSamples === a.lengthSamples &&
    c.loopStartSample === a.loopStartSample &&
    c.loopEndSample === a.loopEndSample &&
    c.startSample === a.startSample;
  const slotOf = (z: Rec) => `${z.keyRangeLo}-${z.keyRangeHi}/${z.velRangeLo}-${z.velRangeHi}`;
  const slotWords = (z: Rec) =>
    `key ${z.keyRangeLo}..${z.keyRangeHi} at velocity ${z.velRangeLo}..${z.velRangeHi}`;
  const AMBIGUOUS =
    'Two different samples in one slot cannot be mapped unambiguously, and guessing which one plays ' +
    'would be silent and wrong.';
  const asZone = (rec: Rec, index: number, recordIndices: number[]): X7lKeyzone => ({
    index,
    ...rec,
    pcm: [],
    pcmOffset: 0,
    pcmBytes: 0,
    totalPcmBytes: 0,
    recordIndex: recordIndices[0],
    recordIndices,
  });

  const zones: X7lKeyzone[] = [];
  const slots = new Map<string, number>();
  const all: Rec[] = []; // every record, in file order (mono path)
  for (let r = 0; r < actual; r += channels) {
    const left = readRecord(r);
    if (left.channels !== channels) {
      throw new X7lFormatError(
        `DWFM record ${wfIndex} keyzone ${r}: channel count ${left.channels} differs from the ` +
          `waveform's ${channels} — a mixed-channel waveform is not understood`,
      );
    }
    if (channels === 2) {
      const right = readRecord(r + 1);
      if (!sameSample(left, right)) {
        throw new X7lFormatError(
          `DWFM record ${wfIndex} keyzones ${r}/${r + 1} are marked stereo but do not describe the ` +
            `same sample (key, velocity, root, rate, length or loop differ), so they cannot be the ` +
            `left and right channels of one recording`,
        );
      }
      const index = zones.length;
      const slot = slotOf(left);
      const prev = slots.get(slot);
      if (prev !== undefined) {
        throw new X7lFormatError(
          `DWFM record ${wfIndex} keyzones ${prev} and ${index} both claim ${slotWords(left)}. ${AMBIGUOUS}`,
        );
      }
      slots.set(slot, index);
      zones.push(asZone(left, index, [r, r + 1]));
    } else {
      all.push(left);
    }
  }
  if (channels === 2) return { keyzones: zones, records: actual };

  // ---- mono records: one per slot, or a hard-panned left/right pair per slot ----
  // See the header: byte +5 is pan, and the Montage "two keybanks panned L63 /
  // R63" idiom stores a stereo sample as two mono keyzones on one slot. Pairs
  // need not be adjacent, so pairing is by slot.
  const bySlot = new Map<string, number[]>(); // slot -> indices into `all`, file order
  all.forEach((z, i) => {
    const k = slotOf(z);
    const list = bySlot.get(k);
    if (list) list.push(i);
    else bySlot.set(k, [i]);
  });

  let single = 0;
  let paired = 0;
  for (const idx of bySlot.values()) {
    if (idx.length === 1) {
      single++;
      continue;
    }
    const a = all[idx[0]];
    if (idx.length === 2) {
      const c = all[idx[1]];
      const hardPanned = (a.pan === 1 && c.pan === 127) || (a.pan === 127 && c.pan === 1);
      if (hardPanned && sameSample(a, c)) {
        paired++;
        continue;
      }
    }
    throw new X7lFormatError(
      `DWFM record ${wfIndex}: ${idx.length} keyzones all claim ${slotWords(a)} (pans ` +
        `${idx.map((i) => all[i].pan).join(', ')}). ${AMBIGUOUS} The one doubling this reader ` +
        `understands is a stereo pair: two otherwise identical keyzones panned hard left (1) and ` +
        `hard right (127).`,
    );
  }
  if (paired > 0 && single > 0) {
    throw new X7lFormatError(
      `DWFM record ${wfIndex}: ${paired} slot(s) hold a hard-panned left/right pair but ${single} hold ` +
        `a single mono keyzone. A waveform that is stereo in some keys and mono in others is not ` +
        `understood, so it is not guessed at.`,
    );
  }

  if (paired === 0) {
    all.forEach((z, i) => zones.push(asZone(z, zones.length, [i])));
    return { keyzones: zones, records: actual };
  }

  // Fold each pair into one stereo keyzone, in the order the pairs' first
  // members appear in the file, so the zone order is stable and matches the
  // C++ reader's.
  const done = new Set<number>();
  all.forEach((z, i) => {
    if (done.has(i)) return;
    const idx = bySlot.get(slotOf(z))!;
    const l = all[idx[0]].pan === 1 ? idx[0] : idx[1];
    const rr = l === idx[0] ? idx[1] : idx[0];
    done.add(l);
    done.add(rr);
    zones.push({ ...asZone(all[l], zones.length, [l, rr]), channels: 2, pan: 64 });
  });
  return { keyzones: zones, records: actual };
}

/**
 * Walk the DWIM record's sub-records and check them against the key map.
 *
 * This is the corroboration that proves the two tables describe the same
 * audio: the record declares one sub-record per keyzone *record* (so two per
 * stereo keyzone), every sub-record's declared byte length must equal
 * `2 × lengthSamples` for the keyzone it belongs to, and the walk must land
 * exactly on the record end with nothing left over. On success each zone's
 * absolute per-channel PCM offsets are recorded.
 */
function verifyLayout(
  bytes: Uint8Array,
  rec: RawRecord,
  zones: X7lKeyzone[],
  records: number,
  wfIndex: number,
): void {
  const declared = u32be(bytes, rec.offset);
  if (declared !== records) {
    throw new X7lFormatError(
      `DWIM record ${wfIndex} holds ${declared} samples but its key map has ${records} keyzone records`,
    );
  }
  // Walk every sub-record first. Sub-record i belongs to keyzone RECORD i, and
  // a logical zone's channels may come from records that are not adjacent (a
  // hard-panned pair), so the spans are indexed before they are handed out.
  const spans: X7lPcmSpan[] = [];
  let p = rec.offset + 4;
  const end = rec.offset + rec.length;
  for (let i = 0; i < records; i++) {
    if (p + 4 > end) {
      throw new X7lFormatError(`DWIM record ${wfIndex} ended before sub-record ${i}'s audio`);
    }
    const byteLength = u32be(bytes, p);
    need(bytes, p + 4, byteLength, `DWIM record ${wfIndex} sub-record ${i}`);
    spans.push({ offset: p + 4, bytes: byteLength });
    p += 4 + byteLength;
  }
  if (p !== end) {
    throw new X7lFormatError(
      `DWIM record ${wfIndex} has ${end - p} bytes left over after its ${records} samples`,
    );
  }

  for (const zone of zones) {
    zone.pcm = [];
    zone.totalPcmBytes = 0;
    if (zone.recordIndices.length !== zone.channels) {
      throw new X7lFormatError(
        `DWFM record ${wfIndex} keyzone ${zone.index} has ${zone.recordIndices.length} source record(s) ` +
          `for ${zone.channels} channel(s)`,
      );
    }
    for (const r of zone.recordIndices) {
      const span = spans[r];
      if (!span) {
        throw new X7lFormatError(`DWIM record ${wfIndex} has no sub-record ${r} for keyzone ${zone.index}`);
      }
      const expected = zone.lengthSamples * 2;
      if (span.bytes !== expected) {
        throw new X7lFormatError(
          `DWIM record ${wfIndex} sub-record ${r} is ${span.bytes} bytes; the key map ` +
            `says ${zone.lengthSamples} 16-bit samples (${expected} bytes)`,
        );
      }
      zone.pcm.push(span);
      zone.totalPcmBytes += span.bytes;
    }
    zone.pcmOffset = zone.pcm[0].offset;
    zone.pcmBytes = zone.pcm[0].bytes;
  }
}

/* ------------------------------------------------------------------ */
/* audio helpers                                                       */
/* ------------------------------------------------------------------ */

export interface SampleMeasurement {
  /** true peak in dBFS, ≤ 0 (floored at -144 for digital silence) */
  peakDb: number;
  /** RMS of the final 5% of the sample, in dBFS */
  tailRmsDb: number;
}

/**
 * The keyzone nearest middle C — a fair, cheap stand-in for a whole waveform
 * when all that is needed is its relative velocity level. Where a waveform
 * carries its own velocity splits the loudest split is preferred, so two
 * waveforms are compared at the same point in their dynamic range.
 */
export function representativeKeyzone(wf: X7lWaveform): X7lKeyzone {
  const topVel = Math.max(...wf.keyzones.map((z) => z.velRangeHi));
  const candidates = wf.keyzones.filter((z) => z.velRangeHi === topVel);
  return (candidates.length ? candidates : wf.keyzones).reduce((best, z) =>
    Math.abs(z.rootNote - 60) < Math.abs(best.rootNote - 60) ? z : best,
  );
}

/** Peak and tail level of one keyzone, read straight from the source PCM. */
export function measureKeyzone(bytes: Uint8Array, zone: X7lKeyzone): SampleMeasurement {
  const n = zone.lengthSamples;
  const tailFrom = Math.floor(n * 0.95);
  let peak = 0;
  let tailSq = 0;
  let tailN = 0;
  for (const span of zone.pcm) {
    const view = new DataView(bytes.buffer, bytes.byteOffset + span.offset, span.bytes);
    for (let i = 0; i < n; i++) {
      const v = view.getInt16(i * 2, true);
      const a = v < 0 ? -v : v;
      if (a > peak) peak = a;
      if (i >= tailFrom) {
        tailSq += v * v;
        tailN++;
      }
    }
  }
  const db = (x: number): number => (x <= 0 ? -144 : Math.max(-144, 20 * Math.log10(x / 32768)));
  return {
    peakDb: Math.min(0, round2(db(peak))),
    tailRmsDb: round2(db(tailN ? Math.sqrt(tailSq / tailN) : 0)),
  };
}

/** The 44-byte canonical RIFF header for 16-bit integer PCM. */
export function pcm16WavHeader(dataBytes: number, sampleRate: number, channels: number): Uint8Array {
  const h = new Uint8Array(44);
  const dv = new DataView(h.buffer);
  const tag = (at: number, s: string) => {
    for (let i = 0; i < s.length; i++) h[at + i] = s.charCodeAt(i);
  };
  tag(0, 'RIFF');
  dv.setUint32(4, 36 + dataBytes, true);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true); // PCM
  dv.setUint16(22, channels, true);
  dv.setUint32(24, sampleRate, true);
  dv.setUint32(28, sampleRate * 2 * channels, true); // byte rate
  dv.setUint16(32, 2 * channels, true); // block align
  dv.setUint16(34, 16, true); // bits
  tag(36, 'data');
  dv.setUint32(40, dataBytes, true);
  return h;
}

/** The 44-byte canonical RIFF header for 16-bit mono PCM. */
export function pcm16MonoWavHeader(dataBytes: number, sampleRate: number): Uint8Array {
  return pcm16WavHeader(dataBytes, sampleRate, 1);
}

/**
 * One keyzone as a complete WAV file.
 *
 * Bit-perfect: a mono zone's PCM is copied as one contiguous block, and a
 * stereo zone's two channel blocks are interleaved frame by frame. Every
 * 16-bit sample word is the source word, unchanged — nothing is resampled,
 * requantised or scaled on this path.
 */
export function keyzoneWav(bytes: Uint8Array, zone: X7lKeyzone): Uint8Array {
  const header = pcm16WavHeader(zone.totalPcmBytes, zone.sampleRate, zone.channels);
  const wav = new Uint8Array(header.length + zone.totalPcmBytes);
  wav.set(header, 0);
  if (zone.channels === 1) {
    wav.set(bytes.subarray(zone.pcm[0].offset, zone.pcm[0].offset + zone.pcm[0].bytes), header.length);
    return wav;
  }
  const left = zone.pcm[0].offset;
  const right = zone.pcm[1].offset;
  let o = header.length;
  for (let i = 0; i < zone.lengthSamples; i++) {
    const l = left + i * 2;
    const r = right + i * 2;
    wav[o++] = bytes[l];
    wav[o++] = bytes[l + 1];
    wav[o++] = bytes[r];
    wav[o++] = bytes[r + 1];
  }
  return wav;
}

function round2(n: number): number {
  return Math.round(n * 100) / 100;
}
