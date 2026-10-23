/**
 * Reader for dropped audio files (pure, Node-safe, no decoding on the write
 * path).
 *
 * This is the untrusted-input boundary of the drag-and-drop importer: every
 * file that lands here came from the user's disk and nothing about it is
 * assumed. Each chunk walk is bounds-checked and anything the reader does not
 * fully understand is rejected with a message that names the problem, because
 * a broken bundle is worse than a refused file.
 *
 * Two separate concerns live here, and keeping them separate is what makes the
 * importer bit-perfect:
 *
 *  • `readAudioFile` reads the *header only* and returns where the PCM starts
 *    and how many bytes it runs for. The conversion path uses nothing else:
 *    it writes a fresh canonical header and copies that byte range verbatim.
 *    No float, no resample, no requantise — see `dropConvert.ts`.
 *
 *  • `decodeChannels` turns the PCM into Float32 for ANALYSIS ONLY — pitch
 *    detection, loop detection, peak and sound-type. Its output never reaches
 *    a sample file.
 */

/** Thrown for any file this reader cannot fully account for. */
export class AudioFileError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'AudioFileError';
  }
}

/** Extensions accepted on the drop path. */
export const DROPPABLE_EXTENSIONS = ['wav', 'wave'] as const;

/**
 * Extensions that are audio but are NOT accepted, with the reason. A dropped
 * `.mp3` cannot be passed through untouched by definition (it would have to be
 * decoded), and `.aif` / `.flac` would need a second container reader that has
 * not been verified against real files — the same rule `.X8L` is held to.
 */
export const REJECTED_EXTENSIONS: Record<string, string> = {
  mp3: 'MP3 is lossy — it cannot be imported untouched, it would have to be decoded and re-encoded.',
  m4a: 'AAC/M4A is lossy — it cannot be imported untouched.',
  ogg: 'Ogg Vorbis is lossy — it cannot be imported untouched.',
  flac: 'FLAC is lossless but this importer has not been verified against FLAC files, so it is refused rather than guessed at.',
  aif: 'AIFF is not read by this importer (big-endian container, unverified here).',
  aiff: 'AIFF is not read by this importer (big-endian container, unverified here).',
  aifc: 'AIFF-C is not read by this importer.',
  caf: 'CAF is not read by this importer.',
  w64: 'Wave64 is not read by this importer.',
  rf64: 'RF64 is not read by this importer.',
};

export type PcmEncoding = 'int' | 'float';

export interface AudioFileInfo {
  sampleRate: number;
  bitDepth: number;
  channels: number;
  /** frames (samples per channel) */
  frames: number;
  encoding: PcmEncoding;
  /** byte offset of the first PCM byte within the file */
  dataOffset: number;
  /** PCM byte count (frames * channels * bitDepth/8) */
  dataBytes: number;
  lengthMs: number;
}

/* ------------------------------------------------------------------ */
/* header                                                             */
/* ------------------------------------------------------------------ */

function ascii(bytes: Uint8Array, at: number, len: number): string {
  let s = '';
  for (let i = 0; i < len; i++) s += String.fromCharCode(bytes[at + i]);
  return s;
}

/** File extension, lowercased, without the dot. */
export function extensionOf(fileName: string): string {
  const i = fileName.lastIndexOf('.');
  return i < 0 ? '' : fileName.slice(i + 1).toLowerCase();
}

/**
 * Read a RIFF/WAVE header. Returns the format and the exact PCM byte range;
 * the audio itself is never touched.
 */
