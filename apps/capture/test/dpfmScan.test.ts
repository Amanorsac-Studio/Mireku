/**
 * DPFM corpus scan, run as a test purely so it executes with the project's
 * TypeScript config. NOT a real test: skipped unless JM_DPFM_IN is set, so
 * `npm test` is unaffected.
 *
 *   JM_DPFM_IN="C:/Users/amano/Downloads/JM Sounds" \
 *     npx vitest run test/dpfmScan.test.ts
 *
 * Prints, per library, what the DPFM block says about grouping and how the
 * resulting bundle count compares with the name heuristic. Read-only: it never
 * writes a bundle.
 */
import { describe, it } from 'vitest';
import { readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';
import { measureKeyzone, parseX7l, representativeKeyzone, type X7lLibrary } from '../src/engine/import/x7l';
import { dpfmUsage } from '../src/engine/import/dpfm';
import { planImport } from '../src/engine/import/x7lPlan';

const IN = process.env.JM_DPFM_IN;

function levelsFor(lib: X7lLibrary, bytes: Uint8Array): number[] {
  return lib.waveforms.map((wf) => measureKeyzone(bytes, representativeKeyzone(wf)).peakDb);
}

describe.skipIf(!IN)('DPFM corpus scan', () => {
  it('reports grouping for every library in the folder', { timeout: 60 * 60 * 1000 }, () => {
    const files = readdirSync(IN!)
      .filter((f) => /\.(x7l|x8l)$/i.test(f))
      .sort((a, b) => a.localeCompare(b));
    for (const f of files) {
      let lib: X7lLibrary;
      let bytes: Uint8Array;
      try {
        bytes = new Uint8Array(readFileSync(join(IN!, f)));
        lib = parseX7l(bytes);
      } catch (e) {
        console.log(`\n### ${f}: refused — ${(e as Error).message.slice(0, 100)}`);
        continue;
      }
      console.log(`\n### ${f}  ${lib.waveforms.length} waveforms, dpfm=${lib.dpfm ? 'yes' : 'no'}`);
      for (const line of lib.dpfm?.corroboration ?? []) console.log('   ', line);
      for (const u of dpfmUsage(lib.dpfm ?? { performances: [], skipped: [], corroboration: [] })) {
        const show = (l: { waveformIndex: number; velLo: number; velHi: number; noteLo: number; noteHi: number; velXfade: number }) =>
          `${lib.waveforms[l.waveformIndex].name}[v${l.velLo}-${l.velHi} k${l.noteLo}-${l.noteHi} x${l.velXfade}]`;
        console.log(
          `    ${u.performanceName} part${u.partIndex + 1}: ${u.body.map(show).join(' + ')}` +
            (u.release.length ? `  REL ${u.release.map(show).join(' + ')}` : '') +
            (u.usesPresetWaveforms ? '  (+preset waveforms)' : ''),
        );
      }
      const levels = levelsFor(lib, bytes);
      try {
        const plan = planImport(lib, levels);
        console.log(
          `    PLAN ${plan.instruments.length} bundle(s): ` +
            plan.instruments
              .map(
                (i) =>
                  `${i.name}(${i.windows.map((w) => `${w.lo}-${w.hi}`).join('/')}` +
                  `${i.fromDpfm ? ' dpfm' : ''}${i.velXfade ? ` x${i.velXfade}` : ''}` +
                  `${i.releaseZones.length ? ` +${i.releaseZones.length}rel` : ''})`,
              )
              .join(', '),
        );
      } catch (e) {
        console.log(`    PLAN failed: ${(e as Error).message.slice(0, 200)}`);
      }
    }
  });
});
