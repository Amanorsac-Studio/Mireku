// folder.mjs — a folder of named WAVs (VCSL style: Inst_Artic_C4_rr1_Main.wav) → region lists.
import { readdirSync, statSync } from 'node:fs';
import { join, basename, extname } from 'node:path';
import { decodeAudioFile, estimateMidi } from './audio.mjs';

const NOTE_IDX = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };
const DYN = /^(v\d+|ppp|pp|p|mp|mf|f|ff|fff|soft|med|medium|hard|loud)$/i;
const MIC = /^(main|close|far|room|mid|side|amb|ambient|hall|spot|near|ribbon|dyn|cond|L|R)$/i;
const DYN_ORDER = ['ppp', 'pp', 'p', 'soft', 'mp', 'med', 'medium', 'mf', 'f', 'hard', 'ff', 'loud', 'fff'];

function tokensOf(file) {
  const base = basename(file, extname(file));
  return base.split(/[_\-\s]+/).filter(Boolean);
}
function noteToken(t) {
  const m = /^([A-G])(#|b)?(-?\d)$/.exec(t);
  if (!m) return null;
  return NOTE_IDX[m[1]] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0) + (parseInt(m[3], 10) + 1) * 12; // scientific C4 = 60
}
function dynRank(t) { const m = /^v(\d+)$/i.exec(t); if (m) return parseInt(m[1], 10); const i = DYN_ORDER.indexOf(t.toLowerCase()); return i < 0 ? 0 : i; }

export function listWavDirs(root) {
  const out = [];
  const walk = (d) => {
    let entries; try { entries = readdirSync(d); } catch { return; }
    const wavs = entries.filter((e) => /\.(wav|flac)$/i.test(e));
    if (wavs.length >= 3) out.push({ dir: d, files: wavs.map((w) => join(d, w)) });
    for (const e of entries) { const p = join(d, e); try { if (statSync(p).isDirectory()) walk(p); } catch {} }
  };
  walk(root);
  return out;
}

/**
 * Map one folder into instruments. Returns [{ name, regions, kit }] — one per
 * distinct name prefix; folders with no note tokens become a chromatic "kit".
 */
export async function mapFolder({ dir, files }, { nameFromPath } = {}) {
  const parsed = files.map((f) => {
    const toks = tokensOf(f);
    let note = null, rr = 1, dyn = null, mic = null, prefix = [];
    for (const t of toks) {
      const n = noteToken(t);
      const rrm = /^rr(\d+)$/i.exec(t);
      if (n != null && note == null) note = n;
      else if (rrm) rr = parseInt(rrm[1], 10);
      else if (MIC.test(t)) mic = t;
      else if (DYN.test(t) && note != null) dyn = t;
      else if (note == null && !/^\d+$/.test(t)) prefix.push(t);
    }
    return { file: f, note, rr, dyn, mic, prefix: prefix.join(' ') };
  });
  // "Baroque Bass Recorder SusVib" + "BassRecorder" → unchanged; "… Staccato" + "SopRecorder Stac" → unchanged.
  // A prefix word is redundant when each of its camel-case parts starts a word already in the folder name.
  const joinName = (folderName, prefix) => {
    const folderWords = (folderName ?? '').toLowerCase().split(/\s+/).filter(Boolean);
    const redundant = (w) => w.split(/(?=[A-Z])/).filter(Boolean).every((p) => folderWords.some((fw) => fw.startsWith(p.toLowerCase())));
    const extra = prefix.split(/\s+/).filter((w) => w && !redundant(w));
    return [folderName, extra.join(' ')].filter(Boolean).join(' ').trim();
  };
  const pitched = parsed.filter((p) => p.note != null);
  const results = [];
  if (pitched.length >= 3) {
    // keep one mic position per (prefix, note, dyn, rr): prefer Main
    const groups = new Map();
    for (const p of pitched) { const k = p.prefix; if (!groups.has(k)) groups.set(k, []); groups.get(k).push(p); }
    for (const [prefix, items] of groups) {
      const mics = [...new Set(items.map((i) => i.mic).filter(Boolean))];
      const pickMic = mics.find((m) => /^main$/i.test(m)) ?? mics[0] ?? null;
      const chosen = items.filter((i) => i.mic === pickMic);
      if (chosen.length < 2) continue;
      // detect octave convention against the audio itself
      const offset = await detectOctaveOffset(chosen);
      const dyns = [...new Set(chosen.map((i) => i.dyn ?? ''))].sort((a, b) => dynRank(a) - dynRank(b));
      const bands = tileBands(dyns.length);
      const regions = chosen.map((i) => {
        const li = dyns.indexOf(i.dyn ?? '');
        const root = Math.max(0, Math.min(127, i.note + offset));
        return { file: i.file, root, lokey: root, hikey: root, lovel: bands[li][0], hivel: bands[li][1], loop: null, useFileLoop: true, tune: 0, volumeDb: 0, offset: 0, rrKey: `rr${i.rr}` };
      });
      results.push({ name: joinName(nameFromPath?.(dir), prefix), regions, kit: false, octaveOffset: offset });
    }
  } else {
    const sorted = [...parsed].sort((a, b) => a.file.localeCompare(b.file)).slice(0, 92);
    const regions = sorted.map((p, i) => ({ file: p.file, root: 36 + i, lokey: 36 + i, hikey: 36 + i, lovel: 1, hivel: 127, loop: null, useFileLoop: false, tune: 0, volumeDb: 0, offset: 0, rrKey: null, keytrack: 0 }));
    results.push({ name: nameFromPath?.(dir) ?? basename(dir), regions, kit: true, octaveOffset: 0 });
  }
  return results;
}

function tileBands(n) {
  if (n <= 1) return [[1, 127]];
  const out = []; let lo = 1;
  for (let i = 0; i < n; i++) { const hi = i === n - 1 ? 127 : Math.round(((i + 1) * 127) / n); out.push([lo, hi]); lo = hi + 1; }
  return out;
}

/** Try C4=60 / C3=60 / C5=60 naming against measured pitch of a few files. */
async function detectOctaveOffset(items) {
  // mid-range notes give the autocorrelation estimator its cleanest fundamentals
  const pool = items.filter((i) => i.note >= 40 && i.note <= 90);
  const src = pool.length >= 3 ? pool : items;
  const sample = src.filter((_, i) => i % Math.max(1, Math.floor(src.length / 6)) === 0).slice(0, 6);
  const score = { 0: 0, 12: 0, '-12': 0 };
  let voiced = 0;
  for (const it of sample) {
    try {
      const a = await decodeAudioFile(it.file);
      const m = estimateMidi(a.channels, a.sampleRate);
      if (m == null) continue;
      voiced++;
      for (const off of [0, 12, -12]) {
        let d = Math.abs(m - (it.note + off)) % 12; d = Math.min(d, 12 - d); // octave errors of the estimator forgiven
        const exact = Math.abs(m - (it.note + off));
        score[off] += exact < 1.5 ? 2 : d < 1 ? 1 : 0;
      }
    } catch {}
  }
  if (!voiced) return 0;
  return [0, 12, -12].reduce((best, o) => (score[o] > score[best] ? o : best), 0);
}
