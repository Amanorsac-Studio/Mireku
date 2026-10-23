#!/usr/bin/env node
/**
 * fake-render.mjs — synthesise what the DAW would hand back, so the slicer can be proved before
 * anyone renders a real patch. Each event becomes a decaying tone at its own pitch, with the
 * velocity controlling level, plus a little noise floor and a deliberate start offset to imitate
 * a DAW's pre-roll.
 *
 *   node src/fake-render.mjs [--manifest DIR/manifest.json] [--out DIR] [--name "Test Tone"]
 *                            [--offset 0.08]
 */
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { homedir } from 'node:os';
import { encodeWav } from '@jm/jmi-format';

const arg = (n, d) => { const i = process.argv.indexOf(`--${n}`); return i >= 0 ? process.argv[i + 1] : d; };
const MANIFEST = arg('manifest', join(homedir(), 'Documents', 'Amanorsac Studio', 'Render', 'MIDI', 'manifest.json'));
const OUT = arg('out', join(dirname(MANIFEST), '..', 'WAV'));
const NAME = arg('name', 'Test Tone');
const OFFSET = Number(arg('offset', 0.08));

const m = JSON.parse(readFileSync(MANIFEST, 'utf8'));
const rate = m.render.sampleRate;
const n = Math.ceil((m.totalSec + OFFSET + 1) * rate);
const L = new Float32Array(n), R = new Float32Array(n);
for (let i = 0; i < n; i++) { const h = (Math.random() * 2 - 1) * 1e-4; L[i] = h; R[i] = h * 0.9; }

// a key-off pass needs something to find after each note-off: a short damper-like thunk
if (m.kind === 'keyoff') {
  for (const e of m.events) {
    const start = Math.round(((e.releaseAt ?? e.at + e.hold) + OFFSET) * rate);
    const len = Math.min(n - start, Math.round(0.35 * rate));
    const f = 110 * Math.pow(2, (e.note - 69) / 24);
    for (let i = 0; i < len; i++) {
      const t = i / rate;
      const env = Math.exp(-t * 18);
      const s = env * 0.05 * (Math.sin(2 * Math.PI * f * t) + (Math.random() * 2 - 1) * 0.6);
      L[start + i] += s;
      R[start + i] += s * 0.95;
    }
  }
}

for (const e of m.events) {
  const f = 440 * Math.pow(2, (e.note - 69) / 12);
  const start = Math.round((e.at + OFFSET) * rate);
  const decay = e.note < 48 ? 9 : 5;             // seconds to fall away
  const len = Math.min(n - start, Math.round(decay * rate));
  const amp = 0.08 + 0.55 * (e.velocity / 127);
  for (let i = 0; i < len; i++) {
    const t = i / rate;
    const env = Math.exp(-t * (3 / decay)) * Math.min(1, t / 0.005);
    const s = env * amp * (Math.sin(2 * Math.PI * f * t) + 0.3 * Math.sin(4 * Math.PI * f * t) + 0.12 * Math.sin(6 * Math.PI * f * t)) / 1.42;
    L[start + i] += s;
    R[start + i] += s * 0.98;
  }
}

mkdirSync(OUT, { recursive: true });
const file = join(OUT, `${NAME}.wav`);
writeFileSync(file, encodeWav({ sampleRate: rate, bitDepth: m.render.bitDepth, channels: [L, R] }));
console.log(`fake render → ${file}\n  ${m.events.length} events · ${(n / rate / 60).toFixed(1)} min · start offset ${OFFSET}s`);
