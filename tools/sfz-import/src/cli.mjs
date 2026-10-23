#!/usr/bin/env node
// sfz-import — SFZ / SF2 / named-WAV libraries → .jmi bundles for the Amanorsac sampler engine.
//
//   node src/cli.mjs [--out <dir>] [--work <dir>] [--only <substr>] [--skip <substr>] [--world <w>] <archive|dir> ...
//
// Archives (.7z .zip .rar .tar.*) are extracted into --work (default: <out>/../_import-work),
// then every .sfz / .sf2 becomes a bundle; folders of note-named WAVs (VCSL style) are mapped
// by file name. Output defaults to Documents/Amanorsac Studio/Sanctuary/Library.
import { existsSync, mkdirSync, readdirSync, statSync, writeFileSync, rmSync } from 'node:fs';
import { join, basename, resolve, relative, dirname, extname } from 'node:path';
import { execFileSync } from 'node:child_process';
import { homedir } from 'node:os';
import { parseSfz } from './sfz.mjs';
import { parseSf2 } from './sf2.mjs';
import { listWavDirs, mapFolder } from './folder.mjs';
import { buildBundle } from './bundle.mjs';
import { categorize } from './category.mjs';

const SEVEN_ZIP = 'C:\\Program Files\\7-Zip\\7z.exe';

/** Known libraries: display name, world, which SFZ to use, or why to skip. */
const CURATED = [
  { match: /ButtonAccordion/i, name: 'Button Accordion', world: 'mks' },
  { match: /ChurchOrgan/i, name: 'Church Organ', world: 'mks' },
  { match: /ConcertHarp/i, name: 'Concert Harp', world: 'fm' },
  { match: /DrawbarOrgan/i, name: 'Drawbar Organ', world: 'mks' },
  { match: /EGuitar/i, name: 'Electric Guitar Clean', world: 'rhodes-ep' },
  { match: /FM-Piano1.*_2\./i, skip: 'duplicate of FM-Piano1' },
  { match: /FM-Piano1/i, name: 'FM Piano 1', world: 'fm' },
  { match: /FM-Piano2/i, name: 'FM Piano 2', world: 'fm' },
  { match: /^Glass-/i, name: 'Glass', world: 'fm' },
  { match: /Kalimba/i, name: 'Kalimba', world: 'fm' },
  { match: /LatelyBass/i, name: 'Lately Bass', world: 'mks' },
  { match: /Ocarina/i, name: 'Ocarina', world: 'strings' },
  { match: /PickedBass/i, name: 'Picked Bass', world: 'rhodes-ep' },
  { match: /Salamander/i, name: 'Salamander Grand Piano', world: 'rhodes-ep', pick: /Retuned/i },
  { match: /SynthBassLead/i, name: 'Synth Bass Lead', world: 'mks' },
  { match: /SynthBrass/i, name: 'Synth Brass', world: 'mks' },
  { match: /SynthGoblins/i, name: 'Synth Goblins', world: 'mks' },
  { match: /SynthPadChoir/i, name: 'Synth Pad Choir', world: 'strings' },
  { match: /SynthStrings/i, name: 'Synth Strings', world: 'strings' },
  { match: /TenorSax/i, name: 'Tenor Saxophone', world: 'strings' },
  { match: /TubularBells/i, name: 'Tubular Bells', world: 'fm' },
  { match: /UprightPianoKW-SFZ\+FLAC/i, skip: 'duplicate of UprightPianoKW-SFZ (WAV)' },
  { match: /UprightPianoKW/i, name: 'Upright Piano', world: 'rhodes-ep' },
  { match: /YDP-GrandPiano/i, name: 'YDP Grand Piano', world: 'rhodes-ep' },
  { match: /VCSL/i, name: 'VCSL', world: null, folderMode: true },
  { match: /^HORN\.rar$/i, skip: 'song stems (full-length mixes), not a sample instrument' },
  { match: /GA HIGHLIFE/i, skip: 'song stems (full-length mixes), not a sample instrument' },
  { match: /\.x7l$/i, skip: 'Yamaha MONTAGE library — use the Import screen in the Amanorsac Studio app' },
];

