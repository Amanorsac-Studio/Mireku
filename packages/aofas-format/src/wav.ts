/**
 * RIFF/WAVE reader that returns where the audio actually is.
 *
 * The packer stores RAW INTERLEAVED PCM, not the WAV file bytes (spec §5.3):
 * the player wants frames, not a RIFF parse, and stripping the container makes
 * the frame/byte/chunk arithmetic exact.
 */

import type { AofasCodec } from './types.js';

export interface WavData {
  codec: AofasCodec;
  sampleRate: number;
  channels: number;
  frames: number;
  /** offset of the data chunk payload within the file */
  dataOffset: number;
  dataBytes: number;
}

/**
 * `fileSize` is the size of the WHOLE file. The packer parses the header from a
 * small probe buffer rather than reading a 40 MB sample into memory, so the
 * data chunk must be measured against the file, never against the probe — that
 * mistake silently truncates every sample larger than the probe.
 */
export function readWav(buf: Buffer, fileSize = buf.length): WavData {
  if (buf.length < 44 || buf.toString('ascii', 0, 4) !== 'RIFF' || buf.toString('ascii', 8, 12) !== 'WAVE') {
    throw new Error('not a RIFF/WAVE file');
  }
  let pos = 12;
  let audioFormat = 0;
  let channels = 0;
  let sampleRate = 0;
  let bitDepth = 0;
  let dataOffset = -1;
  let dataBytes = -1;

  while (pos + 8 <= buf.length) {
    const id = buf.toString('ascii', pos, pos + 4);
    const size = buf.readUInt32LE(pos + 4);
    if (id === 'fmt ') {
      audioFormat = buf.readUInt16LE(pos + 8);
      channels = buf.readUInt16LE(pos + 10);
      sampleRate = buf.readUInt32LE(pos + 12);
      bitDepth = buf.readUInt16LE(pos + 22);
      // WAVE_FORMAT_EXTENSIBLE carries the real tag in the sub-format GUID
      if (audioFormat === 0xfffe && size >= 40) audioFormat = buf.readUInt16LE(pos + 32);
    } else if (id === 'data') {
      dataOffset = pos + 8;
      // Some writers leave a 0 or 0xFFFFFFFF size on a streamed file; trust the
      // file length in that case rather than refusing a good sample.
      const available = fileSize - dataOffset;
      dataBytes = size > 0 && size !== 0xffffffff ? Math.min(size, available) : available;
      break;
    }
    pos += 8 + size + (size % 2);
  }

  if (!channels || !sampleRate || !bitDepth) throw new Error('WAV missing or unreadable fmt chunk');
  if (dataOffset < 0) throw new Error('WAV missing data chunk');

  let codec: AofasCodec;
  if (audioFormat === 1 && bitDepth === 16) codec = 'pcm_s16le';
  else if (audioFormat === 1 && bitDepth === 24) codec = 'pcm_s24le';
  else if (audioFormat === 3 && bitDepth === 32) codec = 'pcm_f32le';
  else throw new Error(`unsupported WAV encoding: format ${audioFormat}, ${bitDepth}-bit`);

  const bytesPerFrame = channels * (bitDepth / 8);
  // Never let a stray odd byte make byteLength disagree with frames*bytesPerFrame;
  // the reader validates that identity and would reject the file (§7.1 step 10).
  const frames = Math.floor(dataBytes / bytesPerFrame);
  return { codec, sampleRate, channels, frames, dataOffset, dataBytes: frames * bytesPerFrame };
}
