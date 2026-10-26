/**
 * The four instrument categories, and THE ONE PLACE their display names live.
 *
 * `World` (types.ts) is the *stable* identifier: `rhodes-ep | mks | fm |
 * strings` is baked into instrument.schema.json, the C++ reader
 * (packages/jmi-format/cpp/jmi_format.h), every saved preset and every bundle
 * the artist has already delivered. Those ids NEVER change.
 *
 * What the player sees is a separate, renameable thing. The client agreed four
 * names with the artist, and before this module they were retyped in six
 * places across two apps (MIREKU's nav strip, its `worldName()`, its empty
 * states, its re-categorise menu, and JM Capture's New Session wizard and drop
 * import), so a rename reached some of them and not the others. Now there is
 * exactly one table per language:
 *
 *   TypeScript  WORLD_DISPLAY_NAMES, below            <- source of truth
 *   C++         jm::worldName() in apps/signature/src/engine/FxParams.h
 *
 * A future rename is one edit here and one there. Nothing else hardcodes a
 * category name; if you are about to type "JM Pianos" anywhere, import it.
 */
import type { World } from './types.js';

/** Canonical order: matches the `World` enum, the nav strip, and slot pages. */
export const WORLDS: readonly World[] = ['rhodes-ep', 'mks', 'fm', 'strings'] as const;

/**
 * The agreed display names. Mixed case is deliberate: this is the name as it
 * is written in prose and in menus. Surfaces whose typography is all-caps
 * (MIREKU's nav plaque, its page titles) upper-case it themselves — see
 * `drawTabCell` in apps/signature/src/ui/Hardware.h, which already does.
 */
export const WORLD_DISPLAY_NAMES: Readonly<Record<World, string>> = {
  'rhodes-ep': 'JM Pianos',
  mks: 'MKS',
  fm: 'FM Keys',
  strings: 'Strings',
};

/**
 * Strings is the deliberate catch-all, per the client: anything that is not a
 * piano/Rhodes/EP, an MKS or FM — organs, bells, pads, choirs, brass — is
 * Strings. The re-categorise menu says so out loud, so the hint travels with
 * the name rather than being a `w === 3` special case in a popup menu.
 */
export const WORLD_CATCH_ALL: World = 'strings';

/** "(everything else)" for the catch-all category, '' for the others. */
export function worldCatchAllHint(world: World): string {
  return world === WORLD_CATCH_ALL ? '(everything else)' : '';
}

/** The display name for a world id. */
export function worldDisplayName(world: World): string {
  return WORLD_DISPLAY_NAMES[world] ?? world;
}

/** `{ id, label }` pairs in canonical order, for pickers and option lists. */
export function worldOptions(): { id: World; label: string }[] {
  return WORLDS.map((id) => ({ id, label: WORLD_DISPLAY_NAMES[id] }));
}
