/**
 * naming.mjs — work out which pass a rendered file belongs to, and what the instrument is called.
 *
 * Two conventions are accepted, because a DAW's stem export names files its own way:
 *
 *   suffix style (what the render sheets ask for)     Warm Grand.wav
 *                                                     Warm Grand keyoff.wav
 *                                                     Warm Grand probe.wav
 *
 *   Studio One stem style (section - track)           01 Start - Warm Grand.wav
 *                                                     02 Notes Off - Warm Grand.wav
 *                                                     03 Velocity Probe - Warm Grand.wav
 *
 * Either way the instrument ends up called "Warm Grand".
 */
import { basename } from 'node:path';

export const RENDER_EXT = /\.(wav|flac|aiff?)$/i;

const PROBE = /velocity\s*probe|(^|[\s\-_])probe([\s\-_]|$)/i;
const KEYOFF = /notes?\s*off|key[\s\-_]?off/i;

/** @returns {{kind: 'attack'|'keyoff'|'probe', patch: string}} */
export function parseRenderName(file) {
  const base = basename(file).replace(RENDER_EXT, '').trim();
  const kind = PROBE.test(base) ? 'probe' : KEYOFF.test(base) ? 'keyoff' : 'attack';

  // "01 Start - Warm Grand" → the part after the section label is the instrument
  const sectioned = base.match(/^\s*\d*\s*[^-]*\s-\s(.+)$/);
  let patch = sectioned ? sectioned[1] : base;
  // suffix style: strip the trailing pass word
  patch = patch.replace(/[\s\-_]*(velocity\s*probe|probe|key[\s\-_]?off|notes?\s*off)\s*$/i, '');
  return { kind, patch: patch.trim() || base };
}
