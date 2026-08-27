# JM Signature SVG Implementation Pack

## Purpose
This package lets Claude/Codex implement the supplied UI direction without visually guessing or redrawing the Kente/material assets. The `*-exact.svg` files embed the exact uploaded Kente images inside SVG containers; the `*-panel.svg` variants add the darkening/lighting treatment needed behind controls.

## Visual source of truth
- Reference screenshot: `reference/ui-reference.png`
- Exact Kente sources: `reference/kente-*.jpeg|png`
- Do **not** invent replacement patterns or generic African motifs.
- Do **not** flatten interactive knobs into screenshots: use the supplied knob shells and rotate only the indicator/control group.

## Core palette
- Chassis `#0A0A09`
- Panel `#10100E`
- Gold `#D9A648`
- Bright gold `#F3C868`
- Cream `#EFE7D4`
- Rhodes accent `#D36B18`
- MKS accent `#A7D61A`
- FM accent `#2FA8FF`
- Strings accent `#C150D8`

## Mapping
| World | Backdrop | Perform slot | Main controls |
|---|---|---|---|
| Rhodes / EP | `kente-rhodes-panel.svg` | `perform-slot-rhodes.svg` | Bark, Tine, Body, Tremolo, Space |
| MKS | `kente-mks-panel.svg` | `perform-slot-mks.svg` | Chorus, Depth, Width, Ensemble, Warmth, Air |
| FM | `kente-fm-panel.svg` | `perform-slot-fm.svg` | Brightness, Attack, Glass, Chorus, Echo |
| Strings | `kente-strings-panel.svg` | `perform-slot-strings.svg` | Swell, Ensemble, Shimmer, Warmth, Motion |

## Layer order for a model panel
1. `#080807` chassis base.
2. Matching `kente-*-panel.svg`, covering the full module.
3. Optional `kente-micro-weave.svg` at 20–35% opacity.
4. Dark translucent control bed (`rgba(5,5,5,.30)` to `.48`).
5. Panel/frame stroke.
6. Knobs, labels, values, meters.
7. Hover/automation glow only.

## Knobs
### Primary
- File: `assets/controls/knob-primary.svg`
- Nominal diameter: 96 px at 1536×1024 reference design.
- Travel: -135° to +135°.
- Map normalized parameter `t` to `deg = -135 + t * 270`.
- The main shell stays fixed. Rotate only `#indicator` (or your application equivalent).
- Put one engraved mark in the 36×36 center area; do not rotate the engraving with value.

### Secondary
- 56 px. Same 270° travel.
- Use in FX rack, slot volume/pan, and secondary parameters.

### Macros
- Use primary knob shell plus `macro-ring-rhodes|mks|fm|strings.svg`.
- Ring is semantic color coding only; knob body remains black/gold.

## Perform slots
- 4 slots A–D.
- Recommended desktop reference size: 520×92 each.
- Backgrounds are in `assets/frames/perform-slot-*.svg`.
- OFF: render slot at 45% opacity, no meter illumination.
- ON: 100% opacity, 1.6 px world-colored edge, active meter.
- SELECTED: add 2 px gold inner focus line; do not change the world color.
- SOLO/POLY uses `toggle-solo-poly.svg`.
- Transpose uses `transpose.svg`; keep front-and-center and stage-legible.

## FX rack
Use the six supplied module faceplate colors cyclically or by effect personality, while keeping identical knob language. Faceplate SVGs: `assets/frames/fx-module-*.svg`. Do not use the world colors for FX; the rack should read as its own JM hardware family.

Suggested mapping from the reference UI:
- Chorus → teal
- Tape Delay / Tremolo → orange
- Phaser → purple
- Shimmer → blue
- Compressor → red
- Hall → green
- Widener → olive/gold (use green with 25% gold overlay)

## Preset browser
- Frame: `preset-browser-frame.svg`
- Active row: `preset-row-active.svg`
- Active text: gold. Inactive: cream/muted.
- Search/filters should stay functional UI elements, not decorative SVG text.

## Text
Do not convert labels to paths inside the application. Use live text for accessibility and scaling. Use a condensed uppercase display family close to the existing brand and a neutral sans for values/menus. If the project already has chosen fonts, retain them.

## JUCE implementation note
For JUCE, cache parsed SVG drawables; do not parse on every repaint. The exact Kente SVGs use embedded data URIs. If the SVG backend does not decode embedded images reliably, use the source raster files from `reference/` as `Image` assets and preserve the same crop/overlay described above. That is preferable to algorithmically recreating the pattern.

## Asset rule for Claude
**Use these assets as source-of-truth. Do not ask an image model to regenerate them. Do not redraw or approximate the Kente patterns.** The only runtime-transformed parts should be knob indicators, meters, toggles, glows, and other stateful UI.
