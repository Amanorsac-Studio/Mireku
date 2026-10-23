/**
 * flac.mjs — lossless FLAC encoding for bundle samples.
 *
 * FLAC halves the size of a sampled instrument with no quality loss whatsoever: the decoded
 * samples are bit-identical to the WAV they came from (verifyFlac() below proves it on real data).
 * Sanctuary reads FLAC through JUCE's format manager; the sampler app decodes it in the main
 * process. Interleaving is done in 32-bit ints because libFLAC takes integer samples.
 */
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);

let flacPromise = null;
/** Load and initialise the wasm build once. */
function flacReady() {
  flacPromise ??= new Promise((resolve, reject) => {
    // the .wasm build fetches its binary over HTTP, which has no meaning in Node: use the
    // self-contained asm.js build (slower, but this runs offline once per sample)
    const Flac = require('libflacjs/dist/libflac.js');
    if (Flac.isReady()) return resolve(Flac);
    Flac.on('ready', () => resolve(Flac));
    setTimeout(() => reject(new Error('libflac did not become ready')), 20000);
  });
  return flacPromise;
}

const clampInt = (v, bits) => {
  const max = Math.pow(2, bits - 1) - 1;
  const min = -Math.pow(2, bits - 1);
  return v > max ? max : v < min ? min : v;
};

/**
 * Encode float channels to a FLAC file.
 *
 * @param channels     Float32Array per channel, -1..1
 * @param sampleRate   Hz
 * @param bitDepth     16 or 24 — must match what the WAV would have used, so the result is the
 *                     same quantisation, just smaller
 * @param compression  0..8 (8 = smallest, still lossless; encoding is offline so take the best)
 */
export async function encodeFlac(channels, sampleRate, bitDepth = 16, compression = 8) {
  const Flac = await flacReady();
  const nch = channels.length, n = channels[0].length;
  const scale = Math.pow(2, bitDepth - 1);
  // interleave + quantise exactly as the WAV writer does (round, then clamp)
  const inter = new Int32Array(n * nch);
  for (let c = 0; c < nch; c++) {
    const src = channels[c];
    for (let i = 0; i < n; i++) inter[i * nch + c] = clampInt(Math.round(src[i] * scale), bitDepth);
  }

  const parts = [];
  let total = 0;
  const id = Flac.create_libflac_encoder(sampleRate, nch, bitDepth, compression, n, true);
  if (!id) throw new Error('could not create a FLAC encoder');
  try {
    const status = Flac.init_encoder_stream(
      id,
      (buf) => { const copy = new Uint8Array(buf); parts.push(copy); total += copy.length; },
      () => {},
    );
    if (status !== 0) throw new Error(`FLAC encoder init failed (status ${status})`);
    const CHUNK = 1 << 16;                                    // frames per call
    for (let off = 0; off < n; off += CHUNK) {
      const frames = Math.min(CHUNK, n - off);
      const slice = inter.subarray(off * nch, (off + frames) * nch);
      if (!Flac.FLAC__stream_encoder_process_interleaved(id, slice, frames)) {
        throw new Error(`FLAC encoding failed: ${Flac.FLAC__stream_encoder_get_state(id)}`);
      }
    }
    Flac.FLAC__stream_encoder_finish(id);
  } finally {
    Flac.FLAC__stream_encoder_delete(id);
  }
  const out = Buffer.allocUnsafe(total);
  let at = 0;
  for (const p of parts) { out.set(p, at); at += p.length; }
  return out;
}

/**
 * Decode a FLAC buffer back to float channels — used by verifyFlac and handy for checks.
 * Returns { sampleRate, bitDepth, channels }.
 */
export async function decodeFlac(buffer) {
  const { FLACDecoder } = await import('@wasm-audio-decoders/flac');
  const dec = new FLACDecoder();
  await dec.ready;
  try {
    const { channelData, samplesDecoded, sampleRate, bitDepth } = await dec.decodeFile(new Uint8Array(buffer));
    return {
      sampleRate, bitDepth,
      channels: channelData.map((c) => c.subarray(0, samplesDecoded)),
    };
  } finally {
    dec.free();
  }
}

/**
 * Prove losslessness: decode the FLAC and compare with the quantised integers that a WAV of the
 * same bit depth would have held. Returns the largest difference in integer steps (0 = identical).
 */
export async function verifyFlac(flacBuffer, channels, bitDepth = 16) {
  const back = await decodeFlac(flacBuffer);
  const scale = Math.pow(2, bitDepth - 1);
  let worst = 0;
  const n = channels[0].length;
  // a truncated or wider file is not "lossless" however well the overlap matches
  if (back.channels.length !== channels.length || back.channels[0].length !== n) return Infinity;
  for (let c = 0; c < channels.length; c++) {
    for (let i = 0; i < n; i++) {
      const want = clampInt(Math.round(channels[c][i] * scale), bitDepth);
      const got = Math.round(back.channels[c][i] * scale);
      const d = Math.abs(want - got);
      if (d > worst) worst = d;
    }
  }
  return worst;
}
