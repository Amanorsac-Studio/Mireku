#!/usr/bin/env node
/**
 * aofas — command line for the sealed container.
 *
 *   aofas keygen                          create a new studio key generation
 *   aofas pack <bundle.jmi> [-o out]      seal a .jmi bundle
 *   aofas info <file.aofas>               public card (no key needed)
 *   aofas open <file.aofas>               card + index + stream table
 *   aofas verify <file.aofas> [--full]    integrity check
 *   aofas test-vector <bundle.jmi> -o dir reference vector, all-zero test key
 *
 * Flags: --test (use the all-zero test key), --key-file <path>, --key-id <n>
 */

import { mkdirSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { Archive, readCard } from './read.js';
import { defaultKeyFilePath, keygen, makeKeyRing } from './keyring.js';
import { pack } from './pack.js';
import { AofasError, userMessage } from './types.js';
import {
  AMANORSAC_ORIGINAL,
  CATEGORIES,
  KNOWN_SOURCES,
  PROVENANCE_UNVERIFIED,
  creditLines,
  isUnverified,
  type Licensing,
  type SourceRef,
} from './catalog.js';

function arg(argv: string[], name: string): string | undefined {
  const i = argv.indexOf(name);
  return i >= 0 ? argv[i + 1] : undefined;
}

function mb(n: number): string {
  return `${(n / 1024 / 1024).toFixed(1)} MB`;
}

/**
 * --source may name a library from the plan's verified list (`--source salamander`)
 * or an ad-hoc one as "Name|licenceId|url|credit line". Anything unrecognised is
 * an error rather than a silent "assume CC0": guessing a licence is the one
 * mistake that cannot be fixed after the file ships.
 */
function sourcesFrom(argv: string[]): SourceRef[] {
  const out: SourceRef[] = [];
  argv.forEach((a, i) => {
    if (a !== '--source') return;
    const spec = argv[i + 1];
    if (!spec) throw new Error('--source needs a value');
    const known = KNOWN_SOURCES[spec.toLowerCase()];
    if (known) {
      out.push({ ...known, retrieved: new Date().toISOString().slice(0, 10) });
      return;
    }
    const [library, id, url, attribution] = spec.split('|');
    if (!library || !id) {
      throw new Error(
        `unknown source "${spec}". Use one of: ${Object.keys(KNOWN_SOURCES).join(', ')}, ` +
          'or describe it as "Library name|licence-id|url|credit line".',
      );
    }
    const requiresAttribution = /^cc-?by/i.test(id);
    out.push({
      library,
      url: url || undefined,
      retrieved: new Date().toISOString().slice(0, 10),
      license: { id, url: url || undefined, requiresAttribution, attribution: attribution || undefined },
    });
  });
  return out;
}

/** The licence of the library as shipped, given its sources. */
function licensingFrom(argv: string[]): Licensing {
  if (argv.includes('--unverified')) return PROVENANCE_UNVERIFIED;
  const sources = sourcesFrom(argv);
  if (sources.length === 0) return AMANORSAC_ORIGINAL;
  const mustCredit = sources.filter((s) => s.license.requiresAttribution);
  return {
    license: {
      id: 'proprietary',
      name: 'Amanorsac Studio — all rights reserved, with third-party material as credited',
      requiresAttribution: mustCredit.length > 0,
      attribution: mustCredit.length > 0 ? mustCredit.map((s) => s.license.attribution).join(' ') : undefined,
    },
    sources,
  };
}

function ring(argv: string[]) {
  return makeKeyRing({ test: argv.includes('--test'), keyFile: arg(argv, '--key-file') });
}

function main(argv: string[]): number {
  const cmd = argv[0];

  if (!cmd || cmd === '--help' || cmd === '-h') {
    console.log(
      [
        'aofas — Amanorsac sealed instrument container',
        '',
        '  aofas keygen [--key-file <path>]',
        '  aofas pack <bundle.jmi> [-o <out.aofas>] [--test] [--key-id <n>]',
        '           [--name <instrument name>] [--author <brand>] [--drop-empty]',
        '           [--category <shelf>] [--tags a,b,c] [--source <id|spec> ...] [--unverified]',
        '  aofas info <file.aofas>',
        '  aofas credits <file.aofas | folder>',
        '  aofas open <file.aofas> [--test]',
        '  aofas verify <file.aofas> [--full] [--test]',
        '  aofas test-vector <bundle.jmi> -o <dir>',
        '',
        `  categories:    ${CATEGORIES.join(' | ')}`,
        `  known sources: ${Object.keys(KNOWN_SOURCES).join(' | ')}`,
        '  ad-hoc source: "Library name|licence-id|url|credit line"',
        '',
        `  key file: ${defaultKeyFilePath()}`,
      ].join('\n'),
    );
    return 0;
  }

  switch (cmd) {
    case 'keygen': {
      const { path, keyId } = keygen(arg(argv, '--key-file'));
      console.log(`Wrote studio key generation ${keyId} to ${path}`);
      console.log('BACK THIS FILE UP. Without it, every library packed with it is unreadable.');
      return 0;
    }

    case 'pack': {
      const bundleDir = argv[1];
      if (!bundleDir) throw new Error('usage: aofas pack <bundle.jmi> [-o out.aofas]');
      const out =
        arg(argv, '-o') ?? join(dirname(bundleDir), `${basename(bundleDir).replace(/\.jmi$/i, '')}.aofas`);
      const keyIdArg = arg(argv, '--key-id');

      let last = '';
      const started = Date.now();
      const res = pack({
        bundleDir,
        out,
        keyRing: ring(argv),
        keyId: keyIdArg ? Number(keyIdArg) : undefined,
        name: arg(argv, '--name'),
        author: arg(argv, '--author'),
        category: arg(argv, '--category') as never,
        tags: arg(argv, '--tags')?.split(',').map((t) => t.trim()).filter(Boolean),
        licensing: licensingFrom(argv),
        dropEmptySamples: argv.includes('--drop-empty'),
        onProgress: (done, total, what) => {
          const phase = what.split(' ')[0];
          if (phase !== last) {
            last = phase;
            process.stdout.write(`\n${phase} ${total} samples`);
          }
          if (done % 25 === 0) process.stdout.write('.');
        },
      });
      process.stdout.write('\n');

      // §8 step 9: a packer that has not round-tripped its own output has not
      // finished. Open the file we just wrote and read audio back out of it.
      const a = Archive.open(res.out, ring(argv));
      try {
        const s = a.streams[Math.floor(a.streams.length / 2)];
        const head = a.readFrames(s.id, 0, Math.min(65536, s.frames));
        if (head.length === 0) throw new Error('round-trip read returned nothing');
      } finally {
        a.close();
      }

      if (res.droppedEmpty.length > 0) {
        console.warn(
          `\nWARNING: ${res.droppedEmpty.length} take(s) recorded no audio and were dropped:\n  ` +
            res.droppedEmpty.join('\n  ') +
            '\nThe zones that used them still play their other round robins. Re-sample when you can.',
        );
      }
      const sealedCard = readCard(res.out);
      const credits = sealedCard.licensing ? creditLines(sealedCard.licensing) : [];
      console.log(
        `Sealed ${res.streams} samples -> ${res.out}\n` +
          `  category ${sealedCard.category}${sealedCard.tags?.length ? ` · tags ${sealedCard.tags.join(', ')}` : ''}\n` +
          (isUnverified(sealedCard.licensing)
            ? '  PROVENANCE UNVERIFIED — internal use only, do not ship until the source is established\n'
            : '') +
          (credits.length
            ? `  attribution required (readable without a key):\n` + credits.map((c) => `    ${c}`).join('\n') + '\n'
            : '  no third-party attribution required\n') +
          `  ${mb(statSync(res.out).size)}, key ${res.keyId}, bundle ${res.bundleId}, ` +
          `${((Date.now() - started) / 1000).toFixed(1)}s\n` +
          `  round-trip verified`,
      );
      return 0;
    }

    case 'credits': {
      // Reads the PLAINTEXT card only: a credits screen, a licence audit or a
      // release checklist must work with no studio key at all.
      const target = argv[1];
      if (!target) throw new Error('usage: aofas credits <file.aofas | folder>');
      const files = statSync(target).isDirectory()
        ? readdirSync(target)
            .filter((n) => n.toLowerCase().endsWith('.aofas'))
            .map((n) => join(target, n))
        : [target];
      if (files.length === 0) {
        console.log('No .aofas libraries found.');
        return 0;
      }
      let any = false;
      for (const f of files) {
        const card = readCard(f);
        const lines = card.licensing ? creditLines(card.licensing) : [];
        console.log(`${card.name} — ${card.category ?? 'uncategorised'} — ${basename(f)}`);
        if (isUnverified(card.licensing)) {
          any = true;
          console.log('  PROVENANCE UNVERIFIED — do not ship until the source is established');
        } else if (!card.licensing) {
          console.log('  (no licensing recorded — packed before catalogue support)');
        } else if (lines.length === 0) {
          console.log(`  ${card.licensing.license.id}: no attribution required`);
        } else {
          any = true;
          for (const l of lines) console.log(`  ${l}`);
        }
        for (const src of card.licensing?.sources ?? []) {
          console.log(`    source: ${src.library} (${src.license.id})${src.url ? ' — ' + src.url : ''}`);
        }
      }
      if (any) {
        console.log(
          '\nCredits must be shown wherever this content ships. A library marked UNVERIFIED must not ship at all ' +
            'until its source is established (plan §10: "if provenance is uncertain, the sample does not ship").',
        );
      }
      return 0;
    }

    case 'info': {
      const card = readCard(argv[1]);
      console.log(JSON.stringify(card, null, 2));
      return 0;
    }

    case 'open': {
      const a = Archive.open(argv[1], ring(argv));
      try {
        console.log(`${a.card.name} — ${a.card.author}`);
        console.log(`  world ${a.card.world}, ${a.streams.length} samples, ${mb(a.card.totalAudioBytes)} audio`);
        console.log(`  zones ${a.index.instrument.zones.length}, presets ${a.index.presets.length}`);
        console.log(`  chunk ${a.header.chunkSize} B, key ${a.header.keyId}, bundle ${a.card.bundleId}`);
        for (const s of a.streams.slice(0, 5)) {
          console.log(`  #${s.id} ${s.path} — ${s.codec} ${s.sampleRate} Hz x${s.channels}, ${s.frames} frames`);
        }
        if (a.streams.length > 5) console.log(`  … ${a.streams.length - 5} more`);
      } finally {
        a.close();
      }
      return 0;
    }

    case 'verify': {
      const full = argv.includes('--full');
      const a = Archive.open(argv[1], ring(argv));
      try {
        let bad = 0;
        for (const s of a.streams) {
          const ok = full ? a.verifyStream(s.id) : a.read(s.id, 0, Math.min(4096, s.byteLength)).length > 0;
          if (!ok) {
            bad++;
            console.error(`  FAIL ${s.path}`);
          }
        }
        console.log(
          bad === 0
            ? `OK — ${a.streams.length} streams ${full ? 'fully verified' : 'open and decrypt'}`
            : `${bad} of ${a.streams.length} streams failed`,
        );
        return bad === 0 ? 0 : 1;
      } finally {
        a.close();
      }
    }

    case 'test-vector': {
      // Spec §11: a reader implementation is correct when it reproduces this
      // file's plaintext byte for byte, using the documented all-zero key.
      const bundleDir = argv[1];
      const dir = arg(argv, '-o') ?? 'testdata/aofas';
      mkdirSync(dir, { recursive: true });
      const out = join(dir, 'vector1.aofas');
      const keyRing = makeKeyRing({ test: true });
      const res = pack({ bundleDir, out, keyRing });

      const a = Archive.open(out, keyRing);
      try {
        const expected = {
          note: 'AOFAS v1 reference vector. Studio key = 32 zero bytes, keyId 1.',
          file: basename(out),
          bundleId: res.bundleId,
          chunkSize: a.header.chunkSize,
          card: a.card,
          streams: a.streams.map((s) => ({
            id: s.id,
            path: s.path,
            codec: s.codec,
            frames: s.frames,
            byteLength: s.byteLength,
            offset: s.offset,
            chunkCount: s.chunkCount,
            plaintextSha256: s.sha256,
          })),
        };
        writeFileSync(join(dir, 'vector1.expected.json'), JSON.stringify(expected, null, 2) + '\n');
      } finally {
        a.close();
      }
      console.log(`Wrote ${out} and vector1.expected.json`);
      return 0;
    }

    default:
      console.error(`Unknown command: ${cmd}`);
      return 2;
  }
}

try {
  process.exit(main(process.argv.slice(2)));
} catch (e) {
  if (e instanceof AofasError) {
    console.error(`${e.message}\n  -> user-facing: ${userMessage(e.code)}`);
  } else {
    console.error(e instanceof Error ? e.message : String(e));
  }
  process.exit(1);
}
