/**
 * The loop-seam rule: a declared loop is only a SUSTAIN loop if the level where
 * it restarts matches the level it wraps from. A decay tail does not loop.
 *
 * This is the fix for the client's "when I hold them for long, like a repeater"
 * on the grand pianos and the strings. Measured on his corpus the two
 * populations sit ~25 dB apart, so these tests pin the measurement and the
 * boundary rather than any single library's numbers.
 */
import { describe, expect, it } from 'vitest';
import {
  TAIL_LOOP_SEAM_DB,
  classifyWaveformLoop,
  loopSeamDb,
  loopSpansWholeSample,
} from '../src/engine/import/loopSeam';
import type { X7lKeyzone, X7lWaveform } from '../src/engine/import/x7l';
import { reportLines, reviewBundleLoops, type BundleFs } from '../src/engine/import/bundleLoops';
import { pcmWavHeader } from '../src/engine/import/audioFile';

/** A mono 16-bit tone whose amplitude follows `ampAt`, as raw PCM. */
function pcm(frames: number, ampAt: (i: number) => number): Uint8Array {
  const out = new Uint8Array(frames * 2);
  for (let i = 0; i < frames; i++) {
    const v = Math.round(Math.sin((i * 2 * Math.PI * 440) / 44100) * 32767 * ampAt(i));
    const u = v < 0 ? v + 65536 : v;
    out[i * 2] = u & 0xff;
    out[i * 2 + 1] = (u >> 8) & 0xff;
  }
  return out;
}

function keyzone(lengthSamples: number, loopStart: number, loopEnd: number): X7lKeyzone {
  return {
    index: 0,
    keyRangeLo: 0,
    keyRangeHi: 127,
    velRangeLo: 1,
    velRangeHi: 127,
    rootNote: 60,
    sampleRate: 44100,
    channels: 1,
    lengthSamples,
    startSample: 0,
    loopStartSample: loopStart,
    loopEndSample: loopEnd,
    sampleId: 1,
    pcm: [{ offset: 0, bytes: lengthSamples * 2 }],
    pcmOffset: 0,
    pcmBytes: lengthSamples * 2,
    totalPcmBytes: lengthSamples * 2,
    recordIndex: 0,
  };
}

function waveform(keyzones: X7lKeyzone[]): X7lWaveform {
  return {
    index: 0,
    id: 1,
    rawName: '1:Test',
    name: 'Test',
    channels: 1,
    keyzones,
    totalPcmBytes: keyzones.reduce((n, z) => n + z.totalPcmBytes, 0),
    oneShot: false,
  };
}

const LEN = 40000;

describe('loop seam', () => {
  it('reads a whole-sample loop as no loop at all', () => {
    expect(loopSpansWholeSample(keyzone(LEN, 0, LEN - 1))).toBe(true);
    expect(loopSpansWholeSample(keyzone(LEN, 500, 499))).toBe(true);
    expect(loopSpansWholeSample(keyzone(LEN, 500, LEN - 1))).toBe(false);
    expect(loopSeamDb(new Uint8Array(LEN * 2), keyzone(LEN, 0, LEN - 1))).toBeNull();
  });

  it('measures ~0 dB across a level-continuous sustain loop', () => {
    const bytes = pcm(LEN, () => 0.5); // steady tone: start and end match
    const seam = loopSeamDb(bytes, keyzone(LEN, 5000, LEN - 1))!;
    expect(Math.abs(seam)).toBeLessThan(1);
  });

  it('measures a large positive seam across a decay tail', () => {
    // -40 dB over the sample, which is what a piano note does
    const bytes = pcm(LEN, (i) => Math.pow(10, (-40 * (i / LEN)) / 20));
    const seam = loopSeamDb(bytes, keyzone(LEN, 5000, LEN - 1))!;
    expect(seam).toBeGreaterThan(TAIL_LOOP_SEAM_DB);
    expect(seam).toBeGreaterThan(25);
  });

  it('classifies a whole waveform from the median, not from one stray zone', () => {
    const steady = pcm(LEN, () => 0.5);
    const decay = pcm(LEN, (i) => Math.pow(10, (-40 * (i / LEN)) / 20));
    const z = () => keyzone(LEN, 5000, LEN - 1);

    // one decaying zone among four steady ones must not drag the waveform over
    expect(
      classifyWaveformLoop(steady, waveform([z(), z(), z()])),
    ).toBe('sustain');
    expect(classifyWaveformLoop(decay, waveform([z(), z(), z()]))).toBe('tail');
    expect(classifyWaveformLoop(steady, waveform([keyzone(LEN, 0, LEN - 1)]))).toBe('none');
  });
});

