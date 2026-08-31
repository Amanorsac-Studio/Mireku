#!/usr/bin/env node
/**
 * jmi CLI
 *   jmi validate <bundle.jmi> [...]     validate one or more bundles
 *   jmi fx-off   <bundle.jmi> [...]     rewrite every preset in the bundle so
 *                                       each FX unit is bypassed
 *   jmi fx-on    <bundle.jmi> [...]     the exact inverse
 *
 * CLIENT REVIEW ROUND 7 (item 1). Default racks are now built bypassed, but
 * `bypassed` is an explicit, REQUIRED property of every saved preset, so a
 * bundle exported before that change carries `bypassed: false` and keeps
 * loading with its FX on — which is right (a preset must mean what it says) but
 * is not what James wants on the bundles he has already imported. `fx-off` is
 * the one-line remedy for those files: it edits the presets in place (writing a
 * `.bak` beside each one the first time) and touches nothing else.
 */
import { setBundleFxBypassed } from './fx-bypass.js';
import { validateBundle } from './validate.js';

const [, , cmd, ...args] = process.argv;

function usage(): never {
  console.log('Usage: jmi validate <path-to-bundle.jmi> [...more bundles]');
  console.log('       jmi fx-off   <path-to-bundle.jmi> [...more bundles]');
  console.log('       jmi fx-on    <path-to-bundle.jmi> [...more bundles]');
  process.exit(2);
}

function doValidate(bundles: string[]): number {
  let anyFailed = false;
  for (const bundle of bundles) {
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
  return anyFailed ? 1 : 0;
}

function doSetBypass(bundles: string[], bypassed: boolean): number {
  let failed = false;
  for (const bundle of bundles) {
    console.log(`\n${bundle}`);
    try {
      const { presets, units } = setBundleFxBypassed(bundle, bypassed);
      console.log(`  ${units} FX unit(s) across ${presets} preset(s) set to ${bypassed ? 'BYPASSED' : 'ACTIVE'}`);
      const res = validateBundle(bundle);
      console.log(res.valid ? '  VALID' : '  INVALID after edit — restore the .bak files');
      if (!res.valid) failed = true;
    } catch (err) {
      console.log(`  ERROR   ${(err as Error).message}`);
      failed = true;
    }
  }
  return failed ? 1 : 0;
}

if (args.length === 0) usage();
if (cmd === 'validate') process.exit(doValidate(args));
else if (cmd === 'fx-off') process.exit(doSetBypass(args, true));
else if (cmd === 'fx-on') process.exit(doSetBypass(args, false));
else usage();
