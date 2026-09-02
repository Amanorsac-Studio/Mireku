# DECISIONS — JM Signature (apps/signature)

Judgment calls made while building the JUCE plugin, per JM-BUILD-SPEC.md §5.
Owner: JM Signature build agent.

## Client review round 8 — "sounds are not changing when I change sound" (2026-09-02)

Two reports, one fix each: a patch chosen on a model page was inaudible, and
placeholders could still be heard once a real library existed. No DSP, no
modulation, no `.jmi` format and no visual-design change; nothing was added to
the audio thread.

### Issue 1 — the confirmed root cause

`slotForWorld()` (`src/ui/Panels.h`) maps a model page to *the first slot hosting
that world*. With the default layout (A=Rhodes, B=MKS, C=FM, D=Strings) the MKS
page edits **slot B**, the FM page slot C, the STRINGS page slot D. But
`createLayout()` declares `slotEnable0` **true and the other three false** — only
slot A is enabled at first run (recorded under "Model pages ↔ Perform slots") —
and **a disabled slot never receives a note**: `handleRoutedMessage()` gates every
note-on on `slotEnable[i]` before it reaches an engine, so it is not merely muted,
it is never played. The patch really did load; the player really did hear nothing
new.

The client's library makes this the majority case: `EP2.jmi` is **rhodes-ep** and
both `MKS20.jmi` and `Piano.jmi` are **mks**, so *two of his three instruments*
live on the MKS page and every one of them was inaudible when chosen.

Measured, before the fix, through the real browser and the real processor
(`UiSelfCheck`, section `R8 (1)`; the fix was temporarily disabled to record it):

| page | slot | patch loaded? | voices | chosen-slot peak | other slots |
|---|---|---|---:|---:|---:|
| RHODES / EP | A | yes | 2 | **0.279173** | 0.000000 |
| MKS | B | yes | **0** | **0.000000** | 0.242646 |
| FM | C | yes | **0** | **0.000000** | 0.188495 |
| STRINGS | D | yes | **0** | **0.000000** | 0.272691 |

Note the third column: every state-level assertion about the load passes. That is
exactly why this survived seven review rounds — the only formulation that catches
it renders audio and asks *which slot is making it*. The last column is the
client's experience in one number: whatever he picked, he kept hearing slot A.

### The behaviour chosen — patch load versus slot enable

New `JMSignatureProcessor::makeSlotAudible (int slot)`, called from the
**audition paths only**: the preset browser's instrument / factory-preset /
user-preset rows (which exist only on model pages) and the model page's
prev/next patch stepper. It is **never** called from the Perform page.

| situation | what happens | why |
|---|---|---|
| **SOLO** | the chosen slot becomes the soloed one | the switch already means "exactly one slot sounds". Picking a sound in SOLO and not hearing it is indefensible. |
| **POLY, ≤ 1 slot enabled** | the enable **moves** to the chosen slot | this is the single-instrument setup the plugin ships in and the client's setup. There is no arrangement to protect, so he hears the sound he picked **and only that sound** — not it layered under the Rhodes he did not ask for. |
| **POLY, ≥ 2 slots enabled** | the chosen slot is **added**; nothing is switched off | the player has deliberately built a layer. His arrangement survives, he gets his new sound, and the Perform power buttons stay the authority. |

**Why the test is the observable enable count and not a hidden "has arranged"
flag.** A stored flag would make identical-looking states behave differently, and
the player has no way to see which one he is in. The enable count is *readable
off the four power lamps*: one lamp lit means "this is my instrument, swap it";
two or more means "this is my rig, don't touch it". A rule the player can predict
by looking is worth more than a rule that is theoretically more precise.

That is also the answer to the Perform-page composition question. The distinction
between auditioning and arranging is drawn **by where the click happened**, not
guessed: the Perform slot cards' world and patch combos deliberately do *not*
call `makeSlotAudible`, because a card carries its own power button an inch away
and a player editing that page is arranging, not shopping. Loading a patch there
behaves exactly as it did before.

Mechanically: the target is enabled **before** the others are cleared, so the
exclusive case never passes through a moment of silence, and each write is a
proper `begin/endChangeGesture` pair so hosts record it as one edit. Slots that
are switched off keep rendering their ringing voices (the existing
`! enabled && activeVoices() == 0` guard), so nothing clicks.

### Issue 2 — placeholders, and stale bundle references in saved state

The scan itself was already right: `Library::rescan()` falls back to
`C:\JM VST\testdata` only when the configured folder yields **no** bundles, and
every `loadBundleIntoSlot` call site takes its `BundleInfo` from
`library.bundles()` / `bundlesForWorld()`. **One** route remained, and it is the
likely second half of the client's complaint: saved plugin state and performance
presets pin a bundle **by absolute path**, and `syncSlotToDsp()` loaded that path
straight off disk regardless of what the library now held. State written while his
library folder was empty pinned the testdata placeholders — and then resurrected
them for ever, in a session whose browser correctly listed only his own three
instruments. A sound he could hear but could not find in the list.

**Decision: the library is the authority, and the substitution is said out loud.**
A reference the library does not offer is no longer loaded. The first library
instrument of the slot's world stands in, the slot's stored path and name are
corrected so the next save is honest, and the bottom status bar carries the line
`"JM Placeholder MKS" is not in your library - loaded "MKS20"` (also written to
the log). If the library has nothing for that world the slot is left **genuinely
empty** with `"…" is not in your library - slot D is empty` — an empty slot the
player can see beats a ghost he cannot find. The notice is deliberately **not
persisted**: it describes this session's load, not the patch.

Silently loading it from disk was the wrong default because it makes the browser
lie. Silently dropping it would have been the other wrong default, because the
player would lose a sound with no explanation. Substituting *and saying so* is the
only option that leaves him able to act.

The `testdata` placeholders are untouched — they are the CI fixtures and the
documented dev fallback, and `UiSelfCheck` still runs against them via
`JM_LIBRARY_DIR`.

### The empty-world pages (FM and STRINGS, for this client)

- The browser listed **nothing, with no explanation** — an unlit black rectangle.
  It now draws why: *"No FM instruments in your library. Sample one with JM
  Capture, or copy a .jmi bundle into your library folder (gear icon → Settings)."*
  with distinct wording for an empty favourites tab, an empty user-preset tab, a
  search that matched nothing, and an empty performance list.
- The model page itself already said `NO BUNDLE - CLICK TO BROWSE`; unchanged.
- **Visiting such a page cannot silence the instrument.** There is nothing to
  load, so `makeSlotAudible` is never reached and no enable moves — asserted, by
  playing a note while sitting on the FM page with an fm-less library.

### New / changed

- `src/plugin/PluginProcessor.{h,cpp}` — `makeSlotAudible()`; the stale-reference
  rule in `syncSlotToDsp()`.
- `src/engine/Library.h` — `find()`, `firstForWorld()`, and
  `setLibraryDirTransient()` (self-check only: repoints the scan **without**
  writing `settings.json`).
- `src/engine/AppState.h` — `libraryNotice`, one line of plain truth, not persisted.
- `src/ui/Panels.h` — `makeSlotAudible` at the audition call sites; the row
  activation moved out of the private `ListBoxModel` callback into a public
  `activateRow()` (the same code path the mouse takes) so the self-check can
  click a patch instead of synthesising mouse events into a `ListBox`; the
  empty-list message.
- `src/ui/Perform.h` — the status-bar notice.
- `tests/SnapshotCheck.cpp` — four new sections; `CMakeLists.txt` gains
  `JM_LIBRARY_TESTDATA` for the snapshot target.

### The regression test that would have caught it

`UiSelfCheck` sections `R8 (1)`–`R8 (4)`, all through the real processor, the real
browser and real rendered audio:

1. **Every model page, in turn**: click its first instrument row, play a note,
   and assert the chosen slot has voices, that *its* peak is non-zero and that
   **every other slot's is zero** — then that the rendered mix differs from the
   previous page's. The per-slot peak matters: an earlier draft compared only the
   mix, and it passed on the broken build, because slot A was still sounding and
   round-robin cycling made consecutive renders differ anyway. Measuring the mix
   answers "did something change"; measuring per slot answers "did the thing I
   picked start playing", which is the actual question.
2. **The three arrangement cases** above — enable moves, enable adds, SOLO.
3. **The client's library shape, built as a fixture** so it is covered on any
   machine: two bundles in one world, one in another, two worlds with nothing.
   Both mks instruments are auditioned from the one page and render different
   audio; the fm page lists zero rows and explains itself; playing there stays
   audible.
4. **A stale reference** to a real testdata placeholder, both ways — substituted
   when the world has instruments, left empty when it does not, notice set in
   both cases.

### Build / test evidence

- Release build (VST3 + AU + Standalone + all console targets): **exit 0**. Two
  relinks hit **LNK1104** because the client's standalone was running; the
  "JM Signature" process (**PID 41440**, and once more after he relaunched) was
  terminated and the build completed. **The standalone was not launched.**
- `ctest -C Release`: **100 % passed, 5/5** — `EngineTests` 4.90 s, `UiSelfCheck`
  19.19 s, `NullTest` 2.11 s, `RateBufferMatrix` 5.10 s, `AudioRegression` 1.03 s.
  The audio-regression reference did **not** move: nothing in this round touches
  a sample the DSP produces.
- **Against the client's own library**
  (`JM_LIBRARY_DIR=C:\Users\amano\Documents\JM Signature\Library`): every `R8`
  assertion passes, including both mks instruments and the two empty worlds
  (`page 2: no instruments in the library`, likewise page 3). The four failures
  that remain in that run are the **pre-existing** round-5 ones — the Omni,
  device-routing and arp assertions need four populated slots, which is precisely
  why `JM_LIBRARY_DIR` was introduced to point `UiSelfCheck` at `testdata`. They
  are not regressions and are unchanged by this round.

## DSP standards pass — aliasing, DC, feedback margin, interpolation, rate independence (2026-08-31)

Raising the FX/voice DSP to `pedal-style-effect-plugin-development-guide.md`
§1.3 (aliasing), §2 (per-category notes) and §4.1 (rate/buffer independence).
DSP track only: `src/engine/FxDsp.h`, `Limiter.h`, `Sampler.h`, one new header
`src/engine/Antialias.h`, and one new test file `tests/DspStandards.inc`.
**No parameter id, range, default or registry index was touched**, so presets
stay portable with JM Capture. The modulation system, the sampler's
zone/RR/velocity logic and the UI are untouched.

### 1. Aliasing — the priority (guide §1.3)

Measurement method, and why it is the one the guide asks for: drive the stage
with a pure sine at **f0 = 0.302185 × fs (14 504.9 Hz at 48 kHz)**. Because
2·f0 > Nyquist, *every* harmonic the stage makes must fold — there is no
legitimate harmonic in the output except f0 itself — so "all energy that is not
the fundamental" is exactly the alias floor. f0 sits on an exact FFT bin;
Blackman-Harris window, ±8-bin fundamental exclusion, measurement floor ≈ -95 dB.

| stage | technique chosen | alias floor before | after | improvement |
|---|---|---:|---:|---:|
| EP Preamp / Drive (`FxDsp.h`) | **4× oversampling + ADAA tanh** | -7.68 dB | **-66.31 dB** | **+58.6 dB** |
| Sampler per-voice drive (Rhodes "Bark") | **ADAA tanh only** | -12.02 dB | **-24.49 dB** | **+12.5 dB** |
| Master limiter brick wall (`Limiter.h`) | **C1 soft ceiling** (see below) | -60.11 dB | **-75.40 dB** | **+15.3 dB** |
| LFO square shaper `tanh(30·sin)` | **nothing needed** — measured, not assumed | — | — | — |

**EP Preamp — why 4× *and* ADAA.** The unit is a single stereo instance, so the
factor is nearly free in context (the whole 6-slot rack still costs ~2 % of the
block budget). 2× was measured first and only reached **-26.5 dB**: at this probe
the second harmonic lands right on the half-band's transition edge, which is
exactly the case 4× exists for. Signal order is tilt + input gain at the host
rate (linear, no need to oversample) → 4× up → ADAA tanh → down → output gain →
DC blocker.

