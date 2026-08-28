/**
 * Photoreal hardware-kit asset registry (docs/hardware-kit → src/ui/assets/kit).
 *
 * The kit ships RGBA PNGs that are directly usable in a web app: <img> /
 * background-image for plates, CSS transform: rotate() for knobs. Vite resolves
 * each `?url` import to a served asset URL, so nothing is decoded by hand and
 * nothing outside the app is referenced.
 *
 * Vendored per the two defects JM Signature logged:
 *  - `knob-small-00.png` ships clipped; ours is recropped from the kit's own
 *    `controls-sheet.png` (67,685,217x220).
 *  - `panel-master-empty.png` is 3.11:1, not 4:1 — it is nine-sliced, never
 *    scaled.
 */

import knobLarge from './kit/knob-large-00.png?url';
import knobSmall from './kit/knob-small-00.png?url';
import panelTeal from './kit/panel-teal.png?url';
import panelOrange from './kit/panel-orange.png?url';
import panelPurple from './kit/panel-purple.png?url';
import panelBlue from './kit/panel-blue.png?url';
import panelRed from './kit/panel-red.png?url';
import panelGreen from './kit/panel-green.png?url';
import panelMaster from './kit/panel-master-empty.png?url';
import buttonUp from './kit/button-empty-up.png?url';
import buttonDown from './kit/button-empty-down.png?url';
import meterLeft from './kit/meter-led-left.png?url';
import meterRight from './kit/meter-led-right.png?url';
import screw from './kit/screw-00.png?url';
import power from './kit/icon-power.png?url';
import rackCorner from './kit/rack-corner.png?url';
import dividerGold from './kit/divider-gold.png?url';
import flourishGold from './kit/flourish-gold.png?url';

import markFlow from './marks/engraved-mark-flow.svg?url';
import markKnot from './marks/engraved-mark-knot.svg?url';
import markOrbit from './marks/engraved-mark-orbit.svg?url';
import markPetal from './marks/engraved-mark-petal.svg?url';
import markWeave from './marks/engraved-mark-weave.svg?url';
import microWeave from './marks/kente-micro-weave.svg?url';
import shield from './marks/jm-shield-reference.svg?url';

import type { Motif } from '../theme/tokens';

export const KIT = {
  knobLarge,
  knobSmall,
  buttonUp,
  buttonDown,
  panelMaster,
  meterLeft,
  meterRight,
  screw,
  power,
  rackCorner,
  dividerGold,
  flourishGold,
  microWeave,
  shield,
} as const;

export const FX_PLATE: Record<string, string> = {
  teal: panelTeal,
  orange: panelOrange,
  purple: panelPurple,
  blue: panelBlue,
  red: panelRed,
  green: panelGreen,
};

/** JM engraved motif family — never described as Adinkra. */
export const MOTIF: Record<Motif, string> = {
  flow: markFlow,
  knot: markKnot,
  orbit: markOrbit,
  petal: markPetal,
  weave: markWeave,
};
