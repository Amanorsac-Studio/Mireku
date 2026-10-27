/**
 * Launcher for `npm run fix:loops`. Same shape as import-x7l.mjs.
 *
 * The importer is TypeScript inside this app, so there is nothing to run
 * directly. This bundles the CLI entry with esbuild (already a devDependency
 * here, and already how dist-electron is built) and then imports the bundle in
 * this same process, so the client's command is one command with no build step
 * to remember and no stale output to run by accident.
 *
 * @jm/jmi-format stays external for the same reason build-electron.mjs keeps it
 * external: its schema lookup is relative to its own import.meta.url and must
 * resolve inside node_modules.
 */
import { build } from 'esbuild';
import { mkdirSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const appDir = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const outfile = join(appDir, 'dist-cli', 'fix-loops.mjs');

mkdirSync(join(appDir, 'dist-cli'), { recursive: true });

await build({
  entryPoints: [join(appDir, 'src', 'cli', 'fixLoopsMain.ts')],
  bundle: true,
  platform: 'node',
  format: 'esm',
  target: 'node20',
  outfile,
  external: ['@jm/jmi-format'],
  logLevel: 'warning',
});

await import(pathToFileURL(outfile).href);
