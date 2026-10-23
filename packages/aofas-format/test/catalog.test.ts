/**
 * Catalogue and licensing tests.
 *
 * The point of these is the guardrail, not the plumbing: a library built on a
 * CC BY source must not be sealable without its credit line, and that credit
 * must be readable from the PLAINTEXT card — because attribution locked inside
 * an encrypted payload satisfies nobody.
 */

import { describe, expect, it } from 'vitest';
import { mkdtempSync, mkdirSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pack } from '../src/pack.js';
import { Archive, readCard } from '../src/read.js';
import { makeKeyRing } from '../src/keyring.js';
import {
  AMANORSAC_ORIGINAL,
  KNOWN_SOURCES,
  categoryFromWorld,
  creditLines,
  licensingProblems,
  type Licensing,
} from '../src/catalog.js';

const keyRing = makeKeyRing({ test: true });

function wav(frames: number): Buffer {
  const ch = 2,
    bps = 3,
    data = frames * ch * bps;
  const b = Buffer.alloc(44 + data);
  b.write('RIFF', 0, 'ascii');
  b.writeUInt32LE(36 + data, 4);
  b.write('WAVE', 8, 'ascii');
  b.write('fmt ', 12, 'ascii');
  b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20);
  b.writeUInt16LE(ch, 22);
  b.writeUInt32LE(48000, 24);
  b.writeUInt32LE(48000 * ch * bps, 28);
  b.writeUInt16LE(ch * bps, 32);
  b.writeUInt16LE(bps * 8, 34);
  b.write('data', 36, 'ascii');
  b.writeUInt32LE(data, 40);
  for (let i = 0; i < data; i++) b[44 + i] = (i * 7) & 0xff;
  return b;
}

function bundle(world = 'mks'): string {
  const dir = mkdtempSync(join(tmpdir(), 'aofas-cat-'));
  const b = join(dir, 'Cat_Test.jmi');
  mkdirSync(join(b, 'samples'), { recursive: true });
  writeFileSync(join(b, 'samples', 'C3_v1_rr1.wav'), wav(5000));
  writeFileSync(
    join(b, 'instrument.json'),
    JSON.stringify({
      formatVersion: 1,
      meta: {
        name: 'Cat Test',
        world,
        author: 'Amanorsac Studio',
        createdAt: '2026-09-03T00:00:00Z',
        captureMode: 'guided',
        channels: 'stereo',
        sampleRate: 48000,
        bitDepth: 24,
      },
      soundType: 'decaying',
      zones: [
        {
          rootNote: 60,
          keyRangeLo: 0,
          keyRangeHi: 127,
          velRangeLo: 1,
          velRangeHi: 127,
          roundRobins: [{ file: 'samples/C3_v1_rr1.wav', peakDb: -6, lengthMs: 100, loop: null }],
        },
      ],
      velocityMap: [{ layer: 1, name: 'mf', lo: 1, hi: 127 }],
      reserved: { releaseSamples: [], pedalDownZones: [] },
    }),
  );
  return b;
}

describe('licensing guardrail', () => {
  it('refuses to seal a CC BY source with no credit line', () => {
    const bad: Licensing = {
      license: { id: 'proprietary', requiresAttribution: true },
      sources: [
        {
          library: 'Salamander Grand Piano V3',
          license: { id: 'CC-BY-3.0', requiresAttribution: true }, // no attribution text
        },
      ],
    };
    const b = bundle();
    expect(() => pack({ bundleDir: b, out: join(b, '..', 'bad.aofas'), keyRing, licensing: bad })).toThrowError(
      /requires attribution/i,
    );
  });

  it('refuses a library that disclaims attribution its sources demand', () => {
    const laundered: Licensing = {
      license: { id: 'proprietary', requiresAttribution: false },
      sources: [KNOWN_SOURCES.salamander],
    };
    expect(licensingProblems(laundered).join(' ')).toMatch(/obligation travels with the audio/);
  });

  it('accepts CC0 sources with no credit', () => {
    const cc0: Licensing = { license: { id: 'proprietary', requiresAttribution: false }, sources: [KNOWN_SOURCES.vcsl] };
    expect(licensingProblems(cc0)).toEqual([]);
    expect(creditLines(cc0)).toEqual([]);
  });

  it('carries credits in the plaintext card, readable with NO key', () => {
    const b = bundle();
    const out = join(b, '..', 'credited.aofas');
    pack({
      bundleDir: b,
      out,
      keyRing,
      category: 'grand-piano',
      tags: ['worship', 'bright'],
      licensing: {
        license: {
          id: 'proprietary',
          requiresAttribution: true,
          attribution: KNOWN_SOURCES.salamander.license.attribution,
        },
        sources: [{ ...KNOWN_SOURCES.salamander, modifications: 'trimmed, looped, re-tuned' }],
      },
    });

    // readCard takes no key ring at all — that is the whole point.
    const card = readCard(out);
    expect(card.category).toBe('grand-piano');
    expect(card.tags).toEqual(['worship', 'bright']);
    const lines = creditLines(card.licensing!);
    expect(lines.join(' ')).toMatch(/Alexander Holm/);
    expect(lines.join(' ')).toMatch(/Modified: trimmed, looped, re-tuned/);
  });

  it('rejects an unknown category rather than guessing', () => {
    const b = bundle();
    expect(() =>
      pack({ bundleDir: b, out: join(b, '..', 'x.aofas'), keyRing, category: 'grand piano' as never }),
    ).toThrowError(/unknown category/i);
  });

  it('falls back to a category derived from the .jmi world', () => {
    const b = bundle('rhodes-ep');
    const out = join(b, '..', 'derived.aofas');
    pack({ bundleDir: b, out, keyRing });
    expect(readCard(out).category).toBe('electric-piano');
    expect(categoryFromWorld('rhodes-ep')).toBe('electric-piano');
    expect(categoryFromWorld('mks')).toBe('synth');
  });

  it('defaults to wholly-original Amanorsac material', () => {
    const b = bundle();
    const out = join(b, '..', 'original.aofas');
    pack({ bundleDir: b, out, keyRing });
    expect(readCard(out).licensing).toEqual(AMANORSAC_ORIGINAL);
  });

  it('seals provenance in the index, not in the public card', () => {
    const b = bundle();
    const out = join(b, '..', 'prov.aofas');
    pack({
      bundleDir: b,
      out,
      keyRing,
      licensing: { license: { id: 'proprietary', requiresAttribution: false }, sources: [KNOWN_SOURCES.vcsl] },
      provenance: { streamSources: { 'samples/C3_v1_rr1.wav': 0 }, notes: 'normalised to -6 dBFS' },
    });
    expect(JSON.stringify(readCard(out))).not.toContain('normalised');
    const a = Archive.open(out, keyRing);
    try {
      expect(a.index.provenance?.notes).toBe('normalised to -6 dBFS');
      expect(a.index.provenance?.streamSources?.['samples/C3_v1_rr1.wav']).toBe(0);
    } finally {
      a.close();
    }
  });
});
