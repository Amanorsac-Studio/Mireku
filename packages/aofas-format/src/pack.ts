/**
 * Packer: a .jmi bundle folder -> one sealed .aofas file (spec §8).
 *
 * Streaming by construction: samples are read and encrypted one chunk at a
 * time and written straight out, so packing a 4 GB library never holds more
 * than one chunk of audio in memory.
 */

import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { closeSync, openSync, readSync, readdirSync, readFileSync, statSync, writeSync } from 'node:fs';
import { join, posix, relative, sep } from 'node:path';
import type { Instrument, Preset } from '@jm/jmi-format';
import {
  DEFAULT_CHUNK_SIZE,
  TAG_BYTES,
  aadChunk,
  aadIndex,
  deriveKeys,
  indexNonce,
  nonceFor,
  seal,
  sha256,
} from './crypto.js';
import { encodeHeader, headerPrefix } from './header.js';
import { type KeyRing, requireKey } from './keyring.js';
import {
  CIPHER_CHACHA20_POLY1305,
  FORMAT_VERSION,
  HEADER_SIZE,
  KDF_HKDF_SHA256,
  type AofasIndex,
  type Card,
  type Header,
  type StreamInfo,
} from './types.js';
import { readWav } from './wav.js';
import {
  AMANORSAC_ORIGINAL,
  categoryFromWorld,
  isCategory,
  licensingProblems,
  type Category,
  type Licensing,
} from './catalog.js';

export interface PackOptions {
  /** .jmi bundle folder */
  bundleDir: string;
  /** destination .aofas path */
  out: string;
  keyRing: KeyRing;
  keyId?: number;
  chunkSize?: number;
  minAppVersion?: string;
  /** override the instrument name written into the card and the index */
  name?: string;
  /** override the author (brand) written into the card and the index */
  author?: string;
  /** Amanorsac catalogue shelf. Falls back to a guess from the .jmi world. */
  category?: Category;
  /** free-form family tags: 'worship', 'neo-soul', … */
  tags?: string[];
  /**
   * Licence as shipped plus every upstream source. Defaults to wholly-original
   * Amanorsac material — which is a claim, so state sources when there are any.
   */
  licensing?: Licensing;
  /** which upstream source each sample came from, and processing notes */
  provenance?: { streamSources?: Record<string, number>; notes?: string };
  /**
   * A take that recorded no audio is a defect in the capture, not something to
   * paper over, so by default packing REFUSES and names the files. Set this to
   * seal what is good: the empty takes are dropped from the zones that
   * reference them, and returned in `droppedEmpty` so they can be re-sampled.
   * A zone that would lose every round robin is still a hard error.
   */
  dropEmptySamples?: boolean;
  onProgress?: (done: number, total: number, what: string) => void;
}

export interface PackResult {
  out: string;
  bundleId: string;
  keyId: number;
  streams: number;
  bytes: number;
  /** bundle-relative paths of empty takes dropped from the instrument */
  droppedEmpty: string[];
}

interface PlannedStream extends StreamInfo {
  srcFile: string;
  srcOffset: number;
}

function uuidBytes(uuid: string): Buffer {
  return Buffer.from(uuid.replace(/-/g, ''), 'hex');
}

/** Every WAV under samples/, as bundle-relative POSIX paths, stably sorted. */
function collectSamples(bundleDir: string): string[] {
  const root = join(bundleDir, 'samples');
  const out: string[] = [];
  const walk = (dir: string) => {
    for (const e of readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
      const p = join(dir, e.name);
      if (e.isDirectory()) walk(p);
      else if (e.isFile() && e.name.toLowerCase().endsWith('.wav')) {
        out.push(posix.join('samples', relative(root, p).split(sep).join('/')));
      }
    }
  };
  walk(root);
  return out.sort();
}

