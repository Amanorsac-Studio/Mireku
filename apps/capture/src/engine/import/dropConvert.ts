/**
 * Executes a drop-import plan: writes one `.jmi` bundle from the dropped
 * audio files.
 *
 * The file system is injected (same `ImportFs` the X7L converter uses), so the
 * whole conversion — including the client's real sample sets — runs under
 * vitest exactly as it runs in Electron.
 *
 * **Bit-perfect by construction.** For every sample the plan marked
 * `passthrough`, the written file is a canonical 44-byte RIFF header followed
 * by a byte-for-byte copy of the source file's `data` chunk. No decode, no
 * float, no resample, no requantise, no normalise, no fade. The ONLY samples
 * that are not byte-identical are the ones the user explicitly asked to have
 * converted so the bundle could be internally consistent, and those are named
 * one by one in `meta.notes`.
 */

import type { LoopRegion, Preset, SoundType } from '@jm/jmi-format/web';
import { buildDefaultRack, defaultEngineParams } from '@jm/jmi-format/web';
import { detectLoop } from '../dsp/loop';
import { decodeChannels, measurePcm, pcmWavHeader, readAudioFile, type AudioFileInfo } from './audioFile';
import { buildDropInstrument, type BundleFormat, type DropImportPlan, type DropSampleFacts } from './dropPlan';
import type { ImportFs } from './x7lConvert';

/** Reading side of the injected file system. */
export interface DropConvertFs extends ImportFs {
  readFile(path: string): Uint8Array;
}

export interface DropConvertOptions {
  author: string;
  /** what the user dropped, for the provenance line in meta.notes */
  sourceLabel: string;
  onProgress?(p: { done: number; total: number; file: string }): void;
}

export interface ConvertedDropBundle {
  instrumentName: string;
  bundlePath: string;
  sampleCount: number;
  zoneCount: number;
  bytesWritten: number;
  /** samples whose PCM is byte-identical to the source */
  passthroughCount: number;
  /** samples that were format-converted */
  convertedCount: number;
  /** samples that ended up with a loop region */
  loopedCount: number;
  keyRangeLo: number;
  keyRangeHi: number;
  velocityMap: { layer: number; name: string; lo: number; hi: number }[];
  notes: string[];
  warnings: string[];
}

/**
 * A neutral starting preset, so a dropped import has the same shape as an
 * exported bundle and as an X7L import. Mirrors `importPreset` in
 * `x7lConvert.ts`: the rack ships BYPASSED so an imported instrument opens dry
 * (client review round 7), with the world's voicing beside it as one click.
 */
export function dropPreset(plan: DropImportPlan, fxOn = false): Preset {
  return {
    formatVersion: 1,
    name: fxOn ? 'With FX' : 'Default',
    tags: [plan.world, 'imported'],
    engine: defaultEngineParams(plan.world),
    fxRack: buildDefaultRack(plan.world, { bypassed: !fxOn }),
    ampEnvelope: envelopeFor(plan.soundType),
  };
}

function envelopeFor(soundType: SoundType) {
  return soundType === 'decaying'
    ? { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 }
    : { attackMs: 10, decayMs: 0, sustain: 100, releaseMs: 400 };
}

export function dropPresets(plan: DropImportPlan): { fileName: string; preset: Preset }[] {
  return [
    { fileName: 'Default.json', preset: dropPreset(plan) },
    { fileName: 'With FX.json', preset: dropPreset(plan, true) },
  ];
}

/* ------------------------------------------------------------------ */
/* conversion                                                         */
/* ------------------------------------------------------------------ */

