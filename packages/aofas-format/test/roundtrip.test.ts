/**
 * Round-trip and tamper tests for the .aofas container.
 *
 * The point of these is not that "encryption works" — Node's ChaCha20-Poly1305
 * is not on trial. The point is that the LAYOUT is right: that a byte range
 * asked for at random comes back identical to the source WAV, that ranges
 * straddling chunk boundaries assemble correctly, and that a single flipped
 * byte anywhere is caught rather than played.
 */

import { describe, expect, it } from 'vitest';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, openSync, readSync, closeSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { randomBytes } from 'node:crypto';
import { pack } from '../src/pack.js';
import { Archive, readCard } from '../src/read.js';
import { makeKeyRing } from '../src/keyring.js';
import { AofasError } from '../src/types.js';
import { readWav } from '../src/wav.js';

const keyRing = makeKeyRing({ test: true });

/** A 24-bit stereo WAV of `frames` frames of deterministic noise. */
function makeWav(frames: number, seed: number): Buffer {
  const channels = 2;
  const bytesPerSample = 3;
  const dataBytes = frames * channels * bytesPerSample;
  const buf = Buffer.alloc(44 + dataBytes);
  buf.write('RIFF', 0, 'ascii');
  buf.writeUInt32LE(36 + dataBytes, 4);
  buf.write('WAVE', 8, 'ascii');
  buf.write('fmt ', 12, 'ascii');
  buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20); // PCM
  buf.writeUInt16LE(channels, 22);
  buf.writeUInt32LE(48000, 24);
  buf.writeUInt32LE(48000 * channels * bytesPerSample, 28);
  buf.writeUInt16LE(channels * bytesPerSample, 32);
  buf.writeUInt16LE(bytesPerSample * 8, 34);
  buf.write('data', 36, 'ascii');
  buf.writeUInt32LE(dataBytes, 40);
  let x = seed >>> 0;
  for (let i = 0; i < dataBytes; i++) {
    x = (x * 1664525 + 1013904223) >>> 0;
    buf[44 + i] = (x >>> 16) & 0xff;
  }
  return buf;
}

function makeBundle(sampleFrames: number[]): string {
  const dir = mkdtempSync(join(tmpdir(), 'aofas-'));
  const bundle = join(dir, 'Test_Piano.jmi');
  mkdirSync(join(bundle, 'samples'), { recursive: true });
  mkdirSync(join(bundle, 'presets'), { recursive: true });

  const zones = sampleFrames.map((frames, i) => {
    const file = `C${i}_v1_rr1.wav`;
    writeFileSync(join(bundle, 'samples', file), makeWav(frames, i + 1));
    return {
      rootNote: 60 + i,
      keyRangeLo: 60 + i,
      keyRangeHi: 60 + i,
      velRangeLo: 1,
      velRangeHi: 127,
      roundRobins: [{ file: `samples/${file}`, peakDb: -6, lengthMs: 100, loop: null }],
    };
  });

  writeFileSync(
    join(bundle, 'instrument.json'),
    JSON.stringify({
      formatVersion: 1,
      meta: {
        name: 'Test Piano',
        world: 'rhodes-ep',
        author: 'Amanorsac Studio',
        createdAt: '2026-09-02T00:00:00Z',
        captureMode: 'guided',
        channels: 'stereo',
        sampleRate: 48000,
        bitDepth: 24,
      },
      soundType: 'decaying',
      zones,
      velocityMap: [{ layer: 1, name: 'mf', lo: 1, hi: 127 }],
      reserved: { releaseSamples: [], pedalDownZones: [] },
    }),
  );
  writeFileSync(join(bundle, 'presets', 'Default.json'), JSON.stringify({ formatVersion: 1, name: 'Default' }));
  return bundle;
}

function sourcePcm(bundle: string, file: string): Buffer {
  const raw = readFileSync(join(bundle, 'samples', file));
  const w = readWav(raw);
  return raw.subarray(w.dataOffset, w.dataOffset + w.dataBytes);
}

