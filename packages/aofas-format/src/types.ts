/** AOFAS v1 types — mirrors docs/AOFAS-FORMAT-SPEC.md §5. */

import type { Instrument, Preset } from '@jm/jmi-format';

export const MAGIC = Buffer.from([0x41, 0x4f, 0x46, 0x41, 0x53, 0x1a, 0x0d, 0x0a]);
export const HEADER_SIZE = 128;
export const HEADER_PREFIX_BYTES = 52;
export const FORMAT_VERSION = 1;
export const KDF_HKDF_SHA256 = 1;
export const CIPHER_CHACHA20_POLY1305 = 1;

/** §5.1 flags */
export const FLAG_INDEX_DEFLATED = 1 << 0;
export const FLAG_LICENSED = 1 << 1;

export interface Header {
  formatVersion: number;
  headerSize: number;
  flags: number;
  bundleId: Buffer;
  kdfId: number;
  cipherId: number;
  keyId: number;
  fileSalt: Buffer;
  cardLength: number;
  indexOffset: bigint;
  indexLength: number;
  indexNonce: Buffer;
  indexTag: Buffer;
  chunkSize: number;
  dataOffset: bigint;
  dataLength: bigint;
}

/** §5.2 — plaintext, keyless, display-only. */
export interface Card {
  format: 'aofas';
  formatVersion: number;
  bundleId: string;
  name: string;
  author: string;
  world: string;
  createdAt: string;
  sampleCount: number;
  totalAudioBytes: number;
  minAppVersion: string;
  keyId: number;
  [k: string]: unknown;
}

export type AofasCodec = 'pcm_s16le' | 'pcm_s24le' | 'pcm_f32le';

/** §5.3 — one sample's audio, as stored. */
export interface StreamInfo {
  id: number;
  /** the .jmi-relative path — the join key back to instrument.json */
  path: string;
  codec: AofasCodec;
  sampleRate: number;
  channels: number;
  frames: number;
  byteLength: number;
  offset: number;
  chunkCount: number;
  sha256: string;
}

/** §5.3 — the sealed table of contents. */
export interface AofasIndex {
  aofas: 1;
  instrument: Instrument;
  presets: { fileName: string; preset: Preset }[];
  streams: StreamInfo[];
}

export function bytesPerSample(codec: AofasCodec): number {
  switch (codec) {
    case 'pcm_s16le':
      return 2;
    case 'pcm_s24le':
      return 3;
    case 'pcm_f32le':
      return 4;
  }
}

/** §9 — error codes. Distinct because they mean different things to the user. */
export type AofasErrorCode =
  | 'AOFAS_ERR_NOT_AOFAS'
  | 'AOFAS_ERR_HEADER'
  | 'AOFAS_ERR_HEADER_CRC'
  | 'AOFAS_ERR_UNSUPPORTED_VERSION'
  | 'AOFAS_ERR_UNSUPPORTED_FEATURE'
  | 'AOFAS_ERR_UNKNOWN_KEY'
  | 'AOFAS_ERR_BAD_NONCE'
  | 'AOFAS_ERR_AUTH'
  | 'AOFAS_ERR_INDEX'
  | 'AOFAS_ERR_MISSING_STREAM'
  | 'AOFAS_ERR_IO';

export class AofasError extends Error {
  constructor(
    readonly code: AofasErrorCode,
    message: string,
  ) {
    super(`${code}: ${message}`);
    this.name = 'AofasError';
  }
}

/** The message a player should show a user, per §9. */
export function userMessage(code: AofasErrorCode): string {
  switch (code) {
    case 'AOFAS_ERR_NOT_AOFAS':
      return 'Not an Amanorsac library file.';
    case 'AOFAS_ERR_UNSUPPORTED_VERSION':
    case 'AOFAS_ERR_UNSUPPORTED_FEATURE':
    case 'AOFAS_ERR_UNKNOWN_KEY':
      return 'This library needs a newer version of the app.';
    case 'AOFAS_ERR_AUTH':
      return 'This library is damaged, or was not made for this app.';
    case 'AOFAS_ERR_IO':
      return 'Could not read the library file.';
    default:
      return 'This file is damaged.';
  }
}
