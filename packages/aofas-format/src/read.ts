/**
 * Reader (spec §7). This is the reference the C++ reader in the stage app is
 * checked against, and it is what the packer round-trips through before it
 * calls a file finished.
 *
 * Opening a 400 MB library touches a few hundred KB: header, card, index. No
 * bulk decryption ever happens at open time, and audio is only decrypted in
 * the byte ranges a caller actually asks for.
 */

import { createHash } from 'node:crypto';
import { closeSync, openSync, readSync, statSync } from 'node:fs';
import { inflateRawSync } from 'node:zlib';
import {
  TAG_BYTES,
  aadChunk,
  aadIndex,
  deriveKeys,
  indexNonce,
  nonceFor,
  open as aeadOpen,
  sha256,
} from './crypto.js';
import { decodeHeader, headerPrefix } from './header.js';
import { type KeyRing, requireKey } from './keyring.js';
import {
  AofasError,
  FLAG_INDEX_DEFLATED,
  HEADER_SIZE,
  bytesPerSample,
  type AofasIndex,
  type Card,
  type Header,
  type StreamInfo,
} from './types.js';

/** §5.2 — reads the public card. Needs NO key: this is the library-list path. */
export function readCard(path: string): Card {
  const fd = openSync(path, 'r');
  try {
    const head = Buffer.alloc(HEADER_SIZE);
    if (readSync(fd, head, 0, HEADER_SIZE, 0) !== HEADER_SIZE) {
      throw new AofasError('AOFAS_ERR_IO', 'file shorter than a header');
    }
    const h = decodeHeader(head);
    const card = Buffer.alloc(h.cardLength);
    if (readSync(fd, card, 0, h.cardLength, h.headerSize) !== h.cardLength) {
      throw new AofasError('AOFAS_ERR_IO', 'short card read');
    }
    return JSON.parse(card.toString('utf8')) as Card;
  } finally {
    closeSync(fd);
  }
}

export class Archive {
  private constructor(
    private readonly fd: number,
    readonly path: string,
    readonly header: Header,
    readonly card: Card,
    readonly index: AofasIndex,
    private readonly prefix: Buffer,
    private readonly kData: Buffer,
    private readonly byId: Map<number, StreamInfo>,
    private readonly byPath: Map<string, StreamInfo>,
  ) {}

  /** §7.1 — open and validate. Throws AofasError with a §9 code. */
  static open(path: string, keyRing: KeyRing): Archive {
    const fd = openSync(path, 'r');
    try {
      const head = Buffer.alloc(HEADER_SIZE);
      if (readSync(fd, head, 0, HEADER_SIZE, 0) !== HEADER_SIZE) {
        throw new AofasError('AOFAS_ERR_IO', 'file shorter than a header');
      }
      const h = decodeHeader(head);
      const studioKey = requireKey(keyRing, h.keyId);

      const cardBytes = Buffer.alloc(h.cardLength);
      if (readSync(fd, cardBytes, 0, h.cardLength, h.headerSize) !== h.cardLength) {
        throw new AofasError('AOFAS_ERR_IO', 'short card read');
      }
      const card = JSON.parse(cardBytes.toString('utf8')) as Card;

      const { kIndex, kData } = deriveKeys(studioKey, h.fileSalt, h.bundleId);

      if (!h.indexNonce.equals(indexNonce())) {
        throw new AofasError('AOFAS_ERR_BAD_NONCE', 'index nonce is not the derived value');
      }

      const ct = Buffer.alloc(h.indexLength);
      if (readSync(fd, ct, 0, h.indexLength, Number(h.indexOffset)) !== h.indexLength) {
        throw new AofasError('AOFAS_ERR_IO', 'short index read');
      }
      const prefix = headerPrefix(head);
      const plain = aeadOpen(kIndex, indexNonce(), aadIndex(prefix, sha256(cardBytes)), ct, h.indexTag);
      if (!plain) throw new AofasError('AOFAS_ERR_AUTH', 'index authentication failed');

      const json = (h.flags & FLAG_INDEX_DEFLATED) !== 0 ? inflateRawSync(plain) : plain;
      const index = JSON.parse(json.toString('utf8')) as AofasIndex;

      const byId = new Map<number, StreamInfo>();
      const byPath = new Map<string, StreamInfo>();
      const fileSize = statSync(path).size;
      const dataFrom = Number(h.dataOffset);
      const dataTo = dataFrom + Number(h.dataLength);

      for (const s of index.streams ?? []) {
        const expectBytes = s.frames * s.channels * bytesPerSample(s.codec);
        if (expectBytes !== s.byteLength) {
          throw new AofasError('AOFAS_ERR_INDEX', `${s.path}: byteLength disagrees with frames/channels/codec`);
        }
        if (s.chunkCount !== Math.ceil(s.byteLength / h.chunkSize)) {
          throw new AofasError('AOFAS_ERR_INDEX', `${s.path}: chunkCount is wrong`);
        }
        const end = s.offset + s.byteLength + s.chunkCount * TAG_BYTES;
        if (s.offset < dataFrom || end > dataTo || end > fileSize) {
          throw new AofasError('AOFAS_ERR_INDEX', `${s.path}: stream extent lies outside the data region`);
        }
        byId.set(s.id, s);
        byPath.set(s.path, s);
      }

      for (const zone of index.instrument?.zones ?? []) {
        for (const rr of zone.roundRobins) {
          if (!byPath.has(rr.file)) {
            throw new AofasError('AOFAS_ERR_MISSING_STREAM', `no stream for ${rr.file}`);
          }
        }
      }

      return new Archive(fd, path, h, card, index, prefix, kData, byId, byPath);
    } catch (e) {
      closeSync(fd);
      throw e;
    }
  }