function readPresets(bundleDir: string): { fileName: string; preset: Preset }[] {
  const dir = join(bundleDir, 'presets');
  let names: string[];
  try {
    names = readdirSync(dir)
      .filter((n) => n.toLowerCase().endsWith('.json'))
      .sort();
  } catch {
    return [];
  }
  return names.map((fileName) => ({
    fileName,
    preset: JSON.parse(readFileSync(join(dir, fileName), 'utf8')) as Preset,
  }));
}

export function pack(opts: PackOptions): PackResult {
  const chunkSize = opts.chunkSize ?? DEFAULT_CHUNK_SIZE;
  if (chunkSize < 4096 || (chunkSize & (chunkSize - 1)) !== 0) {
    throw new Error(`chunkSize must be a power of two >= 4096 (got ${chunkSize})`);
  }

  const keyId = opts.keyId ?? opts.keyRing.activeKeyId;
  const studioKey = requireKey(opts.keyRing, keyId);

  const instrument = JSON.parse(readFileSync(join(opts.bundleDir, 'instrument.json'), 'utf8')) as Instrument;
  if (opts.name) instrument.meta.name = opts.name;
  if (opts.author) instrument.meta.author = opts.author;
  const presets = readPresets(opts.bundleDir);

  // §6.2, the one absolute rule: a FRESH bundleId and salt on every single
  // pack, including a re-pack of the same instrument. Never reuse a salt.
  const bundleIdStr = randomUUID();
  const bundleId = uuidBytes(bundleIdStr);
  const fileSalt = randomBytes(16);
  const { kIndex, kData } = deriveKeys(studioKey, fileSalt, bundleId);

  // ---- plan every stream (headers only; no audio is read into memory yet) --
  const samplePaths = collectSamples(opts.bundleDir);
  if (samplePaths.length === 0) throw new Error(`no .wav files under ${join(opts.bundleDir, 'samples')}`);

  const planned: PlannedStream[] = [];
  const empty: string[] = [];
  let totalAudioBytes = 0;
  samplePaths.forEach((relPath) => {
    const file = join(opts.bundleDir, relPath.split('/').join(sep));
    const fd = openSync(file, 'r');
    try {
      const fileSize = statSync(file).size;
      const probe = Buffer.alloc(Math.min(64 * 1024, fileSize));
      readSync(fd, probe, 0, probe.length, 0);
      const w = readWav(probe, fileSize);
      if (w.dataBytes <= 0) {
        empty.push(relPath);
        return;
      }
      planned.push({
        id: planned.length + 1,
        path: relPath,
        codec: w.codec,
        sampleRate: w.sampleRate,
        channels: w.channels,
        frames: w.frames,
        byteLength: w.dataBytes,
        offset: 0, // assigned during layout, below
        chunkCount: Math.ceil(w.dataBytes / chunkSize),
        sha256: '',
        srcFile: file,
        srcOffset: w.dataOffset,
      });
      totalAudioBytes += w.dataBytes;
    } finally {
      closeSync(fd);
    }
  });

  if (empty.length > 0 && !opts.dropEmptySamples) {
    throw new Error(
      `${empty.length} take(s) recorded no audio:\n  ${empty.join('\n  ')}\n` +
        'Re-sample them, or pass --drop-empty to seal the rest without them.',
    );
  }

  // Drop the empty takes from any zone that references them, and make sure no
  // zone is left with nothing to play.
  const emptySet = new Set(empty);
  if (emptySet.size > 0) {
    for (const zone of instrument.zones) {
      const kept = zone.roundRobins.filter((rr) => !emptySet.has(rr.file));
      if (kept.length === 0) {
        throw new Error(
          `zone at root note ${zone.rootNote} (vel ${zone.velRangeLo}-${zone.velRangeHi}) would have no audio left; ` +
            'that note has to be re-sampled before this bundle can be sealed',
        );
      }
      zone.roundRobins = kept;
    }
  }

  // Every sample referenced by the instrument must be present, or the player
  // would open the file and then fail mid-performance (§7.1 step 10).
  const have = new Set(planned.map((s) => s.path));
  for (const zone of instrument.zones) {
    for (const rr of zone.roundRobins) {
      if (!have.has(rr.file)) throw new Error(`instrument.json references a missing sample: ${rr.file}`);
    }
  }

  // ---- catalogue + licensing (spec §5.2) ---------------------------------
  // Refuse to seal before the credits are right. A CC BY source with no credit
  // line is a licence breach the moment the file ships, and once it is sealed
  // and copied to a stage laptop nobody is going to notice it is missing.
  const licensing: Licensing = opts.licensing ?? AMANORSAC_ORIGINAL;
  const problems = licensingProblems(licensing);
  if (problems.length > 0) {
    throw new Error(`this library cannot be sealed as licensed:\n  ${problems.join('\n  ')}`);
  }
  if (opts.category && !isCategory(opts.category)) {
    throw new Error(`unknown category "${opts.category}" — see CATEGORIES in catalog.ts`);
  }
  const category = opts.category ?? categoryFromWorld(instrument.meta.world);

  // ---- the card (§5.2): display metadata only, no audio, no zone map -------
  const card: Card = {
    format: 'aofas',
    formatVersion: FORMAT_VERSION,
    bundleId: bundleIdStr,
    name: instrument.meta.name,
    author: instrument.meta.author,
    world: instrument.meta.world,
    createdAt: instrument.meta.createdAt,
    sampleCount: planned.length,
    totalAudioBytes,
    minAppVersion: opts.minAppVersion ?? '1.0.0',
    keyId,
    category,
    tags: opts.tags?.length ? opts.tags : undefined,
    licensing,
  };
  const cardBytes = Buffer.from(JSON.stringify(card), 'utf8');

  // ---- digest every stream's plaintext (§7.3, a maintenance aid) ----------
  planned.forEach((s, i) => {
    opts.onProgress?.(i, planned.length, `hashing ${s.path}`);
    s.sha256 = hashRange(s.srcFile, s.srcOffset, s.byteLength);
  });

  // ---- layout ------------------------------------------------------------
  // Stream offsets are absolute, so the index's content depends on the index's
  // own encoded size. That is settled by fixing a target size and padding the
  // JSON to hit it exactly: `_pad` is N spaces, so length is controllable to
  // the byte. The loop below converges in two passes in practice (only the
  // digit count of the offsets can move); the guard is there so a pathological
  // case fails loudly instead of writing a file whose offsets are wrong.
  const indexPlain = layoutIndex(instrument, opts.provenance, presets, planned, cardBytes.length, chunkSize);
  const indexOffset = HEADER_SIZE + cardBytes.length;
  const dataOffset = indexOffset + indexPlain.length;
  const dataLength = planned.reduce((n, s) => n + s.byteLength + s.chunkCount * TAG_BYTES, 0);

  // ---- seal the index (§6.1: its AAD binds the card) ----------------------
  const provisional: Header = {
    formatVersion: FORMAT_VERSION,
    headerSize: HEADER_SIZE,
    flags: 0,
    bundleId,
    kdfId: KDF_HKDF_SHA256,
    cipherId: CIPHER_CHACHA20_POLY1305,
    keyId,
    fileSalt,
    cardLength: cardBytes.length,
    indexOffset: BigInt(indexOffset),
    indexLength: 0,
    indexNonce: indexNonce(),
    indexTag: Buffer.alloc(TAG_BYTES),
    chunkSize,
    dataOffset: BigInt(dataOffset),
    dataLength: BigInt(dataLength),
  };
  // The AAD carries only bytes [0,52), which are already final here — the
  // index length and tag live past that point, so sealing cannot chase itself.
  const prefix = headerPrefix(encodeHeader(provisional));
  const sealedIndex = seal(kIndex, indexNonce(), aadIndex(prefix, sha256(cardBytes)), indexPlain);
  const header: Header = { ...provisional, indexLength: sealedIndex.ct.length, indexTag: sealedIndex.tag };

  // ---- write ---------------------------------------------------------------
  const fd = openSync(opts.out, 'w');
  try {
    let at = 0;
    const put = (b: Buffer) => {
      writeAll(fd, b, at);
      at += b.length;
    };
    put(encodeHeader(header));
    put(cardBytes);
    put(sealedIndex.ct);
    if (at !== dataOffset) throw new Error(`layout drift before data: at ${at}, planned ${dataOffset}`);

    const buf = Buffer.alloc(chunkSize);
    planned.forEach((s, si) => {
      opts.onProgress?.(si, planned.length, `sealing ${s.path}`);
      if (at !== s.offset) throw new Error(`layout drift on ${s.path}: at ${at}, planned ${s.offset}`);
      const src = openSync(s.srcFile, 'r');
      try {
        for (let k = 0; k < s.chunkCount; k++) {
          const plainLen = Math.min(chunkSize, s.byteLength - k * chunkSize);
          const got = readSync(src, buf, 0, plainLen, s.srcOffset + k * chunkSize);
          if (got !== plainLen) throw new Error(`short read on ${s.path} chunk ${k}`);
          const { ct, tag } = seal(kData, nonceFor(s.id, k), aadChunk(prefix, s.id, k, plainLen), buf.subarray(0, plainLen));
          put(ct);
          put(tag);
        }
      } finally {
        closeSync(src);
      }
    });
  } finally {
    closeSync(fd);
  }

  return {
    out: opts.out,
    bundleId: bundleIdStr,
    keyId,
    streams: planned.length,
    bytes: dataOffset + dataLength,
    droppedEmpty: empty,
  };
}