describe('aofas round trip', () => {
  // Sizes chosen to exercise the awkward cases: smaller than a chunk, exactly
  // one chunk, and several chunks with a short tail.
  // 200000 frames = 1.2 MB = 19 chunks, so the multi-chunk paths are real.
  const frames = [1000, 65536 / 6 + 1, 200000, 12345];
  const bundle = makeBundle(frames);
  const out = join(bundle, '..', 'Test_Piano.aofas');
  const result = pack({ bundleDir: bundle, out, keyRing });

  it('packs every sample as a stream', () => {
    expect(result.streams).toBe(frames.length);
  });

  it('reads the public card without a key', () => {
    const card = readCard(out);
    expect(card.name).toBe('Test Piano');
    expect(card.author).toBe('Amanorsac Studio');
    expect(card.sampleCount).toBe(frames.length);
    // the card must never leak the payload
    expect(JSON.stringify(card)).not.toContain('samples/');
  });

  it('returns audio identical to the source WAV', () => {
    const a = Archive.open(out, keyRing);
    try {
      for (const s of a.streams) {
        const src = sourcePcm(bundle, s.path.replace('samples/', ''));
        expect(s.byteLength).toBe(src.length);
        expect(a.read(s.id, 0, s.byteLength).equals(src)).toBe(true);
      }
    } finally {
      a.close();
    }
  });

  it('reads arbitrary ranges, including across chunk boundaries', () => {
    const a = Archive.open(out, keyRing);
    try {
      const s = a.streams.find((x) => x.chunkCount > 1)!;
      const src = sourcePcm(bundle, s.path.replace('samples/', ''));
      const cs = a.header.chunkSize;
      const ranges: [number, number][] = [
        [0, 1],
        [cs - 1, cs + 1],
        [cs, 2 * cs],
        [cs - 10, cs + 10],
        [s.byteLength - 1, s.byteLength],
        [0, s.byteLength],
      ];
      for (const [from, to] of ranges) {
        expect(a.read(s.id, from, to).equals(src.subarray(from, to))).toBe(true);
      }
      for (let i = 0; i < 25; i++) {
        const from = Math.floor(Math.random() * s.byteLength);
        const to = from + Math.floor(Math.random() * (s.byteLength - from));
        expect(a.read(s.id, from, to).equals(src.subarray(from, to))).toBe(true);
      }
    } finally {
      a.close();
    }
  });

  it('verifies every stream against its recorded digest', () => {
    const a = Archive.open(out, keyRing);
    try {
      for (const s of a.streams) expect(a.verifyStream(s.id)).toBe(true);
    } finally {
      a.close();
    }
  });

  it('joins zones to streams by path', () => {
    const a = Archive.open(out, keyRing);
    try {
      for (const zone of a.index.instrument.zones) {
        for (const rr of zone.roundRobins) expect(a.streamForPath(rr.file).byteLength).toBeGreaterThan(0);
      }
    } finally {
      a.close();
    }
  });

  it('gives a fresh bundleId and salt on every pack', () => {
    const second = join(bundle, '..', 'Test_Piano_2.aofas');
    const r2 = pack({ bundleDir: bundle, out: second, keyRing });
    expect(r2.bundleId).not.toBe(result.bundleId);
    const saltOf = (p: string) => {
      const fd = openSync(p, 'r');
      const b = Buffer.alloc(52);
      readSync(fd, b, 0, 52, 0);
      closeSync(fd);
      return b.subarray(36, 52).toString('hex');
    };
    expect(saltOf(second)).not.toBe(saltOf(out));
  });
});

describe('aofas rejects tampering', () => {
  const bundle = makeBundle([20000]);
  const keyRingTest = makeKeyRing({ test: true });

  function packTo(name: string): string {
    const out = join(bundle, '..', name);
    pack({ bundleDir: bundle, out, keyRing: keyRingTest });
    return out;
  }

  function flip(path: string, at: number): void {
    const buf = readFileSync(path);
    buf[at] ^= 0x01;
    writeFileSync(path, buf);
  }

  it('detects a flipped byte in the audio', () => {
    const p = packTo('t1.aofas');
    const a0 = Archive.open(p, keyRingTest);
    const off = a0.streams[0].offset;
    a0.close();
    flip(p, off + 10);
    const a = Archive.open(p, keyRingTest);
    try {
      expect(() => a.read(1, 0, 4096)).toThrowError(AofasError);
    } finally {
      a.close();
    }
  });

  it('detects an edited public card', () => {
    const p = packTo('t2.aofas');
    flip(p, 140); // inside the card JSON
    expect(() => Archive.open(p, keyRingTest)).toThrowError(/AOFAS_ERR_(AUTH|HEADER)/);
  });

  it('detects a corrupted header', () => {
    const p = packTo('t3.aofas');
    flip(p, 40); // inside fileSalt, which the CRC covers
    expect(() => Archive.open(p, keyRingTest)).toThrowError(/AOFAS_ERR_HEADER_CRC/);
  });

  it('refuses a file sealed with a different studio key', () => {
    const p = packTo('t4.aofas');
    const other = makeKeyRing({ test: false, keyFile: writeTempKeyFile() });
    expect(() => Archive.open(p, other)).toThrowError(/AOFAS_ERR_AUTH/);
  });

  it('reports an unknown key generation distinctly', () => {
    const p = packTo('t5.aofas');
    const empty = { activeKeyId: 7, get: () => null };
    expect(() => Archive.open(p, empty)).toThrowError(/AOFAS_ERR_UNKNOWN_KEY/);
  });

  function writeTempKeyFile(): string {
    const dir = mkdtempSync(join(tmpdir(), 'aofas-key-'));
    const p = join(dir, 'studio-keys.json');
    writeFileSync(p, JSON.stringify({ keys: { '1': randomBytes(32).toString('hex') }, activeKeyId: 1 }));
    return p;
  }
});
