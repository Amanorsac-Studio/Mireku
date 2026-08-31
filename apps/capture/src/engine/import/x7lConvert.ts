/**
 * Executes an import plan: writes one `.jmi` bundle per selected instrument.
 *
 * The file system is injected, so the whole conversion — including the real
 * client library — runs under vitest exactly as it runs in Electron.
 *
 * Bit-perfect by construction: each sample file is a canonical 44-byte RIFF
 * header followed by a byte-for-byte copy of the source PCM. Nothing is
 * decoded to float, resampled or requantised anywhere on this path.
 */

import type { Preset } from '@jm/jmi-format/web';
import { buildDefaultRack, defaultEngineParams } from '@jm/jmi-format/web';
import { measureKeyzone, pcm16MonoWavHeader, type X7lLibrary } from './x7l';
import { buildImportInstrument, type SampleFacts, type X7lInstrumentPlan } from './x7lPlan';

export interface ImportFs {
  mkdir(dir: string): void;
  writeFile(path: string, data: Uint8Array | string): void;
  join(...parts: string[]): string;
}

export interface ConvertOptions {
  author: string;
  sourceFileName: string;
  onProgress?(p: { instrument: string; done: number; total: number }): void;
}

export interface ConvertedBundle {
  instrumentName: string;
  bundlePath: string;
  sampleCount: number;
  zoneCount: number;
  bytesWritten: number;
  keyRangeLo: number;
  keyRangeHi: number;
  velocityMap: { layer: number; name: string; lo: number; hi: number }[];
  notes: string[];
}

/**
 * A neutral starting preset, so an imported bundle has the same shape as an
 * exported one.
 *
 * CLIENT REVIEW ROUND 7 (item 1): `buildDefaultRack` now returns the world's
 * rack BYPASSED, so an imported instrument opens dry — which is what James
 * asked for. `fxOn` writes the same preset with the rack active, so
 * `importPresets` below can ship both and switching the world's voicing on is
 * one click rather than six.
 */
export function importPreset(plan: X7lInstrumentPlan, fxOn = false): Preset {
  return {
    formatVersion: 1,
    name: fxOn ? 'With FX' : 'Default',
    tags: [plan.world, 'imported'],
    engine: defaultEngineParams(plan.world),
    fxRack: buildDefaultRack(plan.world, { bypassed: !fxOn }),
    ampEnvelope:
      plan.soundType === 'decaying'
        ? { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 }
        : { attackMs: 10, decayMs: 0, sustain: 100, releaseMs: 400 },
  };
}

/** The presets an import writes: dry first, the world's voicing beside it. */
export function importPresets(plan: X7lInstrumentPlan): { fileName: string; preset: Preset }[] {
  return [
    { fileName: 'Default.json', preset: importPreset(plan) },
    { fileName: 'With FX.json', preset: importPreset(plan, true) },
  ];
}

export function convertInstrument(
  bytes: Uint8Array,
  lib: X7lLibrary,
  plan: X7lInstrumentPlan,
  destDir: string,
  fs: ImportFs,
  opts: ConvertOptions,
): ConvertedBundle {
  const bundlePath = fs.join(destDir, plan.bundleName);
  fs.mkdir(fs.join(bundlePath, 'samples'));
  fs.mkdir(fs.join(bundlePath, 'presets'));

  const facts: Record<string, SampleFacts> = {};
  let bytesWritten = 0;
  let done = 0;
  const total = plan.sampleCount;

  for (const zone of plan.zones) {
    for (const s of zone.samples) {
      const wf = lib.waveforms[s.waveformIndex];
      const kz = wf.keyzones[s.keyzoneIndex];
      const header = pcm16MonoWavHeader(kz.pcmBytes, kz.sampleRate);
      const wav = new Uint8Array(header.length + kz.pcmBytes);
      wav.set(header, 0);
      wav.set(bytes.subarray(kz.pcmOffset, kz.pcmOffset + kz.pcmBytes), header.length);
      fs.writeFile(fs.join(bundlePath, ...s.file.split('/')), wav);
      bytesWritten += wav.length;

      const m = measureKeyzone(bytes, kz);
      facts[s.file] = {
        peakDb: m.peakDb,
        lengthMs: round3((kz.lengthSamples / kz.sampleRate) * 1000),
        loop: wf.oneShot
          ? null
          : {
              startSample: kz.loopStartSample,
              endSample: kz.loopEndSample,
              crossfadeSamples: 0,
            },
      };
      done++;
      opts.onProgress?.({ instrument: plan.name, done, total });
    }
  }

  const instrument = buildImportInstrument(plan, facts, {
    author: opts.author,
    sourceFileName: opts.sourceFileName,
  });
  fs.writeFile(fs.join(bundlePath, 'instrument.json'), JSON.stringify(instrument, null, 2));
  for (const { fileName, preset } of importPresets(plan))
    fs.writeFile(fs.join(bundlePath, 'presets', fileName), JSON.stringify(preset, null, 2));

  return {
    instrumentName: plan.name,
    bundlePath,
    sampleCount: total,
    zoneCount: plan.zones.length,
    bytesWritten,
    keyRangeLo: plan.keyRangeLo,
    keyRangeHi: plan.keyRangeHi,
    velocityMap: instrument.velocityMap,
    notes: plan.notes,
  };
}

function round3(n: number): number {
  return Math.round(n * 1000) / 1000;
}
