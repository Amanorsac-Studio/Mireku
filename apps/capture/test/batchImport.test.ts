/**
 * Tests for the batch X7L importer, which now lives in
 * apps/capture/src/cli/x7lBatch.ts behind a real command:
 *
 *   npm run import:x7l -w jm-capture -- --in <folder> --out <folder>
 *
 * This file used to BE the importer — a vitest file driven by JM_BATCH_IN /
 * JM_BATCH_OUT, because a test was the only thing already wired up to run the
 * TypeScript importer. The conversion logic moved out unchanged; what is left
 * here is the test it always should have been. The env-var entry point is kept
 * (it still works, and DECISIONS-capture.md references it) but it is now a thin
 * call into the same `runBatchImport` the command uses.
 */
import { describe, expect, it } from 'vitest';
import { mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { join as pathJoin } from 'node:path';
import { tmpdir } from 'node:os';
import { X7lFormatError, X7lNoAudioError, X7lTruncatedError } from '../src/engine/import/x7l';
import { freeBundleName, refusalReason, runBatchImport, summaryLines } from '../src/cli/x7lBatch';

const IN = process.env.JM_BATCH_IN;
const OUT = process.env.JM_BATCH_OUT;

describe.skipIf(!IN || !OUT)('batch X7L import', () => {
  it('converts every library in the input folder', { timeout: 6 * 60 * 60 * 1000 }, () => {
    const result = runBatchImport({ inDir: IN!, outDir: OUT! });
    for (const line of summaryLines(result)) process.stdout.write(`${line}\n`);
    expect(result.total).toBeGreaterThan(0);
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

/**
 * The command has to tell the client WHY a library was refused, not just that
 * it was. His own batch has one of each: two truncated downloads, a
 * parameter-only DX7 set, and (if the artist ever sends one) a version this
 * reader does not read.
 */
describe('refusal reasons', () => {
  it('names a truncated download and how much is missing', () => {
    const why = refusalReason(new X7lTruncatedError('ends early', 120_000_000, 40_000_000));
    expect(why).toMatch(/truncated file/i);
    expect(why).toMatch(/80,000,000 missing/);
    expect(why).toMatch(/re-download/i);
  });

  it('names a parameter-only library', () => {
    expect(refusalReason(new X7lNoAudioError('no waveform block'))).toMatch(/parameter-only library/i);
  });

  it('names an unsupported version', () => {
    const why = refusalReason(new X7lFormatError('YSFC version 3.0.0 is not supported: 4.x and 5.x libraries only'));
    expect(why).toMatch(/unsupported version/i);
  });

  it('falls back to the message for anything else', () => {
    expect(refusalReason(new Error('disk full'))).toBe('disk full');
  });
});

describe('batch input folder', () => {
  it('refuses a folder that does not exist instead of creating anything', () => {
    const dir = mkdtempSync(pathJoin(tmpdir(), 'jm-batch-'));
    try {
      expect(() => runBatchImport({ inDir: pathJoin(dir, 'nope'), outDir: pathJoin(dir, 'out') })).toThrow(
        /Input folder does not exist/,
      );
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });

  it('prints the summary table for an empty folder without writing bundles', () => {
    const dir = mkdtempSync(pathJoin(tmpdir(), 'jm-batch-'));
    try {
      const lines: string[] = [];
      const result = runBatchImport({ inDir: dir, outDir: pathJoin(dir, 'out'), log: (l) => lines.push(l) });
      expect(result.total).toBe(0);
      expect(summaryLines(result).join('\n')).toMatch(/RESULT: 0\/0 libraries imported/);
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });
});