export function convertDrop(
  plan: DropImportPlan,
  destDir: string,
  fs: DropConvertFs,
  opts: DropConvertOptions,
): ConvertedDropBundle {
  if (!plan.usable) throw new Error('Nothing in this drop can be converted.');

  const bundlePath = fs.join(destDir, plan.bundleName);
  fs.mkdir(fs.join(bundlePath, 'samples'));
  fs.mkdir(fs.join(bundlePath, 'presets'));

  const nameOf = new Map(plan.rows.map((r) => [r.fileIndex, r.fileName]));

  const facts: Record<string, DropSampleFacts> = {};
  const warnings: string[] = [];
  let bytesWritten = 0;
  let passthroughCount = 0;
  let convertedCount = 0;
  let loopedCount = 0;
  let done = 0;
  const total = plan.sampleCount;

  for (const zone of plan.zones) {
    for (const s of zone.samples) {
      const srcPath = plan.sourcePaths[s.fileIndex];
      if (!srcPath) throw new Error(`No source path for file index ${s.fileIndex} (${nameOf.get(s.fileIndex) ?? '?'})`);
      const bytes = fs.readFile(srcPath);
      const info = readAudioFile(bytes);

      let wav: Uint8Array;
      if (s.passthrough) {
        assertFormat(info, plan.format, srcPath);
        const header = pcmWavHeader(info.dataBytes, info.sampleRate, info.channels, info.bitDepth);
        wav = new Uint8Array(header.length + info.dataBytes);
        wav.set(header, 0);
        wav.set(bytes.subarray(info.dataOffset, info.dataOffset + info.dataBytes), header.length);
        passthroughCount++;
      } else {
        if (info.sampleRate !== plan.format.sampleRate) {
          throw new Error(
            `${nameOf.get(s.fileIndex)} is ${info.sampleRate} Hz and the bundle is ${plan.format.sampleRate} Hz. This importer never resamples.`,
          );
        }
        wav = requantise(bytes, info, plan.format);
        convertedCount++;
      }

      fs.writeFile(fs.join(bundlePath, ...s.file.split('/')), wav);
      bytesWritten += wav.length;

      // Measure what was actually written, so peakDb describes the bundle's
      // audio rather than the source's when a conversion happened.
      const writtenInfo = readAudioFile(wav);
      const m = measurePcm(wav, writtenInfo);

      let loop: LoopRegion | null = null;
      if (plan.soundType === 'sustaining') {
        loop = findLoop(wav, writtenInfo);
        if (loop) loopedCount++;
        else warnings.push(`${s.file}: no stable loop could be found, so it is written as a one-shot (loop: null).`);
      }

      facts[s.file] = {
        peakDb: m.peakDb,
        lengthMs: round3((writtenInfo.frames / writtenInfo.sampleRate) * 1000),
        loop,
      };
      done++;
      opts.onProgress?.({ done, total, file: s.file });
    }
  }

  const instrument = buildDropInstrument(plan, facts, {
    author: opts.author,
    sourceLabel: opts.sourceLabel,
  });
  fs.writeFile(fs.join(bundlePath, 'instrument.json'), JSON.stringify(instrument, null, 2));
  for (const { fileName, preset } of dropPresets(plan)) {
    fs.writeFile(fs.join(bundlePath, 'presets', fileName), JSON.stringify(preset, null, 2));
  }

  return {
    instrumentName: plan.name,
    bundlePath,
    sampleCount: total,
    zoneCount: plan.zones.length,
    bytesWritten,
    passthroughCount,
    convertedCount,
    loopedCount,
    keyRangeLo: plan.keyRangeLo,
    keyRangeHi: plan.keyRangeHi,
    velocityMap: instrument.velocityMap,
    notes: plan.notes,
    warnings: [...plan.warnings, ...warnings],
  };
}

/** A passthrough that does not match the bundle's format would break it. */
function assertFormat(info: AudioFileInfo, want: BundleFormat, path: string): void {
  if (
    info.sampleRate !== want.sampleRate ||
    info.bitDepth !== want.bitDepth ||
    info.channels !== want.channels ||
    info.encoding !== want.encoding
  ) {
    throw new Error(
      `${path} changed on disk since it was inspected (now ${info.sampleRate} Hz / ${info.bitDepth}-bit / ${info.channels}ch, expected ${want.sampleRate} / ${want.bitDepth} / ${want.channels}).`,
    );
  }
}

