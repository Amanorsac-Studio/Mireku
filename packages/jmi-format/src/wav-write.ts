/** 24-bit (and 16-bit) integer PCM WAV writer — proper packed PCM, not float dumps. */

export interface WavWriteOptions {
  sampleRate: number;
  bitDepth: 16 | 24;
  /** one Float32Array per channel, equal lengths, samples in -1..1 */
  channels: Float32Array[];
}

export function encodeWav(opts: WavWriteOptions): Buffer {
  const { sampleRate, bitDepth, channels } = opts;
  const numCh = channels.length;
  if (numCh < 1) throw new Error('encodeWav: no channels');
  const frames = channels[0].length;
  for (const ch of channels) if (ch.length !== frames) throw new Error('encodeWav: channel length mismatch');

  const bytesPerSample = bitDepth / 8;
  const blockAlign = numCh * bytesPerSample;
  const dataBytes = frames * blockAlign;
  const buf = Buffer.alloc(44 + dataBytes);

  buf.write('RIFF', 0, 'ascii');
  buf.writeUInt32LE(36 + dataBytes, 4);
  buf.write('WAVE', 8, 'ascii');
  buf.write('fmt ', 12, 'ascii');
  buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20); // PCM
  buf.writeUInt16LE(numCh, 22);
  buf.writeUInt32LE(sampleRate, 24);
  buf.writeUInt32LE(sampleRate * blockAlign, 28);
  buf.writeUInt16LE(blockAlign, 32);
  buf.writeUInt16LE(bitDepth, 34);
  buf.write('data', 36, 'ascii');
  buf.writeUInt32LE(dataBytes, 40);

  let pos = 44;
  if (bitDepth === 24) {
    const max = 8388607; // 2^23 - 1
    for (let i = 0; i < frames; i++) {
      for (let c = 0; c < numCh; c++) {
        let s = Math.max(-1, Math.min(1, channels[c][i]));
        let v = Math.round(s * max);
        if (v < 0) v += 16777216; // two's complement in 24 bits
        buf[pos++] = v & 0xff;
        buf[pos++] = (v >> 8) & 0xff;
        buf[pos++] = (v >> 16) & 0xff;
      }
    }
  } else {
    for (let i = 0; i < frames; i++) {
      for (let c = 0; c < numCh; c++) {
        const s = Math.max(-1, Math.min(1, channels[c][i]));
        buf.writeInt16LE(Math.round(s * 32767), pos);
        pos += 2;
      }
    }
  }
  return buf;
}

/** Decode integer-PCM WAV data back to Float32 channels (for tests / loop audition). */
export function decodeWav(buf: Buffer): { sampleRate: number; bitDepth: number; channels: Float32Array[] } {
  if (buf.toString('ascii', 0, 4) !== 'RIFF' || buf.toString('ascii', 8, 12) !== 'WAVE')
    throw new Error('Not a WAV');
  let pos = 12;
  let numCh = 0, sampleRate = 0, bitDepth = 0;
  let dataStart = -1, dataBytes = 0;
  while (pos + 8 <= buf.length) {
    const id = buf.toString('ascii', pos, pos + 4);
    const size = buf.readUInt32LE(pos + 4);
    if (id === 'fmt ') {
      numCh = buf.readUInt16LE(pos + 10);
      sampleRate = buf.readUInt32LE(pos + 12);
      bitDepth = buf.readUInt16LE(pos + 22);
    } else if (id === 'data') {
      dataStart = pos + 8;
      dataBytes = size;
      break;
    }
    pos += 8 + size + (size % 2);
  }
  if (dataStart < 0 || !numCh) throw new Error('Bad WAV chunks');
  const bytesPerSample = bitDepth / 8;
  const frames = Math.floor(dataBytes / (numCh * bytesPerSample));
  const channels = Array.from({ length: numCh }, () => new Float32Array(frames));
  let p = dataStart;
  for (let i = 0; i < frames; i++) {
    for (let c = 0; c < numCh; c++) {
      let s: number;
      if (bitDepth === 24) {
        let v = buf[p] | (buf[p + 1] << 8) | (buf[p + 2] << 16);
        if (v & 0x800000) v -= 16777216;
        s = v / 8388608;
        p += 3;
      } else if (bitDepth === 16) {
        s = buf.readInt16LE(p) / 32768;
        p += 2;
      } else {
        throw new Error(`Unsupported bit depth ${bitDepth}`);
      }
      channels[c][i] = s;
    }
  }
  return { sampleRate, bitDepth, channels };
}
