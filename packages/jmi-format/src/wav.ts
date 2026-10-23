/** Minimal RIFF/WAVE and FLAC header readers used by the validator (no audio decoding). */

export interface WavInfo {
  channels: number;
  sampleRate: number;
  bitDepth: number;
  /** PCM frames (samples per channel) */
  frames: number;
  format: 'pcm' | 'float' | 'other';
}

/**
 * FLAC STREAMINFO, which is always the first metadata block: sample rate, channel count, bit depth
 * and total frames live in fixed bit positions right after the 4-byte "fLaC" marker and the 4-byte
 * block header. Bundles may ship FLAC (lossless, roughly a third the size of the WAV), so the
 * validator has to read both.
 */
export function readFlacHeader(buf: Buffer): WavInfo {
  if (buf.length < 42 || buf.toString('ascii', 0, 4) !== 'fLaC') throw new Error('Not a FLAC file');
  if ((buf[4] & 0x7f) !== 0) throw new Error('FLAC: first metadata block is not STREAMINFO');
  const p = 8;                                        // skip "fLaC" + block header
  // 16 bits min block, 16 max block, 24 min frame, 24 max frame, then the packed fields
  const o = p + 10;
  const sampleRate = (buf[o] << 12) | (buf[o + 1] << 4) | (buf[o + 2] >> 4);
  const channels = ((buf[o + 2] >> 1) & 0x07) + 1;
  const bitDepth = (((buf[o + 2] & 0x01) << 4) | (buf[o + 3] >> 4)) + 1;
  // 36-bit total sample count; the top 4 bits sit in the same byte as the bit depth
  const frames = (buf[o + 3] & 0x0f) * 2 ** 32 + buf.readUInt32BE(o + 4);
  if (!sampleRate) throw new Error('FLAC: zero sample rate');
  return { channels, sampleRate, bitDepth, frames, format: 'pcm' };
}

/** Read whichever of the two formats this buffer holds. */
export function readAudioHeader(buf: Buffer): WavInfo {
  return buf.length >= 4 && buf.toString('ascii', 0, 4) === 'fLaC' ? readFlacHeader(buf) : readWavHeader(buf);
}

export function readWavHeader(buf: Buffer): WavInfo {
  if (buf.length < 44 || buf.toString('ascii', 0, 4) !== 'RIFF' || buf.toString('ascii', 8, 12) !== 'WAVE') {
    throw new Error('Not a RIFF/WAVE file');
  }
  let pos = 12;
  let fmt: { audioFormat: number; channels: number; sampleRate: number; bitDepth: number } | null = null;
  let dataBytes = -1;
  while (pos + 8 <= buf.length) {
    const id = buf.toString('ascii', pos, pos + 4);
    const size = buf.readUInt32LE(pos + 4);
    if (id === 'fmt ') {
      fmt = {
        audioFormat: buf.readUInt16LE(pos + 8),
        channels: buf.readUInt16LE(pos + 10),
        sampleRate: buf.readUInt32LE(pos + 12),
        bitDepth: buf.readUInt16LE(pos + 22),
      };
    } else if (id === 'data') {
      dataBytes = size;
      break; // header info complete
    }
    pos += 8 + size + (size % 2);
  }
  if (!fmt) throw new Error('WAV missing fmt chunk');
  if (dataBytes < 0) throw new Error('WAV missing data chunk');
  let format: WavInfo['format'] = 'other';
  let audioFormat = fmt.audioFormat;
  if (audioFormat === 0xfffe) audioFormat = 1; // WAVE_FORMAT_EXTENSIBLE: treat as PCM for our purposes
  if (audioFormat === 1) format = 'pcm';
  else if (audioFormat === 3) format = 'float';
  return {
    channels: fmt.channels,
    sampleRate: fmt.sampleRate,
    bitDepth: fmt.bitDepth,
    frames: dataBytes / (fmt.channels * (fmt.bitDepth / 8)),
    format,
  };
}
