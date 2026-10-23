/**
 * Batch importer, run as a test purely so it executes with the project's
 * existing TypeScript config. NOT a real test: it is skipped unless
 * JM_BATCH_IN and JM_BATCH_OUT are both set, so `npm test` is unaffected.
 *
 *   JM_BATCH_IN="C:/Users/amano/Downloads/JM Sounds" \
 *   JM_BATCH_OUT="<a scratch folder>" \
 *   npx vitest run test/batchImport.test.ts
 *
 * Converts every .X7L / .X8L in the input folder to a .jmi bundle, bit-perfect,
 * one bundle per instrument found inside each library, and prints a per-file
 * table at the end.
 *
 * It NEVER writes over an existing bundle. Bundle names collide across
 * libraries — two different files both want to be called "MKS20" — and an
 * earlier run of this harness overwrote one of the client's bundles that way.
 * A name that is already taken on disk, or already used earlier in this run,
 * gets a numeric suffix instead.
 */
import { describe, expect, it } from 'vitest';
import { mkdirSync, mkdtempSync, writeFileSync, readFileSync, readdirSync, existsSync, rmSync } from 'node:fs';
import { join as pathJoin } from 'node:path';
import { tmpdir } from 'node:os';
import { validateBundle } from '@jm/jmi-format';
import { measureKeyzone, parseX7l, representativeKeyzone, type X7lLibrary } from '../src/engine/import/x7l';
import { planImport } from '../src/engine/import/x7lPlan';
import { convertInstrument, type ImportFs } from '../src/engine/import/x7lConvert';

const IN = process.env.JM_BATCH_IN;
const OUT = process.env.JM_BATCH_OUT;

const diskFs: ImportFs = {
  mkdir: (dir) => mkdirSync(dir, { recursive: true }),
  writeFile: (p, data) => writeFileSync(p, typeof data === 'string' ? data : Buffer.from(data)),
  join: (...parts) => pathJoin(...parts),
};

function levelsFor(lib: X7lLibrary, bytes: Uint8Array): number[] {
  return lib.waveforms.map((wf) => measureKeyzone(bytes, representativeKeyzone(wf)).peakDb);
}

/**
 * A bundle folder name that is free both on disk and within this run.
 *
 * `bundleFolderName` already ends in ".jmi", so the suffix goes before it —
 * "MKS20_2.jmi", not "MKS20.jmi.jmi". (Appending ".jmi" to a name that already
 * had it is what broke this harness's own validate path and its
 * already-present guard: it stat'd and validated a path that never existed,
 * so the guard never fired and every bundle looked invalid.)
 */
function freeBundleName(outDir: string, bundleName: string, taken: Set<string>): string {
  const base = bundleName.replace(/\.jmi$/i, '');
  for (let n = 1; n < 1000; n++) {
    const candidate = n === 1 ? `${base}.jmi` : `${base}_${n}.jmi`;
    if (!taken.has(candidate.toLowerCase()) && !existsSync(pathJoin(outDir, candidate))) {
      taken.add(candidate.toLowerCase());
      return candidate;
    }
  }
  throw new Error(`Cannot find a free bundle name for ${bundleName}`);
}

interface Row {
  file: string;
  status: 'imported' | 'refused' | 'partial';
  bundles: number;
  samples: number;
  mb: number;
  detail: string;
}

