# Live worship keys rig — competitive research

Research memo only. Product names appear here for reference; the build document
([JM-STAGE-BUILD-SPEC.md](JM-STAGE-BUILD-SPEC.md)) is written in our own vocabulary and
names no competitor. Researched 2026-09-01.

## 1. The product category

Every popular worship rig is the same shape: a **host** (MainStage / Ableton / a standalone
app) plus a **template** that pre-wires layered patches, a drone-pad player, transposition,
tap tempo and a hardware-controller map. The market leader is Sunday Keys (Sunday Sounds);
its direct peers are Worship Keys (Worship Start), KeyStudio (Worship Team Resources) and
the Pads Player patches (Worship Tutorials). The hardware reference point is the Nord
Stage, prized for **seamless sound changes with no cutoff**.

## 2. What the leader does well (and why users buy it)

| Feature | Detail | Source |
|---|---|---|
| Layered patches | Patch = stack of sounds (pianos, pads, strings, synths, bells, organs, EPs); example patch has 7 layers; splits/keyzones; synth-bass layers | [App guide 2026](https://sundaysounds.com/blogs/news/the-ultimate-sunday-keys-app-guide-for-2026), [1.4 release](https://sundaysounds.com/blogs/news/new-sunday-keys-mainstage-worship-template) |
| Drone pad player ("Tonic") | 12 key buttons, auto fade in/out, crossfade on key change, 8 pad presets, XY pad (up = shimmer, right = brightness), drone center octave, minor voicings, separate output routing, controllable by MIDI/TouchOSC, **automated per patch and per snapshot** | [Tonic article](https://support.sundaysounds.com/article/3298-sunday-keys-for-mainstage-2021-using-the-tonic-pad-player), [Tonic update](https://sundaysounds.com/blogs/news/superpower-your-ambient-worship-pads-tonic-pad-in-sunday-keys-update) |
| Snapshots | Song sections (Down / Medium / Big / Outro) inside one patch; each stores volumes, FX, mod-wheel position, pad key/brightness/shimmer/on-off; transition duration in bars (e.g. 1 bar) | [Transitions](https://sundaysounds.com/blogs/news/how-to-create-seamless-worship-transitions-using-the-sunday-keys-app) |
| Patch-to-patch transition | Hold last chord, press Next; new patch's pad starts in the new key ("Patch Lock") | same |
| Transposition | "Play In" and "Hear In" keys chosen independently, lockable per patch | [App guide 2026](https://sundaysounds.com/blogs/news/the-ultimate-sunday-keys-app-guide-for-2026) |
| Setlists | Per service type; shareable by link; cloud backup | same |
| Browsing | Library filterable by genre, use-case, included sounds, tempo-synced, complexity (Simple/Moderate/Complex) | same |
| Notes | Click patch name to read per-patch performance notes | [1.4 release](https://sundaysounds.com/blogs/news/new-sunday-keys-mainstage-worship-template) |
| AI patch builder | Text prompt → layered patch, iterate | [App Store](https://apps.apple.com/us/app/sunday-keys/id1615360535) |
| FX | Stereo delay, two reverbs, shimmer reverb, chorus, tremolo, rotary, overdrive, tone, comp, EQ; up to 5 per sound; MIDI FX: arp, easy-chord, sequencer | same |
| Hardware | Full MIDI learn, Bluetooth/network MIDI, multi-output flex routing, tap tempo | same |
| Sync | Track-player section changes can auto-advance patches/snapshots | [Transitions](https://sundaysounds.com/blogs/news/how-to-create-seamless-worship-transitions-using-the-sunday-keys-app) |
| Onboarding | "One go-to patch for an entire setlist" philosophy; heavy tutorial library; very responsive support | [Setlist post](https://sundaysounds.com/blogs/news/how-to-build-a-setlist-in-mainstage-using-sunday-keys-part-1-one-go-to-patch), [Review](https://www.nextlevelworship.com/product-review-sunday-keys/) |

## 3. Where users say it falls short

- **Controller mapping is shallow**: you can only map what is visible on the main screen;
  players want tremolo rate, envelope attack, delay subdivision, chorus depth on knobs.
  ([App Store reviews](https://apps.apple.com/us/app/sunday-keys/id1615360535))
- **No parameter tweaks mid-performance** without opening dialogs.
- **Pitch bend ignored** on some controllers/sounds.
- **No third-party FX**; third-party instruments require an active yearly licence.
- **Generic built-in pianos** compared with owned libraries.
- **Mac / iPad only** for the app; PC users must run the Ableton template. Windows is
  under-served across the whole category (only Cantabile/Gig Performer hosts).
- Pad players in peer products are **audio files, not instruments**, so they can't follow
  chords or respond to playing. ([Pads Player](https://worshiptutorials.com/mainstage-pads-player-patches/))

## 4. Peer features worth matching

- Worship Keys: up to 6 layers, latch-pad player with filter, live arp player (12 presets),
  drum-loop player, TouchOSC layout, nanoKONTROL2 map, "smart transpose".
  ([source](https://www.worshipstart.com/worship-keys))
- KeyStudio: chord triggers, octave shift, track player showing song sections and beat
  count, metronome with custom click, tempo by tap/knob/typed. ([source](https://worshipteamresources.com/keystudio/))
- Hosts: Gig Performer and Camelot are bought for **stability** and setlist-driven flow;
  Nord for **no-cutoff sound changes**. ([hosts](https://keyboardkrush.com/mainstage-alternatives/), [Nord](https://johnsotomusic.com/why-is-nord-stage-4-so-good/))

## 5. Genre requirements we should own (gospel / Black church)

Praise-break and shout music need organ with glissando, stabs and trills, bright piano,
Rhodes that cuts, and fast switching between them; Nord is the church standard for this.
([Praise break](https://www.melodigging.com/genre/praise-break), [Gearspace](https://gearspace.com/board/so-much-gear-so-little-time/734003-keyboard-rig-modern-worship.html))
The leader is aimed at contemporary (Hillsong/Bethel-style) worship and is thin on organ
and gospel voicings. This is the open lane.

## 6. Conclusions that shaped our spec

1. Ship the **rig inside the plugin**: setlist, song, section, drone, transpose, tempo all
   native, on Windows and macOS, with no host template required.
2. **Never cut a sound off**: song/section changes keep old voices ringing (Nord behaviour).
3. **Drone is a real instrument**, not a WAV: it follows detected chords, has major/minor/
   sus modes, per-section automation, and its own output bus.
4. **Every parameter is mappable**, not just the front panel, and can be tweaked live
   without leaving the stage screen. Fixes the leader's biggest complaint.
5. Sounds come from **our own captured bundles** (the JM Capture pipeline), so pianos are
   not generic. Format stays open so users can import their own.
6. Aim the sound set at both contemporary worship and **gospel** (organ, shout, praise break).
7. Windows first-class, since the category ignores it.
