/** The 128-byte AOFAS header (spec §5.1) — encode / decode, byte for byte. */

import { crc32 } from './crypto.js';
import {
  AofasError,
  CIPHER_CHACHA20_POLY1305,
  FLAG_INDEX_DEFLATED,
  FORMAT_VERSION,
  HEADER_PREFIX_BYTES,
  HEADER_SIZE,
  KDF_HKDF_SHA256,
  MAGIC,
  type Header,
} from './types.js';

export function encodeHeader(h: Header): Buffer {
  const b = Buffer.alloc(HEADER_SIZE);
  MAGIC.copy(b, 0);
  b.writeUInt16LE(h.formatVersion, 8);
  b.writeUInt16LE(h.headerSize, 10);
  b.writeUInt32LE(h.flags, 12);
  h.bundleId.copy(b, 16);
  b.writeUInt8(h.kdfId, 32);
  b.writeUInt8(h.cipherId, 33);
  b.writeUInt8(h.keyId, 34);
  b.writeUInt8(0, 35);
  h.fileSalt.copy(b, 36);
  b.writeUInt32LE(h.cardLength, 52);
  b.writeBigUInt64LE(h.indexOffset, 56);
  b.writeUInt32LE(h.indexLength, 64);
  h.indexNonce.copy(b, 68);
  h.indexTag.copy(b, 80);
  b.writeUInt32LE(h.chunkSize, 96);
  b.writeBigUInt64LE(h.dataOffset, 100);
  b.writeBigUInt64LE(h.dataLength, 108);
  b.writeUInt32LE(crc32(b.subarray(0, 116)), 116);
  return b;
}

export function decodeHeader(b: Buffer): Header {
  if (b.length < HEADER_SIZE) throw new AofasError('AOFAS_ERR_IO', 'short header read');
  if (!b.subarray(0, 8).equals(MAGIC)) throw new AofasError('AOFAS_ERR_NOT_AOFAS', 'bad magic');
  if (b.readUInt32LE(116) !== crc32(b.subarray(0, 116))) {
    throw new AofasError('AOFAS_ERR_HEADER_CRC', 'header checksum mismatch');
  }

  const h: Header = {
    formatVersion: b.readUInt16LE(8),
    headerSize: b.readUInt16LE(10),
    flags: b.readUInt32LE(12),
    bundleId: Buffer.from(b.subarray(16, 32)),
    kdfId: b.readUInt8(32),
    cipherId: b.readUInt8(33),
    keyId: b.readUInt8(34),
    fileSalt: Buffer.from(b.subarray(36, 52)),
    cardLength: b.readUInt32LE(52),
    indexOffset: b.readBigUInt64LE(56),
    indexLength: b.readUInt32LE(64),
    indexNonce: Buffer.from(b.subarray(68, 80)),
    indexTag: Buffer.from(b.subarray(80, 96)),
    chunkSize: b.readUInt32LE(96),
    dataOffset: b.readBigUInt64LE(100),
    dataLength: b.readBigUInt64LE(108),
  };

  if (h.formatVersion > FORMAT_VERSION) {
    throw new AofasError('AOFAS_ERR_UNSUPPORTED_VERSION', `format version ${h.formatVersion}`);
  }
  if (h.headerSize < HEADER_SIZE) throw new AofasError('AOFAS_ERR_HEADER', 'headerSize too small');
  if ((h.flags & ~FLAG_INDEX_DEFLATED) !== 0) {
    throw new AofasError('AOFAS_ERR_UNSUPPORTED_FEATURE', `unknown flags 0x${h.flags.toString(16)}`);
  }
  if (h.kdfId !== KDF_HKDF_SHA256 || h.cipherId !== CIPHER_CHACHA20_POLY1305) {
    throw new AofasError('AOFAS_ERR_UNSUPPORTED_FEATURE', 'unknown kdf/cipher');
  }
  if (h.chunkSize < 4096 || (h.chunkSize & (h.chunkSize - 1)) !== 0) {
    throw new AofasError('AOFAS_ERR_HEADER', `chunkSize ${h.chunkSize} is not a power of two >= 4096`);
  }
  return h;
}

/** §6.1 — bytes [0,52) of the header, present in every AAD. */
export function headerPrefix(headerBytes: Buffer): Buffer {
  return Buffer.from(headerBytes.subarray(0, HEADER_PREFIX_BYTES));
}
