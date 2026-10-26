/**
 * Bundle export builder (§3.7) — pure, Node-safe.
 * Builds the instrument.json content and the sample-file manifest from a
 * session. Actual disk I/O (WAV copy, zip, validate) lives in the Electron
 * main process (or the test harness), which consumes this manifest.
 */

import type { Instrument, Preset, SoundType } from '@jm/jmi-format/web';
import { buildDefaultRack, defaultEngineParams, sampleFileName } from '@jm/jmi-format/web';
import type { SessionState } from './session';
import { requiredRRs, takeKey } from './session';

export interface ExportManifest {
  bundleName: string;
  instrument: Instrument;
  presets: { fileName: string; preset: Preset }[];
  /** session sample file → bundle-relative destination */
  files: { from: string; to: string }[];
  warnings: string[];
}

/** Sanitized bundle folder name: "JM_Rhodes_Warm.jmi". */
export function bundleFolderName(instrumentName: string): string {
  const safe = instrumentName.trim().replace(/[^\w\- ]+/g, '').replace(/\s+/g, '_');
  return `${safe || 'JM_Instrument'}.jmi`;
}

/**
 * Key-range boundaries: each zone spans to fill the gaps between sampled
 * notes (spec §3.2: root ±1 st for the default 3-semitone plan). Boundaries
 * sit at midpoints between adjacent roots; the outermost zones extend to
 * 0 and 127 so every key maps somewhere.
 */
export function keyRanges(notes: number[]): { lo: number; hi: number }[] {
  const sorted = [...notes].sort((a, b) => a - b);
  return sorted.map((root, i) => {
    const lo = i === 0 ? 0 : root - Math.floor((root - sorted[i - 1]) / 2);
    const hi = i === sorted.length - 1 ? 127 : root + Math.floor((sorted[i + 1] - root - 1) / 2);
    return { lo, hi };
  });
}

export function buildInstrument(s: SessionState): { instrument: Instrument; warnings: string[] } {
  const warnings: string[] = [];
  const soundType: SoundType = s.resolvedSoundType ?? 'decaying';
  if (!s.resolvedSoundType) warnings.push('Sound type was never resolved; defaulting to decaying');

  const { plan } = s.config;
  const ranges = keyRanges(plan.notes);
  const zones: Instrument['zones'] = [];

  plan.notes.forEach((note, ni) => {
    for (const layerDef of plan.velocityMap) {
      const rrCount = requiredRRs(s, note, layerDef.layer);
      const roundRobins = [];
      for (let rr = 1; rr <= rrCount; rr++) {
        const take = s.takes[takeKey(note, layerDef.layer, rr)];
        if (!take) {
          warnings.push(`Missing take: ${sampleFileName(note, layerDef.layer, rr)}`);
          continue;
        }
        roundRobins.push({
          file: `samples/${take.file}`,
          peakDb: round2(take.peakDb),
          lengthMs: round2(take.lengthMs),
          loop: take.loop,
        });
      }
      if (roundRobins.length === 0) continue; // zone entirely missing — skip, warn above
      zones.push({
        rootNote: note,
        keyRangeLo: ranges[ni].lo,
        keyRangeHi: ranges[ni].hi,
        velRangeLo: layerDef.lo,
        velRangeHi: layerDef.hi,
        roundRobins,
      });
    }
  });

  const instrument: Instrument = {
    formatVersion: 1,
    meta: {
      name: s.config.instrumentName,
      world: s.config.world,
      author: 'Mireku',
      createdAt: s.config.createdAt,
      captureMode: s.config.captureMode,
      channels: s.config.channels,
      sampleRate: s.config.sampleRate,
      bitDepth: s.config.bitDepth,
      notes: s.sessionNotes.length ? s.sessionNotes.join('\n') : undefined,
    },
    soundType,
    zones,
    velocityMap: plan.velocityMap,
    reserved: { releaseSamples: [], pedalDownZones: [] },
  };
  return { instrument, warnings };
}

export function defaultPreset(s: SessionState, name: string): Preset {
  const soundType = s.resolvedSoundType ?? 'decaying';
  return {
    formatVersion: 1,
    name,
    tags: [s.config.world],
    engine: defaultEngineParams(s.config.world),
    fxRack: buildDefaultRack(s.config.world),
    ampEnvelope:
      soundType === 'decaying'
        ? { attackMs: 2, decayMs: 0, sustain: 100, releaseMs: 250 }
        : { attackMs: 10, decayMs: 0, sustain: 100, releaseMs: 400 },
  };
}

export function buildExportManifest(
  s: SessionState,
  userPresets: { fileName: string; preset: Preset }[] = [],
): ExportManifest {
  const { instrument, warnings } = buildInstrument(s);
  const files: ExportManifest['files'] = [];
  for (const z of instrument.zones) {
    for (const rr of z.roundRobins) {
      const base = rr.file.replace(/^samples\//, '');
      files.push({ from: base, to: rr.file });
    }
  }
  const presets = userPresets.length
    ? userPresets
    : [{ fileName: 'Default.json', preset: defaultPreset(s, 'Default') }];
  return {
    bundleName: bundleFolderName(s.config.instrumentName),
    instrument,
    presets,
    files,
    warnings,
  };
}

function round2(n: number): number {
  return Math.round(n * 100) / 100;
}
