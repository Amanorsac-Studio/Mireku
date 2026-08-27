/** Client per-world pattern geometry (vendored SVGs), imported raw for the
 * darkening pipeline in theme/weave.ts. */

import type { World } from '@jm/jmi-format/web';
import rhodes from './pattern-rhodes.svg?raw';
import mks from './pattern-mks.svg?raw';
import fm from './pattern-fm.svg?raw';
import strings from './pattern-strings.svg?raw';

export const WORLD_PATTERN_SVG: Record<World, string> = {
  'rhodes-ep': rhodes,
  mks,
  fm,
  strings,
};
