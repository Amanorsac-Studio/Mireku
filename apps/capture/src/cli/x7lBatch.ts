/**
 * Batch X7L/X8L -> .jmi conversion: the whole of what the client needs when the
 * artist sends a folder of libraries, as a plain function.
 *
 * This logic used to live inside apps/capture/test/batchImport.test.ts, driven
 * by JM_BATCH_IN / JM_BATCH_OUT environment variables, because a vitest file
 * was the only thing in the repo already wired up to run the TypeScript
 * importer. That is fine for an agent and clumsy for the client, who runs this
 * every time a batch arrives. Nothing about the conversion has changed — the
 * same parseX7l -> planImport -> convertInstrument path, the same bit-perfect
 * PCM copy, the same never-overwrite guard. It is only packaged honestly now:
 * the test imports `freeBundleName` from here (so the guard keeps its test),
 * and importX7lMain.ts gives it a command line.
 *
 * Node-only (node:fs). It lives under src/cli rather than src/engine/import so
 * that the importer itself stays free of node builtins and keeps taking an
 * injected `ImportFs`, which is what lets the renderer use it.
 */
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { join as pathJoin } from 'node:path';
import { validateBundle } from '@jm/jmi-format';
import {
  X7lFormatError,
  X7lNoAudioError,
  X7lTruncatedError,
  measureKeyzone,
  parseX7l,
  representativeKeyzone,
  type X7lLibrary,
} from '../engine/import/x7l';
import { planImport } from '../engine/import/x7lPlan';
import { convertInstrument, type ImportFs } from '../engine/import/x7lConvert';

/** The real disk, for `convertInstrument`'s injected filesystem. */
export const diskFs: ImportFs = {
  mkdir: (dir) => mkdirSync(dir, { recursive: true }),
  writeFile: (p, data) => writeFileSync(p, typeof data === 'string' ? data : Buffer.from(data)),
  join: (...parts) => pathJoin(...parts),
};

/** Per-waveform peak levels, which the planner uses to order velocity layers. */
export function levelsFor(lib: X7lLibrary, bytes: Uint8Array): number[] {
  return lib.waveforms.map((wf) => measureKeyzone(bytes, representativeKeyzone(wf)).peakDb);
}

/**
 * A bundle folder name that is free both on disk and within this run.
 *
 * NON-NEGOTIABLE. Bundle names collide across libraries — two different files
 * both want to be called "MKS20" — and an earlier run of the old harness
 * overwrote one of the client's own bundles that way. A name already taken on
 * disk, or already handed out earlier in this run, gets a numeric suffix.
 *
 * `bundleName` already ends in ".jmi", so the suffix goes before it —
 * "MKS20_2.jmi", not "MKS20.jmi.jmi". (Appending ".jmi" to a name that already
 * had it is what broke the harness's own validate path AND this guard: it
 * stat'd a path that never existed, so the guard never fired and every bundle
 * looked invalid.)
 */
