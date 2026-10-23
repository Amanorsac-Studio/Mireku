/**
 * Batch importer, run as a test purely so it executes with the project's
 * existing TypeScript config. NOT a real test: it is skipped unless
 * JM_BATCH_IN is set, so `npm test` is unaffected.
 *
 *   JM_BATCH_IN="C:/Users/amano/Downloads/JM Sounds" \
 *   JM_BATCH_OUT="C:/Users/amano/Documents/JM Signature/Library" \
 *   npx vitest run test/batchImport.test.ts
 *
 * Converts every .X7L / .X8L in the input folder to a .jmi bundle, bit-perfect,
 * one bundle per instrument found inside each library.
 */
import { describe, it } from 'vitest';
import { mkdirSync, writeFileSync, readFileSync, readdirSync, existsSync } from 'node:fs';
import { join as pathJoin } from 'node:path';
import { validateBundle } from '@jm/jmi-format';
import { measureKeyzone, parseX7l, representativeKeyzone, type X7lLibrary } from '../src/engine/import/x7l';
import { planImport } from '../src/engine/import/x7lPlan';
import { convertInstrument, type ImportFs } from '../src/engine/import/x7lConvert';

const IN = process.env.JM_BATCH_IN;
const OUT = process.env.JM_BATCH_OUT;

const diskFs: ImportFs = {
  mkdir: (dir) => mkdirSync(dir, { recursive: true }),
  writeFile: (p, data) => writeFileSync(p, data as Uint8Array),
  join: (...parts) => pathJoin(...parts),
};

function levelsFor(lib: X7lLibrary, bytes: Uint8Array): number[] {
  return lib.waveforms.map((wf) => measureKeyzone(bytes, representativeKeyzone(wf)).peakDb);
}

describe.skipIf(!IN || !OUT)('batch X7L import', () => {
  it('converts every library in the input folder', { timeout: 60 * 60 * 1000 }, () => {
    const files = readdirSync(IN!).filter((f) => /\.(x7l|x8l)$/i.test(f));
    mkdirSync(OUT!, { recursive: true });

    const ok: string[] = [];
    const failed: { file: string; why: string }[] = [];

    for (const file of files) {
      const src = pathJoin(IN!, file);
      process.stdout.write(`\n=== ${file} ===\n`);
      try {
        const bytes = new Uint8Array(readFileSync(src));
        const lib = parseX7l(bytes);
        const plan = planImport(lib, levelsFor(lib, bytes));
        if (plan.instruments.length === 0) {
          failed.push({ file, why: 'no instruments with embedded audio (parameter-only library?)' });
          process.stdout.write('  no embedded audio — skipped\n');
          continue;
        }
        for (const inst of plan.instruments) {
          const dest = pathJoin(OUT!, `${inst.bundleName}.jmi`);
          if (existsSync(dest)) {
            process.stdout.write(`  ${inst.bundleName}: already present, skipped\n`);
            continue;
          }
          convertInstrument(bytes, lib, inst, OUT!, diskFs, {
            author: 'James Mireku',
            sourceFileName: file,
          });
          const res = validateBundle(dest);
          const errs = res.issues.filter((i) => i.level === 'error');
          process.stdout.write(
            `  ${inst.bundleName}: ${res.stats?.sampleCount} samples, ` +
              `${((res.stats?.totalBytes ?? 0) / 1048576).toFixed(0)} MB, ` +
              `${errs.length === 0 ? 'VALID' : 'INVALID'}\n`,
          );
          if (errs.length) {
            errs.slice(0, 3).forEach((e) => process.stdout.write(`      ${e.path}: ${e.message}\n`));
            failed.push({ file: inst.bundleName, why: errs[0].message });
          } else {
            ok.push(inst.bundleName);
          }
        }
      } catch (e) {
        const why = (e as Error).message;
        failed.push({ file, why });
        process.stdout.write(`  FAILED: ${why}\n`);
      }
    }

    process.stdout.write(`\n==== ${ok.length} bundle(s) imported ====\n`);
    ok.forEach((n) => process.stdout.write(`  ok    ${n}\n`));
    failed.forEach((f) => process.stdout.write(`  skip  ${f.file}: ${f.why}\n`));
  });
});
