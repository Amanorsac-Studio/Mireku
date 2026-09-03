/**
 * AOFAS cryptography — the exact primitives of docs/AOFAS-FORMAT-SPEC.md §3–§6.
 *
 * Everything here is a thin, deliberately boring wrapper over Node's own
 * OpenSSL-backed implementations. There is no hand-rolled crypto in this file
 * and there must never be: the C++ reader in the stage app is the only place
 * that implements the primitives itself, and it is checked against the RFC
 * test vectors plus the round-trip vector this package emits.
 */

import { createCipheriv, createDecipheriv, createHash, hkdfSync, randomBytes } from 'node:crypto';

export const TAG_BYTES = 16;
export const NONCE_BYTES = 12;
export const KEY_BYTES = 32;

/** §5.4 — the plaintext size of one audio chunk. */
export const DEFAULT_CHUNK_SIZE = 65536;

/** §6.2 — stream id reserved for the index. */
export const INDEX_STREAM_ID = 0xffffffff;

export function sha256(data: Buffer): Buffer {
  return createHash('sha256').update(data).digest();
}

/** HKDF-SHA256 → 32 bytes (§4). */
export function hkdf32(ikm: Buffer, salt: Buffer, info: Buffer): Buffer {
  return Buffer.from(hkdfSync('sha256', ikm, salt, info, KEY_BYTES));
}

/**
 * §4 key hierarchy. `bundleId` is the raw 16-byte UUID.
 *
 * K_file  = HKDF(K_studio, fileSalt, "AOFAS/v1/file" || bundleId)
 * K_index = HKDF(K_file,   "",       "AOFAS/v1/index")
 * K_data  = HKDF(K_file,   "",       "AOFAS/v1/data")
 */
export function deriveKeys(studioKey: Buffer, fileSalt: Buffer, bundleId: Buffer) {
  if (studioKey.length !== KEY_BYTES) throw new Error(`studio key must be ${KEY_BYTES} bytes`);
  if (fileSalt.length !== 16) throw new Error('fileSalt must be 16 bytes');
  if (bundleId.length !== 16) throw new Error('bundleId must be 16 bytes');
  const kFile = hkdf32(studioKey, fileSalt, Buffer.concat([Buffer.from('AOFAS/v1/file', 'ascii'), bundleId]));
  return {
    kFile,
    kIndex: hkdf32(kFile, Buffer.alloc(0), Buffer.from('AOFAS/v1/index', 'ascii')),
    kData: hkdf32(kFile, Buffer.alloc(0), Buffer.from('AOFAS/v1/data', 'ascii')),
  };
}

/** §6.2 — nonces are derived, never random, and never stored per chunk. */
export function nonceFor(streamId: number, chunkIndex: number | bigint): Buffer {
  const n = Buffer.alloc(NONCE_BYTES);
  n.writeUInt32LE(streamId >>> 0, 0);
  n.writeBigUInt64LE(BigInt(chunkIndex), 4);
  return n;
}

export const indexNonce = (): Buffer => nonceFor(INDEX_STREAM_ID, 0);

/** §6.1 — AAD for the index. `cardHash` is SHA-256 of the card bytes. */
export function aadIndex(headerPrefix: Buffer, cardHash: Buffer): Buffer {
  return Buffer.concat([headerPrefix, cardHash, Buffer.from('AOFAS/v1/index', 'ascii')]);
}

/** §6.1 — AAD for one audio chunk. */
export function aadChunk(headerPrefix: Buffer, streamId: number, chunkIndex: number, plainLen: number): Buffer {
  const t = Buffer.alloc(16);
  t.writeUInt32LE(streamId >>> 0, 0);
  t.writeBigUInt64LE(BigInt(chunkIndex), 4);
  t.writeUInt32LE(plainLen >>> 0, 12);
  return Buffer.concat([headerPrefix, t]);
}

export function seal(key: Buffer, nonce: Buffer, aad: Buffer, plain: Buffer): { ct: Buffer; tag: Buffer } {
  const c = createCipheriv('chacha20-poly1305', key, nonce, { authTagLength: TAG_BYTES });
  c.setAAD(aad, { plaintextLength: plain.length });
  const ct = Buffer.concat([c.update(plain), c.final()]);
  return { ct, tag: c.getAuthTag() };
}

/**
 * Returns null on authentication failure — NEVER a partial plaintext.
 * Callers must treat null as fatal for that region (§6.3).
 */
export function open(key: Buffer, nonce: Buffer, aad: Buffer, ct: Buffer, tag: Buffer): Buffer | null {
  try {
    const d = createDecipheriv('chacha20-poly1305', key, nonce, { authTagLength: TAG_BYTES });
    d.setAAD(aad, { plaintextLength: ct.length });
    d.setAuthTag(tag);
    return Buffer.concat([d.update(ct), d.final()]);
  } catch {
    return null;
  }
}

export function randomKey(): Buffer {
  return randomBytes(KEY_BYTES);
}

/** CRC-32 (IEEE), as used by zlib/PNG. Header sanity only — not a security control. */
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

export function crc32(buf: Buffer): number {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
