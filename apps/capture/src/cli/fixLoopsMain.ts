/**
 * `npm run fix:loops` — re-decide the loops in bundles already on disk.
 *
 * Why this exists rather than a re-import: the loop-seam rule (loopSeam.ts)
 * arrived after the library had been imported, the audio in a bundle is the
 * source PCM unchanged so the same measurement can be made from it, and two of
 * the source libraries are still truncated downloads. Only instrument.json is
 * touched, and the previous copy is kept beside it as instrument.json.bak.
 *
 * It is a DRY RUN unless --apply is passed, because the one folder worth running
 * it on is the live library.
 */
import { readFileSync, writeFileSync, readdirSync, existsSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { reportLines, reviewBundleLoops, type BundleFs } from '../engine/import/bundleLoops';

const USAGE = `
MIREKU — loop repair for bundles already in a library

  npm run fix:loops -w jm-capture -- --library <folder> [--apply]

  --library <folder>   folder holding .jmi bundles
  --apply              write the changes (without it, nothing is written)
  --help               this text

WHAT IT CHANGES, AND WHY. The sampled libraries declare a loop region on most
keyzones, but "has a loop" and "sustains while held" are not the same thing. A
piano element declares a loop over its DECAY TAIL, which the source hardware
reads under a falling envelope. Held at full level — which is what a MIREKU patch
does, so the recording is heard untouched — every wrap jumps back to a point
20-30 dB louder, and the note swells up again once per loop length. That is the
repeat you hear on a long-held grand piano or string note.

The two kinds are told apart by the SEAM: how much louder the loop's restart is
than the point it jumps from. Sustain loops (organ, pad, strings, brass) measure
within a few dB; decay tails measure +8 dB and up. A decay tail has its loop
removed, so the note plays the recording and stops, as a piano does. A sustain
loop is left alone — the engine now gives it a real crossfade.

Audio is never touched. Only instrument.json changes, and only two fields in it:
a round robin's "loop", and "soundType" following the loops that remain.

  npm run fix:loops -w jm-capture -- --library "C:/Users/amano/Documents/JM Signature/Library"
`;

const fs: BundleFs = {
  readFile: (p) => new Uint8Array(readFileSync(p)),
  writeFile: (p, d) => writeFileSync(p, d as Uint8Array | string),
  readDir: (p) => readdirSync(p),
  exists: (p) => existsSync(p),
  join: (...parts) => join(...parts),
};

function flag(argv: string[], name: string): string | undefined {
  const i = argv.indexOf(`--${name}`);
  if (i < 0) return undefined;
  const v = argv[i + 1];
  if (v === undefined || v.startsWith('--')) throw new Error(`--${name} needs a value`);
  return v;
}

function main(argv: string[]): number {
  if (argv.includes('--help') || argv.includes('-h') || argv.length === 0) {
    process.stdout.write(`${USAGE}\n`);
    return argv.length === 0 ? 2 : 0;
  }

  let libDir: string | undefined;
  try {
    libDir = flag(argv, 'library');
  } catch (e) {
    process.stderr.write(`${(e as Error).message}\n${USAGE}\n`);
    return 2;
  }
  if (!libDir) {
    process.stderr.write(`--library is required: the folder holding .jmi bundles.\n${USAGE}\n`);
    return 2;
  }
  const dir = resolve(libDir);
  if (!existsSync(dir)) {
    process.stderr.write(`Library folder does not exist: ${dir}\n`);
    return 2;
  }
  const apply = argv.includes('--apply');

  const bundles = readdirSync(dir).filter((n) => n.toLowerCase().endsWith('.jmi'));
  if (!bundles.length) {
    process.stderr.write(`No .jmi bundles in ${dir}\n`);
    return 2;
  }

  const reports = bundles.map((b) => reviewBundleLoops(join(dir, b), fs, apply));
  for (const line of reportLines(reports, apply)) process.stdout.write(`${line}\n`);
  return 0;
}

process.exitCode = main(process.argv.slice(2));
