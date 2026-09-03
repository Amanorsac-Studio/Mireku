/**
 * Yamaha YSFC library reader (`.X7L` — MONTAGE / MONTAGE M user library).
 *
 * Pure TypeScript: `Uint8Array` in, structures out. No DOM, no `node:fs`, so
 * it runs in the renderer, in the Electron main process and under vitest.
 *
 * The format was decoded from a real library file and every structural claim
 * below is *asserted* while parsing — the reader refuses a file it does not
 * fully understand rather than emitting plausible-looking garbage.
 *
 * ── Container ───────────────────────────────────────────────────────────────
 *   0x00  "YAMAHA-YSFC"        magic, NUL padded to 16 bytes
 *   0x10  "4.0.5"              version string, NUL padded to 16 bytes
 *   0x40  block table          repeating [tag:4 ASCII][offset:u32 BE],
 *                              terminated by the first non-`[A-Z]{4}` tag
 *
 * `E***` blocks are catalogues; `D***` blocks hold the matching payloads.
 * Only four matter here: EWFM/DWFM (key maps) and EWIM/DWIM (audio).
 *
 * ── Catalogue block (`E***`) ────────────────────────────────────────────────
 *   [tag:4][size:u32 BE][count:u32 BE] then `count` × ["Entr"][len:u32 BE][body]
 *   body: [dataLen:u32 BE][dataOffset:u32 BE][u16][id:u16 BE] … NUL-terminated
 *   strings.  `dataOffset` is relative to the *start* of the paired `D***`
 *   block and points at that record's "Data" tag.
 *
 * ── Data block (`D***`) ─────────────────────────────────────────────────────
 *   [tag:4][size:u32 BE][count:u32 BE] then `count` × ["Data"][len:u32 BE][body]
 *
 * ── DWFM record body — the key map (little-endian) ──────────────────────────
 *   +0            u32 LE  keyzone count
 *   then `count` × 64-byte keyzone records:
 *     +0   u8   keyRangeLo          +1   u8   keyRangeHi
 *     +2   u8   velRangeLo          +3   u8   velRangeHi
 *     +8   u8   rootNote
 *     +36  u32 LE  sampleRate  (44100)
 *     +48  u32 LE  loopEndSample    +52  u32 LE  startSample
 *     +56  u32 LE  loopStartSample  +60  u32 LE  lengthSamples
 *
 * ── DWIM record body — the audio (big-endian headers) ───────────────────────
 *   +0            u32 BE  sub-record count (== the DWFM keyzone count)
 *   then `count` × [byteLength:u32 BE][PCM: byteLength bytes]
 *   PCM is 16-bit signed little-endian, MONO.
 *
 * The two tables lock together exactly: sub-record *n* of a DWIM record is the
 * audio for keyzone *n* of the matching DWFM record, and
 * `byteLength === 2 * keyzone.lengthSamples` for every zone in the file, with
 * the walk landing precisely on the record end. That is the corroboration the
 * reader enforces — see `verifyLayout` below.
 */

/** Thrown for anything the reader does not fully understand. */
export class X7lFormatError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'X7lFormatError';
  }
}

export const X7L_MAGIC = 'YAMAHA-YSFC';

/**
 * Library extensions this reader accepts. `.X7L` is the only one verified
 * against a real file. MODX `.X8L` is *believed* to use the same YSFC
 * container, but that is unverified here, so it stays gated off rather than
 * silently accepted — see DECISIONS-capture.md.
 */
export const SUPPORTED_EXTENSIONS = ['x7l'] as const;

/** Extensions that look like YSFC libraries but are not verified. */
export const UNVERIFIED_EXTENSIONS = ['x8l', 'x7u', 'x8u', 'x7a', 'x8a'] as const;

export interface X7lKeyzone {
  index: number;
  keyRangeLo: number;
  keyRangeHi: number;
  velRangeLo: number;
  velRangeHi: number;
  rootNote: number;
  sampleRate: number;
  /** total length of this zone's audio, in mono samples */
  lengthSamples: number;
  /** declared playback start (0 in every file seen) */
  startSample: number;
  /** declared loop start */
  loopStartSample: number;
  /** declared loop end (inclusive). Equal to lengthSamples-1 for one-shots. */
  loopEndSample: number;
  /** byte offset of this zone's PCM within the whole file */
  pcmOffset: number;
  /** PCM byte count (always 2 * lengthSamples — 16-bit mono) */
  pcmBytes: number;
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
  keyzones: X7lKeyzone[];
  totalPcmBytes: number;
  /** true when every declared loop is a plain one-shot (loopEnd == length-1) */
  oneShot: boolean;
}