function worldFor(name, sustaining) {
  const n = name.toLowerCase();
  // instrument-family words first: "Bass Recorder" is a recorder, not a bass
  if (/(glass|bell|kalimba|fm|dx|vibra|marimba|celest|music box|chime|harp|tubular|mallet|xylo|glock|pluck|dulcimer|zither|koto|tranh)/.test(n)) return 'fm';
  if (/(pad|choir|string|sax|ocarina|horn|flute|recorder|voice|vocal|bow|sus|violin|viola|cello|trumpet|trombone|tuba|oboe|clarinet|bassoon|whistle|harmon|reed|pipe)/.test(n)) return 'strings';
  if (/(organ|accordion|synth|juno|goblin|brass|lead|analog)/.test(n)) return 'mks';
  if (/(piano|rhodes|wurli|clav|guitar|bass|kit|perc|drum)/.test(n)) return 'rhodes-ep';
  return sustaining ? 'strings' : 'rhodes-ep';
}

function parseArgs(argv) {
  const o = { inputs: [], only: [], skip: [] };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--out') o.out = argv[++i];
    else if (a === '--work') o.work = argv[++i];
    else if (a === '--only') o.only.push(argv[++i].toLowerCase());
    else if (a === '--skip') o.skip.push(argv[++i].toLowerCase());
    else if (a === '--world') o.world = argv[++i];
    else o.inputs.push(a);
  }
  o.out = resolve(o.out ?? join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library'));
  o.work = resolve(o.work ?? join(dirname(o.out), '_import-work'));
  return o;
}

function extract(archive, work) {
  const base = basename(archive).replace(/\.(7z|zip|rar|tar\.gz|tar\.xz|tar\.bz2|tgz)$/i, '');
  const dest = join(work, base);
  if (existsSync(dest) && readdirSync(dest).length) return dest;
  mkdirSync(dest, { recursive: true });
  if (/\.(tar\.gz|tar\.xz|tar\.bz2|tgz)$/i.test(archive)) {
    // 7-Zip handles the compression layer and the tar layer as two steps
    const tmp = join(work, `${base}.tar-tmp`);
    mkdirSync(tmp, { recursive: true });
    execFileSync(SEVEN_ZIP, ['x', '-y', `-o${tmp}`, archive], { stdio: 'ignore' });
    const tar = readdirSync(tmp).find((f) => /\.tar$/i.test(f)) ?? readdirSync(tmp)[0];
    execFileSync(SEVEN_ZIP, ['x', '-y', '-ttar', `-o${dest}`, join(tmp, tar)], { stdio: 'ignore' });
    rmSync(tmp, { recursive: true, force: true });
  } else execFileSync(SEVEN_ZIP, ['x', '-y', `-o${dest}`, archive], { stdio: 'ignore' });
  return dest;
}

function findFiles(root, re) {
  const out = [];
  const walk = (d) => { for (const e of readdirSync(d)) { const p = join(d, e); const st = statSync(p); if (st.isDirectory()) walk(p); else if (re.test(e)) out.push(p); } };
  walk(root);
  return out;
}

function fmtBytes(b) { return b > 1e9 ? `${(b / 1e9).toFixed(2)} GB` : `${(b / 1e6).toFixed(1)} MB`; }

