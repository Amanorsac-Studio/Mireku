/**
 * `npm run import:x7l` — the command line over runBatchImport.
 *
 * Entry point only: argument parsing, help text and the exit code. The work is
 * in x7lBatch.ts, which the vitest harness also uses, so there is one
 * conversion path and not two.
 *
 * ON THE DEFAULT OUTPUT FOLDER. There is none: --out is required.
 *
 * The only destination that would not need moving afterwards is the client's
 * live library (Documents/JM Signature/Library, set by `libraryPath` in his
 * settings), and that is the folder holding the 25 bundles he performs from.
 * Writing a 10 GB batch of freshly converted, not-yet-auditioned instruments
 * into it by default — on a command whose whole point is that he runs it
 * himself, often, from a shell — is the one outcome worth engineering against.
 * The never-overwrite guard protects the bundles already there, but it cannot
 * un-mix the new ones in among them.
 *
 * The alternative default, something like Documents/MIREKU/Imported Libraries,
 * is safe but silently puts gigabytes somewhere he then has to find. Between a
 * default that is dangerous and a default that is merely unhelpful, the honest
 * third option is to ask: the error below names both the flag and a folder
 * worth using, so one read of it is enough to type the real command. Nothing is
 * read or written until both folders are given.
 */
import { resolve } from 'node:path';
import { runBatchImport, summaryLines } from './x7lBatch';

const USAGE = `
MIREKU — batch X7L / X8L library import

  npm run import:x7l -w jm-capture -- --in <folder> --out <folder>

  --in   <folder>   folder of .X7L / .X8L libraries from the artist
  --out  <folder>   folder to write .jmi bundles into (created if missing)
  --author <name>   author written into each bundle (default "James Mireku")
  --help            this text

Every library is converted bit-perfect — the PCM is copied byte for byte and
never resampled — one bundle per instrument, grouped from the library's own
DPFM performance block. An existing bundle is NEVER overwritten: a name that
is taken gets a numeric suffix (MKS20_2.jmi). Every bundle written is validated
before it is counted, and each library is reported as imported or refused with
its reason.

--out has no default on purpose. Do not point it at your MIREKU library
(Documents/JM Signature/Library) — convert into an empty folder, listen to what
came out, then move the bundles you want to keep. For example:

  npm run import:x7l -w jm-capture -- --in "C:/Users/amano/Downloads/JM Sounds" --out "C:/Users/amano/Documents/MIREKU/Imported Libraries"
`;

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

  let inDir: string | undefined;
  let outDir: string | undefined;
  let author: string | undefined;
  try {
    inDir = flag(argv, 'in');
    outDir = flag(argv, 'out');
    author = flag(argv, 'author');
  } catch (e) {
    process.stderr.write(`${(e as Error).message}\n${USAGE}\n`);
    return 2;
  }

  if (!inDir) {
    process.stderr.write(`--in is required: the folder of .X7L / .X8L libraries.\n${USAGE}\n`);
    return 2;
  }
  if (!outDir) {
    process.stderr.write(
      '--out is required. There is deliberately no default: the only folder that would not ' +
        'need moving afterwards is your live MIREKU library, and a batch of freshly converted ' +
        'instruments does not belong in among the bundles you perform from.\n' +
        'Convert into an empty folder, audition what came out, then move what you want to keep — e.g.\n' +
        '  --out "C:/Users/amano/Documents/MIREKU/Imported Libraries"\n',
    );
    return 2;
  }

  const result = runBatchImport({ inDir: resolve(inDir), outDir: resolve(outDir), author });
  for (const line of summaryLines(result)) process.stdout.write(`${line}\n`);

  // Partial and refused libraries are reported, not fatal: a batch of thirteen
  // in which two are truncated downloads is a successful run that found two bad
  // files. A non-zero code only means nothing at all came through.
  return result.imported === 0 && result.total > 0 ? 1 : 0;
}

process.exitCode = main(process.argv.slice(2));
