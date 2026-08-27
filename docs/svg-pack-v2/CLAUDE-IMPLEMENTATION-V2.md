# JM Signature — Claude Implementation V2

## Authority and non-negotiable instruction

Implement the supplied design. Do not reinterpret, redraw, restyle, redistribute, or “improve” it. The SVG files are production assets, not mood references. `tokens/design-tokens.json` and `specs/screen-layouts.json` are machine-readable geometry. `reference/JM-BUILD-SPEC.md` is the sole authority for behavior and DSP. When visual and functional documents differ, preserve the build-spec behavior and use this pack only for presentation.

Do not substitute CSS patterns for Kente, generic knobs for supplied hardware, emoji or library icons for engravings, or responsive auto-layout for fixed component geometry. Do not invent parameters, DSP units, status lights, tabs, meters, or features.

## Fixed viewport and resizing

The reference viewport is exactly **1000×720 (25:18)**. Render into one root coordinate system. Scale the whole root uniformly using `scale = min(hostWidth/1000, hostHeight/720)`. Center it. Use chassis-colored letterboxing. Never independently stretch X/Y, reflow controls, change gaps, or move items to fill spare space. Supported size is 750×540 through 2000×1440.

## Header

Header bounds: x=16, y=12, w=968, h=52. Brand occupies 230px at left. Navigation is x=270, y=18, w=462, h=40 with five fixed 92px cells. Master is x=760, y=18, w=208, h=40. It is part of the instrument chassis, not an OS/desktop toolbar. Do not add tall title bars or duplicate navigation.

## Model pages

Use the exact world backdrop at `assets/surfaces/{world}-backdrop-v2.svg`. It already contains the energetic Kente surround, vignette, and separate recessed control surface. Never put controls directly on an undimmed pattern.

Five-control pages use 112px knobs at centers: (340,284), (500,284), (660,284), (420,456), (580,456). This is the mandatory compact 3-over-2 composition. MKS has six real controls and therefore uses 96px knobs at (320,270), (500,270), (680,270), (320,445), (500,445), (680,445). Do not force MKS into five controls.

`DECAYING` is sample metadata, not a visible model-page control or light. It must not appear on model pages.

The center engravings are the **JM engraved motif family**. They are original geometric marks influenced by the project’s design language. Do not label them as authentic Adinkra and do not assign traditional names or meanings.

## Perform page

PERFORM is a full central page, never a sidebar. A/B occupy the top row and C/D the second row. Slot size is 390×132. Origins: A (94,116), B (512,116), C (94,264), D (512,264). Each slot exposes only the build-spec functions: enable, world/patch, volume, pan, and output/status.

Place SOLO/POLY at x=236,y=412,w=184,h=46. Place transpose at x=480,y=395,w=284,h=78. Preserve semitone ±12 and octave ±2. Four 116px macros are centered at x=230/410/590/770, y=594. Macro linkage behavior comes from the build spec.

## FX modules

Use the V2 colored faceplates. Their Kente microtexture is intentionally visible at opacity 0.18 with soft-light blend and 0.32 scale. Keep labels on an opaque local plaque or clear area; never lower label contrast to reveal more pattern. A rack supports up to six active units and only the twelve units and parameter sets in the build spec.

## Depth, lighting, and states

Gold illumination uses #FFD77A highlight, #FFB11B hot edge, and #D9A648 base. Active rings may glow; idle rings retain metal without bloom. Disabled controls: opacity .38, saturation .55, no glow. Hover: gold highlight opacity .18. Pressed: translate visually 1px down with inner shadow. Focus: 2px #FFD77A outline outside the hit area. Automation/MIDI motion changes value only; it does not recolor the control.

Z-order is exact: chassis → Kente backdrop → vignette → recessed surface → hardware shadows → hardware → labels/values → focus. Do not place Kente above text or controls.

## Failure modes observed in the prior implementation

1. Wrong aspect ratio and excess dead space: caused by free reflow instead of uniform root scaling.
2. Header read as a desktop toolbar: too tall, weakly integrated, and incorrectly spaced.
3. Pattern used as wallpaper behind controls: reduced legibility and erased the contained hardware panel.
4. Knobs were too small and too widely distributed: destroyed the hero-control hierarchy.
5. Weak/dull gold and flat shadows: removed the warm lamp-like hardware depth.
6. Incorrect arbitrary symbols: visually inconsistent and culturally overclaimed.
7. `DECAYING` appeared as an actionable status light: not required on visible model UI.
8. PERFORM became a narrow sidebar: contradicted its role as the stage view.
9. FX modules lost the Kente material: colored plates became generic flat cards.
10. Components were treated as inspiration: assets and geometry were redrawn or substituted.

## Acceptance checklist

- Pixel overlay at 1000×720 matches supplied screen SVGs within 2px.
- Root preserves 25:18 at all host sizes.
- Header height is 52px and content begins at y=74.
- Five-control model pages are exactly 3-over-2.
- No visible `DECAYING` label/light on model pages.
- PERFORM is central and uses the exact 2×2 grid.
- Kente is visible around the recessed model surface and subtly visible in FX faceplates.
- All SVG paths resolve from `manifest.json`.
- No DSP or controls exist outside `JM-BUILD-SPEC.md`.