/**
 * Assigns each stream its absolute offset and returns the index plaintext,
 * padded so that its length is exactly what those offsets assume.
 */
function layoutIndex(
  instrument: Instrument,
  provenance: AofasIndex['provenance'],
  presets: { fileName: string; preset: Preset }[],
  streams: PlannedStream[],
  cardLength: number,
  chunkSize: number,
): Buffer {
  const build = (pad: number) => {
    const index: AofasIndex & { _pad?: string } = {
      aofas: 1,
      instrument,
      presets,
      streams: streams.map(({ srcFile: _f, srcOffset: _o, ...s }) => s),
      provenance,
    };
    // ALWAYS present, so its ~10 bytes of JSON overhead are part of the
    // measured size and one pad space is worth exactly one byte.
    index._pad = ' '.repeat(pad);
    return Buffer.from(JSON.stringify(index), 'utf8');
  };
  const assign = (dataOffset: number) => {
    let at = dataOffset;
    for (const s of streams) {
      s.chunkCount = Math.ceil(s.byteLength / chunkSize);
      s.offset = at;
      at += s.byteLength + s.chunkCount * TAG_BYTES;
    }
  };

  let target = build(0).length;
  for (let attempt = 0; attempt < 8; attempt++) {
    assign(HEADER_SIZE + cardLength + target);
    const bare = build(0);
    if (bare.length > target) {
      target = bare.length; // offsets gained digits — grow and try again
      continue;
    }
    const padded = build(target - bare.length);
    if (padded.length !== target) throw new Error('index padding is not byte-exact');
    return padded;
  }
  throw new Error('index layout did not converge');
}

function hashRange(file: string, offset: number, length: number): string {
  const fd = openSync(file, 'r');
  try {
    const h = createHash('sha256');
    const buf = Buffer.alloc(1 << 20);
    let done = 0;
    while (done < length) {
      const want = Math.min(buf.length, length - done);
      const got = readSync(fd, buf, 0, want, offset + done);
      if (got <= 0) break;
      h.update(buf.subarray(0, got));
      done += got;
    }
    return h.digest('hex');
  } finally {
    closeSync(fd);
  }
}

function writeAll(fd: number, buf: Buffer, at: number): void {
  let done = 0;
  while (done < buf.length) done += writeSync(fd, buf, done, buf.length - done, at + done);
}
