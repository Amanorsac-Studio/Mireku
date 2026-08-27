# JM Signature — Design Reference Pack

**For Claude Code:** these images are the visual ground truth for the JM Signature Sound Library build (see JM-BUILD-SPEC.md). Read every image in this pack before building any UI. Where an image and written spec conflict on *look and feel*, `ui-reference/` wins; on *functionality and parameters*, the build spec wins.

## The design law

**Kente is material. Adinkra is engraving. Gold is hardware. Black is architecture. Light is feedback.**
Every module (model page, FX unit, Perform slot) wears its own Kente cloth backdrop — muted, dark, woven. Module colors come from cloth, never flat fills. Knobs are gold medallions with an engraved Adinkra symbol, a glowing value arc, and a numeric readout. Never: bright Kente wallpaper, literal symbol illustrations, neon, styling that hurts readability.

## brand/

- **01-jm-logo-packaging.jpg** — The JM shield monogram, wordmark ("JM SIGNATURE SOUND LIBRARY"), and boxed + gold USB packaging. Source for the logo placement (top-left of every window), the gold gradient treatment on type, and the overall premium register. Recreate the shield as vector; do not stretch this bitmap.

## kente/

Authentic reference cloths. Derive tileable, darkened texture assets from these — sampled or procedurally rebuilt at their pattern scale — always desaturated/darkened to sit behind gold UI (target: pattern legible at close look, ≤ ~35% perceived contrast against #0C0A07). Map them as follows:

- **01-kente-flat-graphic-black-red-green-gold.png** — Bold flat geometry (triangles, bands, checker). The most "designable" pattern DNA → Perform page slot strips, master section, section dividers.
- **02-kente-woven-ewe-cloth-amber-blue.png** — Real woven Ewe-style cloth, visible warp, patchwork irregularity → Rhodes/EP "Heritage Room" tapestry backdrop (warm rust/amber recolor). The texture quality (fiber, slight irregularity) is the point.
- **03-kente-ashanti-weave-gold-green.png** — Dense fine-grained Ashanti weave, zigzags and diamonds → MKS "Royal Analog" (recolored gold/burgundy); thinned-out diamond geometry also seeds FM "Gold Circuit" trace patterns.

Per-module cloth tints (from the approved UI reference): Rhodes = rust/amber · MKS = forest/gold · FM = indigo · Strings = violet · FX units each get their own (teal chorus, ember tape delay, violet phaser, indigo shimmer, crimson compressor, forest hall) · Master strip = royal gold/burgundy.

## adinkra/

- **01-adinkra-symbols-reference-chart.png** — Authentic Adinkra symbols (source: adinkrasymbols.org). Use as reference to draw clean vector (SVG) versions in gold stroke for knob-face engravings — one symbol per hero control, consistent stroke weight. Rules: engraved look (inner shadow / stroke, never a flat sticker); the value arc + numeric readout always carry the knob's position, the symbol is identity only. Final symbol-to-control assignment is a client decision (symbols carry meanings) — build the engraving system so symbols are swappable assets, and use placeholder assignments until James picks.

## ui-reference/  ← the look-and-feel authority

- **01-approved-ui-look-and-feel-rhodes-page.png** — CLIENT-APPROVED target for the VST's overall feel. Match this: top nav tabs (Rhodes/EP · MKS · FM · Strings · Perform), left preset browser (search, User/Factory/Favorites, list), center model panel on Kente tapestry with medallion knobs + readouts, sub-tabs (Main/Amp Env/EQ/Settings/Info), right-docked Perform panel (colored slots A–D with power/vol/pan/meters, Solo–Poly, transpose stepper, 4 macros with colored target dots), bottom FX rack of colored cloth units with bypass, Master FX column, top-right master volume + meters + limiter, bottom bar (Keyboard, Mod Matrix, LFO, Arp), status bar (version, license, CPU). Treat the layout and richness as the target; exact parameters per JM-BUILD-SPEC.md.

## concept-renders/

HTML/CSS concept renders built on the build's actual design tokens (see the mockup source in the project) — useful as a faithful-to-token intermediate between spec and the approved reference:

- **rhodes-model-page.png** — model page structure, medallion knobs, arcs, readouts, Decaying badge, output meter, sub-tabs
- **perform-page.png** — slots with cloth identities, power buttons, meters, Solo/Poly, transpose stepper, macros, master FX strip
- **fx-rack.png** — six cloth-backed FX machines, gold hardware, bypass, JM maker's mark
- **capture-guided-meter.png** — JM Capture guided screen: huge note, layer strip, gold target zone + needle, take dots, 30×4 session map

## kente/assets/ — client UX asset pack (delivered Aug 27)

First concrete UX deliverables from the client, normalized names (originals in Downloads):

- **Icons** `icon-*.svg` — gold stroke/fill 24px set: search, filter, star (favorite), heart, left, down, more (⋮), keyboard, cart. Retint via theme tokens, not the hardcoded #D9A648.
- **logo.svg** — vector JM shield + monogram with the official gold gradient (#F2D06B→#D9A648→#8C6222). Replaces bitmap logo usage.
- **knob-recipe.svg** — canonical knob rendering: radial dark body, gold rim gradient (#F2D06B→#D9A648 30%→#8C6222 70%→#4A3412), inner ridge, gold pointer + lit tip.
- **led-meter.svg** — meter zone colors: safe #274E27 · warning #D9A648 · peak #8B1A1A on near-black.
- **toggle.svg** — pill switch: black body, 2px gold border, gold thumb.
- **adinkra-symbol.svg** — first knob-engraving symbol (swappable; more to come from James).
- **pattern-{rhodes,mks,fm,strings}.svg** — per-world kente pattern geometry (rhodes royal weave + split diamonds · mks checker/diamond + zigzag columns · fm vertical stripes + offset blocks · strings radial anansi web). Ship at full brightness — ALWAYS render through the darkening clamp (≤35% contrast law) before use as backdrops.
- `04–09` reference images — bright cloth/backdrop references for the same worlds (pattern DNA only, never used raw).

## Core tokens

chassis #0C0A07 · deep panel #11100D · black #080807 · gold #D9A648 · highlight gold #F1C66A · dark gold #725322 · cream #EFE7D4 · muted #9B9383. Display type: condensed uppercase (Oswald-class); UI/body: clean geometric sans; values: tabular.
