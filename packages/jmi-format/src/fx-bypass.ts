/**
 * Bulk edit of the `bypassed` flag across a bundle's presets — the remedy for
 * bundles exported BEFORE `DEFAULT_UNIT_BYPASSED` became true.
 *
 * `bypassed` is a required property of every saved rack unit, so a preset
 * written under the old default carries `bypassed: false` explicitly and will
 * keep loading with its FX on. That is correct behaviour (a preset means what
 * it says) and deliberately NOT something the loaders second-guess. When the
 * owner of the bundle wants those presets dry instead, this rewrites them, in
 * place, keeping a `.bak` of each file it touches the first time.
 *
 * Node-only (filesystem); not part of the `/web` entry point.
 */
import { copyFileSync, existsSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

export interface FxBypassResult {
  /** preset files rewritten */
  presets: number;
  /** rack units whose flag actually changed */
  units: number;
}

export function setBundleFxBypassed(bundle: string, bypassed: boolean): FxBypassResult {
  const dir = join(bundle, 'presets');
  if (!existsSync(dir) || !statSync(dir).isDirectory()) return { presets: 0, units: 0 };
  let presets = 0;
  let units = 0;
  for (const file of readdirSync(dir)) {
    if (!file.toLowerCase().endsWith('.json')) continue;
    const path = join(dir, file);
    const preset = JSON.parse(readFileSync(path, 'utf8')) as { fxRack?: { bypassed: boolean }[] };
    if (!Array.isArray(preset.fxRack) || preset.fxRack.length === 0) continue;
    let changed = 0;
    for (const unit of preset.fxRack) {
      if (unit.bypassed !== bypassed) {
        unit.bypassed = bypassed;
        changed++;
      }
    }
    if (changed === 0) continue;
    const bak = `${path}.bak`;
    if (!existsSync(bak)) copyFileSync(path, bak);
    writeFileSync(path, `${JSON.stringify(preset, null, 2)}\n`);
    presets++;
    units += changed;
  }
  return { presets, units };
}
