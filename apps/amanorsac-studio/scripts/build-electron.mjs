import { build } from 'esbuild';
import { mkdirSync } from 'node:fs';

mkdirSync('dist-electron', { recursive: true });

// Main process: ESM (.mjs). @jm/jmi-format stays external so its
// import.meta.url-relative schema lookup keeps working from node_modules.
await build({
  entryPoints: ['electron/main.ts'],
  bundle: true,
  platform: 'node',
  format: 'esm',
  target: 'node20',
  outfile: 'dist-electron/main.mjs',
  external: ['electron', '@jm/jmi-format', 'adm-zip'],
});

// Preload: sandboxed context prefers CJS.
await build({
  entryPoints: ['electron/preload.ts'],
  bundle: true,
  platform: 'node',
  format: 'cjs',
  target: 'node20',
  outfile: 'dist-electron/preload.cjs',
  external: ['electron'],
});

console.log('electron main + preload built');
