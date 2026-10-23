#!/usr/bin/env node
// fix-pitch.mjs — find samples whose pitch is out of line with the instrument and retune them.
//   node src/fix-pitch.mjs [--dry] <bundle.jmi> [...]   (defaults to every bundle in the Sanctuary Library)
// Pianos are stretch-tuned (bass flat, treble sharp), so a sample is judged against the median of
// its neighbours, not against equal temperament. A sample is retuned only when it is > 25 cents away
// from that local curve AND two independent pitch methods (partial fit, YIN) agree within 25 cents.
import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { join, basename } from 'node:path';
import { homedir } from 'node:os';
import { encodeWav } from '@jm/jmi-format';
import { decodeAudioFile, resample, peakOf } from './audio.mjs';
import { measure, measureYin, measureHarmonicSum } from './check-pitch.mjs';

const LIB = join(homedir(), 'Documents', 'Amanorsac Studio', 'Sanctuary', 'Library');
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const nn = (m) => `${NAMES[m % 12]}${Math.floor(m / 12) - 1}`;
const LIMIT = 25, AGREE = 25;
const args = process.argv.slice(2);
const dry = args.includes('--dry');
const list = args.filter((a) => a !== '--dry');
const bundles = list.length ? list : readdirSync(LIB).filter((d) => d.endsWith('.jmi')).map((d) => join(LIB, d));
const median = (xs) => { const s = xs.slice().sort((a, b) => a - b); return s.length ? (s.length % 2 ? s[(s.length - 1) / 2] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2) : 0; };

for (const b of bundles) {
  const instPath = join(b, 'instrument.json');
  const inst = JSON.parse(readFileSync(instPath, 'utf8'));
  const rows = [];
  for (const z of inst.zones) for (const rr of z.roundRobins) {
    const p = await measure(join(b, rr.file), z.rootNote);
    const y = await measureYin(join(b, rr.file), z.rootNote);
    // total cents off equal temperament, octave-folded
    const yc = Number.isFinite(y.f0) ? y.cents : NaN;
    rows.push({ z, rr, root: z.rootNote, pc: Number.isFinite(p.f0) ? p.cents : NaN, yc });
  }
  const roots = [...new Set(rows.map((r) => r.root))].sort((a, b) => a - b);
  const fixes = [], unsure = [];
  for (const r of rows) {
    const i = roots.indexOf(r.root);
    const neigh = rows.filter((o) => o.root !== r.root && Math.abs(roots.indexOf(o.root) - i) <= 3 && Number.isFinite(o.pc)).map((o) => o.pc);
    const local = median(neigh);
    const dev = r.pc - local;
    if (!Number.isFinite(r.pc)) { unsure.push(`${basename(r.rr.file)}: pitch could not be measured`); continue; }
    if (Math.abs(dev) <= LIMIT) continue;
    let measured = (r.pc + r.yc) / 2;
    if (!Number.isFinite(r.yc) || Math.abs(r.yc - r.pc) > AGREE) {
      // tie-break with a third, independent method: use the pair that agrees
      const h = (await measureHarmonicSum(join(b, r.rr.file), r.root)).cents;
      const agreeP = Math.abs(h - r.pc) <= 15, agreeY = Number.isFinite(r.yc) && Math.abs(h - r.yc) <= 15;
      const label = `${basename(r.rr.file)} (${nn(r.root)}): partials ${(r.pc - local).toFixed(0)} c, YIN ${Number.isFinite(r.yc) ? (r.yc - local).toFixed(0) : 'n/a'} c, harmonic-sum ${(h - local).toFixed(0)} c vs neighbours`;
      if (agreeP && (!agreeY || Math.abs(h - r.pc) <= Math.abs(h - r.yc))) measured = (h + r.pc) / 2;
      else if (agreeY) measured = (h + r.yc) / 2;
      else { unsure.push(`${label} — no two methods agree, left as is`); continue; }
      if (Math.abs(measured - local) <= LIMIT) { unsure.push(`${label} — confirmed in tune`); continue; }
      unsure.push(`${label} — two methods agree, retuning`);
    }
    const shift = -(measured - local); // cents to apply
    fixes.push({ r, shift, local });
  }
  console.log(`\n### ${basename(b)} — ${rows.length} samples, ${fixes.length} retuned${dry ? ' (dry run)' : ''}`);
  for (const f of fixes) {
    console.log(`  ${basename(f.r.rr.file)} (${nn(f.r.root)}): measured ${(f.local - f.shift).toFixed(0)} c, neighbours ${f.local.toFixed(0)} c → retune ${f.shift > 0 ? '+' : ''}${f.shift.toFixed(0)} c`);
    if (dry) continue;
    const a = await decodeAudioFile(join(b, f.r.rr.file));
    const ch = resample(a.channels, Math.pow(2, f.shift / 1200)); // ratio > 1 reads faster = higher
    writeFileSync(join(b, f.r.rr.file), encodeWav({ sampleRate: a.sampleRate, bitDepth: 24, channels: ch }));
    const pk = peakOf(ch);
    f.r.rr.peakDb = pk > 0 ? Math.min(0, 20 * Math.log10(pk)) : -120;
    f.r.rr.lengthMs = (ch[0].length / a.sampleRate) * 1000;
  }
  for (const u of unsure) console.log('  ? ' + u);
  if (!dry && fixes.length) writeFileSync(instPath, JSON.stringify(inst, null, 2));
}