  close(): void {
    closeSync(this.fd);
  }

  get streams(): StreamInfo[] {
    return this.index.streams;
  }

  stream(id: number): StreamInfo {
    const s = this.byId.get(id);
    if (!s) throw new AofasError('AOFAS_ERR_MISSING_STREAM', `no stream ${id}`);
    return s;
  }

  streamForPath(jmiRelativePath: string): StreamInfo {
    const s = this.byPath.get(jmiRelativePath);
    if (!s) throw new AofasError('AOFAS_ERR_MISSING_STREAM', `no stream for ${jmiRelativePath}`);
    return s;
  }

  /**
   * §7.2 — decrypts [byteFrom, byteTo) of a stream's plaintext.
   *
   * This is the whole random-access story: only the chunks covering the range
   * are read and opened. A tag failure throws; a partial plaintext is never
   * returned (§6.3).
   */
  read(streamId: number, byteFrom: number, byteTo: number): Buffer {
    const s = this.stream(streamId);
    const from = Math.max(0, Math.min(byteFrom, s.byteLength));
    const to = Math.max(from, Math.min(byteTo, s.byteLength));
    const out = Buffer.alloc(to - from);
    if (out.length === 0) return out;

    const cs = this.header.chunkSize;
    const first = Math.floor(from / cs);
    const last = Math.floor((to - 1) / cs);
    let written = 0;

    for (let k = first; k <= last; k++) {
      const plainLen = Math.min(cs, s.byteLength - k * cs);
      const recordOff = s.offset + k * (cs + TAG_BYTES);
      const buf = Buffer.alloc(plainLen + TAG_BYTES);
      if (readSync(this.fd, buf, 0, buf.length, recordOff) !== buf.length) {
        throw new AofasError('AOFAS_ERR_IO', `short read on ${s.path} chunk ${k}`);
      }
      const plain = aeadOpen(
        this.kData,
        nonceFor(s.id, k),
        aadChunk(this.prefix, s.id, k, plainLen),
        buf.subarray(0, plainLen),
        buf.subarray(plainLen),
      );
      if (!plain) throw new AofasError('AOFAS_ERR_AUTH', `${s.path}: chunk ${k} failed authentication`);

      const chunkStart = k * cs;
      const copyFrom = Math.max(from, chunkStart) - chunkStart;
      const copyTo = Math.min(to, chunkStart + plainLen) - chunkStart;
      plain.copy(out, written, copyFrom, copyTo);
      written += copyTo - copyFrom;
    }
    if (written !== out.length) throw new AofasError('AOFAS_ERR_IO', 'incomplete range assembly');
    return out;
  }

  /** Convenience for the player's head preload: the first N frames. */
  readFrames(streamId: number, frameFrom: number, frameCount: number): Buffer {
    const s = this.stream(streamId);
    const bpf = s.channels * bytesPerSample(s.codec);
    return this.read(streamId, frameFrom * bpf, (frameFrom + frameCount) * bpf);
  }

  /** §7.3 — full-stream verification. Maintenance only, never on the load path. */
  verifyStream(streamId: number): boolean {
    const s = this.stream(streamId);
    const step = this.header.chunkSize * 16;
    const h = createHash('sha256');
    for (let at = 0; at < s.byteLength; at += step) {
      h.update(this.read(streamId, at, Math.min(at + step, s.byteLength)));
    }
    return h.digest('hex') === s.sha256;
  }
}
