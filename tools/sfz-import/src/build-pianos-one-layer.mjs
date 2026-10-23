#!/usr/bin/env node
// Every acoustic piano with ONE key-down velocity recording per key: the recorded layer that
// covers velocity 69 (the middle of the Kawai 55–83 range), played at every velocity. No release
// samples, no noise reduction; same per-sample volume/tune, hiss-tail cleanup and de-click.
import { join, basename } from 'node:path';
import { homedir } from 'node:os';
import { buildFaithfulBundle } from './faithful.mjs';
import { parseSf2 } from './sf2.mjs';

const TARGET_VEL = 69;
const W = join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', '_import-work');
const Z = join(W, 'VCSL-master', 'VCSL-master', 'Chordophones', 'Zithers');
const VCSL = 'Versilian Studios (VCSL, CC0) — mapped by Amanorsac Studio';
const PIANOS = [
  { name: 'Kawai Grand Piano', sfz: join(Z, 'Grand Piano, Kawai.sfz'), author: VCSL },
  { name: 'Steinway Model B Grand Piano', sfz: join(Z, 'Grand Piano, Steinway B.sfz'), author: VCSL },
  { name: 'Knight Upright Piano', sfz: join(Z, 'Upright Piano, Knight.sfz'), author: VCSL },
  { name: 'Yamaha Upright Piano', sfz: join(Z, 'Upright Piano, Yamaha.sfz'), author: VCSL },
  { name: 'Salamander Grand Piano (Yamaha C5)', author: 'Alexander Holm (Salamander, CC-BY 3.0) — mapped by Amanorsac Studio',
    sfz: join(W, 'SalamanderGrandPiano-SFZ+FLAC-V3+20200602', 'SalamanderGrandPiano-SFZ+FLAC-V3+20200602', 'SalamanderGrandPiano-V3+20200602.sfz') },
  { name: 'Upright Piano KW', author: 'FreePats (CC0) — mapped by Amanorsac Studio',
    sfz: join(W, 'UprightPianoKW-SFZ-20220221', 'UprightPianoKW-SFZ-20220221', 'UprightPianoKW-20220221.sfz') },
  { name: 'Yamaha Disklavier Pro Grand Piano', author: 'FreePats YDP Grand Piano (CC-BY 3.0) — mapped by Amanorsac Studio',
    sf2: join(W, 'YDP-GrandPiano-SF2-20160804', 'YDP-GrandPiano-SF2-20160804', 'YDP-GrandPiano-20160804.sf2') },
];
const only = process.argv.slice(2);

for (const p of PIANOS.filter((p) => !only.length || only.some((o) => p.name.includes(o)))) {
  const picks = [];
  const select = (regions) => {
    const attack = regions.filter((r) => r.trigger === 'attack');
    const loKey = Math.min(...attack.map((r) => r.lokey)), hiKey = Math.max(...attack.map((r) => r.hikey));
    // ONLY recordings from the range that covers TARGET_VEL — never another velocity layer
    let inRange = attack.filter((r) => r.lovel <= TARGET_VEL && TARGET_VEL <= r.hivel);
    // some VCSL pianos record each note twice, sustain pedal up ("NoSus") and down ("Sus"),
    // switched by CC64; those are not round robins — keep the pedal-up recording only
    const folder = (r) => basename(r.file.replace(/[\\/][^\\/]+$/, ''));
    if (inRange.some((r) => /^nosus$/i.test(folder(r)))) inRange = inRange.filter((r) => !/^sus$/i.test(folder(r)));
    const groups = new Map();
    for (const r of inRange) {
      const k = `${r.root}|${r.lokey}|${r.hikey}`;
      if (!groups.has(k)) groups.set(k, { root: r.root, lokey: r.lokey, hikey: r.hikey, regions: [] });
      groups.get(k).regions.push(r);
    }
    const zones = [...groups.values()].sort((a, b) => a.root - b.root);
    // a key with no recording in that range plays the nearest recorded note, pitched up or down
    for (let k = loKey; k <= hiKey; k++) {
      if (zones.some((z) => z.lokey <= k && k <= z.hikey)) continue;
      const z = zones.slice().sort((a, b) => Math.abs(a.root - k) - Math.abs(b.root - k) || b.root - a.root)[0];
      z.lokey = Math.min(z.lokey, k); z.hikey = Math.max(z.hikey, k);
      picks.push(`key ${k} ← root ${z.root} (${k - z.root > 0 ? '+' : ''}${k - z.root} st)`);
    }
    for (let k = loKey; k <= hiKey; k++) {
      const n = zones.filter((z) => z.lokey <= k && k <= z.hikey).length;
      if (n > 1) picks.push(`OVERLAP key ${k} (${n} zones)`);
    }
    return zones.flatMap((z) => z.regions.map((r) => ({ ...r, lokey: z.lokey, hikey: z.hikey, lovel: 1, hivel: 127 })));
  };
  const t0 = Date.now();
  console.log(`\nbuilding "${p.name}"`);
  const r = await buildFaithfulBundle({
    sfzFile: p.sfz ?? p.sf2, name: p.name,
    regions: p.sf2 ? parseSf2(p.sf2).regions.map((r) => ({ ...r, trigger: 'attack', file: `${p.sf2}#${r.name}` })) : undefined, author: p.author, category: 'Piano', select,
    outDir: join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library'),
    notes: `One key-down recording per key (the layer covering velocity ${TARGET_VEL}) at every velocity, no release samples. Source: ${basename(p.sfz ?? p.sf2)}`,
  });
  console.log(`  → ${r.zones} zones · ${r.samples} samples · ${(r.bytes / 1e6).toFixed(0)} MB · ${((Date.now() - t0) / 1000).toFixed(0)} s`);
  console.log(`  keys filled by pitching the nearest note: ${picks.join(', ') || 'none'}`);
  for (const e of r.errors) console.log('  ERROR', e);
}