async function importInput(input, o, report) {
  const label = basename(input);
  const cur = CURATED.find((c) => c.match.test(label));
  if (cur?.skip) { console.log(`- skip   ${label}: ${cur.skip}`); report.skipped.push({ input: label, reason: cur.skip }); return; }
  const isDir = statSync(input).isDirectory();
  let root;
  try { root = isDir ? input : extract(input, o.work); }
  catch (e) { console.log(`! extract failed ${label}: ${e.message}`); report.failed.push({ input: label, error: `extract: ${e.message}` }); return; }

  const jobs = [];
  const sf2s = findFiles(root, /\.sf2$/i);
  let sfzs = findFiles(root, /\.sfz$/i);
  if (cur?.pick) { const picked = sfzs.filter((f) => cur.pick.test(basename(f))); if (picked.length) sfzs = picked; }
  if (!cur?.folderMode && (sfzs.length || sf2s.length)) {
    for (const f of sfzs) {
      const { regions } = parseSfz(f);
      const ampeg = { attack: regions.find((r) => r.ampegAttack != null)?.ampegAttack, release: regions.find((r) => r.ampegRelease != null)?.ampegRelease };
      const nm = cur?.name && sfzs.length === 1 ? cur.name : basename(f, '.sfz').replace(/[\s_-]*\d{8}.*$/, '').replace(/-SFZ.*$/i, '').trim();
      jobs.push({ name: nm, regions, ampeg, notes: `Imported from ${label} (${relative(root, f)})` });
    }
    for (const f of sf2s) {
      const { name, regions } = parseSf2(f);
      jobs.push({ name: cur?.name ?? name, regions, notes: `Imported from ${label} (${relative(root, f)})` });
    }
  } else {
    const dirs = listWavDirs(root);
    for (const d of dirs) {
      const rel = relative(root, d.dir).split(/[\\/]/).filter((s) => !/^VCSL-master$/i.test(s));
      const nameFromPath = () => rel.slice(-2).join(' ');
      for (const inst of await mapFolder(d, { nameFromPath }))
        jobs.push({ name: `${cur?.name ? cur.name + ' ' : ''}${inst.name}`.trim(), regions: inst.regions, kit: inst.kit, notes: `Imported from ${label} (${relative(root, d.dir)})${inst.octaveOffset ? `; note names read as C${4 - inst.octaveOffset / 12}=60` : ''}` });
    }
  }
  if (!jobs.length) { console.log(`- skip   ${label}: nothing importable found`); report.skipped.push({ input: label, reason: 'nothing importable' }); return; }

  for (const job of jobs) {
    if (o.only.length && !o.only.some((s) => job.name.toLowerCase().includes(s))) continue;
    if (o.skip.some((s) => job.name.toLowerCase().includes(s))) continue;
    const looped = job.regions.filter((r) => r.loop || r.useFileLoop).length;
    const world = o.world ?? cur?.world ?? worldFor(job.name, looped >= job.regions.length / 2);
    const t0 = Date.now();
    try {
      const category = categorize(job.name, job.notes ?? '', world);
      const r = await buildBundle({ ...job, world, category, outDir: o.out, author: 'Imported' });
      const secs = ((Date.now() - t0) / 1000).toFixed(0);
      const status = r.errors.length ? 'ERROR' : 'ok';
      console.log(`- ${status.padEnd(6)} ${job.name} [${world}] ${r.zones} zones / ${r.samples} samples / ${r.layers} layers / ${r.channels}ch ${r.rate} / ${r.sustaining ? 'sustaining' : 'decaying'} / ${fmtBytes(r.bytes)} / ${secs}s`);
      for (const e of r.errors) console.log(`    error: ${e}`);
      if (r.warnings.length) console.log(`    ${r.warnings.length} warning(s): ${r.warnings.slice(0, 3).join(' | ')}${r.warnings.length > 3 ? ' …' : ''}`);
      (r.errors.length ? report.failed : report.bundles).push({ input: label, name: job.name, world, path: r.path, zones: r.zones, samples: r.samples, bytes: r.bytes, errors: r.errors, warnings: r.warnings.length });
    } catch (e) {
      console.log(`! failed ${job.name}: ${e.message}`);
      report.failed.push({ input: label, name: job.name, error: e.message });
    }
  }
}

const o = parseArgs(process.argv.slice(2));
if (!o.inputs.length) { console.error('usage: sfz-import [--out dir] [--work dir] [--only s] [--skip s] [--world w] <archive|dir> ...'); process.exit(2); }
mkdirSync(o.out, { recursive: true });
mkdirSync(o.work, { recursive: true });
console.log(`sfz-import → ${o.out}\nwork dir   → ${o.work}\n`);
const report = { startedAt: new Date().toISOString(), out: o.out, bundles: [], skipped: [], failed: [] };
for (const input of o.inputs) {
  const abs = resolve(input);
  if (!existsSync(abs)) { console.log(`! missing ${input}`); continue; }
  await importInput(abs, o, report);
}
report.finishedAt = new Date().toISOString();
const reportPath = join(o.work, `import-report-${Date.now()}.json`);
writeFileSync(reportPath, JSON.stringify(report, null, 2));
const total = report.bundles.reduce((s, b) => s + b.bytes, 0);
console.log(`\n${report.bundles.length} bundle(s) written (${fmtBytes(total)}), ${report.skipped.length} skipped, ${report.failed.length} failed. Report: ${reportPath}`);
