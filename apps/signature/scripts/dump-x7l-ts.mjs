// dump-x7l-ts.mjs — the TypeScript reader's view of the corpus, in the same line
// format X7lReaderTests.inc writes for the C++ reader. The two files must be
// identical; that is what keeps two implementations of one binary format from
// drifting apart.
//
//   node apps/signature/scripts/dump-x7l-ts.mjs ["<corpus folder>"]
//
// Writes %TEMP%/mireku-x7l-ts.txt, which the C++ test picks up automatically.
import { build } from 'esbuild';
import { readFileSync, readdirSync, writeFileSync, mkdtempSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { tmpdir } from 'node:os';
import { pathToFileURL, fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const app = join(here, '..');
const capture = join(app, '..', 'capture');
const corpus = process.argv[2] || join(process.env.USERPROFILE || process.env.HOME, 'Downloads', 'JM Sounds');

// The reader is TypeScript inside Capture, so it is bundled the same way the
// import command bundles its own entry point.
const tmp = mkdtempSync(join(tmpdir(), 'x7ldump-'));
const out = join(tmp, 'x7l.mjs');
// One entry re-exporting both modules, so the dump can reach the reader AND the
// grouping the planner derives from it.
const entry = join(tmp, 'entry.ts');
writeFileSync(entry, [
  "export { parseX7l, measureKeyzone, representativeKeyzone } from " + JSON.stringify(join(capture, 'src/engine/import/x7l.ts')) + ";",
  "export { dpfmPlan, findKeySplitGroups, planImport } from " + JSON.stringify(join(capture, 'src/engine/import/x7lPlan.ts')) + ";",
].join('\n'));
await build({
  entryPoints: [entry],
  bundle: true, platform: 'node', format: 'esm', target: 'node20',
  outfile: out, logLevel: 'warning',
});
const { parseX7l, measureKeyzone, representativeKeyzone, dpfmPlan, findKeySplitGroups, planImport } =
  await import(pathToFileURL(out).href);
// The C++ dump prints the world as the enum's ordinal, so map the id to it.
const WORLD_ORDER = ['rhodes-ep', 'mks', 'fm', 'strings'];

const files = readdirSync(corpus).filter((n) => /\.(x7l|x8l)$/i.test(n)).sort();
let text = '';
for (const name of files) {
  const bytes = new Uint8Array(readFileSync(join(corpus, name)));
  text += `FILE ${name}\n`;
  let lib;
  try {
    lib = parseX7l(bytes);
  } catch (e) {
    const kind = e?.name === 'X7lTruncatedError' ? 'truncated'
               : e?.name === 'X7lNoAudioError' ? 'noAudio' : 'format';
    text += `REFUSED ${kind}\n`;
    continue;
  }
  text += `VERSION ${lib.version}\n`;
  for (const b of lib.blocks) text += `BLOCK ${b.tag} ${b.offset}\n`;
  text += `PERFORMANCES ${lib.performances.length}\n`;
  for (const p of lib.performances) text += `PERF ${p}\n`;
  const dp = lib.dpfm ?? { performances: [], skipped: [] };
  text += `DPFM perfs=${dp.performances.length} skipped=${dp.skipped.length}
`;
  for (const pf of dp.performances) {
    text += `  PF ${pf.index} parts=${pf.parts.length} declared=${pf.declaredParts} name=${pf.name}
`;
    for (const pt of pf.parts)
      for (const e of pt.elements)
        text += `    EL ${pt.index}.${e.slot} on=${e.on ? 1 : 0} lib=${e.libraryWaveform ? 1 : 0}`
              + ` wf=${e.waveformNumber} idx=${e.waveformIndex === null ? -1 : e.waveformIndex}`
              + ` grp=${e.group} xa=${e.xa} rel=${e.release ? 1 : 0}`
              + ` note=${e.noteLo}-${e.noteHi} vel=${e.velLo}-${e.velHi} xf=${e.velXfade}
`;
  }
  for (const sk of dp.skipped) text += `  PFSKIP ${sk.index} ${sk.reason}\n`;

  // Grouping: which waveforms the library itself says are one instrument.
  const gp = dpfmPlan(lib);
  text += `PLAN valid=${gp ? 1 : 0} groups=${gp ? gp.groups.size : 0}\n`;
  if (gp) {
    for (const [key, g] of gp.groups) {
      const src = g.windowSource ?? '-';
      const rel = g.release.length ? g.release.join('+') : '-';
      text += `  GRP ${key} body=${g.body.join('+')} rel=${rel} xf=${g.velXfade} src=${src}\n`;
      for (const w of g.windows ?? []) text += `    WIN ${w.lo}-${w.hi} wf=${w.waveformIndex}\n`;
    }
  }
  for (const ks of findKeySplitGroups(lib.waveforms))
    text += `  KSPLIT ${ks.map((w) => w.index).join('+')}\n`;

  // The full plan: the bundles this library would become, and each one's zone
  // layout. Agreement here is agreement on the bundles themselves.
  const levels = lib.waveforms.map((wf) => measureKeyzone(bytes, representativeKeyzone(wf)).peakDb);
  let ip = null;
  try {
    ip = planImport(lib, levels);
  } catch (e) {
    text += `INSTRUMENTS refused: ${e.message}\n`;
  }
  if (ip) {
    text += `INSTRUMENTS ${ip.instruments.length}\n`;
    for (const s of ip.skipped) text += `  INSTSKIP ${s.name}: ${s.reason}\n`;
    for (const inst of ip.instruments) {
      text += `  INST ${inst.index} name=${inst.name} bundle=${inst.bundleName}`
            + ` world=${WORLD_ORDER.indexOf(inst.world)} sustaining=${inst.soundType === 'sustaining' ? 1 : 0}`
            + ` ch=${inst.channels} rate=${inst.sampleRate}`
            + ` keys=${inst.keyRangeLo}-${inst.keyRangeHi} notes=${inst.keyzoneCount}`
            + ` samples=${inst.sampleCount} xf=${inst.velXfade} dpfm=${inst.fromDpfm ? 1 : 0}\n`;
      for (const w of inst.windows)
        text += `    VWIN ${w.layer} ${w.name} ${w.lo}-${w.hi} src=${w.sources.join('+')}\n`;
      for (const l of inst.layerNames) text += `    LAYER ${l}\n`;
      for (const z of inst.zones)
        for (const s of z.samples)
          text += `    Z ${z.rootNote} ${z.keyRangeLo}-${z.keyRangeHi}`
                + ` v${z.velRangeLo}-${z.velRangeHi} ${s.file}`
                + ` wf=${s.waveformIndex} kz=${s.keyzoneIndex}\n`;
      for (const z of inst.releaseZones)
        for (const s of z.samples)
          text += `    R ${z.rootNote} ${z.keyRangeLo}-${z.keyRangeHi}`
                + ` v${z.velRangeLo}-${z.velRangeHi} ${s.file}`
                + ` wf=${s.waveformIndex} kz=${s.keyzoneIndex}\n`;
    }
  }
  for (const wf of lib.waveforms) {
    text += `WF ${wf.index} id=${wf.id} ch=${wf.channels} oneShot=${wf.oneShot ? 1 : 0}`
          + ` bytes=${wf.totalPcmBytes} name=${wf.name}\n`;
    for (const z of wf.keyzones) {
      text += `  KZ ${z.index} key=${z.keyRangeLo}-${z.keyRangeHi}`
            + ` vel=${z.velRangeLo}-${z.velRangeHi} root=${z.rootNote} rate=${z.sampleRate}`
            + ` len=${z.lengthSamples} start=${z.startSample}`
            + ` loop=${z.loopStartSample}..${z.loopEndSample} id=${z.sampleId}`
            + ` pcm=${z.pcmOffset}/${z.pcmBytes} total=${z.totalPcmBytes} rec=${z.recordIndex}\n`;
    }
  }
}

const dest = join(process.env.TEMP || tmpdir(), 'mireku-x7l-ts.txt');
writeFileSync(dest, text);
console.log(`wrote ${dest}  (${files.length} file(s), ${text.split('\n').length} lines)`);
