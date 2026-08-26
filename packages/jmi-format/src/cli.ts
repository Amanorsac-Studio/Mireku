#!/usr/bin/env node
/** jmi CLI — `jmi validate <bundle.jmi> [...]` */
import { validateBundle } from './validate.js';

const [, , cmd, ...args] = process.argv;

function usage(): never {
  console.log('Usage: jmi validate <path-to-bundle.jmi> [...more bundles]');
  process.exit(2);
}

if (cmd !== 'validate' || args.length === 0) usage();

let anyFailed = false;
for (const bundle of args) {
  const res = validateBundle(bundle);
  const errors = res.issues.filter((i) => i.level === 'error');
  const warnings = res.issues.filter((i) => i.level === 'warning');
  console.log(`\n${bundle}`);
  if (res.stats) {
    const mb = (res.stats.totalBytes / (1024 * 1024)).toFixed(1);
    console.log(`  ${res.stats.sampleCount} samples · ${res.stats.presetCount} presets · ${mb} MB`);
  }
  for (const i of errors) console.log(`  ERROR   ${i.path}: ${i.message}`);
  for (const i of warnings) console.log(`  warn    ${i.path}: ${i.message}`);
  console.log(res.valid ? '  VALID' : '  INVALID');
  if (!res.valid) anyFailed = true;
}
process.exit(anyFailed ? 1 : 0);
