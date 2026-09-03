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

import { mkdirSync, statSync, writeFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { Archive, readCard } from './read.js';
import { defaultKeyFilePath, keygen, makeKeyRing } from './keyring.js';
import { pack } from './pack.js';
import { AofasError, userMessage } from './types.js';

function arg(argv: string[], name: string): string | undefined {
  const i = argv.indexOf(name);
  return i >= 0 ? argv[i + 1] : undefined;
}

function mb(n: number): string {
  return `${(n / 1024 / 1024).toFixed(1)} MB`;
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
        '  aofas info <file.aofas>',
        '  aofas open <file.aofas> [--test]',
        '  aofas verify <file.aofas> [--full] [--test]',
        '  aofas test-vector <bundle.jmi> -o <dir>',
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
      console.log(
        `Sealed ${res.streams} samples -> ${res.out}\n` +
          `  ${mb(statSync(res.out).size)}, key ${res.keyId}, bundle ${res.bundleId}, ` +
          `${((Date.now() - started) / 1000).toFixed(1)}s\n` +
          `  round-trip verified`,
      );
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
