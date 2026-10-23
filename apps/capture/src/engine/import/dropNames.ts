/**
 * Filename parsing for the drag-and-drop importer (pure, Node-safe).
 *
 * Dropped files do not follow the `.jmi` convention
 * (`{Note}{Octave}_v{layer}_rr{n}.wav`). Real sample sets are named
 * `Piano_C3_mf.wav`, `EP-60-velo2.wav`, `Strings_A#2_v3_rr1.wav`,
 * `Rhodes-Db4-ff-2.wav`, or carry nothing useful at all. This module squeezes
 * what it can out of the name and — just as importantly — says exactly what it
 * matched and how sure it is, so the Import screen can show the user the
 * inference before anything is converted.
 *
 * It never guesses silently. Every field carries a `confidence`, every match
 * records the token it came from, and a name with no pitch in it returns
 * `note: null` so the caller falls back to the YIN pitch detector
 * (`dsp/pitch.ts`) rather than inventing a root note.
 *
 * Rules worth stating, because they are judgement calls:
 *
 *  • **Note names beat dynamics for letter+digit tokens.** `f3` is read as the
 *    note F3, not as forte followed by a 3; a bare `f` with no digits is read
 *    as forte. A filename that means both is ambiguous for a human too.
 *  • **The LAST note-name token wins.** Sample sets put the note after the
 *    instrument name (`Model-D5-C3-mf`), so the trailing candidate is the
 *    pitch and the leading one is part of the name. More than one candidate is
 *    reported as ambiguous.
 *  • **A bare integer is only a MIDI note when no note name was found**, and
 *    only in 12…120, and it is always marked low confidence — `EP-60` could as
 *    easily be a model number as middle C. Low-confidence pitches are
 *    corroborated against the detector, which is what settles it.
 *  • **`v`/`vel`/`velo` + a number ≤ 16 is a layer index; > 16 is a velocity
 *    value.** `v3` is layer 3, `v100` is velocity 100. The schema allows 16
 *    velocity layers, which is where the boundary comes from.
 */

export type Confidence = 'high' | 'low';

export type NoteSource = 'note-name' | 'midi-number' | 'pitch-detect' | 'manual' | 'none';

/** How a file's velocity position was expressed in its name. */
export type VelocityKind = 'layer' | 'velocity' | 'dynamic';

export interface VelocityParse {
  kind: VelocityKind;
  /**
   * Sort key, ascending = softer first. For `layer` it is the index, for
   * `velocity` the 1…127 value, for `dynamic` the rank in the dynamics table.
   */
  order: number;
  /** what to show the user, e.g. "mf", "layer 3", "velocity 100" */
  label: string;
  token: string;
  confidence: Confidence;
}

export interface NameParse {
  /** the file name as dropped, including extension */
  fileName: string;
  note: number | null;
  noteSource: NoteSource;
  noteToken: string | null;
  noteConfidence: Confidence;
  /** true when the name held more than one plausible note token */
  noteAmbiguous: boolean;
  velocity: VelocityParse | null;
  roundRobin: number | null;
  roundRobinConfidence: Confidence;
  /** tokens the parser consumed, in the order they appear */
  matched: string[];
  /** tokens it did not understand — usually the instrument name */
  leftover: string[];
}

/* ------------------------------------------------------------------ */
/* note names                                                          */
/* ------------------------------------------------------------------ */

/** Semitone of each letter within an octave (C = 0). */
const LETTER_SEMITONE: Record<string, number> = { c: 0, d: 2, e: 4, f: 5, g: 7, a: 9, b: 11 };

/**
 * `C4 = 60`, matching `midiToNoteName` in `@jm/jmi-format` and therefore the
 * rest of the app. Sets written to the Yamaha/Roland convention (`C3 = 60`)
 * come out an octave low; that is handled by the plan's semitone offset, which
 * the pitch detector corroborates, not by guessing here.
 */