export function readAudioFile(bytes: Uint8Array): AudioFileInfo {
  if (bytes.length < 44) throw new AudioFileError(`File is only ${bytes.length} bytes — too short to be a WAV.`);
  if (ascii(bytes, 0, 4) !== 'RIFF') {
    const tag = ascii(bytes, 0, 4).replace(/[^\x20-\x7e]/g, '.');
    throw new AudioFileError(`Not a RIFF/WAVE file (starts with "${tag}").`);
  }
  if (ascii(bytes, 8, 4) !== 'WAVE') throw new AudioFileError('RIFF file is not WAVE.');

  const riffSize = readU32(bytes, 4);
  // A RIFF size larger than the file means the file is truncated; smaller is
  // tolerated (trailing junk is common) but the chunk walk is still bounded by
  // the real length.
  const limit = Math.min(bytes.length, Math.max(12, riffSize + 8));

  let pos = 12;
  let fmt: { tag: number; channels: number; sampleRate: number; bitDepth: number } | null = null;
  let dataOffset = -1;
  let dataBytes = -1;

  while (pos + 8 <= limit) {
    const id = ascii(bytes, pos, 4);
    const size = readU32(bytes, pos + 4);
    const body = pos + 8;
    if (size > limit - body) {
      // A chunk that claims to run past the end of the file: if it is the data
      // chunk, clamp to what is actually there and say so; anything else is a
      // malformed header and is refused.
      if (id === 'data') {
        dataOffset = body;
        dataBytes = limit - body;
        break;
      }
      throw new AudioFileError(
        `Malformed WAV: "${id}" chunk at ${pos} claims ${size} bytes but only ${limit - body} remain.`,
      );
    }
    if (id === 'fmt ') {
      if (size < 16) throw new AudioFileError(`Malformed WAV: fmt chunk is ${size} bytes (needs at least 16).`);
      let tag = readU16(bytes, body);
      if (tag === 0xfffe) {
        // WAVE_FORMAT_EXTENSIBLE: the real format is the first u16 of the
        // SubFormat GUID, 24 bytes into the chunk body.
        if (size < 40) throw new AudioFileError('Malformed WAV: extensible fmt chunk is too short to hold a SubFormat GUID.');
        tag = readU16(bytes, body + 24);
      }
      fmt = {
        tag,
        channels: readU16(bytes, body + 2),
        sampleRate: readU32(bytes, body + 4),
        bitDepth: readU16(bytes, body + 14),
      };
    } else if (id === 'data') {
      dataOffset = body;
      dataBytes = size;
      break;
    }
    pos = body + size + (size % 2); // chunks are word-aligned
  }

  if (!fmt) throw new AudioFileError('Malformed WAV: no fmt chunk.');
  if (dataOffset < 0) throw new AudioFileError('Malformed WAV: no data chunk.');

  const encoding: PcmEncoding = fmt.tag === 3 ? 'float' : 'int';
  if (fmt.tag !== 1 && fmt.tag !== 3) {
    throw new AudioFileError(
      `WAV is compressed (format tag ${fmt.tag}) — only uncompressed PCM can be imported untouched.`,
    );
  }
  if (fmt.channels < 1 || fmt.channels > 8) throw new AudioFileError(`WAV declares ${fmt.channels} channels.`);
  if (fmt.sampleRate < 1000 || fmt.sampleRate > 768000) {
    throw new AudioFileError(`WAV declares a sample rate of ${fmt.sampleRate} Hz.`);
  }
  if (![8, 16, 24, 32, 64].includes(fmt.bitDepth)) {
    throw new AudioFileError(`WAV declares ${fmt.bitDepth}-bit samples, which this importer does not read.`);
  }
  if (encoding === 'float' && fmt.bitDepth !== 32 && fmt.bitDepth !== 64) {
    throw new AudioFileError(`WAV declares floating-point samples at ${fmt.bitDepth}-bit.`);
  }

  const frameBytes = fmt.channels * (fmt.bitDepth / 8);
  const frames = Math.floor(dataBytes / frameBytes);
  if (frames < 1) throw new AudioFileError('WAV contains no audio (data chunk is empty).');

  return {
    sampleRate: fmt.sampleRate,
    bitDepth: fmt.bitDepth,
    channels: fmt.channels,
    frames,
    encoding,
    dataOffset,
    // trim any partial trailing frame so the copy is always whole frames
    dataBytes: frames * frameBytes,
    lengthMs: (frames / fmt.sampleRate) * 1000,
  };
}

function readU16(b: Uint8Array, at: number): number {
  return b[at] | (b[at + 1] << 8);
}

