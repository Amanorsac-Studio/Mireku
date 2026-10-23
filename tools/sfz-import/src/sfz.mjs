// sfz.mjs — SFZ text → flat region list with the opcodes the .jmi mapper needs.
import { readFileSync, existsSync } from 'node:fs';
import { dirname, resolve, join } from 'node:path';

const NOTE_IDX = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };
function noteValue(v) {
  if (v == null) return undefined;
  const s = String(v).trim();
  if (/^-?\d+$/.test(s)) return parseInt(s, 10);
  const m = /^([a-gA-G])(#|b)?(-?\d+)$/.exec(s);
  if (!m) return undefined;
  let n = NOTE_IDX[m[1].toLowerCase()] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0);
  return n + (parseInt(m[3], 10) + 1) * 12; // sfz: c4 = 60
}
const num = (v, d) => (v == null || v === '' || Number.isNaN(parseFloat(v)) ? d : parseFloat(v));

function loadText(file, seen = new Set()) {
  const abs = resolve(file);
  if (seen.has(abs)) return '';
  seen.add(abs);
  let text = readFileSync(abs, 'latin1');
  text = text.replace(/\/\/[^\n]*/g, '');
  // #define
  const defs = {};
  text = text.replace(/#define\s+(\$\w+)\s+(\S+)/g, (_, k, v) => { defs[k] = v; return ''; });
  for (const [k, v] of Object.entries(defs)) text = text.split(k).join(v);
  // #include
  text = text.replace(/#include\s+"([^"]+)"/g, (_, inc) => {
    const p = join(dirname(abs), inc.replace(/\\/g, '/'));
    return existsSync(p) ? loadText(p, seen) : '';
  });
  return text;
}

/**
 * Parse an SFZ file. Returns { regions, dir } where each region is the merged
 * opcode map (control < global < master < group < region) plus normalised fields.
 */
export function parseSfz(file, opts = {}) {
  const dir = dirname(resolve(file));
  const text = loadText(file);
  const re = /<(\w+)>|([\w$]+)=/g;
  const scopes = { control: {}, global: {}, master: {}, group: {}, region: null };
  const regions = [];
  let cur = null, curName = null;
  let m, pending = null;
  const flush = (endIdx) => {
    if (pending) {
      const val = text.slice(pending.from, endIdx).trim();
      scopes[pending.scope][pending.key] = val;
      pending = null;
    }
  };
  const closeRegion = () => {
    if (scopes.region) regions.push({ ...scopes.control, ...scopes.global, ...scopes.master, ...scopes.group, ...scopes.region });
    scopes.region = null;
  };
  while ((m = re.exec(text))) {
    flush(m.index);
    if (m[1]) {
      const h = m[1].toLowerCase();
      closeRegion();
      if (h === 'control') { curName = 'control'; }
      else if (h === 'global') { scopes.global = {}; scopes.master = {}; scopes.group = {}; curName = 'global'; }
      else if (h === 'master') { scopes.master = {}; scopes.group = {}; curName = 'master'; }
      else if (h === 'group') { scopes.group = {}; curName = 'group'; }
      else if (h === 'region') { scopes.region = {}; curName = 'region'; }
      else curName = null; // <curve>, <effect>, <midi>… ignored
    } else if (curName) {
      pending = { scope: curName, key: m[2].toLowerCase(), from: re.lastIndex };
    }
  }
  flush(text.length);
  closeRegion();

  const out = [];
  for (const r of regions) {
    if (!r.sample) continue;
    const trig = (r.trigger ?? 'attack').toLowerCase();
    if (trig !== 'attack' && !(opts.includeRelease && trig === 'release')) continue; // first / legato layers
    if (r.on_locc64 != null || r.on_hicc64 != null) continue; // pedal-down zones (reserved in v1)
    if (r.sw_last != null && (r.sw_default == null || noteValue(r.sw_last) !== noteValue(r.sw_default))) continue;
    const key = noteValue(r.key);
    let lokey = noteValue(r.lokey), hikey = noteValue(r.hikey);
    if (key != null) { lokey ??= key; hikey ??= key; }
    lokey ??= 0; hikey ??= 127;
    // SFZ spec: pitch_keycenter defaults to 60 (a `key=` opcode also sets it)
    let root = noteValue(r.pitch_keycenter) ?? key ?? 60;
    const lovel = Math.max(1, num(r.lovel, 1)), hivel = Math.min(127, num(r.hivel, 127));
    const defaultPath = (r.default_path ?? '').replace(/\\/g, '/');
    let samplePath = resolve(dir, defaultPath, r.sample.replace(/\\/g, '/'));
    if (!existsSync(samplePath)) {
      const alt = samplePath.replace(/\.wav$/i, '.flac').replace(/\.flac$/i, (x) => (samplePath.toLowerCase().endsWith('.flac') ? '.wav' : x));
      if (existsSync(alt)) samplePath = alt; else continue;
    }
    const loopMode = (r.loop_mode ?? '').toLowerCase();
    let loop = null, useFileLoop = false;
    if (loopMode === 'no_loop' || loopMode === 'one_shot') loop = null;
    else if (r.loop_start != null && r.loop_end != null) loop = { start: num(r.loop_start, 0), end: num(r.loop_end, 0) + 1 };
    else if (loopMode === 'loop_continuous' || loopMode === 'loop_sustain') useFileLoop = true;
    const seqKey = r.seq_position != null ? `seq${r.seq_position}` : r.lorand != null ? `rand${r.lorand}-${r.hirand}` : null;
    out.push({
      file: samplePath, root, lokey, hikey, lovel, hivel, loop, useFileLoop,
      tune: num(r.tune, 0) + num(r.transpose, 0) * 100,
      volumeDb: num(r.volume, 0),
      offset: Math.max(0, Math.round(num(r.offset, 0))),
      rrKey: seqKey,
      ampegAttack: r.ampeg_attack != null ? num(r.ampeg_attack, 0) : undefined,
      ampegRelease: r.ampeg_release != null ? num(r.ampeg_release, 0) : undefined,
      keytrack: num(r.pitch_keytrack, 100),
      trigger: trig,
      rtDecay: num(r.rt_decay, 0),
    });
  }
  return { regions: out, dir };
}
