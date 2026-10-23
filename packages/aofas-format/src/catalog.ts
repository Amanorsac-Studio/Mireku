/**
 * The Amanorsac sound catalogue: what a library IS, and where it legally came
 * from. Both live in the AOFAS layer, never in @jm/jmi-format — that format
 * belongs to the JM Signature product and must not move because Amanorsac
 * needs a bigger vocabulary.
 *
 * Source of truth for the vocabulary: the Virtual Instrument Sound Library
 * Acquisition & Development Plan (§2 architecture table and the family lists
 * in §3–§7).
 */

/**
 * Top-level category. Deliberately coarse: it answers "which shelf does this
 * sit on" across every Amanorsac app. Everything finer — Worship Grand,
 * Neo-Soul EP, Glass Bell — is a free-form tag, because families multiply
 * faster than any closed list can track.
 */
export const CATEGORIES = [
  'grand-piano',
  'felt-piano',
  'upright-piano',
  'electric-piano',
  'strings',
  'pad',
  'shimmer',
  'synth',
  'bells-mallets',
  'other',
] as const;

export type Category = (typeof CATEGORIES)[number];

export function isCategory(v: unknown): v is Category {
  return typeof v === 'string' && (CATEGORIES as readonly string[]).includes(v);
}

/**
 * Fallback from the .jmi `world` when a bundle predates categories. A guess,
 * not a truth — the plan's taxonomy has no clean mapping onto the four JM
 * worlds (there is no piano world at all), so anything derived this way should
 * be corrected by hand.
 */
export function categoryFromWorld(world: string): Category {
  switch (world) {
    case 'rhodes-ep':
    case 'fm':
      return 'electric-piano';
    case 'strings':
      return 'strings';
    case 'mks':
      return 'synth';
    default:
      return 'other';
  }
}

/**
 * A licence, as it applies to shipped audio.
 *
 * `requiresAttribution` is the field that actually matters: CC BY sources
 * (Salamander Grand, Greg Sullivan's E-Pianos) oblige the product to carry a
 * credit, and CC0 sources (VCSL, VSCO 2 CE, FreePats) do not. The packer
 * refuses to seal a library that claims the former and supplies no credit
 * text, because an attribution that only exists in someone's notes is not
 * shipped.
 */
export interface LicenseInfo {
  /** SPDX id where one exists: 'CC0-1.0', 'CC-BY-3.0', 'CC-BY-4.0', 'proprietary'. */
  id: string;
  name?: string;
  url?: string;
  requiresAttribution: boolean;
  /** The exact credit line an app must be able to show a user. */
  attribution?: string;
}

/** One upstream library this instrument draws on. */
export interface SourceRef {
  /** e.g. "Salamander Grand Piano V3" */
  library: string;
  url?: string;
  license: LicenseInfo;
  /** ISO date the material was obtained — CC BY derivative notes want it. */
  retrieved?: string;
  /** What was changed. CC BY asks that modifications be indicated. */
  modifications?: string;
}

/** The licensing block carried in the PLAINTEXT card (see spec §5.2). */
export interface Licensing {
  /** Licence of the instrument as shipped, all sources considered. */
  license: LicenseInfo;
  /** Every upstream library that contributed audio. Empty = wholly original. */
  sources: SourceRef[];
}

