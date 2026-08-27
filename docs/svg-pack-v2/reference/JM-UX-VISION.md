# JM Signature Sound Library — Vision Brief for UX Design

Prepared by JM Studios / Nene · August 2026
Companion documents: interactive plan sheet (screens & behavior), JM-BUILD-SPEC.md (technical detail). This brief is about the *feel*. The other two are about the *function*.

---

## Who this is for

James Mireku is a keyboardist with a recognizable sound. His signature isn't one instrument — it's a way of treating sound: a warm, expressive core (a Rhodes, a lush Roland MKS, a glassy FM piano, a bed of strings) wrapped in movement, depth and atmosphere. Modulation, delay and reverb are not effects added at the end for him; they are half the instrument. Everything about this product exists to bottle that identity and put it under other musicians' hands.

We are designing two applications that share one brand and one soul:

1. **JM Capture** — the private tool. Only James uses it. It guides him through recording his instruments, note by note, and turns a tedious studio chore into something focused and almost game-like.
2. **JM Signature** — the public instrument. A VST plugin musicians will buy, open in their sessions, and perform with live. This is the product the brand lives or dies on.

## The brand world

Black lacquer and gold. The existing identity — a gold JM shield monogram on black, condensed uppercase type, boxed packaging with a gold-emblazoned USB drive — sets the register: **boutique, premium, physical**. The software should feel like the box looks. Think of a beautifully built piece of stage hardware: matte black panels, warm metallic gold hardware, soft lamp-like glows, engraved labels. Luxurious but *working* — this is gear, not jewelry. Dark UI throughout; gold is the accent and the light source, never the wallpaper.

Reference points the client responds to: **Keyscape** (each sound gets its own purpose-built face with personality) and **Omnisphere's FX page** (effects as a rack of hardware-styled units you'd swear you could touch). We are not copying either — we're aiming at that class of craft with the JM identity.

## The four sound worlds — four personalities, one family

The VST contains four instrument models. Each gets its **own interface with its own look and layout** — a musician should feel which world they're in before reading a single label. They must still be unmistakably one family (shared frame, type, knob language, black-and-gold DNA).

- **Rhodes / EPs** — warm, expressive, human. The vintage soul of the instrument. Feel: worn stage classic, amber warmth, tremolo shimmer, smoke and lamplight.
- **MKS** — lush, wide, chorus-heavy, rich. Feel: glowing analog luxury; depth you can sink into; the sound is immediately gorgeous and the panel should promise that.
- **FM** — clean, expressive, a vintage/modern hybrid. Feel: glassy, precise, slightly futuristic; crisp light on dark glass; the most "digital" face of the four, still warm.
- **Strings** — lush, emotional, cinematic. Feel: wide open space, slow movement, air; the panel that breathes.

## The two levels of the VST

**Model pages** are where a player shapes one world: a small set of big, curated, musical controls (five or six per world — named things like Bark, Tine, Space, Width, Shimmer, Swell), a patch browser carrying James's presets, and that world's FX rack. Curation is the design position: few controls that matter, beautifully presented — never a wall of parameters.

**The Perform page** is the stage view and the heart of the product. Four slots (A–D) hold the four worlds; a musician layers or switches them live. Key interactions the design must make instantly legible: the slot enable knobs with a global **SOLO / POLY** switch (solo: engaging one slot extinguishes the others, like radio buttons; poly: any combination stacks); four **macro knobs** that can be linked to reach any parameter inside any model page and performed from the top; the master FX rack; and a prominent **transpose** control — this instrument will be used in live worship and stage settings where transposing quickly matters. The Perform page should read at arm's length, on a dim stage, in seconds.

**The FX rack** (shared across everything) is a slot-based rack of twelve hardware-styled units — chorus, tremolo, phaser, tape delay, shimmer reverb and friends. Every parameter is a physical knob or switch on the unit's face. This is a place to give personality: each unit can look like a distinct piece of gear from the same imaginary JM product line.

## JM Capture — the guided session

The sampler's design brief in one line: **make James want to finish the session.** He must record thirty notes, at four dynamics, three times each, per instrument — the design's job is focus, encouragement and momentum.

The centerpiece is the **guided meter screen**: one large target note, the current dynamic layer, a meter with a gold target zone, and a needle that answers his playing instantly. In the zone — an affirming acceptance, a round-robin dot fills, it advances. Outside — a calm "harder" or "softer" and an instant retake. No menus, no transport buttons; the app is always listening. Surrounding it: a progress map of the whole session (satisfying to watch fill), and an Auto Mode where the app plays the instrument itself and the same screen becomes a graceful progress display. After capture, a playback view with the FX rack for shaping and simple preset lanes for saving variations.

Tone here matters: the app speaks to one person, by name if we like, briefly and warmly. It's a studio engineer who respects his time, not a wizard with steps.

## Principles to hold the whole thing to

**Play first.** Sound responds before anything is read or configured. Every screen should invite touching a knob within the first second.
**Curated over complete.** When in doubt, fewer, bigger, better-named controls. The depth lives behind the immediacy.
**Physical.** Knobs turn like they have weight; switches click; glow responds. Motion is craft — used where a musician's eye needs to go, never decoration.
**Legible on stage.** Dark venues, arm's length, seconds to act. Especially the Perform page and transpose.
**One family.** A player moving between the four worlds, the rack, and the Perform page should never doubt they're inside one instrument.

## Practical notes

Both apps are dark-themed desktop applications with resizable windows. The engineering build implements all interaction and screen structure with a placeholder skin, fully tokenized — the UX deliverable restyles and refines rather than restructures. Deliverables that plug in best: a visual language (the panel/knob/type/glow system and how the four worlds diverge within it), full designs for the Perform page, one model page (Rhodes as the template), one FX unit as the rack pattern, and the Capture meter screen. Screen inventories, exact control lists and parameter ranges are all in the build spec.
