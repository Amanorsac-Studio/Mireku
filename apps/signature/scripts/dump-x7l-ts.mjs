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
const out = join(mkdtempSync(join(tmpdir(), 'x7ldump-')), 'x7l.mjs');
await build({
  entryPoints: [join(capture, 'src', 'engine', 'import', 'x7l.ts')],
  bundle: true, platform: 'node', format: 'esm', target: 'node20',
  outfile: out, logLevel: 'warning',
});
const { parseX7l } = await import(pathToFileURL(out).href);

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
  for (const sk of dp.skipped) text += `  PFSKIP ${sk.index} ${sk.reason}
`;
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