/** Known-good starting sources from the plan (§12), for CLI presets. */
export const KNOWN_SOURCES: Record<string, SourceRef> = {
  vcsl: {
    library: 'Versilian Community Sample Library (VCSL)',
    url: 'https://versilian-studios.com/vcsl/',
    license: { id: 'CC0-1.0', name: 'CC0 1.0 Universal', requiresAttribution: false },
  },
  vsco2ce: {
    library: 'VSCO 2 Community Edition',
    url: 'https://versilian-studios.com/vsco-community/',
    license: { id: 'CC0-1.0', name: 'CC0 1.0 Universal', requiresAttribution: false },
  },
  salamander: {
    library: 'Salamander Grand Piano V3',
    url: 'https://github.com/sfzinstruments/SalamanderGrandPiano',
    license: {
      id: 'CC-BY-3.0',
      name: 'Creative Commons Attribution 3.0',
      url: 'https://creativecommons.org/licenses/by/3.0/',
      requiresAttribution: true,
      attribution: 'Salamander Grand Piano V3 by Alexander Holm, licensed CC BY 3.0.',
    },
  },
  gregsullivan: {
    library: "Greg Sullivan's E-Pianos",
    url: 'https://github.com/sfzinstruments/GregSullivan.E-Pianos',
    license: {
      id: 'CC-BY-3.0',
      name: 'Creative Commons Attribution 3.0',
      url: 'https://creativecommons.org/licenses/by/3.0/',
      requiresAttribution: true,
      attribution: "E-Piano samples by Greg Sullivan, licensed CC BY 3.0.",
    },
  },
  freepatsfm1: {
    library: 'FreePats FM Piano 1',
    url: 'https://github.com/freepats/fm-piano1',
    license: { id: 'CC0-1.0', name: 'CC0 1.0 Universal', requiresAttribution: false },
  },
};

/** Wholly-original Amanorsac material: no upstream obligation. */
export const AMANORSAC_ORIGINAL: Licensing = {
  license: {
    id: 'proprietary',
    name: 'Amanorsac Studio — all rights reserved',
    requiresAttribution: false,
  },
  sources: [],
};

/**
 * "We do not yet know where this came from."
 *
 * A capture made in Auto mode was played by SOME instrument, and that
 * instrument's licence travels with the recording — processing it does not
 * erase the obligation (plan §1). Marking such a library UNVERIFIED is honest;
 * silently stamping it `proprietary` is the laundering this module exists to
 * prevent. It seals and plays normally, so internal work continues, but the
 * plan's own rule applies: **if provenance is uncertain, it does not ship**.
 */
export const PROVENANCE_UNVERIFIED_ID = 'UNVERIFIED';

export const PROVENANCE_UNVERIFIED: Licensing = {
  license: {
    id: PROVENANCE_UNVERIFIED_ID,
    name: 'Provenance not established — internal use only, not cleared for release',
    requiresAttribution: false,
  },
  sources: [],
};

/** True when a library must not ship until someone establishes its source. */
export function isUnverified(l: Licensing | undefined): boolean {
  return l?.license.id === PROVENANCE_UNVERIFIED_ID;
}

/**
 * Validates a licensing block. Returns the problems; empty means it is
 * shippable. This is a mechanical check of internal consistency, NOT legal
 * advice, and it does not verify that a stated licence is the real one.
 */
export function licensingProblems(l: Licensing): string[] {
  const out: string[] = [];
  const check = (li: LicenseInfo, where: string) => {
    if (!li.id) out.push(`${where}: licence id is missing`);
    if (li.requiresAttribution && !li.attribution?.trim()) {
      out.push(`${where}: licence ${li.id} requires attribution, but no credit line was given`);
    }
  };
  check(l.license, 'instrument');
  l.sources.forEach((s, i) => {
    if (!s.library?.trim()) out.push(`source ${i + 1}: library name is missing`);
    check(s.license, `source "${s.library || i + 1}"`);
  });
  // A library built on an attribution source cannot itself claim to need none:
  // the obligation travels with the audio.
  const inherited = l.sources.filter((s) => s.license.requiresAttribution);
  if (inherited.length > 0 && !l.license.requiresAttribution) {
    out.push(
      `instrument licence says no attribution is required, but ${inherited.length} source(s) ` +
        `(${inherited.map((s) => s.library).join(', ')}) require it — the obligation travels with the audio`,
    );
  }
  return out;
}

/** Every credit line this instrument obliges an app to display. */
export function creditLines(l: Licensing): string[] {
  const lines: string[] = [];
  if (l.license.requiresAttribution && l.license.attribution) lines.push(l.license.attribution.trim());
  for (const s of l.sources) {
    if (s.license.requiresAttribution && s.license.attribution) {
      const mod = s.modifications ? ` Modified: ${s.modifications}` : '';
      const line = `${s.license.attribution.trim()}${mod}`;
      if (!lines.includes(line)) lines.push(line);
    }
  }
  return lines;
}