/** An in-memory bundle so the repair pass can be exercised without a library. */
function bundleFs(files: Map<string, Uint8Array | string>): BundleFs {
  return {
    readFile: (p) => {
      const v = files.get(p);
      if (v === undefined) throw new Error(`no such file ${p}`);
      return typeof v === 'string' ? new TextEncoder().encode(v) : v;
    },
    writeFile: (p, d) => void files.set(p, d),
    readDir: () => [...files.keys()],
    exists: (p) => files.has(p),
    join: (...parts) => parts.join('/'),
  };
}

function wav(data: Uint8Array): Uint8Array {
  const header = pcmWavHeader(data.length, 44100, 1, 16);
  const out = new Uint8Array(header.length + data.length);
  out.set(header, 0);
  out.set(data, header.length);
  return out;
}

function bundle(files: Map<string, Uint8Array | string>, soundType: string, body: Uint8Array) {
  files.set(
    'B.jmi/instrument.json',
    JSON.stringify({
      soundType,
      zones: [
        {
          roundRobins: [
            { file: 'samples/a.wav', loop: { startSample: 5000, endSample: LEN - 1, crossfadeSamples: 0 } },
          ],
        },
      ],
    }),
  );
  files.set('B.jmi/samples/a.wav', wav(body));
}

describe('repairing bundles already on disk', () => {
  it('unloops a decay tail and follows soundType, keeping a .bak', () => {
    const files = new Map<string, Uint8Array | string>();
    bundle(files, 'sustaining', pcm(LEN, (i) => Math.pow(10, (-40 * (i / LEN)) / 20)));
    const fs = bundleFs(files);

    const dry = reviewBundleLoops('B.jmi', fs, false);
    expect(dry.verdict).toBe('decay tail unlooped');
    expect(dry.removed).toBe(1);
    expect(files.has('B.jmi/instrument.json.bak')).toBe(false); // dry run writes nothing

    const applied = reviewBundleLoops('B.jmi', fs, true);
    expect(applied.soundTypeAfter).toBe('decaying');
    expect(files.has('B.jmi/instrument.json.bak')).toBe(true);
    const after = JSON.parse(String(files.get('B.jmi/instrument.json')));
    expect(after.zones[0].roundRobins[0].loop).toBeNull();
    expect(after.soundType).toBe('decaying');
    expect(reportLines([applied], true).join('\n')).toMatch(/instrument\.json\.bak/);
  });

  it('leaves a genuine sustain loop alone', () => {
    const files = new Map<string, Uint8Array | string>();
    bundle(files, 'sustaining', pcm(LEN, () => 0.5));
    const fs = bundleFs(files);

    const r = reviewBundleLoops('B.jmi', fs, true);
    expect(r.verdict).toBe('sustain loop kept');
    expect(r.removed).toBe(0);
    expect(files.has('B.jmi/instrument.json.bak')).toBe(false);
    const after = JSON.parse(String(files.get('B.jmi/instrument.json')));
    expect(after.zones[0].roundRobins[0].loop).not.toBeNull();
    expect(after.soundType).toBe('sustaining');
  });

  it('reports a bundle with no loops without touching it', () => {
    const files = new Map<string, Uint8Array | string>();
    files.set('B.jmi/instrument.json', JSON.stringify({ soundType: 'decaying', zones: [{ roundRobins: [{ file: 'samples/a.wav', loop: null }] }] }));
    const r = reviewBundleLoops('B.jmi', bundleFs(files), true);
    expect(r.verdict).toBe('no loops');
    expect(files.has('B.jmi/instrument.json.bak')).toBe(false);
  });
});
