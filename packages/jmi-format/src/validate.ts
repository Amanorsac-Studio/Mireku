import { readFileSync, readdirSync, existsSync, statSync, openSync, readSync, closeSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Ajv, type ValidateFunction } from 'ajv';
import type { Instrument, Preset } from './types.js';
import { readAudioHeader } from './wav.js';

export interface ValidationIssue {
  level: 'error' | 'warning';
  path: string;
  message: string;
}

export interface ValidationResult {
  valid: boolean;
  issues: ValidationIssue[];
  /** parsed instrument.json when schema-valid */
  instrument?: Instrument;
  stats?: { sampleCount: number; presetCount: number; totalBytes: number };
}

const here = dirname(fileURLToPath(import.meta.url));

function loadSchema(name: string): object {
  // dist/ layout: schemas copied to dist/schema by the build; src layout: ../schema
  for (const rel of ['schema', '../schema']) {
    const p = join(here, rel, name);
    if (existsSync(p)) return JSON.parse(readFileSync(p, 'utf8'));
  }
  throw new Error(`Schema not found: ${name}`);
}

let compiled: { instrument: ValidateFunction; preset: ValidateFunction } | null = null;

function validators() {
  if (!compiled) {
    const ajv = new Ajv({ allErrors: true, strict: false });
    // date-time format used by meta.createdAt; a permissive check is fine here
    ajv.addFormat('date-time', /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})$/);
    compiled = {
      instrument: ajv.compile(loadSchema('instrument.schema.json')),
      preset: ajv.compile(loadSchema('preset.schema.json')),
    };
  }
  return compiled;
}

export function validateInstrumentJson(data: unknown): ValidationIssue[] {
  const v = validators().instrument;
  if (v(data)) return [];
  return (v.errors ?? []).map((e) => ({
    level: 'error' as const,
    path: `instrument.json${e.instancePath}`,
    message: `${e.message ?? 'invalid'}${e.params ? ' ' + JSON.stringify(e.params) : ''}`,
  }));
}

export function validatePresetJson(data: unknown, fileLabel = 'preset'): ValidationIssue[] {
  const v = validators().preset;
  if (v(data)) return [];
  return (v.errors ?? []).map((e) => ({
    level: 'error' as const,
    path: `${fileLabel}${e.instancePath}`,
    message: e.message ?? 'invalid',
  }));
}

