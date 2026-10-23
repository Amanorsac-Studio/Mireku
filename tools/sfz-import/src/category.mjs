// category.mjs — keyboard-player browser categories for imported instruments.
// Written to <bundle>/sanctuary.json; Sanctuary's library browser groups by it.

export const CATEGORIES = [
  'Piano', 'Electric Piano', 'Organ', 'Synth', 'Synth Bass', 'Bass', 'Pads & Choir', 'Strings',
  'Brass', 'Woodwinds', 'Bells & Mallets', 'Guitar & Plucked', 'Percussion', 'World', 'Vocal', 'FX & Other',
];

// Order matters: specific families before generic words ("bass recorder" is a woodwind,
// "synth brass" is a synth, "contrabass" is a string, "claves" is not a clav).
const RULES = [
  ['Synth Bass', /synth ?bass|bass ?lead|lately bass|moog bass|sub ?bass/],
  ['Synth', /synth ?brass|synth ?lead|goblin|juno|tx81z(?!.*piano)/],
  ['Organ', /organ|drawbar|harmonium|hammond|leslie/],
  ['Woodwinds', /sax|flute|recorder|ocarina|clarinet|oboe|bassoon|whistle|piccolo|panpipe|pan flute|shakuhachi|bansuri|duduk|\breed|aerophone|\bpipe/],
  ['Bells & Mallets', /glass|\bbell|kalimba|marimba|vibra|xylo|glock|celest|chime|mallet|tubular|music box|mbira|gamelan|crotale|handpan|metallo|claves|woodblock/],
  ['Percussion', /drum|perc|membranophone|\bkit\b|\btom|snare|cymbal|shaker|tambourine|\bblock|clap|conga|bongo|djembe|cajon|timpani|gong|taiko|hihat|kick|idiophone/],
  ['Strings', /string|violin|viola|cello|contrabass|double bass|bowed|fiddle|erhu|nyckel|psaltery/],
  ['Bass', /\bbass\b/],
  ['Electric Piano', /electric piano|rhodes|wurli|fm ?piano|\bdx|e-?piano|\bep\d?\b|clavinet|clavisynth|\bclav\b|pianet|mks/],
  ['Piano', /piano|grand|upright|disklavier|ydp|salamander|harpsichord|clavichord|pedal (on|off)/],
  ['Pads & Choir', /\bpad|choir|aah|ooh|atmos|texture|drone/],
  ['Brass', /trumpet|trombone|french horn|\bhorn|tuba|cornet|flugel|euphonium|brass|sousa/],
  ['Guitar & Plucked', /guitar|harp\b|strumstick|banjo|ukulele|zither|koto|tranh|dulcimer|lute|mandolin|pluck|chordophone|sitar|oud|bouzouki|balalaika|kantele|lyre/],
  ['Vocal', /vocal|voice|\bvox|sing|\bchoral/],
  ['World', /accordion|concertina|bagpipe|didger|ethnic|world|folk|tabla|steel/],
  ['Synth', /synth|analog|\bsaw|square|\blead|\barp\b|sequence|wave/],
];

/** @param name bundle display name  @param hints extra text (source path, folder family) */
export function categorize(name, hints = '', world = null) {
  const text = `${name} ${hints}`.toLowerCase();
  for (const [cat, re] of RULES) if (re.test(text)) return cat;
  return { 'rhodes-ep': 'Electric Piano', mks: 'Synth', fm: 'Bells & Mallets', strings: 'Pads & Choir' }[world] ?? 'FX & Other';
}