describe.skipIf(!IN || !OUT)('batch X7L import', () => {
  it('converts every library in the input folder', { timeout: 6 * 60 * 60 * 1000 }, () => {
    const files = readdirSync(IN!)
      .filter((f) => /\.(x7l|x8l)$/i.test(f))
      .sort((a, b) => a.localeCompare(b));
    mkdirSync(OUT!, { recursive: true });

    const taken = new Set<string>();
    const rows: Row[] = [];

    for (const file of files) {
      const src = pathJoin(IN!, file);
      process.stdout.write(`\n=== ${file} ===\n`);
      let bundles = 0;
      let samples = 0;
      let bytes1 = 0;
      const problems: string[] = [];
      try {
        const bytes = new Uint8Array(readFileSync(src));
        const lib = parseX7l(bytes);
        const plan = planImport(lib, levelsFor(lib, bytes));
        process.stdout.write(
          `  YSFC ${lib.version}, ${lib.waveforms.length} waveform(s) → ${plan.instruments.length} instrument(s)\n`,
        );
        for (const note of plan.notes) process.stdout.write(`  note: ${note}\n`);
        if (plan.instruments.length === 0) {
          rows.push({ file, status: 'refused', bundles: 0, samples: 0, mb: 0, detail: 'no instruments in the plan' });
          continue;
        }
        for (const inst of plan.instruments) {
          const bundleName = freeBundleName(OUT!, inst.bundleName, taken);
          const dest = pathJoin(OUT!, bundleName);
          if (bundleName.toLowerCase() !== inst.bundleName.toLowerCase()) {
            process.stdout.write(`  ${inst.bundleName} is taken → writing ${bundleName}\n`);
          }
          convertInstrument(bytes, lib, { ...inst, bundleName }, OUT!, diskFs, {
            author: 'James Mireku',
            sourceFileName: file,
          });
          const res = validateBundle(dest);
          const errs = res.issues.filter((i) => i.level === 'error');
          process.stdout.write(
            `  ${bundleName}: ${inst.zones.length} zones, ${res.stats?.sampleCount} samples, ` +
              `${inst.channels === 2 ? 'stereo' : 'mono'}, ${inst.windows.length} vel layer(s), ` +
              `${((res.stats?.totalBytes ?? 0) / 1048576).toFixed(0)} MB, ` +
              `${errs.length === 0 ? 'VALID' : 'INVALID'}\n`,
          );
          if (errs.length) {
            errs.slice(0, 3).forEach((e) => process.stdout.write(`      ${e.path}: ${e.message}\n`));
            problems.push(`${bundleName}: ${errs[0].message}`);
          } else {
            bundles++;
            samples += res.stats?.sampleCount ?? 0;
            bytes1 += res.stats?.totalBytes ?? 0;
          }
        }
        rows.push({
          file,
          status: problems.length ? 'partial' : 'imported',
          bundles,
          samples,
          mb: Math.round(bytes1 / 1048576),
          detail: problems.join('; '),
        });
      } catch (e) {
        const why = (e as Error).message;
        rows.push({ file, status: 'refused', bundles, samples, mb: Math.round(bytes1 / 1048576), detail: why });
        process.stdout.write(`  REFUSED: ${why}\n`);
      }
    }

    process.stdout.write(`\n\n==== RESULT: ${rows.filter((r) => r.status === 'imported').length}/${rows.length} libraries imported ====\n`);
    for (const r of rows) {
      process.stdout.write(
        `${r.status.padEnd(9)} ${r.file.padEnd(30)} ${String(r.bundles).padStart(2)} bundle(s) ` +
          `${String(r.samples).padStart(5)} samples ${String(r.mb).padStart(5)} MB  ${r.detail}\n`,
      );
    }
  });
});

/**
 * This one DOES run under `npm test`: never overwriting an existing bundle is
 * the guard that matters, because a previous batch run overwrote one of the
 * client's own bundles.
 */
describe('batch bundle naming', () => {
  it('never reuses a name that is taken on disk or earlier in the run', () => {
    const dir = mkdtempSync(pathJoin(tmpdir(), 'jm-batch-'));
    try {
      mkdirSync(pathJoin(dir, 'MKS20.jmi'), { recursive: true });
      const taken = new Set<string>();
      // the existing folder on disk is respected
      expect(freeBundleName(dir, 'MKS20.jmi', taken)).toBe('MKS20_2.jmi');
      // and so is the name this run just handed out
      expect(freeBundleName(dir, 'MKS20.jmi', taken)).toBe('MKS20_3.jmi');
      // a free name is used as-is, and the suffix goes before ".jmi"
      expect(freeBundleName(dir, 'Piano.jmi', taken)).toBe('Piano.jmi');
      expect(freeBundleName(dir, 'Piano.jmi', taken)).toBe('Piano_2.jmi');
      // ".jmi" is never doubled up
      for (const n of taken) expect(n.endsWith('.jmi.jmi')).toBe(false);
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });
});