export function freeBundleName(outDir: string, bundleName: string, taken: Set<string>): string {
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

export type RowStatus = 'imported' | 'refused' | 'partial';

export interface BatchRow {
  file: string;
  status: RowStatus;
  bundles: number;
  samples: number;
  mb: number;
  /** the specific reason, in the client's terms, when this is not a clean import */
  detail: string;
}

export interface BatchResult {
  rows: BatchRow[];
  imported: number;
  total: number;
}

export interface BatchOptions {
  inDir: string;
  outDir: string;
  author?: string;
  /** every line the command would print; default writes to stdout */
  log?(line: string): void;
}

/**
 * Why a library was refused, in the client's terms rather than the parser's.
 *
 * The three real cases in his own batch are a truncated download (two of the
 * thirteen), a parameter-only library with no sampled audio (the DX7 set), and
 * an unsupported YSFC version. Each has its own error class in x7l.ts, so the
 * reason is read off the throw rather than guessed from the message.
 */
export function refusalReason(e: unknown): string {
  if (e instanceof X7lTruncatedError) {
    const missing = e.declaredBytes - e.actualBytes;
    return (
      `truncated file — the library declares ${e.declaredBytes.toLocaleString()} bytes but only ` +
      `${e.actualBytes.toLocaleString()} are present (${missing.toLocaleString()} missing). ` +
      `Re-download it; nothing was written.`
    );
  }
  if (e instanceof X7lNoAudioError) {
    return `parameter-only library — it holds patch parameters but no sampled audio, so there is nothing to convert (${e.message})`;
  }
  if (e instanceof X7lFormatError) {
    return /version/i.test(e.message)
      ? `unsupported version — ${e.message}`
      : `unreadable library — ${e.message}`;
  }
  return (e as Error)?.message ?? String(e);
}

/** The `.X7L` / `.X8L` files in a folder, in the order they will be converted. */
export function librariesIn(dir: string): string[] {
  return readdirSync(dir)
    .filter((f) => /\.(x7l|x8l)$/i.test(f))
    .sort((a, b) => a.localeCompare(b));
}

/**
 * Convert every library in `inDir` into `outDir`, one bundle per instrument.
 *
 * Bit-perfect: `convertInstrument` copies each keyzone's PCM byte-for-byte into
 * a WAV with a matching header. Nothing is resampled, re-dithered or
 * normalised. Every bundle written is then validated against
 * instrument.schema.json before it is counted as imported.
 */
export function runBatchImport(opts: BatchOptions): BatchResult {
  const { inDir, outDir } = opts;
  const log = opts.log ?? ((line: string) => process.stdout.write(`${line}\n`));
  const author = opts.author ?? 'James Mireku';

  if (!existsSync(inDir) || !statSync(inDir).isDirectory())
    throw new Error(`Input folder does not exist: ${inDir}`);

  const files = librariesIn(inDir);
  mkdirSync(outDir, { recursive: true });

  const taken = new Set<string>();
  const rows: BatchRow[] = [];

  for (const file of files) {
    const src = pathJoin(inDir, file);
    log('');
    log(`=== ${file} ===`);
    let bundles = 0;
    let samples = 0;
    let bytes1 = 0;
    const problems: string[] = [];
    try {
      const bytes = new Uint8Array(readFileSync(src));
      const lib = parseX7l(bytes);
      const plan = planImport(lib, levelsFor(lib, bytes));
      log(`  YSFC ${lib.version}, ${lib.waveforms.length} waveform(s) -> ${plan.instruments.length} instrument(s)`);
      for (const note of plan.notes) log(`  note: ${note}`);
      for (const s of plan.skipped) {
        log(`  SKIPPED "${s.name}": ${s.reason}`);
        problems.push(`"${s.name}" was skipped: ${s.reason}`);
      }
      if (plan.instruments.length === 0) {
        rows.push({
          file,
          status: 'refused',
          bundles: 0,
          samples: 0,
          mb: 0,
          detail: problems.length
            ? problems.join('; ')
            : 'no instruments in the plan — nothing in the library groups into an instrument',
        });
        continue;
      }
      for (const inst of plan.instruments) {
        const bundleName = freeBundleName(outDir, inst.bundleName, taken);
        const dest = pathJoin(outDir, bundleName);
        if (bundleName.toLowerCase() !== inst.bundleName.toLowerCase())
          log(`  ${inst.bundleName} is taken -> writing ${bundleName}`);

        convertInstrument(bytes, lib, { ...inst, bundleName }, outDir, diskFs, {
          author,
          sourceFileName: file,
        });
        const res = validateBundle(dest);
        const errs = res.issues.filter((i) => i.level === 'error');
        log(
          `  ${bundleName}: ${inst.zones.length} zones, ${res.stats?.sampleCount} samples, ` +
            `${inst.channels === 2 ? 'stereo' : 'mono'}, ${inst.windows.length} vel layer(s), ` +
            `${((res.stats?.totalBytes ?? 0) / 1048576).toFixed(0)} MB, ` +
            `${errs.length === 0 ? 'VALID' : 'INVALID'}`,
        );
        if (errs.length) {
          errs.slice(0, 3).forEach((e) => log(`      ${e.path}: ${e.message}`));
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
      const why = refusalReason(e);
      rows.push({ file, status: 'refused', bundles, samples, mb: Math.round(bytes1 / 1048576), detail: why });
      log(`  REFUSED: ${why}`);
    }
  }

  return { rows, imported: rows.filter((r) => r.status === 'imported').length, total: rows.length };
}

/** The summary table, exactly as the old harness printed it. */
export function summaryLines(result: BatchResult): string[] {
  const out = ['', '', `==== RESULT: ${result.imported}/${result.total} libraries imported ====`];
  for (const r of result.rows)
    out.push(
      `${r.status.padEnd(9)} ${r.file.padEnd(30)} ${String(r.bundles).padStart(2)} bundle(s) ` +
        `${String(r.samples).padStart(5)} samples ${String(r.mb).padStart(5)} MB  ${r.detail}`,
    );
  return out;
}