const NOTE_RE = /^([A-Ga-g])([#sb♯♭]?)(-?\d{1,2})$/;

/** Parse one token as a note name. Returns null when it is not one. */
export function parseNoteToken(token: string): number | null {
  const m = NOTE_RE.exec(token);
  if (!m) return null;
  const semi = LETTER_SEMITONE[m[1].toLowerCase()];
  if (semi === undefined) return null;
  const acc = m[2] === '' ? 0 : m[2] === 'b' || m[2] === '♭' ? -1 : 1;
  const octave = parseInt(m[3], 10);
  if (octave < -1 || octave > 9) return null;
  const midi = semi + acc + (octave + 1) * 12;
  return midi >= 0 && midi <= 127 ? midi : null;
}

/* ------------------------------------------------------------------ */
/* dynamics                                                           */
/* ------------------------------------------------------------------ */

/** Classical dynamics plus the plain-English words sample sets use. */
const DYNAMICS: Record<string, number> = {
  pppp: 0,
  ppp: 1,
  pp: 2,
  p: 3,
  quiet: 3,
  soft: 3,
  mp: 4,
  mid: 5,
  med: 5,
  medium: 5,
  mf: 6,
  f: 7,
  hard: 7,
  loud: 7,
  forte: 7,
  ff: 8,
  vhard: 8,
  strong: 8,
  fff: 9,
  ffff: 10,
};

const LAYER_PREFIX = /^(v|vel|velo|vl|lyr|layer)(\d{1,3})$/;
/** last-resort layer prefix: `L2`. Ambiguous with a left-channel marker. */
const WEAK_LAYER_PREFIX = /^l(\d{1,2})$/;
const RR_PREFIX = /^(rr|rob|take|tk|var|round)(\d{1,3})$/;
const BARE_INT = /^(\d{1,3})$/;

/** The schema allows 16 velocity layers; above that a number is a velocity. */
export const MAX_LAYER_INDEX = 16;

/* ------------------------------------------------------------------ */
/* tokenising                                                         */
/* ------------------------------------------------------------------ */

/**
 * Split a file name into tokens. Generous about separators (space, `_`, `-`,
 * `.`, brackets) and about ordering, because every set does it differently.
 * `A#2` survives as one token; `velo2` is also kept whole and matched by the
 * layer-prefix pattern.
 */
export function tokenise(fileName: string): string[] {
  const stem = fileName.replace(/\.[A-Za-z0-9]{1,5}$/, '');
  return stem
    .split(/[\s_\-.,()[\]{}+]+/)
    .map((t) => t.trim())
    .filter((t) => t.length > 0);
}

/* ------------------------------------------------------------------ */
/* parse                                                             */
/* ------------------------------------------------------------------ */

/** Parse one dropped file name. Never throws. */
export function parseSampleName(fileName: string): NameParse {
  const tokens = tokenise(fileName);
  const matched: string[] = [];
  const leftover: string[] = [];
  const used = new Set<number>();

  // --- pitch -------------------------------------------------------
  const noteCandidates: { i: number; token: string; midi: number }[] = [];
  const bareInts: { i: number; token: string; value: number }[] = [];
  tokens.forEach((t, i) => {
    const midi = parseNoteToken(t);
    if (midi !== null) {
      noteCandidates.push({ i, token: t, midi });
      return;
    }
    const b = BARE_INT.exec(t);
    if (b) bareInts.push({ i, token: t, value: parseInt(b[1], 10) });
  });

  let note: number | null = null;
  let noteSource: NoteSource = 'none';
  let noteToken: string | null = null;
  let noteConfidence: Confidence = 'high';
  let noteAmbiguous = false;

  if (noteCandidates.length > 0) {
    const pick = noteCandidates[noteCandidates.length - 1]; // the trailing one is the pitch
    note = pick.midi;
    noteSource = 'note-name';
    noteToken = pick.token;
    noteAmbiguous = noteCandidates.length > 1;
    noteConfidence = noteAmbiguous ? 'low' : 'high';
    used.add(pick.i);
  } else {
    // No note name: a bare integer in the musically plausible range is a
    // candidate, but never a confident one.
    const plausible = bareInts.filter((b) => b.value >= 12 && b.value <= 120);
    if (plausible.length > 0) {
      const pick = plausible[plausible.length - 1];
      note = pick.value;
      noteSource = 'midi-number';
      noteToken = pick.token;
      noteConfidence = 'low';
      noteAmbiguous = plausible.length > 1;
      used.add(pick.i);
    }
  }

  // --- velocity ----------------------------------------------------
  let velocity: VelocityParse | null = null;
  let weakVelocity: { parse: VelocityParse; i: number } | null = null;
  for (let i = 0; i < tokens.length && !velocity; i++) {
    if (used.has(i)) continue;
    const t = tokens[i];
    const lower = t.toLowerCase();

    const lay = LAYER_PREFIX.exec(lower);
    if (lay) {
      const n = parseInt(lay[2], 10);
      if (n >= 1 && n <= MAX_LAYER_INDEX) {
        velocity = { kind: 'layer', order: n, label: `layer ${n}`, token: t, confidence: 'high' };
      } else if (n >= 1 && n <= 127) {
        velocity = { kind: 'velocity', order: n, label: `velocity ${n}`, token: t, confidence: 'high' };
      }
      if (velocity) used.add(i);
      continue;
    }

    const dyn = DYNAMICS[lower];
    if (dyn !== undefined) {
      velocity = { kind: 'dynamic', order: dyn, label: lower, token: t, confidence: 'high' };
      used.add(i);
      continue;
    }

    if (!weakVelocity) {
      const weak = WEAK_LAYER_PREFIX.exec(lower);
      if (weak) {
        const n = parseInt(weak[1], 10);
        if (n >= 1 && n <= MAX_LAYER_INDEX) {
          weakVelocity = {
            parse: { kind: 'layer', order: n, label: `layer ${n}`, token: t, confidence: 'low' },
            i,
          };
        }
      }
    }
  }
  if (!velocity && weakVelocity) {
    velocity = weakVelocity.parse;
    used.add(weakVelocity.i);
  }

  // --- round robin -------------------------------------------------
  let roundRobin: number | null = null;
  let roundRobinConfidence: Confidence = 'high';
  for (let i = 0; i < tokens.length && roundRobin === null; i++) {
    if (used.has(i)) continue;
    const rr = RR_PREFIX.exec(tokens[i].toLowerCase());
    if (rr) {
      const n = parseInt(rr[2], 10);
      if (n >= 1 && n <= 128) {
        roundRobin = n;
        used.add(i);
      }
    }
  }
  if (roundRobin === null) {
    // A small trailing integer nobody else claimed is the round robin:
    // "Rhodes-Db4-ff-2.wav".
    for (let i = tokens.length - 1; i >= 0; i--) {
      if (used.has(i)) continue;
      const b = BARE_INT.exec(tokens[i]);
      if (b) {
        const n = parseInt(b[1], 10);
        if (n >= 1 && n <= 32) {
          roundRobin = n;
          roundRobinConfidence = 'low';
          used.add(i);
        }
      }
      break; // only the last unclaimed token is considered
    }
  }

  tokens.forEach((t, i) => (used.has(i) ? matched.push(t) : leftover.push(t)));

  return {
    fileName,
    note,
    noteSource,
    noteToken,
    noteConfidence,
    noteAmbiguous,
    velocity,
    roundRobin,
    roundRobinConfidence,
    matched,
    leftover,
  };
}

/**
 * One line describing what the parser got out of a name, for the mapping
 * table. Deliberately explicit: the user is being asked to confirm it.
 */
export function describeParse(p: NameParse): string {
  const bits: string[] = [];
  if (p.noteSource === 'note-name') bits.push(`note from "${p.noteToken}"`);
  else if (p.noteSource === 'midi-number') bits.push(`MIDI number "${p.noteToken}" (unconfirmed)`);
  else bits.push('no pitch in the name');
  if (p.noteAmbiguous) bits.push('more than one note token');
  if (p.velocity) bits.push(`${p.velocity.label} from "${p.velocity.token}"`);
  else bits.push('no velocity in the name');
  if (p.roundRobin !== null) {
    bits.push(`round robin ${p.roundRobin}${p.roundRobinConfidence === 'low' ? ' (from a trailing number)' : ''}`);
  }
  return bits.join(' · ');
}

/**
 * Longest shared prefix of a set of file names, tidied — the instrument-name
 * guess when a loose set of files is dropped rather than a folder. Mirrors
 * `commonInstrumentName` in `x7lPlan.ts`, but over file names.
 */
export function commonNamePrefix(fileNames: string[]): string {
  if (fileNames.length === 0) return '';
  const stems = fileNames.map((f) => f.replace(/\.[A-Za-z0-9]{1,5}$/, ''));
  let n = 0;
  const first = stems[0];
  outer: for (; n < first.length; n++) {
    for (const other of stems) {
      if (other[n] !== first[n]) break outer;
    }
  }
  return first.slice(0, n).replace(/[\s\-_:.]+$/, '').trim();
}