**Sampler drive — why ADAA and not oversampling.** This shaper runs on up to
**64 concurrent voices × 2 channels**. A 2× oversampler per voice would roughly
double the whole render cost for a control that is off by default. The guide
names ADAA as the trade for precisely this case ("CPU-constrained ... where you
can't afford heavy oversampling everywhere") and the nonlinearity is memoryless,
which is the condition it states for ADAA being appropriate. +12.5 dB for
×2.4 on a stage that is skipped entirely when Bark is 0.

**Two ADAA forms were tried; the failure is worth recording.** First-order ADAA
of `f(x)=x` is `(x+x[n-1])/2` — a two-tap average — so textbook ADAA low-passes
the *linear* part of a curve that never needed band-limiting. The obvious fix,
`y = x + ADAA(f(x) − x)`, measured better on the alias metric (-73.6 dB EP,
-33.6 dB sampler) **and is wrong**: in tanh's flat region the residual
antiderivative grows like −x²/2, so the difference quotient degenerates to
`y = 1 + (x−x1)/2`. That is a differentiator, not a saturator — it overshot
tanh's own bound by up to **3.6** at drive 100, i.e. the stage stopped
saturating, and the alias metric was flattered because the spurious term sat on
the fundamental. Clamping the overshoot simply reinstates a hard clipper.
Reverted to the textbook form; the half-sample low-pass is the honest price and
on a drive stage it reads as slightly softer top end, not as a defect.

**The limiter wall — ADAA tried and REJECTED on measurement.** The final ceiling
stage was a hard `juce::jlimit`: a discontinuous derivative, on the master bus,
downstream of everything. It is now `jm::SoftCeiling` — identity below 90 % of
the ceiling, a C1-continuous tanh bend above it, strictly asymptotic to the
ceiling. ADAA on top was implemented and measured and made things **worse**: at
the frequencies where aliasing matters the probe advances 109° of phase per
sample, so averaging over the sample period is a savage low-pass — peak output
fell to exactly the knee and the whole-limiter alias floor went from -66 dB to
**-4.8 dB**. The unbounded variant is not admissible for a brick wall. Plain
`SoftCeiling` delivers what the stage actually needs: **bit-exact below the
knee** (which is what keeps the plugin nulling against its own bypassed path),
C1 so its harmonics fall as 1/n³ instead of 1/n², bounded with no clamp, no
state, no latency.

Because a soft knee deliberately bends *more* of the waveform in exchange for
that energy being *low-order*, the near-Nyquist metric cannot separate the two
effects and reported parity. So the wall is measured a **second** way: a
1092.77 Hz probe on bin 373 of 16384 (373 is prime and does not divide 16384, so
no folded component can ever land on a harmonic bin) with the true harmonics
excluded. That isolates aliasing and nothing else: **+15.3 dB at the level the
wall actually sees** (the sub-0.5 dB residue the gain envelope leaves) and
**+5.4 dB even on an absurd +4 dB transient**. Peak output is still ≤ the
-0.3 dBFS ceiling at every level; every round-3 no-overshoot assertion passes.

**The LFO square shaper needs nothing, and that is a measurement not an
assumption.** `FxDsp.h`'s `tanh(30·sin(phase))` shapes the tremolo's amplitude
modulator. Its bandwidth scales with the LFO rate, not the sample rate: measured
at an exact-bin rate, the highest harmonic above -100 dB is **175 × the rate**,
so at the registry maximum of 12 Hz that is **2.1 kHz — 8.75 % of Nyquist**.
Nothing folds. Left alone.

### 2. DC blocking after clipping (guide §2, distortion row)

New `jm::DcBlocker`, one pole, **8 Hz**, cutoff derived from the host rate.

- **EP Preamp**: always, after the output gain. A 0.3 DC offset plus a 220 Hz
  tone through the drive now leaves **residual output DC = 0.000000**.
- **Sampler**: after `applyVoicing`, and **only while the Rhodes Bark drive is
  actually doing something** — the blocker is always *fed* (so switching it in
  cannot step the signal) but only takes over when `world == 0 && bark > 0.1`.
  This deliberately keeps every other patch's output bit-identical to before, so
  none of the round-5 residency RMS figures move.

Low end verified, not assumed: **30 Hz is within 0.29 dB of 220 Hz** through the
unit, and the blocker measures **-0.35 dB at 27.5 Hz (A0)** and -0.02 dB at
100 Hz.

### 3. Tape delay feedback margin (guide §2, delay row)

Feedback maps `p/100`, so the registry maximum of 90 was **exactly 0.90 with no
headroom**, while wow/flutter modulates the delay length *inside that same loop*
— and a shortening delay line compresses stored energy in time, which is a
transient gain. Nothing bounded it.

`jm::makeLoopGuard()` — a `SoftCeiling` at **+6 dBFS with its knee at 0 dBFS** —
now guards what is written into the line in the **tape delay, digital delay and
flanger**. Any sample at or below full scale passes **bit-exact** (asserted:
worst deviation from identity for |x| ≤ 1.0 is **0**), so no ordinary repeat is
coloured at all; the loop simply cannot store more than 2.0 whatever the
feedback, the modulation and the input conspire to do. It is a safety net, not a
tone control, which is why the knee is where it is.

Proved by the test the brief asked for — feedback 90, wow/flutter 100, tone
wide open, impulse then **30 seconds of silence**, at three sample rates:

| rate | peak | tail after 30 s |
|---:|---:|---:|
| 44 100 Hz | 0.6743 | **-232.5 dB** |
| 48 000 Hz | 0.7899 | **-233.8 dB** |
| 96 000 Hz | 0.5402 | **-234.7 dB** |

### 4. Fractional-delay interpolation (guide §2, modulation row)

It was **linear**, everywhere — `ModDelay` backs the chorus, flanger, tape
delay, digital delay, the widener's ensemble and the reverb pre-delay. A 2-tap
linear interpolator at a half-sample tap has magnitude `cos(pi·f/fs)`, and
because the LFO sweeps the fractional part continuously that loss **modulates**,
which is the dull/swishy artefact the guide is warning about.

Now **4-point 3rd-order Lagrange** (`jm::lagrange3`). Measured at a half-sample
tap:

| probe | linear | Lagrange-3 | recovered |
|---|---:|---:|---:|
| 6.0 kHz (0.125 fs) | -0.69 dB | -0.07 dB | **+0.61 dB** |
| 12.0 kHz (0.250 fs) | -3.01 dB | -1.07 dB | **+1.94 dB** |
| 19.2 kHz (0.400 fs) | -10.20 dB | -6.96 dB | **+3.24 dB** |

The cubic kernel needs a sample either side of the interval, so `read()` clamps
the requested delay to [2, size-3] — the +2 tap would otherwise reach past the
write head into audio that does not exist yet.

**This is an audible, intended tone change**, and it is the one thing in this
pass that a listener will notice: modulated taps and delay repeats keep their
top octave instead of losing it, and in the recirculating delays that HF now
survives each pass. See §7 below for what it did to the audio-regression
reference.

### 5. Sample-rate and buffer-size independence (guide §4.1)

**One real bug found.** `OctaveUpShifter` (the shimmer's +12 st grain shifter)
used its power-of-two *buffer size* as the grain window:
`nextPowerOfTwo(sr * 0.09)`. Rounding a 90 ms window up to a power of two gave
**92.88 ms at 44.1 kHz but 170.67 ms at 48 kHz** — the shimmer's grain rate very
nearly halved between two ordinary session rates, so the same preset had a
different character in a 44.1 kHz session than in a 48 kHz one. That is a bug,
not a feature, exactly as the brief says. The window is now an explicit frame
count derived from the host rate and the masked ring is sized around it:
**90.00 ms at 44.1 / 48 / 88.2 / 96 / 192 kHz.**

Everything else audited and measured clean across all five rates:

| invariant | 44.1 | 48 | 88.2 | 96 | 192 kHz |
|---|---:|---:|---:|---:|---:|
| tape delay echo (set 200 ms) | 200.02 | 200.02 | 200.01 | 200.01 | 200.01 ms |
| EP Preamp transfer curve (RMS) | ref | +0.02 % | +0.04 % | +0.04 % | +0.03 % |
| compressor attack (set 10 ms) | 0.816 | 0.833 | 0.828 | 0.833 | 0.833 ms |
| shimmer grain window | 90.00 | 90.00 | 90.00 | 90.00 | 90.00 ms |

**Buffer size:** all 12 FX units rendered through the identical input in
**32-sample blocks and in 2048-sample blocks**, compared sample for sample —
**worst delta 5.96e-08** across the whole registry, i.e. float rounding.

### 6. CPU and latency

Per-sample costs (median of a quiet run; the ratios are stable across runs, the
absolute numbers are not — this machine was building in parallel):

| stage | before | after | ratio |
|---|---:|---:|---:|
| EP Preamp, whole unit (4× + ADAA + DC block) | 9.5 ns | 83.1 ns | ×8.8 |
| Sampler drive shaper (ADAA tanh) | 7.0 ns | 16.5 ns | ×2.4 |
| ModDelay tap (linear → Lagrange-3) | 2.9 ns | 6.1 ns | ×2.1 |
| Master limiter (C1 soft ceiling) | — | 10.9 ns | ≈ unchanged |

In context, which is what matters:

- **Full 6-slot FX rack, 512 frames @ 48 kHz: median 0.16–0.21 ms against a
  10.67 ms budget — 1.5–2.0 %.** The EP Preamp's ×8.8 is a large multiple of a
  very small number.
- **64 voices across 4 slots: median 0.28 ms, 95th pct 1.08 ms, worst 2.39 ms**
  against the same 10.67 ms budget, i.e. the round-5 polyphony harness still
  passes with the same headroom. The sampler shaper's worst-case arithmetic —
  64 voices × 2 ch × 512 frames — is **+0.6 ms per block**, and it is only paid
  on world 0 with Bark above zero.

**Latency: unchanged, deliberately.** The plugin still reports exactly the
limiter's **96 samples (2.00 ms at 48 kHz)** through `setLatencySamples()`, and
the round-6 assertion that the reported number equals the measured delay through
the bypassed limiter still passes. The EP Preamp's oversampler is a
**minimum-phase** polyphase-IIR half-band whose group delay is **4.43 samples
(0.09 ms at 48 kHz)**, and ADAA adds half a sample. Those are *not* reported,
and that is the judgment call: the FX rack is user-reorderable, so a per-slot
oversampler would make the plugin's reported latency a function of which units
happen to be in the rack — precisely the runtime latency change round 6 recorded
a decision against. A linear-phase FIR at this attenuation would have cost tens
of samples and forced the issue; the minimum-phase IIR keeps it at a twentieth
of the limiter's own lookahead. The same constraint is why the sampler's
64-voice drive gets ADAA (half a sample) rather than an oversampler.

### 7. Tone: what changed and what did not

- **The drive still sounds like the drive.** Same patch (drive 78, tone 20,
  output -3) through the naive stage and the new one: broadband level
  **-0.83 dB on white noise and +0.00 dB on a 1 kHz tone**, with per-band deltas
  of -0.4 to -1.0 dB. The curve is the same curve; what left is the fizz.
- **What a listener *will* hear is the interpolation** (§4): modulated taps and
  delay repeats keep their top octave. On white noise through the validation
  patch that is **+7.7 dB in the top band** and, because white noise is
  dominated by its top octave, **+3.6 dB broadband**; on a 20 Hz–18 kHz sweep it
  shows up only in the last fifth of the sweep (+0.5 … +4 dB) and on an impulse
  only in the first few milliseconds. That distribution is the signature of a
  high-frequency fix, and it is the intended one.
- **Limiter**: bit-exact below -1.2 dB of the ceiling, so anything that was not
  being limited is untouched; when it does engage the wall is smoother.
- **Delay/flanger loops**: bit-exact below 0 dBFS. No repeat is coloured.
- **DC blocker**: -0.35 dB at 27.5 Hz, and only in the drive path.

### 8. Verification

- Release build (VST3 + AU + Standalone + all console targets): **exit 0**.
  One relink hit **LNK1104** because the client's standalone was running; the
  "JM Signature" process (**PID 47440**) was terminated and the build completed.
  The standalone was **not launched**.
- `JMSignatureTests` (`EngineTests`): **ALL TESTS PASSED, 0 failures** —
  every pre-existing assertion including the round-3 limiter suite, the round-5
  residency measurements and the round-5 polyphony budget, plus the new
  assertions in `tests/DspStandards.inc`.
- `ctest -C Release`: **100 % passed, 5/5** — `EngineTests`, `UiSelfCheck`,
  `NullTest`, `RateBufferMatrix`, `AudioRegression`.

  Worth recording because it is the interesting one: the validation track's new
  **`AudioRegression`** test failed against its first reference, by design. The
  drift was the §4 interpolation change **and nothing else** — verified by
  isolating each changed stage: the EP Preamp contributes **-0.83 dB** broadband
  (i.e. nothing), while the top band moves **+7.7 dB**, and on the sweep the
  drift appears only in the last fifth and on the impulse only in the first few
  milliseconds. That distribution is the fingerprint of a high-frequency fix,
  not of a level or gain-staging mistake, which is what justified treating it as
  an intended tone change rather than a regression. The reference was then
  re-baselined deliberately via the test's own documented mechanism
  (`JMSignatureValidation --regression --rebaseline`) and
  `tests/refs/audio-regression.json` now pins the post-pass tone.
  **That reference file belongs to the validation track**; this entry is the
  "commit the change that caused it" half of the contract the test prints.

### New file

`src/engine/Antialias.h` — `logCosh`, `AdaaTanh`, `SoftCeiling`, `DcBlocker`,
`lagrange3`. Header-only, allocation-free, audio-thread safe; every rejected
alternative above is written down next to the thing it was rejected for.

## Client review round 6 — ASIO / device types, residual latency, oversized window (2026-08-31)

Three client reports, all in the **standalone** on Windows with a Behringer UMC 404HD.
This machine has the same interface attached, so every number below is measured here
rather than reasoned about.

### Issue 1 — "can we have ASIO support so it can detect audio interface drivers?"

**What was actually wrong.** The brief's premise — that `CMakeLists.txt` has no `JUCE_ASIO`
flag and therefore JUCE compiles "only WASAPI (shared) and DirectSound" — is half right.
There was indeed no `JUCE_ASIO`, so ASIO was absent. But WASAPI **exclusive** mode was
never missing: `AudioDeviceManager::createAudioDeviceTypes()` in JUCE 8 creates
`WASAPIDeviceMode::shared`, `::exclusive` **and** `::sharedLowLatency` whenever
`JUCE_WASAPI` is on, and it defaults to on. A new `tests/DeviceCheck.cpp` target
(`JMSignatureDevices`, deliberately not a CTest — its output depends on attached
hardware) was written to settle this by enumeration instead of by argument.

**Device types before the change** (4):

| type | UMC 404HD present? | buffer sizes offered @48 kHz |
|---|---|---|
| Windows Audio | yes, `OUT 1-2 (4- BEHRINGER UMC 404HD 192k)` — Windows' default | **480 only** |
| Windows Audio (Exclusive Mode) | yes | 144 … 2048 |
| Windows Audio (Low Latency Mode) | yes | 480 only |
| DirectSound | yes | 64 … 6912, **default 2560** |

**Device types after** (5) — ASIO added, everything else unchanged:

| type | devices |
|---|---|
| ASIO | **`UMC ASIO Driver`** (8 · 16 · 32 · 64 · 128 · 256 · 512 · 1024 · 2048 samples, up to 192 kHz), plus `ASIO4ALL v2`, `Yamaha Steinberg USB ASIO`, `Waves SoundGrid ASIO`, `Focusrite USB/Thunderbolt ASIO`, `Ableton Push`, `Ableton Move` |

So the client's interface was visible under WASAPI all along — under the endpoint name
`OUT 1-2 (4- BEHRINGER UMC 404HD 192k)` rather than a name containing "UMC 404HD" as the
first word. What he could not find was a *driver* he recognised, and DirectSound was where
he stopped looking. That is a naming problem masquerading as a support problem — and it
cost him 53 ms, because **DirectSound's default buffer here is 2560 samples**.

**(a) WASAPI exclusive — the no-SDK win.** The type existed but was never *chosen*.
`JMStandaloneWindow::chooseLowLatencyDefaults()` now switches, **on first launch only**
(detected as "the holder has no saved `audioSetup`"), to `Windows Audio (Exclusive Mode)`
at a 256-sample buffer, validated by checking that a device actually opened and unwound to
the previous type if exclusive mode was refused (another app can hold the endpoint). The
compile flags are now stated explicitly in `CMakeLists.txt` so a future JUCE default cannot
silently drop the exclusive type. Effect for the client: 480 fixed samples (10 ms, *not
adjustable* on this endpoint) → 256 samples (5.33 ms, adjustable down to 144).

**(b) ASIO — and the SDK question resolves itself.** `JUCE 8.0.15 ships the ASIO interface
headers` (`modules/juce_audio_devices/native/asio/{asio,asiosys,iasiodrv}.h`), so
`JUCE_ASIO=1` needs **no download and adds nothing to this repository**. `JM_ASIO` (default
ON) enables it and gates on those headers actually being present, so an unbundled JUCE
degrades to a clear configure-time message rather than a compile error. `JM_ASIO_SDK_DIR`
is still provided for pointing at a locally installed SDK, which then sets
`JUCE_ASIO_USE_EXTERNAL_SDK=1` and adds the include path; a wrong path warns and falls back
rather than failing the build. Configure-time output names the path taken. Steinberg's
licence still governs the *binary*, which `README.md` says.

There is also a real Steinberg **ASIO SDK 2.3 at `C:\SDKs\ASIOSDK`** on this machine
(found by searching for `iasiodrv.h`); the external-SDK path is what `-DJM_ASIO_SDK_DIR=
C:/SDKs/ASIOSDK/common` is for. It was *not* needed, and nothing from it was copied.

**Judgment call: ASIO is compiled in but NOT auto-selected.** The ASIO type's default
driver on a real machine is whatever registered first — here `ASIO4ALL v2`, a generic WDM
wrapper — and five of the eight registered drivers report no sample rates at all because
their hardware is not attached. Silently opening one of those at startup can fail, hang, or
seize hardware the player did not ask for. ASIO is in the gear dialog's type list to be
chosen deliberately; the automatic default is the one that cannot misfire.

### Issue 2 — "there's still delay… check what else could be causing it"

Measured in `tests/SnapshotCheck.cpp` (`== R6 (2) latency budget (measured) ==`).

| stage | cost | irreducible? |
|---|---|---|
| output buffer (device) | **1 × buffer** | no — this is the whole game (see below) |
| routed-MIDI queue → next block | 0 … 1 buffer, **0.5 buffer mean** | structural; bounded by the buffer |
| master limiter lookahead | **96 samples = 2.00 ms** at 48 kHz, measured by impulse | **yes** |
| host MIDI timestamps (plugin builds) | 0 | sample-accurate since Modulation Phase 2 |
| amp-envelope attack | 2 ms (10 ms on the sustaining worlds) | it is the *sound*, not delay — onset is immediate |
| modulation control rate | 64 samples = 1.33 ms, and only for modulation | negligible |
| learned-CC → parameter (30 Hz timer) | ≤ 33 ms | **knobs only — no note ever touches it** |

Totals at 48 kHz, `buffer + 0.5·buffer + 2 ms`:

| buffer | device | typical | worst |
|---:|---:|---:|---:|
| 128 | 2.67 ms | **6.00 ms** | 7.33 ms |
| 144 (WASAPI-excl. floor) | 3.00 ms | 6.50 ms | 8.00 ms |
| 256 (new default) | 5.33 ms | **10.00 ms** | 12.67 ms |
| 480 (old WASAPI-shared default) | 10.00 ms | 17.00 ms | 22.00 ms |
| 2560 (DirectSound default) | 53.33 ms | **82.00 ms** | 108.67 ms |

**The finding: the buffer size dominates everything else by an order of magnitude.** The
client was at 82 ms typical because DirectSound opened at 2560 samples. Moving him to ASIO
at 128 samples is a **13× improvement**, and after that the only fixed cost left is the
limiter's 2 ms.

Specific items the brief asked about:

- **Buffer size is reachable.** The gear's dialog is JUCE's `AudioDeviceSelectorComponent`
  constructed with `hideAdvancedOptionsWithButton = false`, so the sample-rate and buffer-size
  combos are always visible, and low values genuinely apply — the enumeration above is the
  driver's own list, and DeviceCheck opens each device to read it.
- **Note-ons are not timer-driven.** Confirmed by measurement, not reading: a note-on pushed
  into `RoutedMidiQueue` produces audio (magnitude 0.063) in the *very next* `processBlock`.
  The 30 Hz `timerCallback` handles only *learned CC → parameter* mapping and macro base
  tracking; notes, CC64, pitch bend, mod wheel and expression are all handled on the audio
  thread. `drainRoutedQueueAtBlockStart()` applies untimestamped events at offset 0 of the
  next block, which is the honest answer for a source that carries no timestamp.
- **The limiter's lookahead: keep it, keep reporting it.** `setLatencySamples (96)` was
  already being called in `prepareToPlay`, and the self-check now asserts that the number
  reported equals the delay measured through the bypassed limiter — so DAW delay compensation
  is correct. **We deliberately do NOT skip the delay line when the limiter is off.** Doing so
  would (i) change the reported latency at runtime, which hosts handle badly, and (ii) put a
  96-sample discontinuity into the output every time the LIMITER plate is toggled. 2 ms is
  20 % of one old 480-sample buffer but only 0.7 % of what the DirectSound default was
  costing; buying it back is not worth a click. Shortening the 2 ms lookahead itself would be
  a DSP change, which round 6 is not allowed to make. Asserted both ways in the self-check.
- **Smoothing ramps.** Master gain 20 ms, slot volume/pan 20 ms, the round-5 starvation
  fade 5 ms (and that one only fires when audio is genuinely missing). None are in the note
  path. The one long ramp is **expression (CC11) at 50 ms** — a deliberate volume-pedal
  smoothing, audible as a slow *swell*, never as note delay. Left alone: it is DSP behaviour.

### Issue 3 — "when I open the app it opens too wide and goes out of the screen"

`PluginEditor.cpp` called `setSize (Theme::M::designW, Theme::M::designH)` — 1536×1024 —
with no reference to the display. On this machine the primary user area is 1920×1032
logical, so the window was 1552×1072 against 1032 px of usable height: 40 px of overhang,
and far worse at 125 %/150 % scaling.

- `JMSignatureEditor::preferredSizeForArea()` is a **pure function** of a display's *user*
  area (taskbar already excluded) → the largest 3:2 size that fits once the caption (32 px)
  and borders (2 × 8 px) are taken off, never larger than the design size. `preferredSize()`
  applies it to the primary display. Being pure, it is unit-tested across seven display
  geometries without touching the physical display.
- **DPI is handled by not handling it.** `Displays::Display::userBounds` is already in
  *logical* pixels (`physicalBounds / scale`), and `Component` bounds are logical too, so a
  4K display at 200 % reports a 1920×1080 user area and gets a 1080p-sized window. Any
  explicit multiplication by `Display::scale` would double-apply it. Verified: the 4K@200 %
  case and the native-1080p case produce byte-identical results (1476×984).
- **The lower resize limit had to come down with it.** `Theme::M::minH` is 768, which is
  *taller than the usable area of a 1366×768 laptop*; leaving it fixed would only have moved
  the overhang from launch to the first resize. `setResizeLimits` now takes
  `min(Theme::M::minW/H, opening)`. The `Theme::M` constants themselves are untouched.
- **Window size and position now persist**, in `JMStandaloneWindow` under
  `jmWindowBounds` / `jmWindowFullScreen`. JUCE's `StandaloneFilterWindow` already saved
  `windowX`/`windowY` and validated them against the connected displays but **never saved
  the size**, which is why the oversized window came back every launch. We track the last
  non-fullscreen bounds in `moved()`/`resized()` (so a maximised session does not persist
  as the restore size) and, on restore, resolve the saved rectangle to the nearest display
  *that is connected now*, clamp the size into its user area and drag the position fully
  onto it — so a window last used on a second monitor cannot vanish when that monitor is
  gone.

Measured clamp (from `UiSelfCheck`, editor size → resulting window size):

| display user area | editor | window | aspect |
|---|---|---|---|
| 1366×728 (laptop) | 1020×680 | 1036×728 | 1.5000 |
| 1920×1032 (1080p) | 1476×984 | 1492×1032 | 1.5000 |
| 1536×824 (1080p @125 %) | 1164×776 | 1180×824 | 1.5000 |
| 1280×692 (1080p @150 %) | 966×644 | 982×692 | 1.5000 |
| 2560×1400 (1440p) | 1536×1024 | 1552×1072 | 1.5000 |
| 1920×1032 (4K @200 %) | 1476×984 | 1492×1032 | 1.5000 |
| 3840×2120 (4K @100 %) | 1536×1024 | 1552×1072 | 1.5000 |

### Untouched, as required

DSP behaviour, UI layout, the `.jmi` format and the modulation system are unchanged. The
round-3 custom standalone window still hides the Options button, still carries all three
title-bar buttons and still maximises by uniform scale + letterbox (`maximiseButtonPressed`
is unchanged; `resized()` still bypasses `StandaloneFilterWindow::resized()` and now only
additionally records the bounds). Nothing was added to the audio thread.

### Build / test evidence

- Release build (Ninja, MSVC 14.51): **exit 0**. One relink hit `LNK1104` because the
  client's standalone was running; the "JM Signature" process (PID 268) was terminated and
  the build completed — noted here as required.
- `ctest -C Release`: **2/2 passed** (`EngineTests` 4.42 s, `UiSelfCheck` 23.86 s).
- pluginval **1.0.4** (the copy present on this machine) strictness 8 `--skip-gui-tests` on
  the VST3: **SUCCESS, exit 0**.
- The standalone was **not launched** (the client had it open).

### What the client must do for the lowest latency

1. Gear icon → *Audio/MIDI Settings*.
2. **Device type: ASIO**, device: **`UMC ASIO Driver`** (not `ASIO4ALL v2`).
3. **Buffer: 128 samples** (256 if anything crackles). → ~6 ms total.
4. If ASIO is ever unavailable: **Windows Audio (Exclusive Mode)**, buffer 144–256.
5. **Never DirectSound** — its default here is 2560 samples / 53 ms, which is where the
   remaining "delay" was coming from.

## Client review round 5 — streaming defect ("crazy weirdness ... like when the processor is at high usage") (2026-08-31)

The client's first real sampled instrument, `Documents/JM Signature/Library/Piano.jmi`
(313 samples, ~201 MB of 24-bit WAV, median sample 2697 ms ≈ 129 480 frames), made the
plugin misbehave while the CPU meter read low. Three faults in the disk-streaming path,
all in `src/engine/Library.h` and the render path of `src/engine/Sampler.h`.

### Root causes (all three confirmed in the source, one measured)

1. **Most notes outran the preload, and the render path punished them.**
   `SampleData::kAttackFrames = 65536` (~1.365 s) was preloaded per sample and the rest
   streamed. **239 of the 313 samples are longer than that**, so most notes needed streamed
   audio after ~1.4 s. `SampleData::get()` returned `false` past what was resident and
   `Sampler.h` answered by calling `v.release()` and multiplying `env` by `0.995` **per
   sample** — the note was force-released and gone within ~2000 frames. This is what the
   client heard.
2. **A held note could never recover.** `TailLoader::request()` set `loadQueued`, and when
   the ring was full it cleared the flag again and relied on being "retried on next
   note-on". A held note gets no further note-on, so its tail was never requested again —
   and once released by fault 1 the voice was dead regardless.
3. **Priority inversion.** `TailLoader::request()` took a `juce::SpinLock` **on the audio
   thread** which the loader thread held across `AudioFormatReader::read()`, i.e. across
   file I/O. Whenever the loader was descheduled holding it, the audio thread spun. That is
   precisely "dropouts that look like overload while the CPU is idle".

Two further defects found while fixing those:

4. **Use-after-free in eviction.** `evictIdle()` checked `voiceRefs == 0` and then called
   `fullBuf.setSize (0, 0)`. A note-on could increment `voiceRefs` and start reading between
   those two steps, so the audio thread could read a freed buffer.
5. **Leaked voice references and audio-thread `free()`.** `startVoice` decremented
   `voiceRefs` only `if (v->active && ...)`, so every voice silenced by `allNotesOff(false)`
   leaked a reference and pinned its sample against eviction forever. And `setBundle` left
   the old bundle's `SamplePtr`s in silent voices, so the last reference to a ~1 MB
   `SampleData` was released — i.e. `free()`d — on the **audio thread** the next time that
   voice was stolen.

### The strategy: residency, not streaming — and why

Spec §5.1 asked for disk streaming. Streaming exists to play libraries that cannot fit in
RAM; at 200–400 MB per bundle it bought nothing and cost an entire class of concurrency
bug. Note the arithmetic: the old design already held a 65536-frame float attack buffer for
every sample — **217 MB for this bundle, 737 MB for the four dev bundles** — and then
allocated the *whole* sample again on top when a tail was needed. Full residency is not
more memory than the old design; measured, it is **less**.

So the model is now:

- **Every sample keeps a head**, still 65536 frames, preloaded synchronously when the
  bundle is assigned to a slot. Unchanged from what shipped, so nothing regresses in the
  window between "bundle assigned" and "body loaded" — including a DAW rendering faster
  than real time.
- **A background `SampleLoader` promotes samples to full residency**, demand first, then
  eagerly in enrolment order, up to a byte budget.
- **The head is handed back the moment the body lands** (`dropRedundantHead`), so
  steady-state residency is exactly one copy of the sample and never two. If a body is ever
  reclaimed, `retireFull` **rebuilds the head out of it first**, so every sample always has
  its head in memory.
- **The audio thread never blocks.** `SampleData::acquire()` / `release()` is a pair of
  atomic RMWs taken once per voice per block; the inner sample loop touches no atomics at
  all. Asking for missing audio is `wantsAudio.store (true, relaxed)` — one relaxed store,
  no queue to overflow, no lock to invert. The loader **polls**; nothing is pushed to it
  from the render path. Same law as `src/engine/MidiRouting.h`.
- **Starvation is a recoverable hold, not a death sentence.** When a voice runs past
  resident audio it now freezes its playback position, ramps to silence over 5 ms, and
  re-issues the wait-free request **every block** — which is exactly what makes a *held*
  note recover where the old note-on-only retry could not. When the audio arrives it ramps
  back up over 5 ms and continues from the frozen position, so there is no discontinuity.
  Only ~4 s of unbroken starvation finally reclaims the voice. With residency this path is
  a genuine last resort rather than something every note walks into.
- **Memory stays bounded.** `SampleLoader::kEagerBytes` = 768 MB, `kMaxResidentBytes` =
  1 GB, process-wide (the loader is a singleton, shared by every plugin instance) and
  tracked in `residentByteCounter()`, which `~SampleData` credits back. Past the ceiling the
  loader reclaims the bodies of samples no voice holds and that have been idle ≥ 10 s. A
  many-gigabyte library therefore degrades to head-plus-on-demand instead of exploding.

**Reclamation is safe by construction.** `retireFull` / `dropRedundantHead` publish a
retiring flag (seq_cst store) and then wait for `readers_` to drain (seq_cst load); every
reader increments `readers_` (seq_cst) *before* it reads the flag. The two orderings cannot
miss each other, so a body is never freed under a reader. The waiting is done on the loader
thread only — the audio thread merely sees "no body this block" and starves recoverably for
one block. `EngineTests` exercises this at maximum contention (a thread retiring and
republishing bodies underneath a 64-voice render loop).

Nothing else changed: zone selection, round-robin cycling, velocity crossfade, loop
crossfades, CC64, pitch bend, the modulation system, the `.jmi` format and the UI are all
untouched.

### Fixed alongside

- `startVoice` now releases the previous sample reference unconditionally (fault 5), and
  `setBundle` / `allNotesOff(false)` drop voice `SamplePtr`s on the thread that owns them,
  so no `SampleData` is ever destroyed on the audio thread.
- `juce::Time::currentTimeMillis()` is gone from the audio thread; the loader publishes a
  coarse clock into `coarseNowMs()` (~100 ms granularity, only ever compared against a 10 s
  idle threshold) which the audio thread reads with a relaxed load.
- **The UI self-check was depending on the developer's own library folder.** With only the
  client's one-world `Piano.jmi` in `Documents/JM Signature/Library`, three of the four
  Perform slots came up empty and twelve routing/audio assertions failed for reasons that
  had nothing to do with the code. `Library::loadSettings` now honours a `JM_LIBRARY_DIR`
  environment override (process-local, never written back to `settings.json`) and
  `CMakeLists.txt` sets it for the `UiSelfCheck` test, so the self-check runs hermetically
  against `testdata/`.

### Measurements — the defect, before and after

A steady 200 Hz tone, one note-on and then **never touched again**, sample longer than the
preload, body published at frame 100000. Identical harness on both builds
(`tests/Round5Streaming.inc`, `testHeldNoteOutrunsResidentAudio`):

| window                          | **before** | **after** |
|---------------------------------|-----------:|----------:|
| RMS in-preload  [40k..60k]      |   0.265039 |  0.265039 |
| RMS past-preload [70k..95k]     |   0.000000 (0.0 %) | 0.000000 (5 ms fade to silence, position frozen) |
| RMS after body arrives [120k..200k] | **0.000000 (0.0 %)** | **0.295318 (111.4 %)** |
| active voices at the end        |      **0** |     **1** |
| voice released by starvation    |    **yes** |    **no** |

The pre-fix build killed the note at the preload boundary and it never came back, however
long the note was held and however quickly the tail arrived. Worst waveform step across the
recovery seam is < 0.05 (a 200 Hz sine steps 0.013 per sample), i.e. no click.

Real bundle, sustained note across the exact old boundary
(`testRealBundleResidency`, `testdata/JM_Placeholder_Rhodes.jmi`, 360 samples):

- RMS just **before** frame 65536 = 0.052835
- RMS just **after** frame 65536 = 0.044965 — **85.1 %**, pre-fix **0.0 %**
- RMS at frame 90000–95000 = 0.023949 (45.3 %) — ordinary sample decay, not a duck
- voices just past the old cliff: active 1, releasing 0

Same test against the **client's own bundle** (`JM_BUNDLE=…/Library/Piano.jmi`):
before 0.012292 → after **0.010773 = 87.6 %** (pre-fix 0.0 %), voice still held.

### Load time and memory — the client's Piano.jmi (313 samples, 201 MB on disk)

- **Message-thread bundle load: 491.6 ms** for all 313 samples (65536-frame head preload).
  No UI freeze; the instrument plays from the head while the bodies load behind it.
- **Head residency straight after load: 197.0 MB.** After the background loader finishes
  (397 promotions) and hands the redundant heads back: **266.4 MB fully resident** — the
  expected 24-bit → float expansion of 201 MB, exactly one copy.
- **pluginval host process peak working set, client library: 286.9 MB.**
- Reference: `testdata/JM_Placeholder_Rhodes.jmi` (274.5 MB on disk, 360 samples) settles at
  **365.9 MB** resident.
- **Bounded-memory check:** pointed at the four dev bundles (1.1 GB of 24-bit WAV ≈ 1.46 GB
  as float — deliberately more than the budget), the pluginval host peaked at **989.7 MB**
  and passed. The ceiling held; the excess degraded to head-plus-on-demand as designed.
- For comparison, the *old* design's attack buffers alone were 197 MB for Piano.jmi and
  ~740 MB for the four dev bundles, **before** a single tail was loaded on top.

### Polyphony under load

64 voices (4 slots × 16), every voice starting head-only and every body landing mid-flight:
**median render block 0.424 ms, 95th percentile 0.798 ms, worst 2.441 ms** against a
10.67 ms budget at 512 frames / 48 kHz. Then, with a thread continuously retiring and
republishing bodies underneath the render loop (112 completed reclaim cycles): **worst block
1.555 ms, 64/64 voices still alive**, output finite throughout.

### Verification after client review round 5

- Full Release build (VST3 + Standalone + tests + snapshot tool): **exit 0**. The standalone
  relinked without LNK1104 this time; the user's running instance did not have to be killed.
- `ctest -C Release`: **100 % passed, 2/2** (EngineTests incl. the four new round-5 tests,
  UiSelfCheck).
- pluginval (the copy on this machine reports **1.0.4**) strictness 8 `--skip-gui-tests`:
  **SUCCESS, exit 0** — run twice, once against the client's own library (Piano.jmi only,
  peak 286.9 MB) and once against the four dev bundles (peak 989.7 MB).
- Standalone deliberately not launched (the user has it open). The Release relink of
  `JM Signature.exe` succeeded without LNK1104, so no running process had to be terminated.


## Modulation Phase 2 — Arpeggiator (2026-08-28)

Implements `docs/JM-MODULATION-SPEC.html` §E following `docs/MODULATION-RESEARCH.md`
§3 (and §4.2). One global arp with per-slot enable, classic arp, not a step
sequencer. New file: `src/engine/Arp.h`; new panel `ArpPanel` in `src/ui/ModPanels.h`.
Phase 1 is untouched except for two additive hooks (a new destination kind and an
offset accessor).

### Sub-block MIDI splitting — the structural change, and what it fixes
- **What was wrong.** `handleMidiAtBlockStart()` iterated the host buffer and
  **discarded every timestamp**: the whole block was applied at sample 0
  (research §3.7 / trap 13). Arp steps would have quantised to the block
  boundary — 5.3 ms at 256 samples, 21 ms at 1024 — and ordinary play carried
  the same error.
- **What it is now.** `processBlock` walks the block, splitting at **every host
  MIDI timestamp and every arp event**, with **JUCE's own 32-sample minimum
  subdivision** (`juce::Synthesiser::setMinimumRenderingSubdivisionSize`'s
  default, non-strict: an event inside the first 32 samples of a sub-block is
  applied at its start). Phase 1's 64-sample control block is the third boundary
  source, so the three compose: `chunk = min(64 when the matrix is live, next
  MIDI event, next arp event)`. `handleRoutedMessage()` gained an `offset`
  parameter; `handleMidiAtBlockStart()` is gone, replaced by
  `drainRoutedQueueAtBlockStart()`.
- **Ordinary play changed, deliberately and for the better**: a note stamped at
  sample 400 of a 512-sample block now starts *there*. Asserted exactly in
  `SnapshotCheck` by measuring the voice's playback advance — 512.0 samples when
  stamped at 0, 112.0 when stamped at 400, ratio 0.219 = 112/512. **Nothing
  depended on block-start application**: the two things that could have (the
  §5.4 sustain-pedal deferral and the LFO first-note retrigger edge) are both
  order-driven, not offset-driven, and are asserted unchanged.
- **What deliberately still applies at offset 0**: the `RoutedMidiQueue` events
  (standalone MIDI devices, the on-screen keyboard and wheels). They cross from
  device/message threads through a lock-free FIFO of 5-byte PODs and carry no
  timestamp; offset 0 is the honest answer (research §4.2). They are drained
  **after** `arp.beginBlock()` so a key played there starts its phrase inside
  the current block's schedule.

### Step scheduling and the transport
- **Step positions are derived from ppq**, never counted in samples:
  `ppqStep = k·stepQuarters + (k odd ? swing : 0)`, converted to a sample offset
  through `samplesPerQuarter`. While the transport rolls the grid is anchored to
  the **timeline origin**, so a phrase lands on the beat the instant the
  transport rolls rather than where the key went down (asserted). Stopped or in
  standalone, the anchor is the **first note** (Korg's Key Sync semantics), and
  the rest of that block is re-scheduled from the key-down sample so a long
  buffer does not swallow the opening steps.
- **`TransportClock` is reused, not duplicated** — there is still exactly one
  `getPlayHead()` reader in the plugin, in `processBlock`.
- Transport edges tested: **start** (pattern snaps to the grid), **locate**
  (`jumped` → note-offs flushed, `k` recomputed from the new ppq, **pattern
  index NOT restarted**), **loop wrap** (backwards jump, identical), **stop**
  (note-offs flushed *in the same block the stop is detected*, because some
  hosts stop calling `processBlock` immediately afterwards; the latch drops,
  Omnisphere's precedent), and a **tempo change** (120 → 60 doubles the step in
  samples because scheduling is in the ppq domain, which absorbs the tempo map
  to within one block).

### The five sustain-pedal rules (research §3.6), as implemented
1. **The arp consumes CC64 for the slots it drives.** The routing fork happens
   after the device/channel slot mask is resolved: `arpMask = routedMask &
   arpSlotEnable`, `directMask = routedMask & ~arpMask`. CC64 reaches
   `SamplerEngine::sustainPedal()` only for `directMask`, so a slot with the arp
   off keeps §5.4 **byte for byte** (asserted) and an arp-driven slot never
   sustains its steps' release tails into mush.
2. **CC64 ≥ 64 → latch on.** The keys physically held at that moment enter the
   latched set; keys played while the pedal is down are **added**.
3. **New-phrase rule.** All physical keys up + pedal down → the next note-on
   **clears** the latched set and starts a new chord. At least one key still
   down → the note is **added**. (Ableton's Hold rule, Sequential's "relatch"
   and Logic's Reset/Add boundary in one rule, no extra control.)
4. **CC64 < 64 → latch off**: fall back to the physically held keys; with none,
   the pattern stops and every generated note-on gets its explicit note-off.
5. **The Hold control is the same latch and ORs with the pedal** — dropping one
   while the other is engaged keeps the latch.
Plus rule 6, transport stop stops the latch (above). Each rule is asserted
separately in `EngineTests`; rule 1 end to end in `SnapshotCheck`.

### Note ordering
- Seven modes; the **Up/Down turnaround is an explicit Inclusive/Exclusive
  toggle** (Arturia's names), **defaulting to Exclusive**. Cycle lengths n / 2n /
  2n−2, with **n = 1 and n = 2 special-cased** — `2n−2` is 0 and would divide by
  zero (research trap 7).
- **Octave expansion is Bitwig "Thin"**: flatten notes × octaves into one linear
  shape *first*, then order it, so the expansion composes with the turnaround
  (2 notes × 2 octaves Up/Down exclusive = 6 steps, asserted). Every direction
  produces exactly `octaves` transpositions — up `0…+12(n−1)`, down `0…−12(n−1)`,
  **both centred** `12·(i − ⌊(n−1)/2⌋)` — so the cycle length does not change
  with direction. **[INFER: no vendor names a "both" convention.]**
- **Notes added or removed mid-pattern re-derive the pattern and clamp the
  index** (`stepIndex %= cycleLength`); nothing ever restarts implicitly
  (research §3.2 / trap 8). Asserted at 3 → 4 → 2 notes.
- Random is the **shuffled bag** (Ableton's "Random Other"), not pure random.

### Swing — the ambiguity resolved in code and on screen
The research flagged the spec's bare "0–75%" as meaning three different things.
Resolved as: **the control displays 0–75%, and the offbeat step's position
within its pair is `p = 0.5 + swing/300`** — 0% straight, **50% = triplet feel**
(p = 2/3), 75% = the MPC's documented ceiling (p = 0.75). The extra delay is
`swing/150` of a step and always < one step. The knob prints "STRAIGHT" /
"TRIPLET" / "MAX" at those values and the panel states the formula. Asserted at
0 / 50 / 75 to within 2 samples at 48 kHz.
**This is a deliberate deviation from research §3.4's `delay = T·S/100`** (which
put triplet at 33.3%): the brief chose the pair-position form so the round
display numbers land on musically meaningful feels. Same three-way ambiguity,
different resolution, written down once.

### Gate, velocity, routing
- Gate 1–100% of the step, `max(1, …)` samples so a note-off never lands on its
  own note-on's sample (trap 10); at 100% the off lands on the next on and is
  **emitted strictly before it** (NI's documented ordering) — note-offs are
  drained ahead of note-ons at every sub-block boundary. Nothing above 100%.
- Velocity as played / fixed / **accent**: a 4-step pattern (bitmask, default
  step 1) keyed to the **timeline step index** so it aligns to the bar;
  unaccented steps take 55% of the level.
- **Per-slot enable**: each held key carries its own `arpMask`, and generated
  notes inherit it, so a chord routed to slots A–D with the arp enabled on C is
  arpeggiated into C **and** played as-played into A, B and D — asserted through
  the real processor.
- **Anti-stuck-note contract**: every generated note-on is tracked as an exact
  `(note, slot mask)` pair and flushed explicitly on arp-off, latch release,
  transport stop, locate/loop jump, panic and `prepareToPlay`. CC 123 is never
  used as a substitute (the MIDI spec says it does not release notes while the
  receiver's hold pedal is on).

### Modulation destinations, concurrency, state
- `arp.rate` and `arp.gate` are matrix destinations (`ModDestKind::arpParam`),
  applied as **normalised offsets on top of the stored setting** — the setting
  itself is never written (asserted: gate 10% + a macro at full sweeps to >90%
  while `state.mod.arp.gate` stays 10). Mode, octaves and the per-slot enables
  are structural and deliberately **not** offered: modulating them would
  invalidate the step schedule on every control block.
  **Deviation worth naming:** rate modulation applies to the **free-run Hz**. A
  synced division is an integer index into an append-only table; sweeping it
  would jump the grid and force a schedule flush per control block, which is a
  stutter, not a musical control. The panel labels the destination "RATE (FREE)".
- The arp reads its two offsets once per host block, so they carry the previous
  block's last control value — the same one-block latch phase 1 uses for
  cross-modulation.
- Settings cross to the audio thread through the **same double-buffered POD +
  atomic index** idiom as the routing table (research §4.1). `publishArp()` is
  called by `publishModulation()`, so every existing publish site covers it. The
  message thread **never** touches the audio thread's held-note set, latch or
  sounding set; a settings change that invalidates the schedule is resolved in
  `beginBlock`, where the note-off flush can legally happen.
- State: `ModState` version **1 → 2**, adding one appended `arp` property. A
  version-1 state has no `arp` and loads with the arpeggiator **off** — exactly
  how that state behaved when it was written (asserted). Arp settings ride in
  plugin state **and** performance presets and are **absent from slot / `.jmi`
  presets** (spec decision 4), asserted both ways.

### UI
- The bottom-bar **ARP plate is live**; it opens `ArpPanel` as an overlay in the
  identical `ChromePanel` treatment as LFO and MOD MATRIX. `ux::Hardware`
  registers, `jm::styledMenu`, every colour and metric from `Theme.h`, no
  default JUCE chrome (asserted: every combo box is drawn by `JmLookAndFeel`),
  the approved arrangement untouched and no new navigation.
- Layout: ARP power + per-slot A–D + HOLD on the header row; RATE / GATE / SWING
  knobs; MODE, TURNAROUND, DIVISION, SYNC, OCT, OCT DIR; velocity mode with a
  LEVEL knob and four accent-step plates; a live IDLE / RUNNING / LATCHED lamp.
  The two sentences that keep the feature honest — the swing formula and the
  pedal rule — are printed on the panel.

### Deviations from the research (all deliberate, none silent)
1. **Swing scale**: the pair-position form `p = 0.5 + swing/300` rather than
   §3.4's `delay = T·S/100`. Same ambiguity, different resolution, per the
   brief. Documented above and on screen.
2. **Rate modulation is free-run only** (above).
3. **Octave "both" is centred** — no vendor documents a convention; centring
   keeps the cycle length independent of direction.
4. **The arp is not offered a Retrigger control** — research §3.2 recommends
   shipping none in v1, and "arp starts at the first note" covers the
   stopped/standalone case.
5. A preset load does not `hardReset()` the arp from the message thread (that
   would race the audio thread); the discontinuity is resolved on the audio
   thread instead, and an arp turned off by the load flushes there.

### Testing
- `tests/EngineTests.cpp` (header-only, no processor), 63 new assertions:
  every mode's exact note order; the inclusive/exclusive turnaround and both
  degenerate cases in both directions; octave expansion up / down / both and its
  composition with the turnaround; swing measured against `p = 0.5 + swing/300`
  at 0 / 50 / 75% to ±2 samples; gate at 50 / 100 / 1% including the ≥1-sample
  floor; **the five pedal rules each asserted separately** plus transport stop;
  beat alignment after transport start, locate and loop wrap with the index
  proved not to restart; a tempo change; notes added and removed mid-pattern;
  free-run from the first note; fixed and accent velocity; the per-note slot mask
  surviving into generated notes; and the versioned state round trip including a
  v1 mod block.
  The block driver mirrors `processBlock`'s loop exactly (with a 1-sample
  minimum subdivision so the tests can measure exact onsets).
- `tests/SnapshotCheck.cpp` (the real processor): sub-block timing measured on
  the voice (512.0 vs 112.0 samples); per-slot enable routing — the same chord
  arpeggiated into C while A, B and D play it as played; **rule 1** (CC64
  consumed by C, forwarded to A/B/D, arp latched instead); **§5.4 normal-play
  sustain unchanged with the arp off** (deferred note-off under the pedal,
  released on pedal up); arp state round trip; arp in performance presets and
  absent from slot presets; `arp.gate` swept by the matrix as an offset with the
  stored setting untouched; the ARP panel opening with its four knobs and six
  menus, all drawn by `JmLookAndFeel`; and the bottom bar's fourth plate live.

### Verification
- Release build (VST3 + Standalone + both console targets): **exit 0**.
- `ctest -C Release`: **100% passed, 2/2**.
- `JMSignatureSnapshot`: **SELF-CHECK PASSED (0 failures)**.
- pluginval strictness 8 `--skip-gui-tests --validate-in-process` on the VST3:
  **SUCCESS**.
- New snapshot `apps/signature/build/snapshots/arp-panel.png`; the whole phase-1
  and round-2/3/4 set regenerated alongside and unchanged.
- The standalone was **not launched**, and no relink failed this round.

## Modulation Phase 1 — LFOs + matrix (2026-08-28)

Implements `docs/JM-MODULATION-SPEC.html` §C/§D following the decisions in
`docs/MODULATION-RESEARCH.md`. The arpeggiator (§E) is **phase 2 and not started**;
the ARP bottom-bar plate is deliberately still a disabled placeholder. New files:
`src/engine/Modulation.h`, `src/ui/ModPanels.h`.

### The architectural correction — the macro path is no longer a parameter writer
- **What was wrong.** `timerCallback()` → `applyMacro()` → `setParamNormalized()` ran at
  **30 Hz on the message thread** and *overwrote the stored value* (`engineParams[id]`,
  `unit.params[id]`, `ampEnv`), and on a `host.*` path called `setValueNotifyingHost()`.
  Research §0: that is below the Nyquist rate of a 40 Hz LFO, it destroys the player's
  knob position (contradicting the spec's "modulation never overwrites the knob"), and
  it would write LFO motion into host automation — the shape of a known Bitwig VST3
  modulation feedback bug.
- **What it is now — base + offset.** `state` and APVTS keep the **base** value the
  player set and are never written by modulation. The matrix is evaluated on the
  **audio thread every 64 samples** (research §1.5: ≈689 Hz at 44.1 kHz, SynthLab's
  figure and the low end of u-he's range) and produces a normalised offset added at the
  point of use. `applyMacro()` survives as an API and now simply republishes the routing
  table, so every existing call site (macro editor `live()`, drag-drop, LEARN,
  right-click assign) is unchanged.
- **Macros became source #1 of the same matrix.** `publishModulation()` resolves macro
  links *and* matrix rows into ONE table, so there is one modulation system rather than
  two. Existing macro links, their serialization (`macrosToVar`) and host automation of
  `macro1..4` are untouched.
- **Keeping existing links working.** `MacroTarget` gained one function,
  `offsetFor(norm)`, and nothing else. It is provably the same curve the link always
  had, anchored at the end of the window `apply()` swept *from* — unit-tested as
  `offsetFor(v) == apply(v) − anchor` for every depth/curve/invert combination, so a
  saved link keeps its span, its direction, its curve, its range and its invert flag.
  With the destination knob at the low end of its range (which is what a link created by
  drag / LEARN / assign has always produced) the behaviour is **value-for-value
  identical** to the old destructive path — covered end to end in `SnapshotCheck` by
  loading a real version-1 state. The one deliberate difference is the one the spec
  demands: a macro **at rest** now leaves the knob alone instead of forcing the target to
  an end stop.
- Expanding the anchored form gives exactly the research's recommended depth formula, so
  macros and LFOs share one code path:
  `offset = depth · (max−min) · f`, with `f = shaped` (unipolar) or `shaped − 0.5`
  (bipolar), `shaped = clamp01(bipolar ? s·0.5+0.5 : s)^curve`, inverted if `invert`.
  Peak-to-peak swing is `|depth|·(max−min)` for **every** source type ("School A",
  Vital/Serum), so swapping velocity for an LFO in a row does not double the effect. The
  matrix panel says so on screen ("DEPTH IS TOTAL SWING"), because Massive X / Pigments
  users expect double.

### Concurrency and the control block
- Routing is a fixed `std::array<ResolvedRow, 96>` inside a **double-buffered POD table
  with an atomic index** (research §4.1) — message thread fills the inactive buffer,
  publishes with one release store; the audio thread does one acquire load per block. No
  lock, no allocation, no dropped block, and **the audio thread never touches a
  `juce::String`**: every destination path is resolved to integer indices (kind / slot /
  unit / param / lo / hi / logTaper / quantise) on the message thread. The existing
  `RoutedMidiQueue` was *not* reused — a MIDI FIFO is the wrong shape for a table.
- Destinations are grouped at publish time, so the audio thread does `sum[groupIdx] +=
  offset` rather than an O(n²) destination match. Sum in **normalised** space, clamp once
  at `[0,1]`, denormalise once through the cached range (research §1.2 / trap 6).
- `processBlock` splits the host block into 64-sample chunks **only when the matrix is
  live**; with no modulation the whole block renders in one call exactly as before, so an
  unmodulated patch pays nothing. Slot render → slot rack → master rack all happen per
  chunk (allocation-free `AudioBuffer` views over the existing buffers); master gain,
  expression and the limiter stay whole-block.
- Smoothing (research §1.5): master volume keeps its 20 ms `SmoothedValue`; **slot volume
  and pan gained one each**, and pan is smoothed as a **position** with the equal-power
  law recomputed per sample (never the two channel gains independently). The per-sample
  path is taken only while a value is actually moving. Engine and FX destinations update
  at the 64-sample control rate; at ≤40 Hz that is ≥17 updates per LFO cycle, which is
  the granularity SynthLab and u-he ship. **Known delta:** an FX unit that reads its
  parameters once per `process()` therefore steps at 64-sample boundaries rather than
  per sample; audibly fine for depth/mix/rate destinations, and the place to improve if a
  sound designer ever reports it.
- `FxRackDsp` gained `setParamRt` / `setBypassedRt`, which use the **same try-lock
  discipline as `process()`** (skip this control block if the rack is mid-rebuild) and an
  index-addressed `FxUnit::setParamIndexed`.
- Cross-modulation (`mod.lfoN.rate`, `mod.rowN.depth`) is evaluated in a **fixed source
  order — macros, MIDI/note sources, then LFO 1, 2, 3 — using the previous control
  block's value for any backward reference**. That is Vital's `cr::Feedback` behaviour
  with none of the machinery (research §1.4), and it makes a cycle cost ~1.5 ms of
  latency instead of hanging. The row editor additionally refuses Vital's degenerate
  self-references: a row may not drive its own source's rate, its own depth, or its own
  macro.

### LFOs (three, global)
- Shapes sine / triangle / saw up / saw down / square / S&H / smooth random. Pirkle's
  start-phase offsets are folded in so triangle and both ramps **begin at zero output**.
  Smooth random uses Vital's **raised-cosine** (`sinInterpolate`) — C¹-continuous,
  monotone between knots, cannot overshoot ±1 and therefore cannot break the clamp.
- **Synced phase is `frac(ppq / beatsPerCycle)`**, derived from `ppqPosition`, not
  integrated from Hz (research §2.4 / trap 2). Stateless, so a tempo ramp, a locate and a
  loop wrap all resolve correctly with no repair, and a bounce is reproducible.
  `TransportClock` latches the host ppq **only while playing** and free-runs at the last
  known bpm otherwise (Surge XT's rule) — which is also the entire standalone code path.
  A jump is detected by prediction (no such flag exists in JUCE or VST3) purely so the
  S&H / smooth-random knots can be reseeded; the phase itself needs no repair. The last
  valid bpm is kept forever and never falls back to 0. **Anchored, no anchor switch**
  (Serum's tradeoff, decided in favour of reproducible bounces).
- Free rate 0.01–40 Hz on a log taper (`setSkewFactorFromMidPoint(2 Hz)`, the Vital
  default). Phase offset 0–360° is kept **separate from the running phase and added at
  read time** (Vital's separation), so it is meaningful even in Free retrigger and
  turning the knob is a pure rotation with no drift — which is exactly what the spec asks
  Phase for. Fade-in 0–5 s uses the **crossfade** form (Serum's Rise / Vital), not a
  depth envelope, so a unipolar destination does not jump; it is armed on the 0-held-notes
  → 1 edge for **all** retrigger modes (Omnisphere's legato rule), including Free.
  Retrigger Free / First note / Every note.
- `divisionBeats()` is **extended append-only** to the full 8/1…1/64 × straight/dotted/
  triplet table plus Vital's **FREEZE** (ratio 0) at index 30. Indices 0–7 keep exactly
  their original values — FX presets store the integer index, and reordering would
  silently corrupt every saved preset (research trap 16). The FX rack's own
  `syncDivisions()` list is byte-identical to before; the LFO menu presents
  `lfoDivisionOrder()`, a display-only ordering over the canonical indices. Note the LFO
  menu labels indices 2 and 4 "1/16d"/"1/8d" where the FX list calls them "3/32"/"3/16" —
  same ratio, clearer name for an LFO; the FX choice **strings** (which are what FX preset
  JSON stores) were not touched.

### UI
- MOD MATRIX and LFO open from the existing bottom-bar plates as overlays in the macro
  editor's `ChromePanel` treatment. `ux::Hardware` registers, `jm::styledMenu`, all
  metrics and colours from `Theme.h`; the approved arrangement is untouched and no new
  navigation was added. ARP stayed disabled here; it is live as of Phase 2.
- **Modulated-knob display** (the one genuinely new piece of drawing, research §1.3): the
  base value stays the pointer and the value arc; the **modulation span** is drawn as a
  translucent band on the same ring beneath the value arc, in the source's own colour
  (Serum's coloured-halo idiom), with a **live marker** at the value being swept. Negative
  depth is a dimmer variant of the same colour rather than a second hue — the palette is
  one gold family and a rival hue fights the chrome. The span is clipped at the
  −135°/+135° ends and the clipped end is **capped with a bright tick**, which is
  Pigments' documented failure mode (a ring lit but useless) made visible.
- Assignment for matrix rows is the row's own source/destination dropdowns; the existing
  drag-a-parameter-onto-a-macro gesture is untouched and is still the drag route. A
  second drag idiom was deliberately not added.

### State
- Plugin state version **1 → 2**, adding a `mod` block (3 LFOs + 16 rows + `rowCount`),
  itself versioned. A version-1 state simply has no `mod` property, loads as a clean
  default and keeps all its macro links. Modulation is written into plugin state **and**
  performance presets, and **never** into slot / `.jmi` bundle presets — that schema is
  shared with JM Capture, which has no LFOs to model (spec decision 4). Asserted in tests
  both ways.
- `rowCount` is stored and the array is sized 16, so growing the spec's eight rows to
  sixteen later is a UI change only (spec decision 3).
- The processor listens to its **own** `stateChanged` broadcast and republishes the
  routing table, because destination paths carry FX unit *indices*: a rack edit must not
  leave a row pointing at the wrong unit.

### Deviations from the research (all deliberate, none silent)
1. **Sub-block MIDI splitting was not implemented.** Research §3.7/§4.2 wants
   `handleMidiAtBlockStart()` to become `handleMidi(offset,count)`. That is arpeggiator
   work (its whole value is note timing), it changes note behaviour for *all* play, and
   phase 1 does not need it — global LFOs and note-derived sources are block-accurate by
   construction. The 64-sample control block is in place and composes with it, so phase 2
   inherits the structure. Logged as the remaining delta.
   **RESOLVED in Phase 2** — the render path is sub-block split at MIDI
   timestamps and arp events now; see "Modulation Phase 2 — Arpeggiator".
2. **`modSourceColour` returns an ARGB `uint32`, not a `juce::Colour`.** `Modulation.h`
   is included by the console test target, which does not link `juce_graphics`.
3. **Neutral note sources use `#C9B27A`, not `Theme::muted`**, purely for legibility of
   the span band on the dark ring.
4. Arp decisions (Inclusive/Exclusive turnaround, swing scale) are **left entirely to
   phase 2** — defining the enum now was not free, since no arp code exists to hold it.

### Testing
- `tests/EngineTests.cpp` (header-only, no processor): the append-only division table
  (0–7 frozen, the full table present, the display order a bijection); depth semantics
  for a unipolar vs a bipolar source including the equal-peak-to-peak property; the
  legacy-macro-link identity over every depth/curve/invert combination; every LFO shape's
  start phase and ±1 bound; the log rate taper; **synced phase across a tempo change, a
  transport locate and a loop wrap** plus free-run when stopped and FREEZE; fade-in as a
  crossfade; smooth random continuous at the control rate while S&H deliberately steps;
  sum-then-clamp with two rows on one destination; base-moves-the-whole-range; the
  cross-modulation latch; and a versioned `ModState` round trip including a disabled row.
- `tests/SnapshotCheck.cpp` (the real processor): **base + offset through the DSP** — an
  LFO at depth 50% sweeps `bark` ±25% of range, the stored value stays exactly on the
  knob, and turning the knob by 35% moves both ends of the modulated range by 35%;
  disabling a row hands the destination back to the knob. **No zipper at 39 Hz** —
  rendered twice with the same routing and only the depth changed (so both take the same
  64-sample path), the per-sample ratio is the applied gain: worst step **0.0011**, which
  is the 20 ms ramp's own limit (1/(0.02·48000) = 0.00104), and **zero** steps above 1%;
  a determinism control asserts the reference render is bit-identical twice. **600 live
  re-routings interleaved with real `processBlock` calls** produce no NaN, inf or runaway
  output. A real **version-1 state** loads and its macro link sweeps value-for-value as
  before. Modulation state round-trips; it is in performance presets and absent from slot
  presets. The cross-modulation guard. The two panels open, expose their controls and are
  drawn entirely by `JmLookAndFeel`; the bottom-bar plates are live (ARP still is not);
  modulated knobs publish their span and a bipolar source reports a symmetric ±30% while
  a unipolar one reports a one-sided span anchored at the knob.

### Verification
- Release build (VST3 + Standalone + both console targets): **exit 0**.
- `ctest -C Release`: **100% passed, 2/2** — EngineTests, UiSelfCheck.
- `JMSignatureSnapshot`: **SELF-CHECK PASSED (0 failures)**, worst hero offset (0,0) px.
- pluginval strictness 8 `--skip-gui-tests --validate-in-process` on the VST3: **SUCCESS**.
- New snapshots in `apps/signature/build/snapshots/`: `mod-lfo-panel.png`,
  `mod-matrix-panel.png`, `mod-knob-span.png` (three modulated hero knobs showing a
  bipolar span, a unipolar span and a negative-depth span). The whole round-2/3/4 set is
  regenerated alongside and unchanged.
- The standalone was **not launched**. The Release relink failed once with `LNK1104`
  because the user's running "JM Signature" standalone (PID 57868) held the exe; that
  instance was terminated to complete the link, as in rounds 2 and 4.

## Framework / build
- **JUCE pinned to 8.0.15** (newest 8.x tag at build time; the spec's 8.0.4 is superseded — verified via `git ls-remote`). A pre-cloned copy sits in `apps/signature/external/JUCE` so CI/offline builds skip FetchContent; the CMakeLists falls back to FetchContent with the same pinned tag when the folder is absent.
- **AU target declared** in `FORMATS VST3 AU Standalone`; JUCE ignores AU on non-Apple platforms, so the Windows build produces VST3 + Standalone only, and a mac build gets AU with zero changes.
- Tests are a `juce_add_console_app` target (`JMSignatureTests`) registered with CTest.

## Sample streaming
- Spec: "preload ~64k-sample attack buffers at load, stream tails on a background thread." Implementation: attack preloads (65,536 frames) are read synchronously when a bundle is loaded into a slot; **full tails are loaded on demand by a single background thread** (`TailLoader`), kicked at note-on via a fixed-size lock-free-ish ring (no allocation on the audio thread). A 64k-frame attack is ~1.37 s at 48 kHz — far longer than any realistic disk load, so the tail is in memory before playback needs it. If a tail genuinely isn't ready (cold start + extreme disk pressure), the voice fades out over ~30 ms instead of clicking.
- **Cache eviction:** tails idle > 45 s with no reading voices are freed once the cache exceeds 640 MB (attack preloads always stay). This bounds memory with 4 bundles × 360 samples loaded.
- Attack preload happens at *slot load*, not at library scan — scanning only parses `instrument.json` (preloading all four placeholder bundles at scan would cost ~740 MB).

## Library fallback (dev builds)
- Per the task brief: if the configured library folder (default `~/Documents/JM Signature/Library`, changeable in Settings) contains no bundles, the engine scans `C:\JM VST\testdata` (and `./testdata` relative to CWD) so dev builds find the generated placeholders. The status bar shows "library empty — using testdata placeholders" when the fallback is active.

## Engine
- **Voice stealing:** free voice → quietest voice already in release → oldest voice. 64 voices shared per slot engine (4 engines = up to 256 total).
- **Velocity crossfade:** equal-power over a ±8-velocity window centred on each layer boundary; gains below 1e-4 are dropped (so the overlap edges collapse to a single voice). Unit-tested.
- **MIDI timing:** ~~MIDI events are applied at block start (not sample-accurate splitting).~~ **Superseded by Modulation Phase 2**: the render path is split at every host MIDI timestamp (and every arp event) with JUCE's 32-sample minimum subdivision, so a note starts on the sample the host stamped it.
- **Transpose + held notes:** changing transpose while notes are held can orphan a note-off mapping (standard synth caveat); all-notes-off/sustain logic still clears everything.
- ~~Pitch bend is intentionally not implemented for v1 (sampled worlds; not in the spec).~~
  **Superseded by client review round 4 (item 2)**: pitch bend is implemented as a smoothed
  playback-rate multiplier with a configurable ±range (default ±2 st).

## Model pages ↔ Perform slots
- The plugin owns **4 slot engines** (Perform A–D). By default slot A=Rhodes, B=MKS, C=FM, D=Strings. A model page edits **the first slot hosting that world**; if no slot hosts the world, the page falls back to the same-index slot and loading a patch from that page assigns the world to it. This keeps one engine instance count (4) while satisfying "each slot loads any world/patch".
- Default slot enables: only slot A enabled at first run (all four stacked would be a wall of sound); the approved reference's all-on look is one click away.
  **Qualified by client review round 8**: because a disabled slot never receives a note, this default made every model page but the first silent when a patch was chosen. Choosing a patch on a model page now makes that slot audible — moving the enable in a single-instrument setup, adding it to a layer the player built, and soloing it in SOLO. Perform-page loads are untouched.
- **Round 8**: a slot whose stored `bundleDir` is not in the current library is no longer loaded from that path; the library substitutes (or the slot is left empty) and the status bar says so.
- Model-page "EQ" sub-tab binds to the slot rack's Vintage EQ unit, auto-inserting one (flat) if the rack has room — keeps a single EQ implementation and the preset JSON portable.
- Rhodes **Bark** maps to layer-crossfade bias (+0..+30 velocity toward harder layers) plus a gentle per-voice tanh drive. **Tine** = 3.2 kHz bell ±8 dB, **Body** = 200 Hz low shelf ±8 dB, **Tremolo/Space** drive the rack units (tremolo depth; tape delay + plate mix). Similar curated mappings for MKS/FM/Strings per §5.2 (FM Brightness = velocity-scaled per-voice LP; Strings Swell = 10 ms–2 s attack override; Motion = slow pan/level LFO).

## FX
- Reverbs are Freeverb-class (`juce::Reverb`) with added predelay, wet-path modulation, and a granular +12 st shifter in the hall's feedback for shimmer — "quality over novelty" per §4.
- Bypass is a 20 ms smoothed dry/wet crossfade (clickless); bypassed units keep processing until the fade completes, fully-bypassed units are skipped.
- Rack layout changes (add/remove/reorder/replace) rebuild slots on the message thread; the audio thread takes a try-lock and passes dry for the (rare) colliding block.
- Tempo-sync divisions: 1/32…1/2 including dotted (1/4d used by defaults). Sync defaults ON for delays with the spec's musical defaults (3/16, 1/4, 1/4d).

## MIDI learn / macros
- Pre-mapped fast paths (CC64 sustain, CC1 per-world, CC11 expression) are handled directly on the audio thread (atomic writes only). **Learned CC assignments** are applied on a 30 Hz message-thread timer via the parameter router (~max 33 ms latency) so state and UI stay coherent without audio-thread hazards.
- CC1/CC11 modulation is ephemeral (drives DSP, not persisted into preset state) — matching how mod wheel/expression behave on hardware.
- Macro learn: arm LEARN on a macro (round 4: right-click the macro knob -> Learn; the chip
  is gone), then click any knob (model page or FX). Targets get full-range linear mapping by default; per-target invert and linear/exp curve toggle via the macro's target menu. Macros are host parameters (`macro1..4`) and therefore automatable; they can also be MIDI-learned like any control (path `host.macroN`).

## State / presets
- Plugin state and performance presets serialize to JSON (slots, engine params, racks, amp envelopes, macros + targets, MIDI map, host params). FX/engine params inside that JSON use the exact fx-params.ts ids with enums as choice strings and bools as true/false, so slot preset JSON is directly portable with the `.jmi` preset schema.
- User presets: `~/Documents/JM Signature/Presets/<world>/*.json`; performance snapshots in `.../Presets/Performance/` (a `Factory` subfolder is scanned as factory content when present).
- Host parameters (automatable): 4 macros, master volume, transpose, octave, solo/poly, per-slot volume/pan/enable. World-engine and FX params are internal (macro/MIDI-learnable) to keep the host parameter list stable while racks change.

## UI
- All colors/fonts/metrics live in `src/Theme.h`; drawing in `JmLookAndFeel` + panel `paint()` using only Theme tokens. Flat per-module accent tints stand in for the future kente textures; the knob face draws a placeholder engraving where adinkra SVGs will slot in.
- Editor is resizable with fixed aspect (1380×900 reference, scaled metrics).
- Reference-image elements that are placeholder-only in v1: cart/settings icons, license text (static). Keyboard button shows a working on-screen keyboard.

## Kente visual pass (client-requested, ahead of the UX doc)
- `src/ui/KenteTexture.h`: procedural, deterministic (seeded by style+tint), tileable cloth renderer with a per-(style,tint) image cache. Two styles: **Weave** (Ewe/Ashanti patchwork — blocks of warp stripes, weft bands, checker, zigzag, sawtooth triangles, fine hatch, plus warp micro-threads and light weft seams; block rhythm from kente/02+03) and **Flat** (bold kente/01-style geometry for the Perform slot strips). All parameters/tints in `Theme.h` (`Theme::Kente`).
- Contrast law enforced in code: every cloth colour is clamped to brightness ≤ 0.21 (weave) / 0.24 (flat) and desaturated before drawing, keeping the pattern ≤ ~35% perceived contrast against #0C0A07; readability scrims sit behind control areas.
- Application: model pages = full-panel tapestry frame + recessed dark control panel; FX units = per-type cloth faceplates; Perform slots = flat-graphic strips in the hosted world's tint; master rack + Perform center = royal gold/burgundy weave (`masterCloth #5C2A20`); preset browser, dock body and non-master rack strip = faintest near-black weave (`chassisCloth #17130E`); chassis stays flat black.
- **Tint table** (Theme.h tokens):
  | Module | Token | Hex |
  |---|---|---|
  | Rhodes/EP page + slots | rhodesAccent | #7A3B22 (rust/amber) |
  | MKS page + slots | mksAccent | #2E4A2B (forest/gold) |
  | FM page + slots | fmAccent | #23305C (indigo) |
  | Strings page + slots | stringsAccent | #4A2B5C (violet) |
  | Master section/rack | masterCloth | #5C2A20 (royal gold/burgundy) |
  | JM Chorus | fxChorus | #1E4A44 (teal) |
  | Tremolo/Auto-Pan | fxTremolo | #4A3A1E (dark amber) — my pick |
  | Phaser | fxPhaser | #45265C (violet) |
  | Flanger | fxFlanger | #26455C (steel blue) — my pick |
  | EP Preamp/Drive | fxDrive | #5C3A26 (burnt sienna) — my pick |
  | Vintage EQ | fxEq | #3A4A2E (olive/forest) — my pick |
  | Compressor | fxComp | #5C2626 (crimson) |
  | Tape Delay | fxTapeDelay | #5C4526 (ember) |
  | Digital Delay | fxDigDelay | #26305C (deep indigo) — my pick |
  | Plate Reverb | fxPlate | #2E3E4A (slate/forest) — my pick |
  | Hall/Shimmer | fxHall | #2E4A38 (forest; shimmer amount lives in the unit) |
  | Widener/Ensemble | fxWidener | #44265C (violet) — my pick |
  | Browser/dock chassis weave | chassisCloth | #17130E |

## Client UX asset pack integration (kente/assets/, 2026-08-27)
- Assets embedded as inline SVG string constants in `src/ui/Assets.h` and loaded via `juce::Drawable::createFromSVG` (no BinaryData step needed for ~4 KB of text; keeps the build simple). Icons ship gold `#D9A648` and are retinted by textual colour substitution before parsing, so Theme.h remains the single colour source.
- Icon wiring: search (inside the browser search field), filter (button opening the User/Factory/Favorites filter menu), star (favorite marker in browser rows), heart (model-page header — toggles favorite on the current preset), left/right (model-page patch prev/next, cycling bundle presets; "right" is the client's icon-left mirrored — not a separate pack file), down (ComboBox chevron via LookAndFeel::drawComboBox), more (FX unit menu + new per-slot menu with reset-mix/enable), keyboard (status-bar toggle), cart (top-right, decorative only per the reference — no store exists to link).
- `logo.svg` renders top-left via Drawable (replaces the hand-drawn shield path), gradient kept verbatim.
- `adinkra-symbol.svg` is the knob engraving, exposed as a swappable `assets::knobEngravingSvg()` string; the LookAndFeel renders whatever symbol that returns (client will deliver more symbols; per-control assignment slots in there).
- Knob recipe (`knob-recipe.svg`) implemented in `JmLookAndFeel::drawRotarySlider` with all gradient stops as Theme tokens (`knobBody*`, `knobRim*`, `knobRidge`, `knobTip`); value arc + numeric readout kept per the design law. The SVG's gaussian blur shadow is approximated with a soft offset ellipse (no blur filter in the JUCE renderer).
- LED meter zones (`led-meter.svg`) applied to the shared `LevelMeter` (master, per-slot, model-page output): #274E27 / #D9A648 / #8B1A1A on #0C0A07 with #2A2622 border, plus LED segmenting.
- Pill toggle (`toggle.svg`) implemented as `PillToggle`; used for SOLO/POLY (thumb right = POLY) and for every boolean FX parameter (sync, ping-pong) via ParamChoiceButton's bool mode.
- **Pattern SVGs: transcribed into the procedural generator rather than parsed.** Reason: the SVGs ship at full gold brightness; transcription lets every colour pass the existing `clampCloth()` ≤35%-contrast pipeline, pulls the palette 35% toward each world's Theme tint (keeping module identity), and layers the woven warp/weft threadwork on top so the pattern still reads as cloth. Geometry follows the files 1:1 (rhodes 150px tile: stripe bands + split diamonds + burgundy borders; mks 100px: burgundy diamond column + zigzag column + cream divider; fm 120px: 4 vertical stripes + offset blue and red/green blocks with dashed weave lines; strings 120px: shard triangles + dashed radial threads + two octagon rings). Strings' "spider abstraction" reduced to a single small hub dot so it reads as fine radial threadwork, not a literal spider (per instruction).
- Deliberately not used from the pack: nothing omitted; only adaptations are the decorative (non-clickable) cart, the mirrored right chevron, and the strings hub simplification above.

## UX pack v1.0.0 — supersedes interim kente pass (2026-08-27)
Implemented from `docs/svg-pack` (CLAUDE-IMPLEMENTATION.md + manifest.json). Wherever this pack and the earlier interim styling differ, the pack wins.

- **World cloth = exact pack assets, not procedural.** The `kente-*-panel.svg` files are ~6 MB SVGs whose only pixel content is an embedded data-URI raster plus a described overlay (x0.88 brightness filter, 0.30→0.68 vertical shade, 10% world-accent radial warmth). Per the pack's own JUCE note, the rasters were extracted verbatim from the data URIs at vendor time (`apps/signature/resources/kente_*.png|jpeg` — byte-identical payloads, no re-encode) and embedded via `juce_add_binary_data`; the overlay treatments are transcribed 1:1 in `src/ui/UxAssets.h` (`drawWorldPanel`, 0.70 opacity within the recommended 0.55–0.75). The SVG's `feColorMatrix 0.88` is reproduced exactly as a 12%-alpha black scrim (mathematically identical over opaque pixels); JUCE renders no SVG filters, so parsing the SVG directly would have LOST the darkening — extraction is the faithful path. Perform slots reuse the same rasters with the `perform-slot-*.svg` overlay (0.25→0.62 horizontal shade + 8% accent wash + 1.6 px accent border), so each raster ships once (~13.7 MB total embedded).
- The procedural `KenteTexture` remains ONLY as an in-code fallback if a raster fails to decode, plus `kente-micro-weave.svg` (pure vector) transcribed as a cached 64 px tile for the 20–35% overlay duty.
- **Palette**: manifest colors adopted wholesale in Theme.h (bg/chassis/panel/panelRaised/line/gold/goldBright/goldDark/cream/muted; world accents rhodes #D36B18, mks #A7D61A, fm #2FA8FF, strings #C150D8 — now accents for edges/labels/rings only; fx family teal/orange/purple/blue/red/green). Old cloth-tint tokens deleted or kept only for the fallback path.
- **Knobs**: `knob-primary.svg` / `knob-secondary.svg` geometry transcribed into `JmLookAndFeel::drawRotarySlider` (primary shell for ≥72 px, secondary below; all radii/strokes at the SVG ratios; rotation −135°..+135° via `setRotaryParameters`; only the indicator rotates, shell + engraving fixed). Marks are cached `Drawable`s, never parsed per repaint. `knob-recipe.svg` from the earlier interim pack is superseded.
- **Adinkra engraving assignments** (pack marks, swappable via slider property "jmMark"): model-page hero knobs get marks in control order — #1 cross-knot, #2 four-petal, #3 spiral-unity, #4 star-weave, #5 triple-ring (so Rhodes: Bark=cross-knot, Tine=four-petal, Body=spiral-unity, Tremolo=star-weave, Space=triple-ring; same order per world). Macros all use spiral-unity. Secondary knobs (FX, slot vol/pan) have no engraving, matching knob-secondary.svg.
- **Macro rings**: `macro-ring-*.svg` transcribed (base #2B2418 ring + glowing colored arc); macro 1→rhodes, 2→mks, 3→fm, 4→strings accents (semantic color only).
- **Perform slots**: state rules per pack — OFF = whole strip at 45% component alpha + dead meter; ON = full + 1.6 px world edge; SELECTED (the slot the current model page edits) adds the 2 px gold inner line. Power = `power-button.svg` transcription (48 px ref). SOLO/POLY pill restyled to `toggle-solo-poly.svg` (green #B4E51A thumb). Transpose = new `TransposeControl` per `transpose.svg` (220×62, front-and-center; live value text). Octave stepper kept below it (pack has no octave asset; kept in the same visual language).
- **FX faceplates** (`fx-module-*.svg` transcription: color→#0B0B0A gradient, #6B5128 border, inner #E9C56A 25% line, corner screws; `feTurbulence` grain approximated by a deterministic sparse speckle — JUCE renders no SVG filters): chorus **teal**, vintage EQ **teal**, tape delay / tremolo / EP drive **orange**, phaser / flanger **purple**, hall-shimmer / digital delay **blue**, compressor **red**, plate reverb **green**, widener **green + 25% gold overlay**. Master rack + generic panels use `master-fx-frame` / `panel-frame` language.
- **Meters**: `vertical-meter.svg` segment scheme (#070707/#392A17 track, 6 px segments, green→yellow→orange at the asset's 8/3/2 proportions). Our meters are stereo, so the single-column asset is drawn as two columns.
- **Preset browser**: `preset-browser-frame` + `preset-row-active` treatments; active row text gold, inactive cream/muted; search/filter stay live controls. Pack icons (star/heart/kebab/chevrons) replace the interim ones where they overlap; interim search/filter/keyboard/cart icons are retained where the pack ships no counterpart. Combo chevron drawn in the pack chevron language (rotated to point down — no chevron-down asset).
- **Branding**: `jm-shield-reference.svg` replaces the interim logo asset in the top bar.
- Editor reference viewport now 1536×1024 (was 1380×900), fixed aspect, limits 1152×768–3072×2048.
- Assets not used as-shipped: the four `*-exact.svg` (panel variants used instead, per the mapping table); `perform-slot-*.svg` and `kente-*-panel.svg` used via raster-extraction + transcribed overlays (reason above); `gear.svg` currently unused (no gear-shaped control in the layout — Settings is a text sub-tab); `reference/*.png|jpeg` used as visual reference only.

## UX pack V2 — supersedes V1 (2026-08-27)
Implemented from `docs/svg-pack-v2` (CLAUDE-IMPLEMENTATION-V2.md + tokens/design-tokens.json + specs/screen-layouts.json + screen SVGs). Presentation authority = V2 pack; behavior/DSP authority = JM-BUILD-SPEC.md (no functions added or removed).

- **Root & scaling (failure #1)**: one 1000x720 (25:18) root component; whole root scaled by `min(w/1000, h/720)` via a single AffineTransform, centered, chassis-colored letterbox; limits 750x540–2000x1440, fixed aspect. No reflow, no independent stretch — every control lives at absolute reference coordinates.
- **Header (failure #2)**: exact tokens.header geometry — bounds (16,12,968x52), brand 230px (shield path + wordmark per the screen SVGs), five 92px-pitch/88px nav cells at x=270 y=18 h=40 (active fill #8A621D), master cluster at x=760 (MASTER caption, 30px master-volume knob at (817,44), 112x18 LED meter at (845,34)). Drawn as chassis, not a toolbar; content begins at y=74.
- **Model pages (failures #3/#4/#7)**: full-page world backdrop transcribed from `assets/surfaces/{world}-backdrop-v2.svg` — the exact kente raster cover-graded per tokens.materials.kenteBackdrop (saturation 1.18, contrast 1.16, brightness 0.74 applied as a one-time cached pixel pass — JUCE renders no SVG feColorMatrix, so parsing the SVG would silently DROP the grading), radial vignette (transparent to 35%, black .7 at edge), recessed control surface (#080604 @.93, #8A5B17 2px, inner #E3A92B @.22 at backdrop coords 174,76,652x454 → root 190,150). Knobs: five-control worlds 112px at exactly (340,284)(500,284)(660,284)(420,456)(580,456); MKS six 96px at its listed centers; labels −74 / values +68 from centre. Output meter (830,185,54x330) per the screen (single #7EE126 bar). **DECAYING badge removed from model pages entirely** — sound type appears only as text in the Info sub-tab. MKS `chorusMode` renders as a knob over its 3 choices (screens show 6 knobs; value readout shows I/II/I+II).
- **Arrangement of build-spec functions the V2 screens do not place** (kept per spec, visually subordinate): sub-tabs became MAIN / AMP ENV / EQ / FX / SETTINGS / INFO in a discreet 20px row at y=682; non-MAIN tabs and the preset browser open as an opaque plaque overlay on the recessed surface, so the MAIN view stays pixel-pristine. The per-slot FX rack is the FX tab; the browser opens by clicking the live patch title under the world name (prev/next/favorite sit beside it). On PERFORM, small PRESETS / MASTER FX buttons in the top corners open the same overlays (performance presets, post-mix master rack). Master volume+meter live in the header per the pack.
- **Engravings (failure #6)**: switched to `engraved-mark-{flow,knot,orbit,petal,weave}.svg`; all identifiers/comments renamed to the **JM engraved motif family** per tokens.symbolPolicy (enum `ux::Motif`, slider property "jmMotif"; old V1 mark files dropped from the build). Assignment: model hero knobs in control order flow→petal→knot→orbit→weave (the screens' inline motifs are simplified variants, so the shipped file order is a judgment call); macros use the same sequence. Still swappable per-control.
- **PERFORM = full central page (failure #8)**: right dock deleted. Exact 2x2 grid — A(94,116) B(512,116) C(94,264) D(512,264), 390x132 slots per the perform screen (accent border, kente panel raster @.40 + #050505 @.42, letter/power/world+patch/VOL/PAN/output-status only — the per-slot convenience menu was removed to keep slots to build-spec functions). SOLO/POLY at (236,412,184x46) and transpose at (480,395,284x78) per the screen (semitones by clicking −/+, octaves via shift/ctrl-click, both ranges preserved and captioned). Four 116px macros at x=230/410/590/770, y=594 with learn/targets kept as a small row under each value.
- **FX modules (failure #9)**: `fx-module-*-v2.svg` transcribed — #050505 outer with 8px #090909 frame, color→#120D08 plate, **kente microtexture intentionally visible** (rotated-45° 24px weave tile at tokens opacity 0.18; soft-light approximated as low-alpha normal blend — no blend modes in the JUCE renderer), gold .42 plate stroke, inner #FFE08A .2 line; unit label sits on an opaque #0B0A08 plaque. Personality mapping unchanged from V1 (logged above). Six active units max, 12 spec units only.
- **Lighting & states (failure #5)**: tokens.colors adopted wholesale in Theme.h (goldBright #FFD77A, goldHot #FFB11B, goldDark #6B4612, cream #F4EAD5, muted #A59A87, chassis #090908, panel #100E0B, V2 world accents). V2 knob transcribed from the screen SVG knob groups: accent glow ring r63 @.35, #FFF0A1→#FFBE2E→#784500 gold rim, warm radial face, soft offset drop shadow, #FFD77A indicator (only element that rotates; shell + engraving fixed). States implemented: disabled = opacity .38 no glow (saturation .55 approximated by the alpha dim — no per-component saturation filter in JUCE); hover = gold .18 face highlight + brighter ring; automation moves values only (no recolor). Pressed-1px and focus-outline states are not yet wired (no keyboard focus traversal in this UI) — noted as the remaining delta from the state spec.
- Z-order per tokens.zOrder (chassis → kente → vignette → recessed surface → shadows → hardware → labels → focus); overlays hide underlying hardware entirely rather than dimming it.
- Removed with the V2 pass: right Perform dock, status bar, on-screen keyboard button (not a build-spec function; MidiKeyboardState remains in the processor), DECAYING badge, V1 macro ring overlays (V2 knob ring replaces them), V1 logo/engraving string assets.
- **Acceptance self-check**: new `JMSignatureSnapshot` console target renders the pages offscreen at exactly 1000x720, measures every hero-control centre against specs/screen-layouts.json, verifies header/content constants, 3-over-2, PERFORM 2x2 bounds, absence of DECAYING, and writes PNG snapshots (build/snapshots/). Reference-screen SVGs could not be rasterized headlessly here (they reference sibling asset files + data URIs; no external SVG rasterizer available), so the overlay comparison is done against the machine-readable coordinates those SVGs are generated from — measured offsets reported below.
- One more legibility call: at (500,137) the live patch subtitle sat directly on an undimmed gold band of the rhodes cloth (above the recessed surface). Added the polished-reference's dark title plaque (352,92,296x54) behind the title block — it doubles as the click affordance for the preset browser. Title/patch text never sits on raw pattern.
- Standalone note: the in-progress Release relink failed once with LNK1104 because the user's running (pre-V2) standalone locked the exe; that stale instance was terminated to complete the link (coordinator relaunches). The new standalone was NOT launched.

## Client review round 4 — MIDI routing, pitch bend + wheels, button shape, macro chips (2026-08-28)

Four items (1-4). Item 5 (MOD MATRIX / LFO / ARP) is explicitly **not started** — those
bottom-bar plates are still inert placeholders, out of `JM-BUILD-SPEC.md` scope and under
separate discussion with the client. Behaviour outside items 1-4 is unchanged.

### 1. Multi-MIDI input routing — "each MIDI input triggers a different module"
- **One model, two feeds** (`src/engine/MidiRouting.h`). The common currency is a 4-bit
  **slot mask** (bit i = Perform slot A..D) that every MIDI message carries into the engine:
  - **Plugin (VST3/AU)** — the host delivers ONE merged stream with no device identity, so
    the meaningful routing is **MIDI channel -> slot mask** (16 atomic masks). Omni is the
    default (0b1111 on every channel), which is byte-for-byte the pre-round-4 behaviour.
  - **Standalone** — the useful question is "which **device** plays which slot". JUCE's
    `StandalonePluginHolder` *can* hold several inputs open, but it merges them all into a
    single buffer through one `MidiMessageCollector` and **throws the device identity away**,
    so it cannot answer that question at all. We therefore open the devices ourselves
    (`MidiDeviceRouter`, one `juce::MidiInput` per enabled device) and stamp each message
    with that device's mask on the MIDI thread.
  - The two **compose**: effective mask = `deviceMask AND channelMask[channel]`. A plugin
    build never constructs a `MidiDeviceRouter` — a plugin must not grab MIDI hardware.
- **Double-trigger avoided**: the holder auto-enables every MIDI input it finds (see its
  `timerCallback`), so before/while owning a device we call
  `holder->deviceManager.setMidiInputDeviceEnabled(id, false)` through
  `MidiDeviceRouter::releaseFromHolderHook()`, installed by `StandaloneApp.cpp`. The hook
  runs on **every** sync, not only on first open, because hot-plug makes the holder re-enable
  inputs. Devices the user has NOT enabled in our panel keep flowing through the holder as
  before, so the default standalone experience is unchanged.
- **Thread-safety law**: the MIDI input thread does an atomic mask read plus a lock-free push
  into `RoutedMidiQueue` (a fixed 1024-slot `AbstractFifo` of 5-byte PODs — no `MidiMessage`,
  no allocation). Producers take a spin flag among themselves; the audio thread never does.
  The audio thread drains the queue at block start and otherwise reads only
  `std::atomic<uint8>` masks. `refreshMidiDevices()` bounces to the message thread if a host
  calls `setStateInformation` from a loader thread.
- **Filtering is applied once**, in `handleRoutedMessage()`, so note-ons, note-offs, CC64
  sustain, CC1 mod wheel and pitch bend all honour the same mask and §5.4 cannot leak across
  slots. Judgment calls: **all-notes-off / all-sound-off ignore the mask** (a stuck note must
  always be clearable), and **CC11 expression stays global** because there is one master gain
  — it is applied whenever the message reaches any slot at all. Changing routing while notes
  are held can orphan a note-off: the same standard caveat already logged for transpose.
- The **on-screen keyboard and wheels** are part of the instrument, not an external source:
  they push pre-resolved omni events (`resolved = 1`) into the routed queue instead of the
  host buffer, so a "channel 1 -> slot A" routing cannot silently mute the UI keyboard.
- **UI**: the gear's Preferences window (now 760x640) grew a MIDI ROUTING section — a
  scrolling table of rows, each with an A/B/C/D cell group lit in the slot's world accent.
  Standalone shows a device row per input (with an ON plate) above the 16 channel rows;
  plugin builds show the channel table alone, with a line saying why. Shortcuts: **ALL -> ALL**,
  **CH 1-4 -> A-D**, **RESCAN MIDI**. Pitch-bend range sits in the same window.
- **Persistence**: `midiRouting` (16 channel masks + a device table keyed by JUCE device
  identifier, so routing survives re-plugging) is written into the plugin state JSON.

### 2. Octave beside transpose, pitch bend + mod wheel on the keyboard
- The V2 pack hid octave behind a shift-click on the transpose stepper. `TransposeControl` is
  now a reusable **`StepperControl`** placed twice — **TRANSPOSE** (semitones, ±12) and
  **OCTAVE** (±2) — side by side, each captioned, each with its own end plates, chevrons and
  mouse-wheel nudge. The `transpose` / `octave` parameters are untouched, so automation,
  MIDI learn and saved state behave exactly as before. Layout constants moved in `Theme::L`
  (dock: mode 214 / trans 190 / oct 136; PERFORM centre: 290 / 300 / 204).
- **The keyboard grew its wheels** (`KeyboardStrip`, `PerformanceWheel`): a spring-return
  **PITCH** wheel (it glides home over a few frames rather than snapping, which is what a
  physical wheel does) and a **MOD** wheel that stays put. Both emit **real MIDI**
  (`pitchWheel` / CC1) into the routed queue, so nothing about them is a special case: CC1
  keeps its per-world pre-mapping from §5.3 (Rhodes tremolo depth / MKS chorus depth / FM
  brightness / Strings swell) with no extra code.
- **Pitch bend is implemented** (it was deliberately omitted in v1 — see "Engine" above,
  now superseded). `SamplerEngine::setPitchBend(semitones)` stores one atomic; `render()`
  does ONE block-rate one-pole smoothing step (tau 12 ms) and hands every voice a per-sample
  **linear ramp** of the rate multiplier, so a sweep glides continuously instead of stepping
  at block boundaries and the cost is one multiply-add per sample regardless of voice count.
  Applied as `pos += ratio * bend`, i.e. a true playback-rate change on sounding voices.
- **Range is configurable** — it was cheap: a new `AudioParameterInt "bendRange"` (0..12,
  **default 2**, the standard), edited by a stepper in Preferences and persisted with
  everything else. RPN 0 (a host or controller setting the range over MIDI) is **not**
  parsed; the range is ours. Logged as the remaining delta.

### 3. Keyboard button selection shape — root cause
- The kit's button plate (`button-empty-up.png`, **377x123**) is a **stadium**: its "corners"
  are two semicircular caps of radius = half the art's height. It was being drawn with
  `kit::drawNineSlice`, which is only correct for a rounded *rectangle*. At the bottom bar's
  220x40 cell that meant `capY = 30` of source squeezed into `dcy = 18`, while the art's
  **63 px flat waist was crushed into 4 px** — so each rounded end kept a tall cap profile
  next to a hairline middle and collapsed into a **wedge**. That is the "triangle" the client
  saw. On top of it the engaged state filled `r.reduced(4)` with a **corner-4 rounded
  rectangle**, which does not follow a stadium either.
- Fix: **`kit::drawPillPlate()`** 3-slices the art *horizontally* at ONE uniform vertical
  scale (full source height -> full destination height, only the flat middle stretched), so
  the silhouette is an exact stadium at any width. `kit::pillInner()` / `kit::pillRadius()`
  return the plate's own inner well (measured off the art: frame ~12.2 % of height top and
  bottom, ~16 % at the caps) and its stadium radius, and **every** state — engaged fill,
  engaged rim, hover wash, the no-image fallback — is now drawn on that one geometry.
- **Where else it appeared**: the defect was in `ux::drawPlateButton`, so it affected *every*
  `PlateButton` in the app, not just KEYBOARD — the header **LIMITER**, all five FX
  **BYPASS** plates, **+ ADD UNIT**, **SAVE**, **PRESETS / MASTER FX**, the modal **CLOSE /
  ACTIVATE / CANCEL / BROWSE / RESCAN** plates and the (now removed) macro chips. All are
  fixed by the one change. **FACTORY** in the browser is *not* affected: the USER / FACTORY /
  FAVORITES strip is the Segment register (`drawTabCell`), whose active cell was already a
  rounded rectangle matching its own cell — verified against the snapshot.

### 4. Macro interaction — the chips are gone
- `MacroKnobUnit` no longer owns LEARN and EDIT `PlateButton`s. The 20 px chip row is
  reclaimed and the `Theme::L` macro cell heights drop by exactly 20 (180->160, 142->122)
  with `macroY` unchanged, so **the knob centres do not move**.
- **Right-click the knob** -> styled context menu (`jm::styledMenu`, so it is drawn in the
  CHROME register from round 3): Learn (ticked while armed) · Edit targets... · Rename... ·
  a Targets submenu listing every link by friendly name with a one-click Remove · Clear all
  targets · MIDI Learn / Clear MIDI mapping.
- **Double-click the knob** -> the existing `MacroEditorPanel` (bipolar depth / invert /
  min / max / curve / remove / rename / clear all). To stop the two gestures fighting,
  `ParamKnob::setDoubleClickResets(false)` is called for macro knobs only, so the slider's
  own "double-click returns to default" is off there and nowhere else. Single-click drag
  still turns the macro exactly as before.
- Because there is no LEARN chip to light, the armed state is drawn **on the knob** (gold
  ring + a small LEARN caption) in `paintOverChildren`.
- **Every existing route still works** and is covered by tests: drag-and-drop a parameter's
  gold link tab (or alt-drag) onto a macro, right-click a *parameter* -> "Assign to Macro
  1-4", host automation of `macro1..4`, and MIDI learn on `host.macroN`.

### Testing (round 4)
- `tests/EngineTests.cpp` — **pitch bend**: base ratio 1.0 at the root note, exactly one
  frame per sample with no bend, settled ratio 1.122462 for +2 st, the sounding voice's
  measured position advance over 8 blocks matching that ratio to 2e-3, a monotonic glide
  (never stepping past the target), a downward bend below unity, and an exact return to 1.0
  when the wheel comes back to centre.
- `tests/SnapshotCheck.cpp` — **MIDI routing** through the real processor: Omni reaches all
  four slots; channel 2 routed to B sounds **only** B while unrouted channels still play;
  CC64 sustain is filtered by the same mask and the deferred note-off still works on the
  routed slot; a **device**-stamped event routed to C sounds only C; and channel + device
  routing both survive a `getStateInformation` / `setStateInformation` round trip.
  **Pitch bend** end to end (a real `pitchWheel` message moves slot A's rate to 2^(2/12) and
  back). **Button shape**: the engaged plate is rendered at the real 220x40 bottom-bar size
  twice (on/off) and *differenced*, so the assertions test the engaged state alone — it fills
  the plate's inner well edge to edge, its corners are rounded (not square), and **zero**
  changed pixels fall outside the stadium path. **Macro chips**: zero `PlateButton`s remain
  inside a macro cell. **Steppers**: TRANSPOSE and OCTAVE exist as two non-overlapping,
  same-baseline controls. **Wheels**: pitch springs, mod does not. **Settings**: the routing
  table is present, with a device row per input in the standalone view.
- Round-3 coverage (limiter, macro state round-trip, master section, chrome, letterbox) is
  unchanged and still passing.

## Verification after client review round 4 (2026-08-28)
- Release rebuild (VST3 + Standalone + both console targets): **exit 0**.
- `ctest -C Release`: **100% passed, 2/2** — EngineTests, UiSelfCheck.
- `JMSignatureSnapshot`: **SELF-CHECK PASSED (0 failures)**, worst measured hero offset (0,0) px.
- pluginval 1.3.5 strictness 8 `--skip-gui-tests --validate-in-process` on the VST3: **SUCCESS**.
- New snapshots in `apps/signature/build/snapshots/`: `round4-settings-midi.png` (plugin view),
  `round4-settings-midi-standalone.png` (device -> slot table), `round4-keyboard-wheels.png`
  (transpose + octave, keyboard with PITCH/MOD wheels, KEYBOARD engaged),
  `round4-macro-menu.png` (macro right-click menu over the live window),
  `round4-keyboard-button-engaged.png` / `round4-keyboard-button-idle.png` (the item-3
  before/after pair). The round-2/3 set is regenerated alongside.
- The standalone was **not launched** (per instruction). The Release relink failed once with
  `LNK1104` because the user's running "JM Signature" standalone (PID 24852) held the exe;
  that stale pre-round-4 instance was terminated to complete the link, as in round 2.

## Client review round 3 — chrome, standalone ownership, limiter, macro drag-drop (2026-08-28)

Seven client items (A-G). Behaviour outside these items is unchanged.

### A. One chrome language for menus, search field and dialogs
- **Root cause of "the menus look out of place":** a `juce::PopupMenu`'s window takes its
  LookAndFeel from `PopupMenu::setLookAndFeel()` and, failing that, from the **default**
  LookAndFeel — `Options::withTargetComponent()` does *not* hand it the target's look
  (`juce_PopupMenu.cpp :: findLookAndFeel`). Inside a plugin the default LookAndFeel is
  JUCE's own, so every menu we opened was stock chrome no matter how the editor was
  styled. Every PopupMenu in the plugin now goes through `jm::styledMenu()`
  (`ui/Widgets.h`), which sets the menu's LookAndFeel from the opening component.
- A fourth register — **CHROME** — was added to the `ux::Hardware` family
  (`drawChromePlaque`, `drawChromeWell`, `drawChromeHighlight`, `drawChromeSectionHeader`,
  `drawChromeTick`, `drawChromeChevron`): near-black recessed plaque, hairline gold-dark
  border, soft drop shadow, condensed uppercase section headers with 0.16em tracking,
  cream item text, a **gold highlight row (never the system blue)** and gold ticks.
- `JmLookAndFeel` now overrides `drawPopupMenuBackground`, `drawPopupMenuItem`,
  `drawPopupMenuSectionHeader`, `getIdealPopupMenuItemSize`, `getPopupMenuBorderSize`,
  `drawPopupMenuUpDownArrow`, `fillTextEditorBackground`/`drawTextEditorOutline`,
  `drawLinearSlider` and `drawAlertBox` + the alert-window fonts. The WithOptions
  variants inherit through `LookAndFeel_V2`, so one override covers both.
- Search field: the hand-drawn bed is gone; the recessed well, gold hairline and gold
  focus ring are the LookAndFeel's, the glyph is drawn *inside* the well
  (`paintOverChildren` + `setIndents(30,5)`), and the placeholder is muted.
- Audit: every `PopupMenu` / `ComboBox` / `TextEditor` / `AlertWindow` in `src/` is routed.
  `ComboBox` and `TextEditor` already set their own popup LookAndFeel in JUCE, so they
  inherit ours from the editor. The one untargeted menu (preset-list right-click) is
  targeted now. `FileChooser` remains the OS dialog by design.

### B. "Options" removed, gear icon made live — we own the standalone
- **Judgment call:** we **subclass** `StandaloneFilterWindow` rather than reimplement it
  (`src/plugin/StandaloneApp.cpp`, `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`, source added
  only to the `JMSignature_Standalone` target). JUCE's window carries the plugin holder,
  device manager, state save/restore and the no-device fallback UI; reimplementing all of
  that to delete one button would be a robustness regression. `optionsButton` is private
  but is an ordinary child, so it is found by text in the constructor, hidden and disabled,
  and our `resized()` calls `DocumentWindow::resized()` directly (JUCE's override exists
  only to reposition that button).
- The standalone app sets `JmLookAndFeel` as the **default** LookAndFeel, so JUCE's own
  audio-settings dialog is drawn in the JM language too.
- The header **gear** is now a live `DrawableButton` opening a styled menu: Audio / MIDI
  Devices (standalone only), Preferences..., Rescan Library, Activate licence...
  The standalone installs `SettingsPanel::audioSettingsHook()`, so plugin builds have no
  compile-time dependency on the standalone client; there the gear opens our own
  `SettingsPanel` (library folder + browse + rescan) instead.

### C. Cart -> key, licence dialog — UI SHELL ONLY (scope boundary)
- `assets::Icon::cart` replaced by a new `Icon::key`; the header icon is a live button
  opening `LicensePanel` (key field, ACTIVATE / CANCEL plates, status line). The status
  bar's licence lamp reads the stored key ("LICENSE ACTIVE" / "UNLICENSED"); the
  testdata-fallback notice moved to the right of the bar so both are visible.
- **Scope boundary held:** `JM-BUILD-SPEC.md` section 8 puts copy protection/licensing
  explicitly out of scope and the client said "we'll work more on the license later". This
  build therefore stores the typed key in the existing `settings.json`
  (`Library::setLicenseKey`) and reflects it in the UI — and does **nothing else**. No
  validation, no checksum, no cryptography, no server activation, no obfuscation, no
  anti-tamper, and **nothing in the plugin is gated on the key**. The panel itself says so
  on screen.

### D. Master volume + limiter
- **Limiter — was genuinely dead.** `ui/Perform.h` bound the plate to
  `apvts.getParameter("limiter")`, but `createParameterLayout` never declared it, so the
  button was `setEnabled(false)` forever and no DSP existed. Added an
  `AudioParameterBool "limiter"` (default on) **and** a real master-bus safety limiter
  (`src/engine/Limiter.h`), applied after master volume: soft-knee (3 dB) brick wall at a
  **-0.3 dBFS** ceiling, 2 ms lookahead with a **sliding minimum** of the target gain over
  the lookahead window (monotonic deque, O(1) amortised), a fast attack (tau =
  lookahead/3) and a **program-dependent release** (fast 60 ms / slow 550 ms limbs, minimum
  taken), plus a final ceiling clamp so the output is a true brick wall. The delay line
  runs even when the limiter is switched off, so toggling is click-free and the reported
  latency (`setLatencySamples`) never changes. The build spec does not list a limiter; the
  approved reference UI does — logged here as an addition, scoped as a safety limiter.
- **Master knob — the diagnostic disproved the interception theory.** `getComponentAt()`
  at the rotary's centre returned the slider itself, the slider drove `masterVol`, the knob
  followed host-side changes and `masterPeakL/R` were fed from the real master bus. The
  actual defect was **layout**: `ParamKnob::resized()`'s default branch always removed the
  top 15 px for its caption *even when the caption is empty*, so the header's 56x56 cell
  produced a **41 px rotary whose centre sat 7 px below `Theme::L::masterKnobCy`**, with
  only ~35 px of drawn hardware — a small, mis-centred target sitting low inside the
  MASTER plaque, which reads (and grabs) as dead. Fixes: the empty-caption case no longer
  reserves that strip (this also enlarges the slot VOL/PAN knobs), and the master knob is
  placed with explicit `setV2()` geometry so component centre == knob centre == layout
  centre. Verified: rotary 51x51 centred on (1262,43), exactly the layout centre.
- Master volume was also applied as a **per-block constant**, stepping the gain at every
  block boundary; it is now a `SmoothedValue` (20 ms), so knob moves and automation are
  zipper-free.

### E. Maximise
- With the window ours, we request `minimiseButton | maximiseButton | closeButton`.
- The editor's **fixed-aspect constrainer was removed** — it would have prevented a
  maximised window from filling the screen. The law that replaces it is the root scaling
  law that was already there: min(w/1536, h/1024), uniform, centred, remainder painted in
  chassis colour. Resize limits widened to 7680x4320 so large displays are reachable.
  The self-check now asserts uniform scale, symmetric letterbox and containment at three
  window shapes (native, ultrawide, tall) instead of asserting a locked ratio.

### F. Drag-and-drop macro assignment
- **References consulted before designing:** Serum, Vital and Arturia Pigments all drag a
  *modulation source* onto a parameter; Bitwig drags into a modulator's target field. All
  of them show a name-carrying ghost under the cursor and highlight every legal drop target
  while a drag is live, and all of them put depth editing on the source. The client asked
  for the **opposite direction** (parameter -> macro), so we keep that feedback vocabulary
  and reverse only the direction.
- The editor root is the `DragAndDropContainer`; `MacroKnobUnit` is the `DragAndDropTarget`.
- **Affordance:** a small gold **link tab** on the knob's north-west shoulder, revealed on
  hover and left lit in the owning macro's accent colour while the parameter is linked
  (the Pigments "handle appears on hover" idiom). It is a separate child component, so it
  can never fight the knob's own value drag; **alt-drag** anywhere on the knob is the
  power-user shortcut, and a click on the tab opens the assignment menu.
- Drag ghost = a chrome plaque carrying the parameter's friendly name. Macro knobs light
  up (soft gold wash + ring) while any parameter drag is live, and brighten with a "LINK"
  caption when hovered.
- Dropping creates a link at **min 0, max 1, depth +1, linear curve** — the parameter's
  full range at full positive depth. Dropping the same parameter twice is a no-op.
- Three routes preserved: **drag**, the original **LEARN** flow, and **right-click ->
  "Assign to Macro 1-4"** (the old nested "Macro target" submenu was flattened).
- **Macro editor popup** (`MacroEditorPanel`): opened from the macro's EDIT chip or by
  right-clicking the macro knob (a left click still turns it — logged as the reading of
  "clicking a macro opens a popup editor" that does not break the control). Lists every
  target with its friendly name (`paramDisplayName` now renders "A RHODES/EP - Bark"
  instead of a router path), a **bipolar DEPTH** slider (-100..+100 %), an INV toggle,
  MIN/MAX range sliders, a CURVE box (LINEAR / SOFT / HARD / STEEP) and REMOVE, plus macro
  rename and CLEAR ALL. Every edit calls `applyMacro()` immediately, so targets update live.
- `MacroTarget` gained `depth` (bipolar) and `invert`. Semantics: |depth| is how much of
  [min,max] the macro covers, the sign chooses whether the macro sweeps **up from the low
  end or down from the high end**, and `invert` mirrors the macro's own travel. Round-2
  state expressed inversion as max < min; that is migrated on load to depth = -1 with the
  endpoints swapped, so old sessions keep behaving the same.
- Serialization: `depth` and `invert` are written by `macrosToVar()`, so links survive
  plugin state **and** performance presets exactly as before. Macros remain APVTS
  parameters, so host automation and MIDI learn are untouched (section 5.3 unaffected).
- Structural edits (REMOVE, CLOSE) are deferred through `MessageManager::callAsync`,
  because they delete the button that is mid-callback.

### G. Polish against the approved reference
- **Nav tabs:** round 2 weighted the cells by label length and gave the remainder to
  PERFORM, so FM/STRINGS crowded while RHODES/EP and PERFORM sprawled. The five cells are
  now **evenly distributed** with cumulative rounding (widths 121-122 px, max spread 1 px).
  `Theme::L::navCellW` is left in place but no longer used by the header.
- **Kente surround:** the round-2 treatment stacked a flat 0.34 black scrim on a vignette
  reaching 0.80 black at the frame, which killed exactly the band that is still on show.
  The scrim is down to 0.12, the vignette to 0.30 starting at 70 % radius, and a warm
  `masterCloth` rust wash at 0.24 lifts the cloth back to the reference's rich rust frame.
  The gold frame line went 0.55 -> 0.62 alpha and 1.5 -> 1.8 px.
- **PERFORM browser was empty** because the spec's factory performance-preset folder was
  never created. `ensureFactoryPerformancePresets()` writes four on first run (Stage Stack,
  Solo Rhodes, EP And Strings, Wide Keys) into
  `~/Documents/JM Signature/Presets/Performance/Factory`. They carry only the parameters
  they mean to set, and `loadPerformancePreset()` now skips absent sections instead of
  clearing the master rack and MIDI map.

### Testing
- The leftover master-knob diagnostic block was converted into permanent assertions in
  `tests/SnapshotCheck.cpp` (rotary centred on the layout centre, at least the token
  diameter, receives the mouse at its centre, drives and follows `masterVol`, meter fed
  from real audio, limiter parameter present, LIMITER plate enabled). The printf
  exploration (`getComponentAt` chain dumps, component enumeration) was dropped.
- New coverage: **limiter** (no overshoot above the ceiling on a +12 dBFS sine, a lone
  6.0 spike caught by the lookahead, no NaN/denormal residue after a long decay, click-free
  across a 20 dB level step taken at a zero crossing, bypassed-but-still-delayed) and
  **macro serialization round-trip** (path + min/max + curve + depth + invert survive
  save -> load, restored links produce identical output, legacy max < min still reads as a
  decreasing link).
- `JMSignatureSnapshot` is now registered with CTest as `UiSelfCheck` — it is a real test,
  not a manual tool.

## Verification after client review round 3 (2026-08-28)
- Release rebuild (VST3 + Standalone + both console targets): exit 0.
- `ctest -C Release`: **100% passed, 2/2** — EngineTests (0.70 s), UiSelfCheck (9.10 s).
- `JMSignatureSnapshot`: **SELF-CHECK PASSED (0 failures)**, worst measured hero offset (0,0) px.
- pluginval 1.3.5 strictness 8 `--skip-gui-tests` on the VST3: **SUCCESS**.
- Snapshots regenerated in `apps/signature/build/snapshots/`: `round3-full-rhodes.png`,
  `round3-full-perform.png`, `round3-menu-open.png` (gear menu over the live window),
  `round3-chrome-menu.png` (menu + search-field register sheet), `round3-macro-editor.png`,
  `round3-licence.png`, `round3-settings.png`, plus the round-2 set.
- Standalone deliberately **not launched** (per instruction); the window/maximise changes
  are compile- and code-verified only.

## Client review round 2 — arrangement restore + hardware kit (2026-08-27)
UI-only pass. No DSP, engine, parameter or feature change; JM-BUILD-SPEC.md remains the sole
behaviour authority. **Authority resolution**: where the client's review conflicts with
`docs/svg-pack-v2/CLAUDE-IMPLEMENTATION-V2.md`, the CLIENT WINS on (a) overall arrangement and
(b) canvas aspect ratio. Everything else in V2 still stands and is retained: colour tokens,
kente materials/grading, state rules, z-order, hero-knob sizing, the JM engraved motif family
naming policy, live text only, cached drawables.

### Task 1 — approved full-window arrangement restored
- **Root is 1536x1024 (3:2)** — the approved reference image's own size, so its coordinates could
  be read off directly. V2's root discipline is kept verbatim: ONE root coordinate system, whole
  root scaled by `min(w/1536, h/1024)` through a single AffineTransform, centred, chassis-coloured
  letterbox, fixed aspect, limits 1152x768–3072x2048. No reflow, no independent stretch.
- The V2 "one page at a time" model is gone. Every approved region is on screen simultaneously:
  header · left preset browser column · centre model stage · right PERFORM dock · bottom FX rack
  strip · bottom/status bar. All band geometry lives in the new `Theme::L` block.
- Recovered from the pre-V2 commit `2376b5a` rather than rewritten: `PerformDock`, `SlotStrip`,
  `TransposeControl`, `MacroKnobUnit`, `TopBar`→`HeaderBar`, `StatusBar`→`BottomBar`, and the full
  preset-browser column. V2 refinements were ported forward onto them (tokens, state rules,
  motif naming, graded kente).
- Restored elements: JM shield + wordmark, cart + gear icons, MASTER knob + horizontal LED meter +
  LIMITER, the browser's JM box/USB brand-art panel, the model page's title/patch row with prev/next
  and favourite heart, the OUTPUT meter along the panel's right edge, the FX rack's left icon rail,
  page dots and MASTER FX column, and the KEYBOARD/MOD MATRIX/LFO/ARP bottom bar with version,
  license and CPU.
- Sub-tabs went back to the reference's five (MAIN · AMP ENV · EQ · SETTINGS · INFO). The V2-only
  "FX" sub-tab was dropped because the slot's FX rack is now permanently on screen in the bottom
  strip — no function lost, one less indirection.
- **LIMITER, MOD MATRIX, LFO and ARP are rendered disabled placeholders** (no `limiter` host
  parameter exists and those three are out of build-spec scope). Same call as v1; drawing them live
  would have meant adding function.

### Task 2 — PERFORM: slots move to the centre
- Selecting PERFORM hides the right dock and widens the centre stage to `centerW + dockW`
  (1254x584). The four slots occupy it as a 2x2 grid — A/B top row, C/D second row — with
  SOLO/POLY and TRANSPOSE beneath and the four macros under those. Browser, FX rack, bottom bar
  and status bar all stay exactly where they are.
- `SlotStrip` gained two layouts over one behaviour set: `Row` (dock, 550x78) and `Card`
  (centre grid, 590x132). Both expose the same build-spec functions; nothing was added or removed.

### Task 3 — the "matured" tab/button language (`src/ui/Hardware.h`)
The approved reference's sub-tab strip was reproduced as the single button language and applied to
**every** segmented/tabbed/plate surface: nav tabs, model sub-tabs, USER/FACTORY/FAVORITES, FX
BYPASS, bottom-bar buttons, LIMITER, macro LEARN/target chips, SAVE, BROWSE/RESCAN, + ADD UNIT,
SOLO/POLY and TRANSPOSE housings. Three registers, one family:
- **Segment** (`SegmentStrip` + `TabCellButton`): dark recessed plaque (vertical #141210→#0B0A09,
  gold #6E5320 hairline, soft outer shadow, inner recess line), cells divided by #2C2823 hairlines.
  ACTIVE cell is lifted (#402D0F→#1A1208), carries a 2px #FFC24A **top-edge line**, a soft #FFB11B
  underline, a subtle radial gold inner glow and a #FFD77A label; inactive cells are muted cream
  #C9C2B4. Condensed uppercase with 0.04em tracking throughout.
- **Plate** (`PlateButton` + `JmLookAndFeel::drawButtonBackground`, so plain TextButtons inherit it):
  the kit's `button-empty-up.png` / `button-empty-down.png` plaque, nine-sliced. **This finally
  wires the pressed state** (down plaque + 1px content offset + inner shadow) that V2 logged as
  missing. Engaged/toggled = the reference's filled-gold pill (#F4C95A→#B9822A, dark label).
- Below ~26px tall the kit plaque's bevel reads as a lozenge, so small chips (macro LEARN/target)
  drop to a flat member of the same family — identical palette, stroke weight and type, no bevel.
- V2 state tokens are now all live: disabled .38, hover gold .18, pressed 1px + inner shadow, and
  the previously-unwired **focus 2px #FFD77A** outline on tabs, plates and knobs.

### Task 4 — photoreal hardware kit (`docs/hardware-kit` → `src/ui/Kit.h`)
19 PNGs vendored to `apps/signature/resources/kit/` and embedded through the existing `JMAssets`
`juce_add_binary_data` target (~4.3 MB). All decoding is cached; nothing is decoded per repaint.
- **Knobs**: ONE image per size, rotated −135°→+135° (the kit's recommended approach), replacing the
  V2 drawn knob. The crops are not square, so the rotation axis is derived at runtime by
  `kit::optical()` from the **binary alpha silhouette** (alpha > 128, which excludes the soft drop
  shadow) and its area centroid — for a disc with an evenly distributed stud bezel that is the true
  axis. Measured: knob-large 278x283 → optical centre (145.1, 168.0), span 270 (the pixel centre
  (139.0, 141.5) is 26px out — rotating about it would visibly orbit); knob-small 217x220 →
  (108.6, 115.4), span 208. Verified visually at 0 % / 50 % / 100 % against cross-hairs in the new
  `round2-knob-rotation.png` snapshot. Value arc + numeric readout kept per the design law (arc at
  r = 0.545·d behind the hardware, in the world/macro accent); the engraved JM motif is composited
  into the face centre and **does not rotate**.
- **FX faceplates**: `panel-{teal,orange,purple,blue,red,green}.png` replace the V2 SVG plates
  (they already carry screws, bevel and depth). The rack cells are laid out at the kit's native
  **0.764:1** ratio (164x215) so the art is never stretched non-uniformly — asserted in the
  self-check. The FX personality→colour mapping is unchanged from V1/V2.
- **Meters**: `meter-led-left/right.png` for slot, master and output metering, **masked by level**
  (clip, not tint) over a recessed bed, with a 0.14 unlit ghost. The header's horizontal master
  meter is the same column rotated +90° (source bottom → destination left, so green leads).
- `panel-master-empty.png` (MASTER FX column), `button-master-empty.png` (browser brand-art panel),
  `screw-00.png` (recessed-surface corners), `icon-power.png` (slot + FX power buttons),
  `rack-corner.png` (two rack corners), `divider-gold.png` (dock title rule) and
  `flourish-gold.png` — the flourish used **once**, as a single accent in the brand-art panel,
  never as repeated wallpaper.
- Kente is retained exactly where V2 put it — the world backdrops and the Perform slot strips — plus
  micro-weave at 10–12 % on the browser/dock/rack chassis. Where kente and a kit asset both applied
  (the FX plates) the kit's photoreal art wins, per the brief.
- **Typography**: `Theme::faceDisplay()` resolves once against the installed typefaces, preferring
  Bahnschrift Condensed SemiBold and falling back through Bahnschrift / Oswald SemiBold / Barlow
  Condensed SemiBold / Arial Narrow. Tracking 0.04em; live text only.

### Kit assets that could not be used as shipped
- `knob-small-00.png` (215x185) ships **clipped**: the top of the bezel is cut off and a stray gold
  arc from the neighbouring knob bleeds in — visible as an orbiting artefact once rotated. Fallback:
  **recropped from the kit's own `controls-sheet.png`** (row 3, column 1; alpha bbox 73..277 x
  691..898, crop 67,685,217x220), which the README explicitly ships the sheets for. The shipped
  crop is not used.
- `panel-master-empty.png` is **1230x395 (3.11:1)**, not the 1600x400 (4:1) the README states, and
  the MASTER FX column in the approved reference is a tall 196x215 panel. Fallback: nine-sliced
  (fixed corner caps carrying the screws and bevel, stretched edges) rather than scaled — the
  corner screws and border weight survive at any aspect. Same treatment for the button plaques,
  whose 3.07:1 native ratio never matches a nav cell or a BYPASS button.
- Only the pointer-up state (`knob-large-00`, recropped `knob-small-00`) is embedded; the other 8+4
  state PNGs are unused by design — the kit names rotation as "the lighter-weight app approach".
- `font-style-preview.png` is a style reference only (live text is required), and the three sheet
  PNGs are used as source material for the recrop, not at runtime.

### CONFLICT FOR THE CLIENT TO RESOLVE — the DECAYING badge
The V2 pack forbids the sound-type badge on model pages ("DECAYING badge removed from model pages
entirely — sound type appears only as text in the Info sub-tab"), but the **approved reference image
shows it** under the SPACE knob. It is **kept hidden for now** (V2 wins, since the client's stated
corrections were arrangement, Perform-slot placement and the button language — not the badge), and
the sound type remains live in the INFO sub-tab. **Flagged for the client to decide**: restoring it
is a one-line change to `ModelPagePanel::paint`.

### Verification after client review round 2 (2026-08-27)
- Reconfigure + full Release build (VST3 + Standalone + tests + snapshot tool): **exit 0**.
- `ctest -C Release`: **100 % passed** (EngineTests) — the engine was not touched.
- pluginval 1.3.5 strictness 8 `--skip-gui-tests`: **SUCCESS (exit 0)**.
- `JMSignatureSnapshot` (rewritten for the restored layout — the old 1000x720 coordinate assertions
  were replaced, the harness kept): **SELF-CHECK PASSED, 0 failures, worst measured hero offset
  (0,0) px**. It now verifies the 19 kit assets decode, both knob optical centres, every hero-knob
  centre on all four model pages against `Theme::L`, the simultaneous presence of browser + centre +
  dock + rack + MASTER FX + bottom bar + header on a model page, the four dock slot rows, the FX
  units' 0.764:1 plate ratio, and on PERFORM that the dock steps aside while the four slots sit on
  the centre stage's 2x2 grid (A/B top, C/D second) with the browser, rack and bottom bar unmoved.
- Snapshots in `apps/signature/build/snapshots/`: `round2-full-rhodes.png`,
  `round2-full-perform.png` (both full-window at reference size, for direct comparison with
  `docs/jm-design-reference/ui-reference/01-approved-ui-look-and-feel-rhodes-page.png`),
  `round2-page-mks-6knob.png` (the 3x2 outlier) and `round2-knob-rotation.png`.
- Standalone deliberately not launched. The Release relink failed once with LNK1104 because the
  user's running "JM Signature" standalone (PID 48076) locked the exe; that instance was terminated
  to complete the link, as briefed.

## Verification after UX pack V2 (2026-08-27)
- Release rebuild (VST3 + Standalone + tests + snapshot tool): exit 0.
- **Acceptance self-check (JMSignatureSnapshot, offscreen 1000x720)**: ALL PASS, `worst measured offset: (0,0) px`.
  - Rhodes/FM/Strings hero centres measured (340,284)(500,284)(660,284)(420,456)(580,456) — offsets (+0,+0) each.
  - MKS six centres measured (320,270)(500,270)(680,270)(320,445)(500,445)(680,445) — offsets (+0,+0) each.
  - Macros measured (230,594)(410,594)(590,594)(770,594) — offsets (+0,+0).
  - Slot bounds A(94,116) B(512,116) C(94,264) D(512,264) at 390x132; mode (236,412,184x46); transpose (480,395,284x78) — exact.
  - Header 52px / content y=74; root 1000x720 fixed 25:18; no DECAYING/SUSTAINING text on any model page; PERFORM central 2x2 confirmed.
  - Snapshots written to `apps/signature/build/snapshots/` (v2-check-rhodes/perform/editor.png) and visually compared against reference/polished-*.png.
- ctest: 100% passed (EngineTests).
- pluginval 1.3.5 strictness 8 `--skip-gui-tests`: SUCCESS (exit 0), repeated after each rebuild.

## Verification after UX pack v1.0.0 (2026-08-27)
- Reconfigure + Release rebuild (VST3 + Standalone + tests, incl. new JMAssets binary-data target): exit 0.
- ctest: 100% passed (EngineTests, 58 checks).
- pluginval 1.3.5 strictness 8 `--skip-gui-tests`: SUCCESS (exit 0).
- Standalone deliberately not launched (user had it open).

## Verification results (2026-08-26, this machine)
- Clean Ninja Release configure + build: success (VST3, Standalone, tests; AU declared, skipped on Windows as expected).
- `ctest`: EngineTests 100% passed (58 checks).
- pluginval 1.3.5 strictness 8 (full run incl. editor tests): **SUCCESS**, twice consecutively. The very first full-GUI run reported one FAILURE with no named failing test; subsequent runs (and the `--skip-gui-tests` run) all pass — treated as a first-run flake (likely editor-open timing during cold bundle preload) and worth a re-check in CI.
- Standalone launches and stays alive (12 s smoke, then killed).

## Client review round 7 — FX rack: off by default, removable, reorderable (2026-08-31)

Client: *"Master effects and effects on the modules should be off by default… also
there's no way to delete effects from rack… and reorder them."* Three changes, one
of them in the shared format package so the two apps cannot drift.

### 88. FX off by default — a DEFAULT, never an override
`buildDefaultRack` (FxDsp.h) now builds every unit **present but bypassed**, mirroring
`DEFAULT_UNIT_BYPASSED` in `packages/jmi-format/src/fx-params.ts`; the C++ side carries
`kDefaultUnitBypassed` and a `defaultUnitState (type, bypassed)` helper so the flag lives
in exactly two places that reference each other by name.

The units are **not removed** — the world's documented rack is still there, in its
documented order with its documented parameters, so the player can see what the
instrument offers and switch it on. Only the sound changed: an instrument loads dry.

**Why this cannot disturb a saved preset.** `bypassed` is a *required* property of every
rack unit in `schema/preset.schema.json`, so no valid preset is silent about it. Every
load path reads the explicit value verbatim and none of them consults the default:
`rackFromVar` (plugin state, performance presets), `rackFromJmiPreset` (.jmi presets),
Capture's `FxRack.load`. A preset written before this change — `"bypassed": false` on
every unit — therefore still loads with its FX **on**, byte for byte the behaviour it had.
Pinned by tests in all three packages, including a mixed rack whose per-unit flags must
survive `false,true,true,false,true` intact.

Scope of the default: it applies wherever a rack is *constructed from defaults* — the four
slot racks in a fresh `AppState`, a bundle loaded without a preset (`PluginProcessor.cpp`),
`Perform.h`'s world switch. Signature's **master rack is empty by default** and always was,
so it is dry for free; what the master menu must not do is build itself a default rack that
is on, and it does not. A unit the player **adds or replaces by hand** arrives **active** —
that is an explicit act, not a default, and a unit that added itself silent would read as
broken.

### 89. Removal: a control on the unit, not a menu entry
Removal existed (kebab → "Remove"), which is why the client could not find it. Each FX
panel now carries a `ux::RemoveChipButton` — the same recessed plate register as BYPASS,
sitting beside it, 18×18 at the unit's native scale, taking `Theme::warnRed` on hover so a
destructive control announces itself. The kebab moved down beside it and keeps every route
it had. Both routes funnel through one `commitStructural()`, so they cannot drift.

That also fixed a latent **use-after-free**: the old menu handler called `onRackEdit()` —
which rebuilds the strip and deletes the panel — and then touched `proc_` through the dead
`this`. `commitStructural` copies the callback out and calls it last, with nothing after it.

### 90. Drag to reorder (spec §4), and how it stays clickless
- **Gesture.** The unit's **header strip** is the handle, marked with a drawn `ux::drawGrip`
  and a dragging-hand cursor. Knobs are child components and consume their own mouse
  events, so a drag begun on a knob never reaches the panel and stays a knob drag; the
  panel arms only from the header, past a 6 px threshold.
- **Ghost.** `fxdrag::makeGhost` snapshots the unit's own faceplate, translucent, at half
  size through `juce::ScaledImage` — the player drags a picture of the thing they grabbed.
- **Indicator.** `FxRackStrip` is the `DragAndDropTarget` (the panels are not, so every
  drag-move lands on the strip). It computes the insertion **gap** from the pointer against
  the unit centres and paints a gold rule with end caps in that gap. Payload
  (`jm.fx.move|<rack>|<index>`) carries the rack prefix, so a unit can only be dropped back
  into the rack it came from — asserted for macro-link drags and cross-rack drags.
- **Real-time safety.** Reordering is *audible*, so it is a two-step message-thread
  operation and the audio thread gains nothing new:
  `FxRackDsp::beginRestructure()` ramps every slot's wet/dry mix to 0 over the same 20 ms
  smoothing the BYPASS button already uses — the rack keeps running the units and keeps
  passing **dry**, so audio never stops and delay/reverb tails stay fed. 25 ms later
  `setState (next, rampIn=true)` swaps the slots *while every one of them is fully dry* and
  ramps the un-bypassed ones back to wet. No allocation, no lock and no file I/O on the
  audio thread; the audio thread still only takes the try-lock it always took. The deferred
  step is guarded by a `std::weak_ptr` lifetime token so a 25 ms timer can never outlive the
  processor.
- **Measured, not asserted by eye** (`EngineTests`, `R7 (3)`): the same rack renders a
  220 Hz tone in each of the two orders undisturbed, and once with the reorder applied
  live. Worst per-sample step across the change **0.022485** against a steady-state
  reference of **0.022591** — i.e. the change is *smoother* than the signal's own slope.
  RMS during the hand-over 0.3495 vs 0.4500 steady: it goes dry, it does not go silent.
- **Persistence** is unchanged because order was always positional: `rackToVar` /
  `rackFromVar`, `slotToVar` / `slotFromVar` and the .jmi preset array all carry it.
  Asserted through preset JSON and through the slot record plugin/performance presets save.

### 91. Known consequence, unchanged from before
Macro links and modulation-matrix rows address FX parameters positionally
(`slot0.fx2.mix`), so reordering or removing a unit re-points them — exactly as the
pre-existing "Move left / Move right" menu items always did. Not touched this round;
naming links by unit identity is a separate change to the saved state.

### 92. Not touched
No `.jmi` schema, parameter id, range or registry change; `FxParams.h`'s unit list and
indices are untouched, as its warning requires. No DSP behaviour, modulation or sampler
change.

### 93. Build / test evidence (2026-08-31)
- Release build (Ninja, MSVC): **exit 0**, VST3 + Standalone + both console targets. One
  relink hit `LNK1104` because the client's standalone was running; the "JM Signature"
  process (PID 31860) was terminated and the build completed — noted as required.
- `ctest -C Release`: **100% passed, 2/2** (EngineTests 2.21 s, UiSelfCheck 15.03 s).
- `JMSignatureTests`: **ALL TESTS PASSED (0 failures)**, including the three new R7 blocks.
- `JMSignatureSnapshot`: **SELF-CHECK PASSED (0 failures)**, including a live click on a
  remove chip (rack 5 → 4, and the DSP rack follows) and a live drop that reorders.
- New snapshots in `apps/signature/build/snapshots/`: `round7-fx-remove-control.png`,
  `round7-fx-rack-strip.png` (the strip alone, legibly) and `round7-fx-reorder-drag.png`
  (ghost, cursor and insertion rule). The whole earlier set regenerated unchanged.
- **pluginval could not be run this round: the binary is no longer present on this
  machine** (searched `C:\`, the user profile, `C:\SDKs`, `C:\JUCE`, `C:\work`,
  `C:\DAW VST`). Downloading it was not done without the user's go-ahead. Nothing in this
  round adds a parameter, changes the parameter layout, or touches `processBlock`'s
  real-time contract, so the previous strictness-8 result should still stand — but it
  needs re-running once the binary is back.
- The standalone was **not launched**.

## Validation and UX standards pass (2026-08-31)

Raising JM Signature to §5 (testing/validation) and §6 (UI/UX conventions) of
`docs/pedal-style-effect-plugin-development-guide.md`. Runs alongside the DSP anti-aliasing
pass, which owns `engine/FxDsp.h`, `engine/Limiter.h`, `engine/Sampler.h`, `engine/Antialias.h`
and `tests/DspStandards.inc`; nothing here touches those files.

### 94. pluginval at strictness 10 — the binary is back, and it passes
The guide calls pluginval "the closest thing the industry has to a certification suite" and
asks for strictness **10** before every release. Round 7 could not run it at all (the binary
had gone missing from this machine) and CI pinned **8**.

- The current release (**pluginval 1.0.4**, Tracktion, built on JUCE 8.0.3 — the newest
  tagged release, 2024-12-04) is downloaded to `tools/pluginval/`, which is **gitignored**:
  it is a build tool, not a source artefact, and CI fetches its own copy.
- `.github/workflows/ci.yml` now runs **strictness 10** in both places, with
  `--timeout-ms 600000 --repeat 2 --randomise`. Level 10 turns on the long tests
  (parameter fuzzing, background-thread state restoration, repeated open/close, the
  non-releasing processing sweep); the 30 s default per-test timeout is not enough for them
  on a shared CI runner, and repeating in a randomised order is what catches order-dependent
  state — the failure mode a single ordered pass cannot see.
- **Result: SUCCESS, nothing flagged.** Run locally against the Release VST3 at strictness
  10, twice, randomised, *including* the GUI tests that CI skips (Editor, Editor Automation,
  Open editor whilst processing). 25 test groups: cold/warm open, plugin state, state
  restoration from a background thread, editor automation, audio processing, non-releasing
  audio processing across 44.1/48/96 kHz × 64…1024, automatable parameters, parameter thread
  safety, the four bus-layout groups and parameter fuzzing. There was nothing to fix.

**Real-time-safety checks.** The guide notes that "newer builds run real-time-safety checks
(flags allocations/locks on the audio thread via sanitizer integration)". 1.0.4 exposes **no
such switch**: its full option list is sample-rates, block-sizes, random-seed, data-file,
strictness-level, timeout-ms, repeat, randomise, skip-gui-tests, disabled-tests,
vst3validator, output-dir/filename and verbose. Those checks live in pluginval builds
compiled *with* a sanitizer, not behind a runtime flag, and Tracktion ships no such binary.
So this is stated rather than claimed: **the RT-safety half of §5 is covered by review and by
the tests below, not by pluginval.** `processBlock` allocates nothing (the slot buffer is
pre-sized in `prepareToPlay`, the UI MIDI buffer is `ensureSize`d, the rack takes a
*try*-lock and passes dry rather than blocking), and the matrix test below re-prepares the
processor 70 times across five rates and seven buffer sizes without a single non-finite
sample.

**`--vst3validator` was not run**: JUCE 8.0.15 vendors a trimmed VST3 SDK with no
`public.sdk/samples/vst-hosting/validator`, so Steinberg's own validator cannot be built from
what this repository has. pluginval reports it as skipped. Adding it means vendoring the full
SDK — a separate decision.

### 95. Null test (§5) — and the one intentional non-null, stated exactly
`tests/ValidationTests.cpp`, CTest target **`NullTest`**. This had never been asserted.

JM Signature is an instrument, so "bypass" cannot mean "pass the input through". The two
things that genuinely can be null-tested are, and both null to **exactly zero** (float
equality, not a tolerance):

1. **The bypassed master limiter is a mathematically pure delay.** White noise in, output
   inverted and summed against the input delayed by `latencySamples()`: residual **0.0** at
   44.1, 48 and 96 kHz, and the priming samples are exactly silent. The lookahead is 2 ms at
   every rate (88 / 96 / 192 samples).
2. **The enabled limiter is transparent below its ceiling.** At −6 dBFS — under the −1.8 dBFS
   point where the 3 dB soft knee starts — limiter ON minus limiter OFF is **0.0**. Above the
   ceiling the two differ and the peak is held at −0.3 dBFS: that difference is the limiter
   working, and is asserted as such.
3. **A bypassed FX unit is bit-transparent.** Three units present, prepared, holding 100 % wet
   and full drive, all bypassed: residual **0.0** against an empty rack. And an empty rack is
   a bit-perfect wire.
4. **The whole plugin nulls on the LIMITER parameter.** A fixed one-note render at −18 dB
   master, limiter off vs on: residual **0.0**.

**THE INTENTIONAL NON-NULL, precisely.** `MasterLimiter::setEnabled(false)` does not stop the
lookahead delay line — the samples still travel through it and come out `latencySamples()`
later. That was the round-6 decision, for two reasons: switching the delay out of circuit
would jump the output 2 ms forward mid-signal (LIMITER would click), and the latency reported
with `setLatencySamples()` must never change while the plugin runs, because hosts recompute
plugin delay compensation on a latency change and several glitch the transport when a plugin
does that from a parameter callback. **So the plugin is not bit-transparent end to end: its
output is always `latencySamples()` behind its MIDI input — 96 samples / 2.00 ms at 48 kHz,
limiter on or off — and that offset is declared to the host rather than removed.** Once the
delay is compensated the bypassed limiter nulls exactly, which is what the first assertion
proves. The test asserts the reported latency equals the limiter's own, so the two can never
drift apart.

### 96. Sample-rate and buffer-size matrix (§4.1) — the whole processor
CTest target **`RateBufferMatrix`**. 44.1 / 48 / 88.2 / 96 / 192 kHz × 32, 64, 128, 256, 512,
1024, 2048 — 35 configurations, each a full `prepareToPlay` + a fixed one-note render through
the processor, engine, racks, limiter and state.

- **Buffer-size independence is bit-exact.** At every one of the five rates, the worst
  deviation of any buffer size from the 512-sample render is **0.0** (reported as −200 dBFS).
  Nothing in the plugin changes with the buffer size.
- **Sample-rate independence** is asserted on the shape, because a sampler cannot be
  sample-identical across rates (the resampling ratio changes): the 16-bucket RMS envelope
  over the note is within **0.65 dB** of the 48 kHz reference at 44.1 kHz, 0.56 dB at
  88.2 kHz, 0.16 dB at 96 kHz and 0.26 dB at 192 kHz.
- Reported latency tracks the rate (2 ms) at all five.
- Plugin state is **byte-identical** across a 192 kHz/32 → 44.1 kHz/2048 re-prepare (§4.2).

**Two findings, both about reproducibility rather than correctness, both left alone and
worked around in the test rather than "fixed" behind the DSP agent's back:**

- `SamplerEngine`'s round-robin counters are reset by `setBundle()`, not by `prepare()`. That
  is *correct* — a host re-preparing mid-session must not rewind a player's round-robin
  position — but it means two "identical" renders are not identical, because the second
  note-on picks the next variant.
- `FxUnit::prepare()` clears delay lines and filter state but does **not** rewind LFO phase,
  so the tremolo / phaser / chorus / wow LFOs free-run across a re-prepare. Harmless in
  performance; it does mean an offline bounce is not byte-reproducible.

The test neutralises both by re-seating the same `BundlePtr` (no disk I/O) and by emptying and
restoring each rack, which destroys and rebuilds the unit instances. With that, two identical
renders are **bit-identical** — asserted, so the matrix numbers above mean what they say.

### 97. Automated audio regression (§5)
CTest target **`AudioRegression`**, reference committed at
`apps/signature/tests/refs/audio-regression.json` (≈4 KB of plain text).

- **Signals**: a 20 Hz → 18 kHz exponential sine sweep, a unit impulse, and white noise from a
  fixed LCG (not `juce::Random` or `std::rand`, so a reference rendered on one machine
  reproduces on every other). 2 s each at 48 kHz, 256-sample blocks.
- **Patch**: a fixed six-unit chain — EP preamp driven hard at 78, vintage EQ, compressor,
  chorus, tape delay and plate reverb — plus the enabled master limiter. Deliberately *not*
  the default rack, whose units are all bypassed (round 7), which would make the reference a
  straight wire. The preamp is the loudest thing in the chain on purpose: it is the nonlinear
  stage, so this is the test that notices when its aliasing behaviour changes.
- **Comparison** is on features, not raw samples, so the reference stays a small reviewable
  text file and a last-bit compiler difference cannot fail the build: 48 RMS time buckets
  (envelope / level / timing), 24 log-spaced spectral bands to Nyquist (tone, filtering and
  **aliasing** — new fold-back energy moves the top bands), plus whole-signal peak and RMS.
  Tolerance **0.5 dB**, with anything under −100 dBFS treated as silence.
- **Re-baselining is a deliberate act.** `JMSignatureValidation --regression --rebaseline`
  rewrites the JSON; nothing does it automatically and CI never passes the flag. The
  committed file then shows up as a reviewable diff in the pull request that changed the tone.
  An accidental change fails loudly instead — every drifted feature is printed with its
  measured and reference value, followed by the re-baseline command.
- It works: the harness caught the parallel DSP pass's anti-aliasing changes on its first run
  (top spectral band +7.8 dB, peak +4.4 dB) before anyone looked at a waveform. The reference
  in this commit is baselined **after** those changes. **If further `FxDsp.h` work lands, the
  re-baseline has to be re-run and the diff reviewed — that is the intended workflow, not a
  defect.**

### 98. Knob and control conventions (§6)
`src/ui/Widgets.h`. Round 2 gave us double-click-to-reset; the rest of the guide's knob
contract lands here. Every gesture is asserted on a real knob inside a real editor by
`JMSignatureSnapshot` (§6 block).

| gesture | behaviour | status |
|---|---|---|
| plain drag | value, **relative** (delta-based) | confirmed, now proven |
| Shift / Ctrl + drag | value at ×0.15 speed | **new** |
| wheel | ±2 % of range per notch | **new** |
| Shift / Ctrl + wheel | ±0.3 % of range | **new** |
| double-click | back to default | round 2, untouched |
| Cmd/Ctrl + click | typed value entry | **new** |
| right-click | MIDI-learn / macro-assign menu | round 3/4, untouched (typed entry added at its top) |
| Alt + drag | drag-to-assign to a macro | round 3, untouched |
| arrows / PgUp / PgDn / Home / End | 1 % / 10 % steps, Shift = 0.2 %, ends | **new** |
| Return | typed value entry | **new** |

- **The drag is relative, and is now re-implemented rather than configured.** It always was
  delta-based (JUCE derives a dragged value from the value at mouse-down plus the pixel
  offset, never from the absolute cursor position), and the test proves it: a 40 px sweep
  gives **+0.2000** whether it starts at (10,60) or (55,12). But the fine-adjust modifier
  cannot be layered on top of JUCE's version — JUCE divides the *total* offset by a fixed
  sensitivity, so changing the sensitivity part-way through a drag rescales everything
  accumulated so far and the knob jumps the moment Shift goes down. `PathSlider` now
  accumulates the per-event delta itself, which is what makes "grab, sweep, hold Shift, trim,
  release" behave the way it does on hardware. It is relative by construction: nothing in the
  drag path ever reads the absolute pointer position.
- **The wheel** was already handled by JUCE, at **15 % of the range per notch** — a shove, not
  an increment. 2 % (0.3 % fine) is what a hardware encoder gives.
- **Typed entry is on Cmd/Ctrl-click, not right-click, on purpose.** Right-click is already
  the MIDI-learn + macro-assign menu on every parameter in the plugin (round 3, item F), and
  that menu is the discoverable route to two features the product sells; it is not ours to
  take. The guide offers Ctrl/Cmd-click as the alternative, so that is the direct route — and
  **"Type value…" is also the first item of that right-click menu**, with the shortcut printed
  beside it, so the feature is discoverable without knowing it. `isCommandDown()` is Cmd on
  macOS and Ctrl on Windows, and on macOS Ctrl-click stays the context menu because
  `isPopupMenu()` is tested first. The click is resolved on mouse **up**, so Ctrl-*drag* on
  Windows is still a fine adjust rather than a mis-fire.
- **The entry box** is a small `juce::TextEditor` over the value readout; the LookAndFeel
  already draws TextEditors in the `ux::Hardware` chrome register, so it needed no new visual
  language. It opens pre-filled and selected, commits on Return, cancels on Escape or focus
  loss, and inverts the parameter's own taper (log or linear) or, for host parameters, uses
  `getValueForText`. Snapshots: `ux-typed-entry.png`, `ux-typed-entry-full.png`,
  `ux-knob-menu.png`.
- **No collisions.** Alt-drag is tested to leave the value untouched (0.5000 after a 20 px
  alt-drag). The double-click reset is tested together with the next pixel of drag, because
  re-anchoring the accumulator after the reset is exactly the bug that implementation could
  introduce. FX drag-to-reorder (round 7) is armed only from a unit's header strip and a knob
  is a child component that consumes its own mouse events, so it cannot be reached.

### 99. Accessibility (§6)
The guide's claim — "JUCE's accessibility APIs expose plugin UI to screen readers" — is only
true for components that are focusable and carry a title. We had neither.

- **`juce::Slider` ships with `setWantsKeyboardFocus(false)`.** Every knob in the plugin was
  therefore unreachable by Tab *and* announced as an unnamed slider. All 37 knobs on a page
  now take focus (the LookAndFeel already drew a focus ring — it was simply unreachable), and
  `PathSlider`'s arrow / page / Home / End keys drive them.
- **Knobs announce a name and a real value.** The slider carries the control's caption, and
  where a knob has no visible caption (the master column, the slot volume/pan controls, which
  the panel around them labels) the name falls back to the parameter router's own display
  name. `textFromValueFunction` reports the panel's formatted readout, so a screen reader says
  *"Slot A Volume, −9.00"* rather than *"0.7727"*. The two duplicate Labels are removed from
  the accessibility tree, as is the hover-only macro link tab (it duplicates a menu item).
- **Host-parameter readouts were unreadable.** `juce::AudioParameterFloat` derives its decimal
  places from its `NormalisableRange` interval, and ours are continuous (interval 0), so the
  stock text was **`-9.0000000`** — on the knob and to the screen reader. Continuous host
  parameters now print to 2 dp; int/bool/choice parameters keep their own text. No parameter
  id, range or default changed.
- **Toggles are operable from the keyboard.** `PillToggle` and `PowerButton` are hand-drawn
  Components, so they were invisible to assistive technology and to Tab. Both now take focus,
  respond to Space/Return, draw a focus ring in the existing `ux::HwState::focus` colour, and
  publish an `AccessibilityHandler` with the `toggleButton` role and toggle/press actions. All
  nine power/enable buttons on a page carry a name (FX unit name, slot letter, arp/LFO
  function).
- **Contrast** was already comfortable and is unchanged: cream `#F4EAD5`, gold `#D9A648` and
  muted `#A59A87` on the `#100E0B`/`#090908` panels all clear WCAG AA for their sizes by a
  wide margin. No colour in `Theme.h` was touched.

### 100. Not touched
No DSP, no parameter id / range / default, no `.jmi` format change, no visual design language
change: `Theme.h` remains the single source of metrics and colours, every new surface is drawn
through `ux::Hardware`, and the one new menu goes through `jm::styledMenu`. The audio thread's
contract is unchanged — nothing added here runs on it.

### 101. Build / test evidence (2026-08-31)
- Release build (Ninja, MSVC 2026): **exit 0** — VST3, Standalone and all four console
  targets. Two intermediate builds hit `C1083`/`LNK1104` on shared build outputs; no
  "JM Signature" process was running (checked) — it was the parallel DSP agent's `ninja`
  building the same `apps/signature/build` directory at the same time, and the build was
  simply re-run once it finished. The standalone was **not launched**.
- New CTest entries out of one binary, so a failure names itself in the CI log:
  `NullTest`, `RateBufferMatrix`, `AudioRegression`.
- `JMSignatureSnapshot`: **SELF-CHECK PASSED (0 failures)** including the new §6 block.
- pluginval **strictness 10: SUCCESS**, quoted above.