/**
 * The one path that does touch the audio, and only for files the user chose to
 * include at the wrong bit depth or channel count.
 *
 * Bit depth is rounded, not dithered: a dither would be a deliberate addition
 * of noise to audio the client asked to leave alone, and anyone importing a
 * 32-bit file into a 24-bit bundle is not asking for noise shaping. Channel
 * count folds by averaging (stereo → mono) or duplicating (mono → stereo).
 * The sample rate is never touched — `convertDrop` throws if it would have to.
 */
export function requantise(bytes: Uint8Array, info: AudioFileInfo, want: BundleFormat): Uint8Array {
  const channels = decodeChannels(bytes, info);
  let out = channels;
  if (want.channels === 1 && channels.length > 1) {
    const n = channels[0].length;
    const mono = new Float32Array(n);
    for (const ch of channels) for (let i = 0; i < n; i++) mono[i] += ch[i] / channels.length;
    out = [mono];
  } else if (want.channels === 2 && channels.length === 1) {
    out = [channels[0], channels[0].slice()];
  } else if (want.channels !== channels.length) {
    out = channels.slice(0, want.channels);
  }
  return encodeIntPcm(out, info.sampleRate, want.bitDepth);
}

/** Integer-PCM WAV writer for the conversion path (16 / 24 / 32-bit). */
export function encodeIntPcm(channels: Float32Array[], sampleRate: number, bitDepth: number): Uint8Array {
  const numCh = channels.length;
  const frames = channels[0].length;
  const bytesPerSample = bitDepth / 8;
  const dataBytes = frames * numCh * bytesPerSample;
  const header = pcmWavHeader(dataBytes, sampleRate, numCh, bitDepth);
  const out = new Uint8Array(header.length + dataBytes);
  out.set(header, 0);
  const dv = new DataView(out.buffer, header.length, dataBytes);
  const peak = bitDepth === 16 ? 32767 : bitDepth === 24 ? 8388607 : 2147483647;

  let at = 0;
  for (let f = 0; f < frames; f++) {
    for (let c = 0; c < numCh; c++) {
      const x = Math.max(-1, Math.min(1, channels[c][f]));
      const v = Math.max(-peak - 1, Math.min(peak, Math.round(x * peak)));
      if (bitDepth === 16) dv.setInt16(at, v, true);
      else if (bitDepth === 24) {
        dv.setUint8(at, v & 0xff);
        dv.setUint8(at + 1, (v >> 8) & 0xff);
        dv.setUint8(at + 2, (v >> 16) & 0xff);
      } else dv.setInt32(at, v, true);
      at += bytesPerSample;
    }
  }
  return out;
}

/**
 * Loop points for sustaining material, from Capture's existing loop detector.
 * The detector's default search window is 4–10 s, which assumes a held
 * Guided-mode capture; a dropped sample may be much shorter, so the window is
 * scaled to the file. A sample with no stable loop is left as a one-shot
 * rather than given a fabricated one.
 */
export function findLoop(bytes: Uint8Array, info: AudioFileInfo): LoopRegion | null {
  const seconds = info.frames / info.sampleRate;
  if (seconds < 1.5) return null; // nothing to loop
  const channels = decodeChannels(bytes, info);
  const searchStartS = Math.min(4, seconds * 0.35);
  const searchEndS = Math.min(Math.max(searchStartS + 1, seconds * 0.95), seconds);
  const span = searchEndS - searchStartS;
  return detectLoop(channels, info.sampleRate, {
    searchStartS,
    searchEndS,
    minLoopS: Math.min(0.5, span / 4),
    maxLoopS: Math.min(4, span / 2),
  });
}

function round3(n: number): number {
  return Math.round(n * 1000) / 1000;
}