export interface X7lLibrary {
  version: string;
  blocks: { tag: string; offset: number }[];
  /** performance / program names from the EPFM catalogue (informational) */
  performances: string[];
  waveforms: X7lWaveform[];
  totalPcmBytes: number;
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

/** NUL-terminated printable strings inside a catalogue entry body. */
function strings(body: Uint8Array, from: number): string[] {
  const out: string[] = [];
  let cur = '';
  for (let i = from; i < body.length; i++) {
    const c = body[i];
    if (c === 0) {
      if (cur.length) out.push(cur);
      cur = '';
    } else if (c >= 0x20 && c <= 0x7e) {
      cur += String.fromCharCode(c);
    } else {
      // a non-printable byte breaks the run without emitting it
      cur = '';
    }
  }
  if (cur.length) out.push(cur);
  return out;
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
    throw new X7lFormatError(`Block ${tag} runs past the end of the file (ends at ${blockEnd})`);
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
  names: string[];
}

function readCatalogue(b: Uint8Array, blockOffset: number, tag: string): CatalogueEntry[] {
  return readBlockRecords(b, blockOffset, tag, 'Entr').map((r) => {
    const body = b.subarray(r.offset, r.offset + r.length);
    if (body.length < 12) throw new X7lFormatError(`${tag} entry is only ${body.length} bytes`);
    return {
      dataLength: u32be(body, 0),
      dataOffset: u32be(body, 4),
      id: (body[10] << 8) + body[11],
      names: strings(body, 12),
    };
  });
}

/* ------------------------------------------------------------------ */
/* parse                                                               */
/* ------------------------------------------------------------------ */

const KEYZONE_RECORD_BYTES = 64;

/** Sample rates the reader will accept (matches the .jmi schema's enum). */
const LEGAL_SAMPLE_RATES = new Set([44100, 48000, 88200, 96000]);

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
  if (!version.startsWith('4.')) {
    throw new X7lFormatError(
      `YSFC version ${version} is not supported — this reader is verified against version 4.x (MONTAGE) libraries only`,
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
    blocks.push({ tag, offset });
  }
  const at = (tag: string): number => {
    const hit = blocks.find((x) => x.tag === tag);
    if (!hit) {
      throw new X7lFormatError(
        `Library has no ${tag} block (found: ${blocks.map((x) => x.tag).join(', ') || 'none'})`,
      );
    }
    return hit.offset;
  };

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
  if (ewim.length === 0) throw new X7lFormatError('Library contains no waveforms');

  let performances: string[] = [];
  try {
    performances = readCatalogue(bytes, at('EPFM'), 'EPFM').map((e) => e.names[e.names.length - 1] ?? '');
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

    const rawName = entry.names[entry.names.length - 1] ?? `Waveform ${i + 1}`;
    const keyzones = readKeyMap(bytes, map, i);
    verifyLayout(bytes, audio, keyzones, i);

    let bytesHere = 0;
    let oneShot = true;
    for (const z of keyzones) {
      bytesHere += z.pcmBytes;
      if (z.loopEndSample !== z.lengthSamples - 1 || z.startSample !== 0) oneShot = false;
    }
    totalPcmBytes += bytesHere;

    waveforms.push({
      index: i,
      id: entry.id,
      rawName,
      name: rawName.replace(/^\d+\s*:\s*/, '').trim() || rawName,
      keyzones,
      totalPcmBytes: bytesHere,
      oneShot,
    });
  }

  return { version, blocks, performances, waveforms, totalPcmBytes };
}

/** DWFM record → keyzones (PCM offsets are filled in by `verifyLayout`). */
function readKeyMap(bytes: Uint8Array, rec: RawRecord, wfIndex: number): X7lKeyzone[] {
  const body = bytes.subarray(rec.offset, rec.offset + rec.length);
  if (body.length < 4 || (body.length - 4) % KEYZONE_RECORD_BYTES !== 0) {
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

  const zones: X7lKeyzone[] = [];
  for (let z = 0; z < actual; z++) {
    const o = 4 + z * KEYZONE_RECORD_BYTES;
    const zone: X7lKeyzone = {
      index: z,
      keyRangeLo: body[o],
      keyRangeHi: body[o + 1],
      velRangeLo: body[o + 2],
      velRangeHi: body[o + 3],
      rootNote: body[o + 8],
      sampleRate: u32le(body, o + 36),
      loopEndSample: u32le(body, o + 48),
      startSample: u32le(body, o + 52),
      loopStartSample: u32le(body, o + 56),
      lengthSamples: u32le(body, o + 60),
      pcmOffset: 0,
      pcmBytes: 0,
    };
    const where = `DWFM record ${wfIndex} keyzone ${z}`;
    if (zone.keyRangeLo > zone.keyRangeHi || zone.keyRangeHi > 127) {
      throw new X7lFormatError(`${where}: impossible key range ${zone.keyRangeLo}..${zone.keyRangeHi}`);
    }
    if (zone.rootNote > 127) throw new X7lFormatError(`${where}: root note ${zone.rootNote} is out of range`);
    if (zone.velRangeLo < 1 || zone.velRangeHi > 127 || zone.velRangeLo > zone.velRangeHi) {
      throw new X7lFormatError(`${where}: impossible velocity range ${zone.velRangeLo}..${zone.velRangeHi}`);
    }
    if (!LEGAL_SAMPLE_RATES.has(zone.sampleRate)) {
      throw new X7lFormatError(`${where}: unsupported sample rate ${zone.sampleRate}`);
    }
    if (zone.lengthSamples === 0) throw new X7lFormatError(`${where}: zero-length sample`);
    if (zone.loopEndSample >= zone.lengthSamples || zone.loopStartSample >= zone.lengthSamples) {
      throw new X7lFormatError(
        `${where}: loop ${zone.loopStartSample}..${zone.loopEndSample} falls outside the ${zone.lengthSamples}-sample audio`,
      );
    }
    if (zone.startSample >= zone.lengthSamples) {
      throw new X7lFormatError(`${where}: start offset ${zone.startSample} is past the end of the audio`);
    }
    zones.push(zone);
  }
  return zones;
}

/**
 * Walk the DWIM record's sub-records and check them against the key map.
 *
 * This is the corroboration that proves the two tables describe the same
 * audio: every sub-record's declared byte length must equal `2 × lengthSamples`
 * for the keyzone at the same index (16-bit **mono** — a stereo reading would
 * need twice the bytes and does not fit), and the walk must land exactly on
 * the record end with nothing left over. On success each zone's absolute PCM
 * offset is recorded.
 */
function verifyLayout(bytes: Uint8Array, rec: RawRecord, zones: X7lKeyzone[], wfIndex: number): void {
  const declared = u32be(bytes, rec.offset);
  if (declared !== zones.length) {
    throw new X7lFormatError(
      `DWIM record ${wfIndex} holds ${declared} samples but its key map has ${zones.length} keyzones`,
    );
  }
  let p = rec.offset + 4;
  const end = rec.offset + rec.length;
  for (const zone of zones) {
    if (p + 4 > end) {
      throw new X7lFormatError(`DWIM record ${wfIndex} ended before keyzone ${zone.index}'s audio`);
    }
    const byteLength = u32be(bytes, p);
    const expected = zone.lengthSamples * 2;
    if (byteLength !== expected) {
      throw new X7lFormatError(
        `DWIM record ${wfIndex} sub-record ${zone.index} is ${byteLength} bytes; the key map says ${zone.lengthSamples} mono 16-bit samples (${expected} bytes)`,
      );
    }
    need(bytes, p + 4, byteLength, `DWIM record ${wfIndex} sub-record ${zone.index}`);
    zone.pcmOffset = p + 4;
    zone.pcmBytes = byteLength;
    p += 4 + byteLength;
  }
  if (p !== end) {
    throw new X7lFormatError(
      `DWIM record ${wfIndex} has ${end - p} bytes left over after its ${zones.length} samples`,
    );
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
 * when all that is needed is its relative velocity level.
 */
export function representativeKeyzone(wf: X7lWaveform): X7lKeyzone {
  return wf.keyzones.reduce((best, z) =>
    Math.abs(z.rootNote - 60) < Math.abs(best.rootNote - 60) ? z : best,
  );
}

/** Peak and tail level of one keyzone, read straight from the source PCM. */
export function measureKeyzone(bytes: Uint8Array, zone: X7lKeyzone): SampleMeasurement {
  const n = zone.lengthSamples;
  const view = new DataView(bytes.buffer, bytes.byteOffset + zone.pcmOffset, zone.pcmBytes);
  const tailFrom = Math.floor(n * 0.95);
  let peak = 0;
  let tailSq = 0;
  let tailN = 0;
  for (let i = 0; i < n; i++) {
    const v = view.getInt16(i * 2, true);
    const a = v < 0 ? -v : v;
    if (a > peak) peak = a;
    if (i >= tailFrom) {
      tailSq += v * v;
      tailN++;
    }
  }
  const db = (x: number): number => (x <= 0 ? -144 : Math.max(-144, 20 * Math.log10(x / 32768)));
  return {
    peakDb: Math.min(0, round2(db(peak))),
    tailRmsDb: round2(db(tailN ? Math.sqrt(tailSq / tailN) : 0)),
  };
}

/** The 44-byte canonical RIFF header for 16-bit mono PCM. */
export function pcm16MonoWavHeader(dataBytes: number, sampleRate: number): Uint8Array {
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
  dv.setUint16(22, 1, true); // mono
  dv.setUint32(24, sampleRate, true);
  dv.setUint32(28, sampleRate * 2, true); // byte rate
  dv.setUint16(32, 2, true); // block align
  dv.setUint16(34, 16, true); // bits
  tag(36, 'data');
  dv.setUint32(40, dataBytes, true);
  return h;
}

function round2(n: number): number {
  return Math.round(n * 100) / 100;
}
