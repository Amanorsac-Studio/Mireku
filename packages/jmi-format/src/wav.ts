/** Minimal RIFF/WAVE header reader used by the validator (no audio decoding). */

export interface WavInfo {
  channels: number;
  sampleRate: number;
  bitDepth: number;
  /** PCM frames (samples per channel) */
  frames: number;
  format: 'pcm' | 'float' | 'other';
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
