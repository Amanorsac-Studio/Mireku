/**
 * Is a declared loop a SUSTAIN loop or a DECAY TAIL?
 *
 * The sampled libraries declare a loop region on most keyzones, and the reader
 * carries it over faithfully. But "has a loop" and "sustains while held" are not
 * the same thing, and conflating them is what made held piano and string notes
 * repeat.
 *
 * On the source hardware a loop is read under the element's own falling
 * amplitude envelope. A piano element therefore declares a loop that starts
 * AFTER the attack and runs to the end of the recording: the loop is there so
 * the oscillator has something to read while the envelope fades out, not so the
 * note sustains. Read that same region under a held envelope — which is what
 * this product does, because a patch opens with decay 0 / sustain 100 so the
 * recording is heard untouched — and every wrap jumps from the quiet end of the
 * decay back to a point 20-30 dB louder. The note swells back up, once per loop
 * length, for as long as the key is down.
 *
 * The two kinds are told apart by the SEAM: how much louder the loop's restart
 * point is than the point it jumps from. Measured over the client's whole
 * corpus (553 looped keyzones, 25 waveforms) the two populations are distinct
 * and they split by instrument, not by library:
 *
 *   sustain loops   Brass +0.2  analogpad -2.6  VelvetPad -10.7  M1 Organ -1.7
 *                   Uni CHoir -3.7  String +2.4  Strung High +1.8  Fantasia +0.4
 *                   Staccato H +1.2                       <- median seam, dB
 *   decay tails     Grand Lady +27.6..+31.8   Imperial +28.1..+29.1
 *                   RomancePiano +24.5/+28.8  Stage Ep +23.6/+26.4
 *                   MKS20 pn1/pn2 +28.3/+29.9  SQ1P piano +8.7
 *
 * A decay tail is imported as a one-shot: the note plays the recording and
 * stops, which is what a piano does. A sustain loop is kept and the engine
 * gives it a real crossfade, because every library declares crossfadeSamples 0
 * and a literal zero is a one-sample splice.
 */
import type { X7lKeyzone, X7lWaveform } from './x7l';

/**
 * Seams at or above this are decay tails. The corpus leaves a clear gap here:
 * the loudest-seamed sustain loop is +5.1 dB (analogpad High) and the quietest
 * -seamed decay tail is +8.7 dB (SQ1P piano), so the boundary sits between two
 * measured populations rather than on a round number chosen by eye.
 */
export const TAIL_LOOP_SEAM_DB = 7;

/** Window the seam is measured over, in frames (~46 ms at 44.1 kHz). */
export const SEAM_WINDOW = 2048;

export type LoopKind = 'none' | 'sustain' | 'tail';

/** RMS of a 16-bit LE mono span, in dBFS. Returns -120 for silence. */
function rmsDb(bytes: Uint8Array, byteOffset: number, frames: number, from: number, n: number): number {
  const lo = Math.max(0, Math.min(from, Math.max(0, frames - 1)));
  const hi = Math.min(frames, lo + n);
  let sum = 0;
  let count = 0;
  for (let i = lo; i < hi; i++) {
    const o = byteOffset + i * 2;
    let v = bytes[o] | (bytes[o + 1] << 8);
    if (v > 32767) v -= 65536;
    const f = v / 32768;
    sum += f * f;
    count++;
  }
  if (!count || sum === 0) return -120;
  return 10 * Math.log10(sum / count);
}

/** True when the declared loop covers the whole sample, i.e. is not a loop. */
export function loopSpansWholeSample(kz: X7lKeyzone): boolean {
  return (
    (kz.loopStartSample === 0 && kz.loopEndSample === kz.lengthSamples - 1) ||
    kz.loopStartSample >= kz.loopEndSample
  );
}

/**
 * How much LOUDER the loop's restart point is than the point it wraps from.
 * Positive means the wrap jumps up — the audible swell. Null when there is no
 * loop to measure.
 */
export function loopSeamDb(bytes: Uint8Array, kz: X7lKeyzone): number | null {
  if (loopSpansWholeSample(kz)) return null;
  const atStart = rmsDb(bytes, kz.pcmOffset, kz.lengthSamples, kz.loopStartSample, SEAM_WINDOW);
  const atEnd = rmsDb(bytes, kz.pcmOffset, kz.lengthSamples, kz.loopEndSample - SEAM_WINDOW, SEAM_WINDOW);
  return atStart - atEnd;
}

/**
 * Classify a whole waveform at once, from the MEDIAN seam of its keyzones.
 *
 * Per-waveform rather than per-keyzone on purpose: individual zones scatter
 * widely (Imperial F runs +8.9 to +57.5), and a waveform whose zones disagreed
 * would loop on some notes and not others — the same instrument behaving two
 * ways across the keyboard is worse than either answer applied consistently.
 */
export function classifyWaveformLoop(bytes: Uint8Array, wf: X7lWaveform): LoopKind {
  const seams: number[] = [];
  for (const kz of wf.keyzones) {
    const s = loopSeamDb(bytes, kz);
    if (s !== null) seams.push(s);
  }
  if (!seams.length) return 'none';
  seams.sort((a, b) => a - b);
  const median = seams[Math.floor(seams.length / 2)];
  return median >= TAIL_LOOP_SEAM_DB ? 'tail' : 'sustain';
}