function readU32(b: Uint8Array, at: number): number {
  return (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;
}

/* ------------------------------------------------------------------ */
/* canonical header (the write path)                                  */
/* ------------------------------------------------------------------ */

/**
 * The 44-byte canonical RIFF header for integer PCM, generalised from the X7L
 * importer's `pcm16MonoWavHeader` to any channel count and bit depth. This is
 * the ONLY thing the converter writes besides a verbatim copy of the source
 * PCM, which is what makes the passthrough bit-perfect.
 */
export function pcmWavHeader(dataBytes: number, sampleRate: number, channels: number, bitDepth: number): Uint8Array {
  const h = new Uint8Array(44);
  const dv = new DataView(h.buffer);
  const tag = (at: number, s: string) => {
    for (let i = 0; i < s.length; i++) h[at + i] = s.charCodeAt(i);
  };
  const blockAlign = channels * (bitDepth / 8);
  tag(0, 'RIFF');
  dv.setUint32(4, 36 + dataBytes, true);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  dv.setUint32(16, 16, true);
  dv.setUint16(20, 1, true); // PCM
  dv.setUint16(22, channels, true);
  dv.setUint32(24, sampleRate, true);
  dv.setUint32(28, sampleRate * blockAlign, true);
  dv.setUint16(32, blockAlign, true);
  dv.setUint16(34, bitDepth, true);
  tag(36, 'data');
  dv.setUint32(40, dataBytes, true);
  return h;
}

/* ------------------------------------------------------------------ */
/* decode — ANALYSIS ONLY                                             */
/* ------------------------------------------------------------------ */

export interface DecodeOptions {
  /** stop after this many frames (pitch analysis never needs the whole tail) */
  maxFrames?: number;
}

/**
 * Decode PCM to one Float32Array per channel, for analysis only.
 *
 * Never call this on the conversion path — `dropConvert.ts` copies bytes.
 */
export function decodeChannels(bytes: Uint8Array, info: AudioFileInfo, opts: DecodeOptions = {}): Float32Array[] {
  const frames = Math.min(info.frames, opts.maxFrames ?? info.frames);
  const bytesPerSample = info.bitDepth / 8;
  const frameBytes = info.channels * bytesPerSample;
  const need = info.dataOffset + frames * frameBytes;
  if (need > bytes.length) throw new AudioFileError('WAV data chunk is shorter than its header declares.');

  const out: Float32Array[] = [];
  for (let c = 0; c < info.channels; c++) out.push(new Float32Array(frames));
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

  for (let f = 0; f < frames; f++) {
    const base = info.dataOffset + f * frameBytes;
    for (let c = 0; c < info.channels; c++) {
      const at = base + c * bytesPerSample;
      out[c][f] = sampleAt(dv, at, info.bitDepth, info.encoding);
    }
  }
  return out;
}

function sampleAt(dv: DataView, at: number, bitDepth: number, encoding: PcmEncoding): number {
  if (encoding === 'float') return bitDepth === 64 ? dv.getFloat64(at, true) : dv.getFloat32(at, true);
  switch (bitDepth) {
    case 8:
      return (dv.getUint8(at) - 128) / 128; // 8-bit WAV is unsigned
    case 16:
      return dv.getInt16(at, true) / 32768;
    case 24: {
      const v = dv.getUint8(at) | (dv.getUint8(at + 1) << 8) | (dv.getInt8(at + 2) << 16);
      return v / 8388608;
    }
    default:
      return dv.getInt32(at, true) / 2147483648;
  }
}

/* ------------------------------------------------------------------ */
/* measurement straight off the PCM                                   */
/* ------------------------------------------------------------------ */

export interface PcmMeasurement {
  /** true peak in dBFS, ≤ 0 (floored at -144 for digital silence) */
  peakDb: number;
  /** RMS of the final 5% of the file, in dBFS */
  tailRmsDb: number;
  /** RMS of the loudest 200 ms window, in dBFS — the body level */
  bodyRmsDb: number;
}

/**
 * Peak and tail level read straight off the source PCM, in the same shape and
 * with the same -144 dB floor as the X7L importer's `measureKeyzone`, so an
 * imported bundle's `peakDb` means the same thing whichever importer wrote it.
 */
export function measurePcm(bytes: Uint8Array, info: AudioFileInfo): PcmMeasurement {
  const bytesPerSample = info.bitDepth / 8;
  const frameBytes = info.channels * bytesPerSample;
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const n = info.frames;
  const tailFrom = Math.floor(n * 0.95);

  const bodyWin = Math.max(1, Math.round(info.sampleRate * 0.2));
  let peak = 0;
  let tailSq = 0;
  let tailN = 0;
  let winSq = 0;
  let bestWinSq = 0;
  const ring = new Float64Array(bodyWin);

  for (let f = 0; f < n; f++) {
    const base = info.dataOffset + f * frameBytes;
    let frameAbs = 0;
    let frameSq = 0;
    for (let c = 0; c < info.channels; c++) {
      const v = sampleAt(dv, base + c * bytesPerSample, info.bitDepth, info.encoding);
      const a = v < 0 ? -v : v;
      if (a > frameAbs) frameAbs = a;
      frameSq += v * v;
    }
    frameSq /= info.channels;
    if (frameAbs > peak) peak = frameAbs;
    if (f >= tailFrom) {
      tailSq += frameSq;
      tailN++;
    }
    const slot = f % bodyWin;
    winSq += frameSq - ring[slot];
    ring[slot] = frameSq;
    if (f >= bodyWin - 1 && winSq > bestWinSq) bestWinSq = winSq;
  }

  const db = (x: number): number => (x <= 0 ? -144 : Math.max(-144, 20 * Math.log10(x)));
  const bodyFrames = Math.min(n, bodyWin);
  return {
    peakDb: Math.min(0, round2(db(peak))),
    tailRmsDb: round2(db(tailN ? Math.sqrt(tailSq / tailN) : 0)),
    bodyRmsDb: round2(db(Math.sqrt(Math.max(0, bestWinSq) / bodyFrames))),
  };
}

function round2(n: number): number {
  return Math.round(n * 100) / 100;
}