/** Full bundle validation: schema + cross-file semantic checks + WAV headers. */
export function validateBundle(bundlePath: string): ValidationResult {
  const issues: ValidationIssue[] = [];
  const err = (path: string, message: string) => issues.push({ level: 'error', path, message });
  const warn = (path: string, message: string) => issues.push({ level: 'warning', path, message });

  if (!existsSync(bundlePath) || !statSync(bundlePath).isDirectory()) {
    err(bundlePath, 'Bundle folder does not exist');
    return { valid: false, issues };
  }
  const instPath = join(bundlePath, 'instrument.json');
  if (!existsSync(instPath)) {
    err('instrument.json', 'Missing instrument.json');
    return { valid: false, issues };
  }

  let inst: Instrument;
  try {
    inst = JSON.parse(readFileSync(instPath, 'utf8'));
  } catch (e) {
    err('instrument.json', `JSON parse error: ${(e as Error).message}`);
    return { valid: false, issues };
  }
  issues.push(...validateInstrumentJson(inst));
  if (issues.some((i) => i.level === 'error')) return { valid: false, issues, instrument: inst };

  let totalBytes = 0;
  let sampleCount = 0;

  // Semantic checks over zones
  const allZones = [
    ...inst.zones.map((z, i) => ({ z, zp: `zones[${i}]` })),
    ...(inst.releaseZones ?? []).map((z, i) => ({ z, zp: `releaseZones[${i}]` })),
  ];
  for (const { z, zp } of allZones) {
    if (z.keyRangeLo > z.keyRangeHi) err(zp, `keyRangeLo ${z.keyRangeLo} > keyRangeHi ${z.keyRangeHi}`);
    if (z.velRangeLo > z.velRangeHi) err(zp, `velRangeLo ${z.velRangeLo} > velRangeHi ${z.velRangeHi}`);
    if (z.rootNote < z.keyRangeLo || z.rootNote > z.keyRangeHi)
      warn(zp, `rootNote ${z.rootNote} outside key range ${z.keyRangeLo}–${z.keyRangeHi}`);

    for (let ri = 0; ri < z.roundRobins.length; ri++) {
      const rr = z.roundRobins[ri];
      const rp = `${zp}.roundRobins[${ri}]`;
      const wavPath = join(bundlePath, rr.file);
      if (!existsSync(wavPath)) {
        err(rp, `Missing sample file: ${rr.file}`);
        continue;
      }
      sampleCount++;
      const st = statSync(wavPath);
      totalBytes += st.size;
      try {
        // Read just enough for header chunks
        const fd = openSync(wavPath, 'r');
        const head = Buffer.alloc(Math.min(st.size, 4096));
        readSync(fd, head, 0, head.length, 0);
        closeSync(fd);
        const info = readAudioHeader(head);
        if (info.format !== 'pcm') err(rp, `${rr.file}: not integer PCM (float dump?)`);
        if (info.sampleRate !== inst.meta.sampleRate)
          err(rp, `${rr.file}: sampleRate ${info.sampleRate} != meta ${inst.meta.sampleRate}`);
        if (info.bitDepth !== inst.meta.bitDepth)
          err(rp, `${rr.file}: bitDepth ${info.bitDepth} != meta ${inst.meta.bitDepth}`);
        const expectCh = inst.meta.channels === 'stereo' ? 2 : 1;
        if (info.channels !== expectCh) err(rp, `${rr.file}: ${info.channels}ch, expected ${expectCh}`);
        if (rr.loop) {
          if (rr.loop.endSample > info.frames)
            err(rp, `${rr.file}: loop endSample ${rr.loop.endSample} beyond file length ${info.frames}`);
          if (rr.loop.startSample >= rr.loop.endSample) err(rp, `loop startSample >= endSample`);
          if (rr.loop.crossfadeSamples > rr.loop.endSample - rr.loop.startSample)
            err(rp, `loop crossfade longer than loop region`);
        }
        if (inst.soundType === 'sustaining' && !rr.loop) warn(rp, 'sustaining bundle sample has no loop');
        const expectedMs = (info.frames / info.sampleRate) * 1000;
        if (Math.abs(expectedMs - rr.lengthMs) > 50)
          warn(rp, `${rr.file}: lengthMs ${rr.lengthMs.toFixed(0)} vs actual ${expectedMs.toFixed(0)}`);
      } catch (e) {
        err(rp, `${rr.file}: ${(e as Error).message}`);
      }
    }
  }

  // Velocity map coverage: layers should tile 1..127 without overlap
  const sorted = [...inst.velocityMap].sort((a, b) => a.lo - b.lo);
  for (let i = 1; i < sorted.length; i++) {
    if (sorted[i].lo !== sorted[i - 1].hi + 1)
      warn('velocityMap', `layers ${sorted[i - 1].layer}→${sorted[i].layer} do not tile (${sorted[i - 1].hi} → ${sorted[i].lo})`);
  }

  // Presets
  let presetCount = 0;
  const presetsDir = join(bundlePath, 'presets');
  if (existsSync(presetsDir)) {
    for (const f of readdirSync(presetsDir).filter((f) => f.endsWith('.json'))) {
      presetCount++;
      try {
        const preset: Preset = JSON.parse(readFileSync(join(presetsDir, f), 'utf8'));
        issues.push(...validatePresetJson(preset, `presets/${f}`));
      } catch (e) {
        err(`presets/${f}`, `JSON parse error: ${(e as Error).message}`);
      }
    }
  }
  if (presetCount === 0) warn('presets', 'Bundle has no presets');

  return {
    valid: !issues.some((i) => i.level === 'error'),
    issues,
    instrument: inst,
    stats: { sampleCount, presetCount, totalBytes },
  };
}
