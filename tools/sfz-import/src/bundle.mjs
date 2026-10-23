// bundle.mjs — region list → a validated .jmi bundle on disk.
import { mkdirSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { encodeWav, midiToNoteName, validateBundle, buildDefaultRack, defaultEngineParams } from '@jm/jmi-format';
import { probeAudio, decodeAudioFile, resample, peakOf, applyGain, trimStart, toChannelCount } from './audio.mjs';

const RATES = [44100, 48000, 88200, 96000];
const VEL_TARGETS = [23, 63, 95, 119];
const MAX_LAYERS = 4, MAX_RR = 4;
const LAYER_NAMES = { 1: ['Full'], 2: ['Soft', 'Hard'], 3: ['Soft', 'Medium', 'Hard'], 4: ['Soft', 'Medium', 'Hard', 'Very Hard'] };

function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
function nearestRate(r) { return RATES.reduce((b, x) => (Math.abs(x - r) < Math.abs(b - r) ? x : b), RATES[0]); }

/** Group regions into key groups → velocity bands → round robins; fold to ≤4 layers; tile velocities; fill key gaps. */
function planZones(regions) {
  const groups = new Map();
  for (const r of regions) {
    const gk = `${r.lokey}-${r.hikey}-${r.root}`;
    if (!groups.has(gk)) groups.set(gk, { lokey: r.lokey, hikey: r.hikey, root: r.root, bands: new Map() });
    const g = groups.get(gk);
    const bk = `${r.lovel}-${r.hivel}`;
    if (!g.bands.has(bk)) g.bands.set(bk, { lovel: r.lovel, hivel: r.hivel, rrs: [] });
    g.bands.get(bk).rrs.push(r);
  }
  let maxLayers = 1, repBands = null;
  for (const g of groups.values()) {
    let bands = [...g.bands.values()].sort((a, b) => a.lovel - b.lovel);
    if (bands.length > MAX_LAYERS) {
      const chosen = new Set();
      for (const t of VEL_TARGETS) {
        let best = null, bd = 1e9;
        for (const b of bands) { if (chosen.has(b)) continue; const d = Math.abs((b.lovel + b.hivel) / 2 - t); if (d < bd) { bd = d; best = b; } }
        if (best) chosen.add(best);
      }
      bands = bands.filter((b) => chosen.has(b)).sort((a, b) => a.lovel - b.lovel);
    }
    // re-tile to cover 1..127
    const mids = bands.map((b) => (b.lovel + b.hivel) / 2);
    for (let i = 0; i < bands.length; i++) {
      bands[i].lo = i === 0 ? 1 : Math.round((mids[i - 1] + mids[i]) / 2) + 1;
      bands[i].hi = i === bands.length - 1 ? 127 : Math.round((mids[i] + mids[i + 1]) / 2);
      bands[i].layer = i + 1;
      bands[i].rrs = dedupeRr(bands[i].rrs).slice(0, MAX_RR);
    }
    g.layers = bands;
    if (bands.length > maxLayers) { maxLayers = bands.length; repBands = bands; }
    if (!repBands) repBands = bands;
  }
  // fill key gaps between non-overlapping neighbours
  const ordered = [...groups.values()].sort((a, b) => a.lokey - b.lokey || a.root - b.root);
  for (let i = 0; i + 1 < ordered.length; i++) {
    const a = ordered[i], b = ordered[i + 1];
    if (b.lokey > a.hikey + 1) {
      const mid = Math.floor((a.root + b.root) / 2);
      a.hikey = clamp(mid, a.hikey, b.lokey - 1);
      b.lokey = a.hikey + 1;
    }
  }
  const velocityMap = repBands.map((b, i) => ({ layer: i + 1, name: LAYER_NAMES[repBands.length][i], lo: b.lo, hi: b.hi }));
  return { groups: ordered, velocityMap };
}

function dedupeRr(rrs) {
  const seen = new Set(), out = [];
  for (const r of rrs) { const k = r.file ?? r.name; if (seen.has(k)) continue; seen.add(k); out.push(r); }
  return out;
}

function loopFor(loop, ratio, offset, frames, rate) {
  if (!loop) return null;
  let start = Math.round((loop.start - offset) / ratio), end = Math.round((loop.end - offset) / ratio);
  if (start < 0) start = 0;
  if (end > frames) end = frames;
  const len = end - start;
  if (len < 64) return null;
  const xf = len < 4096 ? Math.floor(len / 4) : Math.min(Math.floor(len / 2), Math.round(rate * 0.05));
  return { startSample: start, endSample: end, crossfadeSamples: xf };
}

/**
 * Build one bundle. opts: { name, world, regions, outDir, author, notes, ampeg: {attack, release}, kit }
 * Returns { path, zones, samples, warnings, errors, bytes }.
 */
export async function buildBundle(opts) {
  const { name, world, outDir } = opts;
  const regions = opts.regions.filter((r) => r.file || r.pcm);
  if (!regions.length) throw new Error('no usable regions');
  // probe rates / channels
  const rates = new Map(); let anyStereo = false;
  for (const r of regions) {
    let info;
    if (r.pcm) info = { sampleRate: r.sampleRate, channels: r.pcm.length };
    else { try { info = probeAudio(r.file); } catch (e) { r.bad = e.message; continue; } }
    r.info = info;
    rates.set(info.sampleRate, (rates.get(info.sampleRate) ?? 0) + 1);
    if (info.channels >= 2) anyStereo = true;
  }
  const usable = regions.filter((r) => !r.bad);
  const majority = [...rates.entries()].sort((a, b) => b[1] - a[1])[0][0];
  const rate = nearestRate(majority);
  const nch = anyStereo ? 2 : 1;
  const { groups, velocityMap } = planZones(usable);

  const bundleDir = join(outDir, `${name.replace(/[<>:"/\\|?*]+/g, '').trim()}.jmi`);
  if (existsSync(bundleDir)) rmSync(bundleDir, { recursive: true, force: true });
  mkdirSync(join(bundleDir, 'samples'), { recursive: true });
  mkdirSync(join(bundleDir, 'presets'), { recursive: true });

  const rrCounter = new Map();
  const zones = [];
  let bytes = 0, samples = 0, looped = 0;
  const warnings = [];
  for (const g of groups) {
    for (const band of g.layers) {
      const roundRobins = [];
      for (const r of band.rrs) {
        const nameKey = `${g.root}-${band.layer}`;
        const rrIndex = (rrCounter.get(nameKey) ?? 0) + 1;
        rrCounter.set(nameKey, rrIndex);
        const fileName = `${midiToNoteName(g.root)}_v${band.layer}_rr${rrIndex}.wav`;
        try {
          const a = r.pcm ? { sampleRate: r.sampleRate, channels: r.pcm, loop: null } : await decodeAudioFile(r.file);
          let ch = trimStart(a.channels, r.offset);
          const ratio = Math.pow(2, (r.tune ?? 0) / 1200) * (a.sampleRate / rate);
          ch = resample(ch, ratio);
          ch = toChannelCount(ch, nch);
          let gain = Math.pow(10, (r.volumeDb ?? 0) / 20);
          const peak0 = peakOf(ch);
          if (peak0 * gain > 0.989) gain = 0.989 / peak0; // never clip; peakDb stays ≤ 0
          applyGain(ch, gain);
          const frames = ch[0].length;
          const srcLoop = r.loop ?? (r.useFileLoop ? a.loop : null);
          const loop = loopFor(srcLoop, ratio, r.offset, frames, rate);
          if (loop) looped++;
          const peak = peakOf(ch);
          const buf = encodeWav({ sampleRate: rate, bitDepth: 24, channels: ch });
          writeFileSync(join(bundleDir, 'samples', fileName), buf);
          bytes += buf.length; samples++;
          roundRobins.push({ file: `samples/${fileName}`, peakDb: peak > 0 ? Math.min(0, 20 * Math.log10(peak)) : -120, lengthMs: (frames / rate) * 1000, loop });
        } catch (e) {
          warnings.push(`${r.file ?? r.name}: ${e.message}`);
        }
      }
      if (roundRobins.length)
        zones.push({ rootNote: g.root, keyRangeLo: g.lokey, keyRangeHi: g.hikey, velRangeLo: band.lo, velRangeHi: band.hi, roundRobins });
    }
  }
  if (!zones.length) throw new Error('no zones could be written');
  // a piano with looped bass notes is still a decaying instrument: only call it
  // sustaining when (nearly) every sample loops
  const sustaining = opts.kit ? false : looped >= samples * 0.9;

  const instrument = {
    formatVersion: 1,
    meta: {
      name, world, author: opts.author ?? 'Imported', createdAt: new Date().toISOString(),
      captureMode: 'auto', channels: nch === 2 ? 'stereo' : 'mono', sampleRate: rate, bitDepth: 24,
      notes: opts.notes ?? '',
    },
    soundType: sustaining ? 'sustaining' : 'decaying',
    zones,
    velocityMap,
    reserved: { releaseSamples: [], pedalDownZones: [] },
  };
  writeFileSync(join(bundleDir, 'instrument.json'), JSON.stringify(instrument, null, 2));

  // presets: a dry Default plus two texture starting points
  const attackMs = clamp(Math.round((opts.ampeg?.attack ?? (sustaining ? 0.01 : 0.002)) * 1000), 0, 5000);
  const releaseMs = clamp(Math.round((opts.ampeg?.release ?? (sustaining ? 0.4 : 0.25)) * 1000), 1, 10000);
  const ampEnvelope = { attackMs, decayMs: 0, sustain: 100, releaseMs };
  const engine = defaultEngineParams(world);
  const rackWith = (active) => buildDefaultRack(world).map((u) => ({ ...u, bypassed: !active.includes(u.type) }));
  const presets = [
    { name: 'Default', tags: ['imported', world], fxRack: buildDefaultRack(world) },
    { name: 'Lush', tags: ['imported', 'texture'], fxRack: rackWith(['jmChorus', 'widener', 'plateReverb', 'hallShimmer']) },
    { name: 'Motion', tags: ['imported', 'texture'], fxRack: rackWith(['tremoloAutoPan', 'phaser', 'tapeDelay', 'digitalDelay', 'plateReverb']) },
  ];
  for (const p of presets)
    writeFileSync(join(bundleDir, 'presets', `${p.name}.json`), JSON.stringify({ formatVersion: 1, name: p.name, tags: p.tags, engine, fxRack: p.fxRack, ampEnvelope }, null, 2));

  if (opts.category) writeFileSync(join(bundleDir, 'sanctuary.json'), JSON.stringify({ category: opts.category, tags: [] }, null, 2));

  const v = validateBundle(bundleDir);
  const errors = v.issues.filter((i) => i.level === 'error').map((i) => `${i.path}: ${i.message}`);
  warnings.push(...v.issues.filter((i) => i.level === 'warning').map((i) => `${i.path}: ${i.message}`));
  return { path: bundleDir, zones: zones.length, samples, bytes, rate, channels: nch, layers: velocityMap.length, sustaining, warnings, errors };
}
