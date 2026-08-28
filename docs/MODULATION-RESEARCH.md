# JM Signature — Modulation Research

Implementation research for the LFO / modulation-matrix / arpeggiator work approved in
`docs/JM-MODULATION-SPEC.html`. This document does not relitigate that spec: global LFOs,
one global arp with per-slot enable, eight matrix rows with the count stored, modulation kept
out of `.jmi` bundle presets, and no mod envelope in v1 are all taken as settled. Everything
here serves those decisions.

Each finding is tagged:

- **[SPEC]** — written in a standard, a vendor manual, or verifiable source code.
- **[COMMON]** — consistent across two or more primary sources, but nobody specifies it.
- **[INFER]** — my reasoning, not documented anywhere. Treat as a design decision, not a convention.

Recommendations are marked **→ JM**.

---

## 0. What the codebase already does (and the one thing that must change)

Read before section 1; several recommendations below only make sense against this.

| Fact | Where |
|---|---|
| `MacroTarget { path, min, max, curve, depth (bipolar), invert }` + `apply(norm)` | `apps/signature/src/engine/AppState.h` |
| Macros are applied on a **30 Hz message-thread timer**, not on the audio thread | `PluginProcessor.cpp` `timerCallback()` → `applyMacro()` |
| `setParamNormalized()` **overwrites the stored parameter value** (`engineParams[id]`, `unit.params[id]`, `ampEnv`) and pushes it to the DSP | `PluginProcessor.cpp` ~L385–470 |
| MIDI reaches the audio thread through a 1024-slot `AbstractFifo` of 5-byte PODs; producers spin among themselves, the audio thread never locks | `engine/MidiRouting.h` `RoutedMidiQueue` |
| Learned CCs are deferred to the same 30 Hz timer; only CC64/CC1/CC11 are handled on the audio thread (atomic writes only) | `DECISIONS-signature.md` L41 |
| Host BPM is cached once per block from `getPlayHead()->getPosition()->getBpm()`; **`ppqPosition` is never read** | `PluginProcessor.cpp` L246–248 |
| FX tempo divisions are beats-per-unit `{0.125, 0.25, 0.375, 0.5, 0.75, 1.0, 1.5, 2.0}` — 1/32 … 1/2 with dotted, **no triplets, nothing above 1/2** | `engine/FxParams.h` `divisionBeats()` |
| RT patterns already in use: `ScopedNoDenormals`, `SmoothedValue` (20 ms) on master gain, one-pole tau 12 ms on pitch bend, 20 ms crossfade on FX bypass, try-lock + dry passthrough for rack rebuilds | `processBlock`, `DECISIONS-signature.md` L36–38, L191 |
| Knobs are drawn with a rotary arc from −135° to +135° at radius `0.545·d`, accent-coloured | `ui/Widgets.h` `drawRotarySlider` |

**The one thing that must change.** The existing macro path is a *destructive parameter writer*
running at 30 Hz on the message thread. That is fine for a knob a human turns; it is unusable for
an LFO, for three independent reasons:

1. 30 Hz is below the Nyquist rate of a 40 Hz LFO, and stepping a parameter 30 times a second
   zippers audibly.
2. It writes the destination's **stored** value, so the player's knob position is destroyed by the
   modulator — directly contradicting the spec's "modulation never overwrites the knob".
3. `setValueNotifyingHost()` on `host.*` paths would write LFO motion into host automation.
   Bitwig has shipped a known VST3 parameter-modulation feedback-loop bug of exactly this shape
   (fixed in guitarix.vst); do not invite it.

**→ JM:** introduce a **base + offset** model. `state` and APVTS keep the *base* value the player
set. A per-destination `modOffset` (normalised, summed from the matrix) is computed on the audio
thread each sub-block and added at the point of use. Macros migrate onto the same path — they
become source #1 rather than a parallel writer — which also removes the 30 Hz latency users can
currently feel on a macro sweep. **[INFER]**, but forced by the spec's own wording.

---

## 1. Modulation matrix conventions

### 1.1 Bipolar vs unipolar, and what "+50% depth" means

There are **two incompatible industry conventions**, and picking one is a real decision.

**School A — "remap on the connection" (Vital, Serum).** All sources are internally unipolar
`0…1`. Bipolar is a per-row toggle that subtracts 0.5. From Vital's
`modulation_connection_processor.cpp` **[SPEC/code]**:

```cpp
poly_float bipolar_offset = -bipolar_->value() * 0.5f;              // 0 or -0.5
dest[i] = (modulation_value + bipolar_offset) * current_amount;     // amount ∈ [-1,+1]
```

i.e. `offset = A · range · f(s)` where `f(s) = s` (unipolar) or `s − 0.5` (bipolar).
**Peak-to-peak swing is `A · range` in both modes**; bipolar just re-centres it.
So at A = +50%: velocity gives `0 … +0.5·range`; a bipolar LFO gives `−0.25·range … +0.25·range`.

Serum agrees and says why, in the manual (p.32): *"You can achieve similar sonic results with
either, it depends on whether you prefer the destination control … be at the beginning
(Unidirectional) or the middle (bi-directional)."* Steve Duda on the Xfer forum: the drag-and-drop
default is chosen by the destination — *"A knob at exactly 50% will be bipolar assigned (e.g. pan
knob) otherwise unipolar is assigned."*

**School B — "polarity belongs to the source" (Pigments, Massive X, Bitwig).** The source itself
emits `−1…+1`, and `offset = ±A·range`, so peak-to-peak is `2A·range` — **twice School A** at the
same depth number. Massive X **[SPEC]**: *"When Bi (Bipolar) is selected, the output range of the
LFO is -100% to +100% … Uni (UniPolar) offers a range of 0 to 100%."* Pigments **[SPEC]**:
*"the modulation range will grow in both directions from the setting of the parameter control."*
Bitwig makes polarity a transfer function on the routing instead — Linear (bipolar), Positives,
Negatives, Absolute, Toward Zero, Exponential, Logarithmic.

**Where this bites us.** `MacroTarget::apply()` is already School-A-with-a-twist: it anchors the
sweep at `lo` for positive depth and `hi` for negative, which is a *unipolar-source* formulation.
Feeding a bipolar LFO through it unchanged would produce a one-sided wobble.

**→ JM: School A, with polarity a property of the source and the row's `min/max` retained as a
clamp window.** Concretely, add one field and one function; leave `MacroTarget` otherwise intact:

```
// s  = raw source output, already normalised
// bipolarSource: LFO/random = true; macro, velocity, mod wheel, key track, AT, expr = false
// depth ∈ [-1,+1], curve, invert as today; min/max become the modulation *window*

float shaped = pow(clamp01(bipolarSource ? s*0.5f + 0.5f : s), max(0.01f, curve));
if (invert) shaped = 1.0f - shaped;
float f = bipolarSource ? (shaped - 0.5f) : shaped;      // f ∈ [-0.5,+0.5] or [0,1]
float offset = depth * (max - min) * f;                  // added to the base value
```

Rationale: it keeps the existing curve/invert/range semantics, keeps peak-to-peak equal to
`|depth|·(max−min)` for every source type, and means swapping velocity for an LFO in a row does not
double the effect. A bipolar LFO at depth +50% over a full-range window swings ±25% of the
parameter — the Vital/Serum number. **[INFER, following School A.]**

Document it in the UI as *"depth = total swing"*, because Massive X/Pigments users will otherwise
expect double.

### 1.2 Combining several rows on one destination

Additive sum, clamped at the destination's own range, is the near-universal default **[COMMON]**.

- **Vital [SPEC/code]:** `SynthModule::createBaseModControl` builds `base + Σ offset_i` via
  `ModulationSum` / `cr::VariableAdd`. Individual amounts are clamped to ±1; the sum is not.
- **Bitwig [SPEC]:** *"you can twist the modulation range past the parameter's normal range, and
  this is correct"* — sum first, clamp at the parameter boundary.
- **Pigments [SPEC]:** *"no limit on the number"* of routings; the manual devotes a whole section
  to displaying combined ranges.
- **Pirkle SynthLab [SPEC]:** `runModMatrix()` *"fetches source value and accumulates with
  destination modulation value"*.

Multiplicative behaviour is offered **per row, opt-in** — Serum's Aux Src (*"The two modulation
sources are 'multiplied' together, so that one is scaling the other"*), Pigments' Sidechain,
u-he's classic Source·Via·Depth, Bitwig's Math modulator and nesting. Nobody makes multiplication
the stacking rule.

One ordering detail worth copying from Vital **[SPEC/code]**: modulations sum in the parameter's
**linear/pre-skew domain**, and the skew/taper is applied afterwards. Our router already has this
shape — `denorm()` applies `logTaper` after the normalised value is decided — so summing in
normalised space and denormalising once is both correct and free.

**→ JM:** sum in normalised space, clamp to `[0,1]` at the end, denormalise through the existing
`denorm()`. No per-row clamp. Ship no multiplicative option in v1 — the spec's eight rows and
four macros do not need it, and Pigments' Sidechain is the model if it is ever asked for.

### 1.3 Offset vs replace, and how to draw it

Unanimous **[SPEC]** across Serum, Vital, Bitwig, Pigments and Massive X: modulation **offsets**
the base value, and the base knob stays live and draggable. Bitwig states the design goal
explicitly: *"It also preserves parameter control as often as possible (so the modulated
parameter's knob can still be used, allowing you to easily shift the modulation range)."*

The display convention is three layers:

1. base value as the pointer / primary arc (what we already draw);
2. a **modulation span** arc in the source's colour, anchored at the base value and extending to
   `base + depth·(max−min)·f_max` (and `f_min` for bipolar);
3. a **live dot or meter** at the currently modulated value.

Serum draws depth as a coloured halo, with a *different hue* for negative depth and grey for
routings belonging to a non-selected source. Pigments adds per-source "pie chart" mini-discs under
the control, one per modulating source, draggable to set depth. Pigments also documents the
failure mode to design against: a mod ring that is lit but dim all the way round, because the base
value pushed the modulation out of range.

**→ JM:** draw the span as a second arc **outside** the existing value arc (`r ≈ 0.545·d`), in the
source's chip colour, with a small live marker. Clip the span visually at the −135°/+135° ends so
the player can see when modulation is being clamped away. Negative depth gets a dimmer variant of
the same colour rather than a new hue — our palette is single-family gold and a second hue would
fight the chrome. Reuse the existing drag-a-parameter-onto-a-row gesture documented at the top of
`ui/Macro.h`; do not add a second assignment idiom.

### 1.4 Cross-modulation and cycles

The spec allows LFO rate and depth as destinations, so a genuine cycle is reachable.

Vital is the only instrument with a documented, readable answer **[SPEC/code]**,
`processor_router.cpp`:

```cpp
if (isDownstream(destination, source->owner)) {
    Feedback* feedback = new cr::Feedback();   // one-block latch
    feedback->plug(source); destination->plug(feedback, index);
} else { reorder(destination); }               // topological re-sort
```

`cr::Feedback` stores the value at the end of a block and publishes it at the start of the next.
So: **fixed topological source ordering per block, with a one-block delay spliced in only where a
real cycle exists.** Vital also hard-blocks the degenerate self-reference — `createConnection()`
refuses `"modulation_N_amount"` as a destination for connection N.

Everyone else is silent or permissive. Massive X actively encourages it (*"try modulating the Rate
with another Switcher LFO"*). Pigments' Sidechain is a sanctioned, structurally acyclic form.
u-he's Urs Heckmann notes their 16–64-sample block granularity is itself the implicit cycle-breaker.

**→ JM:** with three global LFOs and eight rows the graph is tiny. Evaluate sources in a **fixed
order — macros, MIDI/note sources, then LFO 1, 2, 3 — once per control block, before any DSP
consumes them**, using the previous block's value for any backward reference. That is Vital's
Feedback behaviour with none of the machinery. Additionally refuse, in the row editor, a row whose
destination is its own source's rate/depth (Vital's self-reference block). One-block latency at
~1.5 ms is inaudible for LFO-on-LFO. **[INFER, modelled on Vital.]**

### 1.5 Per-block vs per-sample, and where smoothing goes

**Numbers from shipping instruments [SPEC]:**

| Instrument | Control granularity |
|---|---|
| Vital | host buffer split into ≤ **128**-sample chunks; `ModulationConnectionProcessor` is `setControlRate(true)` → ≈ 2.9 ms / 345 Hz at 44.1 kHz worst case |
| u-he (Urs Heckmann, KVR) | **16 / 32 / 48 / 64**-sample blocks, parameters linearly interpolated across the block |
| Pirkle SynthLab | *"usually short, 64 samples/channel or less, to maintain the tactile response of the instrument"*; `runModMatrix()` profiled *"always at the bottom of the CPU percentage list"* |

Vital's anti-zipper strategy is layered and worth copying wholesale **[SPEC/code]**: the modulation
*amount* is linearly ramped across the block (`delta_amount = (target − current)/num_samples`)
even in the audio-rate path; base controls are `SmoothValue`; and changing a destination's scale
resets the ramp to zero rather than stepping.

JUCE specifics **[SPEC]**: `SmoothedValue<float, ValueSmoothingTypes::Linear>` for anything that
must reach zero (gain, pan) — *"when you are using multiplicative smoothing you cannot ever reach a
target value of zero!"* — and `Multiplicative` for cutoff/pitch. The classic misuse, documented on
the JUCE forum, is calling `reset()` every block, which restarts the ramp and reintroduces zipper;
`reset(sr, rampSeconds)` belongs in `prepareToPlay`, `setTargetValue()` per block,
`getNextValue()` per sample.

**→ JM: evaluate the matrix once per 64-sample sub-block** (≈1.45 ms / 689 Hz at 44.1 kHz).
64 matches SynthLab and the low end of u-he's range, comfortably tracks a 40 Hz LFO, and costs
nothing. Then, per destination class:

| Destination | Smoothing |
|---|---|
| Slot/master gain, FX mix, depth-type params | `SmoothedValue<float, Linear>`, 10–20 ms — matches the existing 20 ms master gain |
| Pan | smooth the **pan position**, recompute the equal-power law per sample (smoothing `gl`/`gr` independently breaks the power constraint — note `processBlock` currently computes them per block from a raw atomic) |
| Filter cutoff / FM brightness | Multiplicative (log) smoothing, coefficients recomputed **per sub-block**, ~5–20 ms |
| Pitch / playback ratio | smooth in the **semitone domain** before converting to a ratio, then per-sample increment — exactly the pattern `SamplerEngine::render()` already uses for pitch bend (one-pole tau 12 ms → per-sample linear ramp) |

Do **not** smooth on the message thread, and do not keep the 30 Hz timer in the modulation path.

---

## 2. LFO implementation

### 2.1 Shapes, and a musically useful smooth random

Standard shapes need no research. Pirkle's `LFOCore` is a clean reference and contributes one
detail worth stealing **[SPEC]**: start-phase offsets so the LFO begins at zero output rather than
at −1 — **+0.25 for triangle, +0.5 for ramps**.

Smooth random is where instruments actually differ. Vital ships four random styles
(`kPerlin, kSampleAndHold, kSinInterpolate, kLorenzAttractor`) and the two interpolators are
readable **[SPEC/code]**:

```cpp
// raised-cosine crossfade  (futils.h, "Sine Interpolate")
sin_t = sin(t*0.5f - 0.25f) * 0.5f + 0.5f;
return from + (to - from) * sin_t;

// 1-D gradient (Perlin) noise  (poly_utils.h)
interpolate(from*t, to*(t-1), t*t*(t*-2 + 3)) * 2;   // smoothstep 3t² − 2t³, gradients not values
```

Others: Massive X's Random LFO has a **continuously variable** Shape control that morphs steps into
*"a linear interpolation"* **[SPEC]**. Bitwig's Classic LFO lists *"random, and smoothed random"*
and its Random modulator has a stepped→smooth knob. u-he Hive/Zebra ship `rand hold` (*"stepped
random"*) and `rand glide` (*"random curves"*) plus a separate `SLEW` stage — *"off can produce
clicks"*. musicdsp's "Smooth Random LFO Generator" uses plain linear interpolation with random
dwell times. Sequential's Prophet-6 documents plain S&H: *"Random, also known as 'sample and
hold,' generates a series of random values, each held for the duration of one cycle."*

**→ JM: raised-cosine (Vital's `sinInterpolate`) for Smooth Random, plain S&H for Sample & Hold.**
Raised-cosine is C¹-continuous, monotone between knots, never overshoots ±1 (so it cannot break the
clamp), and costs one `sin`. Cubic/Catmull-Rom overshoots and needs clamping; Perlin-gradient is a
different flavour rather than a better one. If we ever modulate pitch heavily and hear a kink at
the knots, upgrade the easing to Perlin's quintic `6t⁵ − 15t⁴ + 10t³`, which makes the second
derivative continuous. **[INFER on the choice; the formulas are [SPEC].]**

Anti-aliasing: not needed. Our top rate is 40 Hz, three orders of magnitude below Nyquist, and
band-limiting the LFO would not help anyway — modulation generates sidebands beyond the source's
band limit regardless **[COMMON, KVR]**. Rendering per sub-block with a linear ramp across it is
the whole mitigation.

### 2.2 Phase, offset and retrigger for a *global* LFO

Convention, with manual text **[SPEC]**:

| Mode | Documented meaning |
|---|---|
| Free | Omnisphere: *"The LFO will cycle independently, regardless of when a note is triggered."* Serum's Off: *"Note on's are essentially ignored… always synced to your song's time position."* |
| First note / legato | Omnisphere Legato: *"will start its cycle when the first note is played, but will continue its cycle when additional notes are played legato"*. u-he Hive `single`: *"restarted at the next MIDI note after all previous notes have been released."* Prophet-6: *"the LFO wave cycle is reset when you press a key (but is not reset if you press a key while other notes are held)."* |
| Every note | Omnisphere: *"will restart its cycle with every new note that is played."* Serum Trig: *"the LFO will start with a new note."* |

That triple maps exactly onto the spec's Free / First note / Every note. Note that "first note"
is precisely *zero held notes → one*, and it is what the hardware precedent means too.

**Phase offset** is universally a *start phase*, not a running modulation **[SPEC]**: Omnisphere,
*"determines the point at which the LFO waveform will begin its cycle"*; Zebra2, *"meaningless if
Restart is set to free"*.

For a **global** LFO the Zebra convention breaks down — with Free retrigger there is no restart to
offset. Vital solves this by keeping the running phase and the user offset **separate and adding
them only at read time** `phased_offset = mod(current_offset + phase)` **[SPEC/code]**.

**→ JM: keep `phase` (accumulator) and `phaseOffsetDeg` distinct; add at read time,
`p = frac(phase + deg/360)`.** This makes the offset meaningful in *every* retrigger mode
(including Free), makes turning the Phase knob a pure rotation with no drift, and keeps three
LFOs sharing one transport-locked accumulator rigidly related — which is exactly what the spec
asks Phase for ("offsets one LFO against another for pseudo-stereo motion"). Vital's own stereo
convention, `phase + stereoPhase·(+0.5, −0.5)`, splits the offset symmetrically; ours is a
per-LFO absolute offset, which is simpler and matches the spec's wording. **[INFER, following
Vital's separation.]**

Worth knowing: Zebra2's global LFO (LFOG) drops per-note restart entirely and instead offers
*"automatic restart after the specified number of bars (off, or 1 to 32)"* **[SPEC]** — the
genuinely global-native retrigger mode, and a cheap post-v1 addition if Free/First/Every feels thin.

### 2.3 Fade-in (delayed vibrato)

Two conventions, and the common one is not the better one.

**Depth-envelope (hardware and Omnisphere/Zebra/SynthLab) [SPEC]:** the fade multiplies the LFO
output, from 0. DX7 `DELAY` 0–99, *"delay between initial key closure and application of LFO
modulation"*. Juno-106 `DELAY TIME` 0–3 s. Zebra2: *"Delay — Fade-in time. Typically used for
so-called 'delayed vibrato'."* Omnisphere: *"determines how quickly the LFO's depth will reach its
maximum strength after a note has been triggered"* — and, critically,
**"The LFO delay always operates in legato mode"**, i.e. subsequent legato notes do *not* restart it.

**Crossfade-from-start-value (Serum, Vital) [SPEC]:** Serum's Rise: *"the LFO will begin with a
fixed output … and slowly (based on the Rise time) becoming the shape of the visible graph."*
Vital in code: `result = interpolate(start_value, value, fade_amplitude)` where
`start_value = getValueAtPhase(phase)`.

The crossfade form is better for a unipolar source: a depth fade from 0 makes a tremolo or filter
sweep start at "no modulation" (parameter centre) and jump, whereas the crossfade starts at the
LFO's own value at its start phase and has no discontinuity. For a bipolar shape starting at 0 the
two are identical.

**→ JM: crossfade form, retriggered on first note only** (Omnisphere's legato rule), so playing a
held chord does not re-hide the vibrato under the player's hands. Range 0–5 s as specced (Vital
ships 0–8 s fade + 0–4 s delay; our 0–5 s single control is fine). Because our LFOs are global,
"retrigger the fade" means *the transition from zero held notes to one, across all slots* — the
same edge that drives First-note retrigger. **[INFER on the choice; both forms are [SPEC].]**

### 2.4 Tempo sync — the part implementations get wrong

**What JUCE gives us [SPEC].** `AudioPlayHead::getPosition()` returns `Optional<PositionInfo>` and
*"Fetches details about the transport's position at the start of the current processing block …
You can ONLY call this from your processBlock() method!"* `PositionInfo` warns:
*"Not all hosts and plugin formats can provide all of the possible time information … As a plugin
developer, you should code defensively so that the plugin behaves sensibly even when the host fails
to provide timing information."*

- `getPpqPosition()` — *"The current play position, in units of quarter-notes."* Despite the name
  it is **quarter notes and fractions since the timeline origin**, not pulses.
- `getPpqPositionOfLastBarStart()` — *"the start of the last bar, in units of quarter-notes"*
  (documented as unavailable in some hosts, Pro Tools named).
- `getBpm()`, `getIsPlaying()`, `getIsLooping()`, `getLoopPoints()`, `getTimeInSamples()`.

VST3 equivalents **[SPEC]**: `projectTimeMusic` (*"musical position in quarter notes"*),
`barPositionMusic`, `cycleStartMusic`/`cycleEndMusic`, `projectTimeSamples` (*"always valid"*),
`continousTimeSamples` (*"project time, without loop"* — the loop-free counter), flags `kPlaying`,
`kCycleActive`, `kTempoValid`, `kProjectTimeMusicValid`, `kContTimeValid`. **There is no
"the transport jumped" flag in either API.** JUCE does not surface `continousTimeSamples`, so a
JUCE plugin must detect discontinuity by prediction.

**The formula, as shipped in two open-source synths [SPEC/code].**

Surge XT, `LFOModulationSource.cpp`, `lm_freerun`:
```cpp
double timePassed = storage->songpos * storage->temposyncratio_inv * 0.5;  // songpos = ppqPos
float  totalPhase = phaseslider + timePassed * lrate;                       // lrate *= tempo/120
phase = modf(totalPhase, &ipart);
```
When tempo-synced the tempo terms cancel and **phase becomes a pure function of `ppqPosition`** —
which is the entire point of phase locking.

Vital, `synth_plugin.cpp` + `poly_utils.h`: `last_seconds_time_ = ppqPosition / (bpm/60)` then
`phase = frac(freqHz × transportSeconds)`.

Both reduce to the same thing:

```
beatsPerCycle = 4.0 · (numerator/denominator of the division)   // 4/4; ×(4·num/den) for other meters
                × 1.5 if dotted,  × 2/3 if triplet
phase01       = frac(ppqPosition / beatsPerCycle + phaseOffsetDeg/360)
freeRunHz     = (bpm/60) / beatsPerCycle       // only needed when stopped or in standalone
```

Beats per cycle, for the table the spec asks for (8/1 … 1/64):

| Division | Straight | Dotted (×1.5) | Triplet (×2/3) |
|---|---|---|---|
| 8/1 | 32 | 48 | 21.333 |
| 4/1 | 16 | 24 | 10.667 |
| 2/1 | 8 | 12 | 5.333 |
| 1/1 | 4 | 6 | 2.667 |
| 1/2 | 2 | 3 | 1.333 |
| 1/4 | 1 | 1.5 | 0.667 |
| 1/8 | 0.5 | 0.75 | 0.333 |
| 1/16 | 0.25 | 0.375 | 0.1667 |
| 1/32 | 0.125 | 0.1875 | 0.0833 |
| 1/64 | 0.0625 | 0.09375 | 0.041667 |

Note this is a **superset of the FX rack's existing `divisionBeats()`**, which stops at 1/2, starts
at 1/32, and has no triplets. The spec says the LFO divisions should match the FX rack's; strictly
they cannot without extending it.
**→ JM: extend `divisionBeats()` to the full 8/1…1/64 × straight/dotted/triplet table and have both
the FX rack and the LFOs read it, appending new indices so existing FX preset indices 0–7 keep
their meaning.** That is a two-line change now and a migration later.

**Anchor to timeline or to the rate?** Serum exposes the tradeoff and states it best **[SPEC]**:
with Anchor on, *"changing the [rate] from ¼ note to 1 bar means the time position may 'jump' in
order to have the playback properly affixed to the bar cycle"*; with it off, *"the play position
will not jump when you change the LFO Rate, but now may feel rhythmically off-the-beat. This is how
a traditional LFO behaves."*
**→ JM: anchor on, no switch.** A phase-locked LFO that renders identically on every bounce is the
whole reason for the feature; we can add the switch if a sound designer asks.

**Edge cases — the ones that actually break.**

- **Transport stopped.** Surge and Vital do the same thing and it is the right answer: *latch the
  host's ppq only while playing; free-run your own ppq otherwise.* Surge:
  `if (cp.isPlaying || cp.isRecording) time_data.ppqPos = cp.ppqPosition;` then every block
  `ppqPos += BLOCK_SIZE * tempo / (60 * samplerate)`. Net effect: the LFO free-runs at the last
  known tempo when stopped and snaps back into lock the instant playback starts. Standalone falls
  out of the same code path. **[SPEC/code, both.]**
- **Transport jump / locate / loop wrap.** No flag exists. Detect by prediction:
  `predicted = lastPpq + blockBeats; jumped = |ppq − predicted| > max(0.5·blockBeats, 1e-3)`.
  Because the anchored branch is **stateless**, a jump needs no repair at all for the LFO — the
  phase is simply recomputed correctly. Reset the smooth-random held values on a jump so a
  frozen S&H step does not survive a locate. **[INFER; the detection idiom is [COMMON].]**
- **Tempo change mid-block.** `PositionInfo` describes only the block start; a mid-block tempo
  change is invisible. This is the strongest argument for driving phase from `ppq` rather than
  integrating Hz: `ppqPosition` already reflects the host's tempo map, whereas converting
  `ppq → seconds` with the *current* bpm ignores tempo history and drifts across a ramp (a
  documented JUCE-forum bug class). Vital has exactly this bug; Surge does not.
- **Hosts that lie.** Logic has been observed reporting ppq consistently 2048 samples ahead on a
  128-sample buffer, and up to 20 beats ahead, and reports *negative* ppq during count-in.
  **[COMMON, JUCE forum.]** Defences: clamp the block-start ppq against your own prediction; never
  derive per-sample position by differencing consecutive blocks' ppq; keep the last valid bpm
  forever and never fall back to 0.

**Pseudocode.**

```cpp
struct TransportClock {                 // one per processor, updated once per processBlock
    double ppq = 0.0, bpm = 120.0;
    bool   playing = false, jumped = false;

    void update (juce::AudioPlayHead* ph, int numSamples, double sr)
    {
        const double blockBeats = numSamples * (bpm / 60.0) / sr;
        const double predicted  = ppq + blockBeats;
        jumped = false;

        if (ph != nullptr)
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm(); b && *b > 0.0) bpm = *b;   // keep last good bpm
                playing = pos->getIsPlaying();
                if (playing)
                    if (auto q = pos->getPpqPosition())
                    {
                        jumped = std::abs (*q - predicted) > std::max (0.5 * blockBeats, 1.0e-3);
                        ppq = *q;                       // hard lock while playing
                        return;
                    }
            }
        ppq = predicted;                                // stopped / standalone / host gave nothing
    }
};

// per LFO, once per 64-sample sub-block
double p = sync ? std::fmod (clock.ppq / beatsPerCycle, 1.0)
                : (freePhase += freeHz * subBlock / sr, freePhase -= std::floor (freePhase));
p += phaseOffsetDeg / 360.0;  p -= std::floor (p);
double out = shape (p);                                  // then crossfade the fade-in, then ramp
```

### 2.5 Rate range and taper

Shipping ranges **[SPEC]**: Vital `2⁻⁷ … 2⁹` Hz (0.0078–512) on a base-2 exponential, default 2 Hz.
Surge `pow(2.0, rate)`. Massive X *"0.004 Hz to approximately 60 Hz, with center position around
5.3 Hz"*. u-he: a 0.1 s / 1 s / 10 s time base × a bipolar rate where *"integer steps halve or
double the speed"*.

The spec's 0.01–40 Hz with a logarithmic taper is squarely conventional.

**→ JM: store the parameter as log₂(Hz)** — 0.01–40 Hz is `−6.64 … +5.32` — and expose it
linearly, which is what everyone does and makes automation perceptually even. Our router already
supports this shape: `ParamSpec::logTaper` with `denorm()` = `min·pow(max/min, norm)` is exactly
base-agnostic log interpolation, so a `{0.01f, 40.0f, 1.0f, logTaper=true}` spec needs no new code.
Put the knob's centre near 2 Hz (the Vital default) — with the log taper that falls at ≈64% of
travel, so use `NormalisableRange::setSkewForCentre` if the visual centre matters. **[INFER.]**

Worth stealing: Vital's synced list includes a **"Freeze"** entry (ratio 0) that holds the LFO
still — one enum value, occasionally very useful.

---

## 3. Arpeggiator

### 3.1 Note ordering, mode by mode

**The turnaround is the one real fork.** Arturia names both behaviours **[SPEC]** (KeyStep 37
manual p.60): *Inclusive* — *"low to high, then from high to low, repeating the high note and low
note"*; *Exclusive* — *"without repeating the high note and low note"*. Logic ships both as
separate modes in the Environment arpeggiator **[SPEC]**: `Up/Down` — *"highest and lowest notes
repeat"*; `Up/Down2` — *"highest and lowest notes don't repeat"*. Ableton ships all four
permutations (`UpDown`, `DownUp`, `Up & Down`, `Down & Up`), which is itself evidence the
distinction matters. NI and Novation ship one unnamed "Up & Down" and never say which.

Cycle lengths, for `n` held notes **[INFER, arithmetic]**: Up / Down / As Played / Random = `n`;
Up/Down inclusive = `2n`; exclusive = `2n − 2`. **The exclusive case degenerates at `n = 1` (1 step)
and `n = 2` (2 steps) and will divide by zero if not special-cased.**

Other modes, with the documented definitions worth copying **[SPEC]**:

- **As Played** — Ableton `Play Order`: *"arranges notes in the pattern according to the order in
  which they are played"*.
- **Chord** — Ableton `Chord Trigger`: *"repeats the incoming notes as a block chord"*; Logic's
  `All`: *"All notes play at once"*.
- **Random** — Ableton distinguishes three: `Random` (*"a continuously randomized sequence"*),
  `Random Other` (*"will not repeat a given note until all other incoming notes have been used"* —
  a shuffled bag), `Random Once` (one pattern, regenerated when the input changes).

**→ JM:** ship the spec's seven — Up, Down, Up/Down, Down/Up, As Played, Random, Chord — and
**make the Up/Down turnaround an explicit Inclusive/Exclusive toggle rather than two more modes**,
defaulting to **Exclusive**. Exclusive keeps the cycle at `2n−2` and avoids the double-hit on the
top note that reads as a stumble. For Random, use the **shuffled-bag** variant (Ableton's
`Random Other`) — pure random repeats notes and sounds broken on 2- and 3-note chords.
**[INFER on both defaults.]**

### 3.2 Notes added or removed mid-pattern

There is no standard, but the dominant documented behaviour is **re-sort in place, do not restart,
clamp the index**:

- **Yamaha Montage [SPEC]** makes the sorting philosophy an explicit parameter, `KEY MODE`:
  *Sort* — notes sorted low→high regardless of playing order, extra keys folded into the sequence;
  *Thru* — *"notes play in the order they are pressed"* and *"no matter how many notes you add, the
  additional note is simply added to the end of the phrase"*.
- **Logic [SPEC]:** *"As you play additional notes, these are seamlessly added to the arpeggio."*
- **Ableton [SPEC]:** with Hold on, *"hold an initial set of keys and then press additional keys to
  add notes"* — framed as *"gradual buildup and rearrangement of the pattern over time"*.
- **Restart is opt-in and named** — Ableton `Retrigger` (Off / Note / Beat); Korg `Key Sync`
  (*"each time you press a key … the arpeggio notes will restart at the beginning of the pattern"*).

Note the interaction: with **Sort** ordering, adding a low note under a running Up pattern shifts
every index, so a clamped index keeps its *position* but changes its *pitch*. With **As Played**,
appending is index-stable. That is exactly Montage's Sort/Thru distinction.

**→ JM:** keep the held-note set in a sorted (Up/Down/Up-Down modes) or as-played (As Played)
container; on add/remove, re-derive the pattern and `stepIndex %= cycleLength` rather than
resetting to 0. Never restart implicitly. Ship no Retrigger control in v1 — the spec does not ask
for one and Korg's Key Sync semantics fall out of "arp starts at first note" in standalone anyway.
**[INFER on the exact rule; the no-restart principle is [COMMON].]**

### 3.3 Octave expansion

Two genuinely different conventions, and only Bitwig documents both **[SPEC]** (Octave Behavior):

- **Broad** (default) — *"takes each additional octave in sequence, stacking them up"*.
- **Thin** — *"flattens & sorts all notes into one linear shape"* (expand the note set to
  `notes × octaves` first, *then* order it: C E G C' E' G' as one long Up run).
- **1 by 1** — *"executes the full pattern in each octave before moving to the next octave"*
  (whole pattern repeated transposed — the classic hardware behaviour, and what Logic's
  *"The arpeggio can be repeated over 1 to 10 octaves"* implies).

Direction: octaves conventionally extend **upward** from the played notes, even for a Down pattern
**[COMMON]**. NI states the octaves play *"above, below, or both directions depending on your Type
setting"* — no cross-vendor standard name exists.

**→ JM: Thin/flatten as the only behaviour, octaves 1–4 as specced, with the spec's up/down/both
direction switch.** Flattening is what a keyboard player hears as "a wider arpeggio"; 1-by-1
whole-pattern repetition reads as a different, more mechanical effect and would need its own name.
**[INFER.]**

### 3.4 Swing

**Which steps move: unanimous — the even-numbered / offbeat step of each pair is delayed, the odd
step stays put [SPEC].** Logic: Swing *"moves every second note closer to the nearest downbeat"*.
NI: Swing *"delays every second note"*, 0–100%.

**Two incompatible scales, and this is the trap.**

**(a) The MPC scale, 50–75%** — Akai MPC60 Operator's Manual, Roger Linn's original **[SPEC]**:
shuffle is *"a percentage of the odd numbered note lengths to the even numbered note lengths. The
range of values is from 50% to 75%."* **50%** = *"no shuffle effect"*; **66%** = *"a technically
perfect shuffle — the odd numbered notes have a length of twice that of the even numbered notes"*
(exactly 66.667%, triplet feel); **75%** = *"the highest shuffle setting — the odd numbered notes
have a length of three times that of the even"*.

```
delay(even step) = T · (S/100 − 0.5) · 2      // S = 50 → 0 ; 66.667 → T/3 ; 75 → T/2
onset(step k)    = k·T + (k odd ? delay : 0)
```

**(b) The 0–100% "amount" scale** (Logic, NI, most plugins), where the maximum is not standardised.
Mapping: `S_mpc = 50 + 0.25·A`, `delay = T · A/200`, so `A = 66.67` gives triplet feel and
`A = 100` gives `T/2`. **[INFER on the 100% → T/2 endpoint; it matches the MPC's 75% ceiling.]**

The spec's control is **0–75%**, which reads like the MPC scale with 0 substituted for 50. That is
ambiguous and will be implemented wrong.

**→ JM: define the control as 0–75% meaning "extra delay as a percentage of the step",
`delay = T · S/100`, so 0% = straight, 33.3% = triplet feel, 75% = a hard shuffle, and clamp
`delay < T` always.** Label 33% as "triplet" in the UI. If the client prefers the familiar MPC
numbers, relabel the same control 50–87.5% — but pick one and write it in the spec, because
"0–75% swing" currently means three different things across the three scales above.

### 3.5 Gate

Definition is unanimous **[SPEC]**: Ableton — Gate *"determines the length of notes played by
Arpeggiator as a percentage of the current Rate setting"*, and *">100% result in notes that
overlap, creating a legato effect"*. NI: 1–200%.

At exactly 100%, NI is unusually explicit **[SPEC]**: the arp *"sends a MIDI note-off message
before each note and plays back the sequence legato"* — note-off of step *k* precedes note-on of
step *k+1*, zero gap, zero overlap. The alternative "tie" reading (no note-off at all) is used by
some step sequencers and is not what a sampled instrument wants.

No manual specifies a **minimum** gate. NI's floor of 1% is 1.25 ms at 1/16 @ 120 BPM.

**→ JM:** spec range 1–100% is right; do not add >100%, which would force same-pitch overlap
handling in a 64-voice sampler for no musical gain. Use NI's 100% ordering (off strictly before the
next on). Enforce `gateSamples = max(1, round(step · gate))` so a note-off never lands on the same
sample index as its note-on — JUCE `MidiBuffer` events at the same position are delivered in
insertion order, which is fragile, and some receivers drop the note entirely. **[INFER on the
floor; the 100% ordering is [SPEC].]**

### 3.6 Latch, Hold, and the sustain pedal — the part the spec flags

**What is actually specified.** CC 64 is *"Damper Pedal on/off (Sustain)"*, **≤63 off, ≥64 on**
**[SPEC, midi.org]** — which is what `§5.4` of `JM-BUILD-SPEC.md` and `SamplerEngine::sustainPedal()`
already implement. The MIDI 1.0 spec also ties the pedal to note lifetime at the receiver:
All Notes Off (CC 123) *"turns off all notes … If the device's Hold Pedal controller is on, the
notes aren't actually released until the Hold Pedal is turned off"*, whereas All Sound Off (CC 120)
mutes *"regardless of whether the Hold Pedal is on"*.

**The MIDI spec says nothing about arpeggiators.** Pedal-as-arp-latch is entirely an instrument
design convention. Say so in the spec.

**The convention, and the best single citation.** Yamaha, on Montage **[SPEC]**: when Hold is
enabled the arpeggiator maintains which keys receive pressure — *"the sustain pedal does not
sustain the sound, it holds the keys for you."* The naive alternative — passing CC64 through to the
sampler so every arpeggiated step's release tail is sustained — is what users describe as broken;
the Sequential forum wording is *"dissonant mush"*.

**Add vs replace — Logic ships both, and its Latch menu is the best specification of the design
space [SPEC]:**

- **Reset** — *"the first key played clears the currently latched notes"*.
- **Add** — *"Play keys … to add them to the latched arpeggio. You can play the same key multiple
  times and the note repeats the number of times it is struck."*
- **Add Temporarily** — as Add, *"but played notes are added to the latched arpeggio only while
  held"*.

Ableton documents the hybrid, and it is the cleanest rule **[SPEC]**: the pattern continues after
release *"until another key is pressed"*; while **any** original key remains physically held,
new notes are **added**, and re-playing a latched note **removes** it. Sequential calls the same
fork "relatch". Nord solves it by giving latch its own pedal, with sostenuto semantics —
*"only the keys that are pressed down when the pedal is activated will sustain"* — a documented
third variant. Omnisphere stops latch on toggle-off, on replaying the same notes, **or on host
transport stop**.

**→ JM — write this into the spec verbatim:**

1. **While the arp is enabled on any slot, the arp consumes CC64. It is never forwarded to
   `SamplerEngine::sustainPedal()` for arp-driven slots.** For slots with the arp *off*,
   CC64 keeps its existing §5.4 behaviour — those slots receive the notes as played.
2. **CC64 ≥ 64 → latch on.** Keys physically held at that moment enter the latched set, and keys
   played while the pedal is down are **added** to it.
3. **New-phrase rule:** if **all** physical keys are up and the pedal is still down, the next
   note-on **clears the latched set and starts a new chord**. If **at least one** key is still
   down, new notes are **added**. (This is Ableton's documented Hold rule, Sequential's "relatch",
   and Logic's Reset/Add boundary in a single rule — no extra control needed.)
4. **CC64 < 64 → latch off.** Fall back to whatever keys are *physically* held. If none, stop the
   pattern and emit note-offs for the sounding step.
5. The **Hold** control in the spec is the same latch, toggled by hand; pedal and Hold OR together.
6. Transport stop stops latch (Omnisphere's precedent), and emits note-offs in the same block.

Rules 1–4 are **[COMMON]** (Yamaha + Logic + Ableton + Sequential agree); the ORing in rule 5 and
the transport rule are **[INFER]**.

### 3.7 Host sync and sample-accurate scheduling

**Both idioms exist and JUCE uses both, at different layers.** For a note generator the answer is
**timestamped events**: write note-on/off into the output `MidiBuffer` with sample offsets. That is
what JUCE's own `ArpeggiatorPluginDemo` does. **Sub-block splitting** is what `juce::Synthesiser`
does *downstream* to make those timestamps real — Jules (JUCE): *"the audio blocks that are passed
into renderNextBlock() will be split up into smaller blocks that lie between all the incoming midi
messages"*, with `setMinimumRenderingSubdivisionSize` (default 32) batching close events.
**[SPEC.]**

We own both halves. `SamplerEngine` is hand-rolled, not `juce::Synthesiser`, and
`handleMidiAtBlockStart()` currently **discards timestamps entirely** — every event in the block is
applied at sample 0. So arp timing would quantise to the block boundary (5.3 ms at 256 samples,
21 ms at 1024) unless the engine learns to split.

**→ JM: two-layer design.**
- The arp writes into an internal event list with sample offsets.
- `SamplerEngine::render()` gains sub-block splitting at those offsets, with a 32-sample minimum —
  the same figure JUCE uses, and it composes with the 64-sample modulation control block (do the
  modulation update at sub-block boundaries, splitting further only for MIDI).
- This also fixes note timing for *non*-arp play, which currently has the same block quantisation.

**Deriving step positions from ppq:**

```cpp
const double samplesPerQuarter = sr * 60.0 / clock.bpm;
const double stepQuarters      = 4.0 * noteFraction * (dotted ? 1.5 : triplet ? 2.0/3.0 : 1.0);
const double ppqStart          = clock.ppq;
const double ppqEnd            = ppqStart + numSamples / samplesPerQuarter;

for (long k = (long) std::ceil (ppqStart / stepQuarters); ; ++k)
{
    const double swing   = (k & 1) ? stepQuarters * swingPercent / 100.0 : 0.0;
    const double ppqStep = k * stepQuarters + swing;
    if (ppqStep >= ppqEnd) break;
    int offset = (int) ((ppqStep - ppqStart) * samplesPerQuarter);
    offset = juce::jlimit (0, numSamples - 1, offset);
    emitStep (k, offset);
}
```

The `jlimit` is required, not defensive: MIDI events added with a sample number outside
`[0, numSamples-1]` are simply ignored **[COMMON, JUCE forum]**. Note the loop emits **all** steps
falling in the block — JUCE's demo can only emit one per block and drops steps at fast rates with
large buffers.

**Transport handling**, mirroring §2.4: hard-lock to `ppq` while playing, free-run the internal
clock when stopped or in standalone. On a detected jump (loop wrap or locate) emit note-offs for
anything sounding, recompute `k` from the new `ppq`, and do not restart the pattern index.
On `isPlaying` going false, flush note-offs **in the same block you detect the stop** — some hosts
(Pro Tools is the named example) stop calling `processBlock` shortly after transport stop
**[COMMON, JUCE forum]**. Standalone: no playhead → free-run from the first note-on, which is
Korg's documented Key Sync semantics.

Tempo changes mid-block are invisible in `PositionInfo`; scheduling in the ppq domain absorbs them
to within one block, which is the best available.

### 3.8 Arp ↔ voice allocation ↔ held notes

**The arp consumes the held notes; the sampler never sees them.** JUCE's demo makes this structural
— after harvesting note-ons/offs it calls `midi.clear()`. Architecturally it is also why Logic,
Ableton and Bitwig arpeggiators are MIDI/Note FX devices upstream of the instrument. **[SPEC.]**

For our built-in arp with per-slot enable, that means the routing fork happens **after** channel/
slot-mask resolution in `handleRoutedMessage()`: a note routed to a slot with the arp on goes to
the arp's held-note set; a note routed to a slot with the arp off goes straight to
`SamplerEngine::noteOn()`. One note can do both, for different slots. **[INFER.]**

Two disjoint states to maintain **[INFER]**:

- the **held-note set** (physical keys + latch), arp-side only;
- the **sounding-voice set**, which is what the 64-voice allocator sees. Size polyphony against arp
  *steps*, not held keys — a 4-note chord with 4 octaves at 1/32 and a 100% gate is still only a
  handful of concurrent voices, but a long release makes it many more.

Velocity: Logic offers fixed / **Original** (*"velocities of the recorded notes are retained"*) /
Random; NI a 1–200% dynamic scaler; the spec's As played / Fixed / Accent pattern is conventional.
Forward pitch bend, mod wheel and aftertouch to the slot as normal — only CC64 is consumed.

**Note-off contract (the anti-stuck-note rule).** No vendor documents a formal contract, but stuck
arp notes are a well-attested failure across u-he, Logic and Kontakt. **[INFER, but strongly
indicated]:** track the exact `(slot, note)` of every generated note-on not yet matched by a
note-off. On **any** discontinuity — arp toggled off, mode/rate/octave/gate change that invalidates
the schedule, latch released, transport stop, loop jump, preset load, `releaseResources`, standalone
exit — emit explicit note-offs for exactly those pairs at offset 0, **before** any new note-on.
Prefer tracked note-offs over CC 123: per the MIDI 1.0 spec, All Notes Off does not release notes
while the receiver's hold pedal is on, and All Sound Off kills release tails abruptly.

---

## 4. JUCE / real-time specifics

### 4.1 Editing the matrix while audio runs

The matrix is edited from the message thread and read on the audio thread every 64 samples.
`std::mutex` (even `try_lock`) is out on the audio thread; the documented alternatives are
`juce::SpinLock` with a scoped try-lock, or an atomic swap of an immutable structure. JUCE's
Dave96: *"Generally for lock-free stuff, the simpler the better. It gets really complicated really
quickly."* The consensus pattern is to **rebuild the whole structure and swap it atomically**
rather than mutate in place. **[COMMON, JUCE forum.]**

This codebase already has both idioms: `RoutedMidiQueue` (fixed 1024-slot `AbstractFifo` of 5-byte
PODs, producers spin among themselves, audio thread never locks) and the FX rack rebuild
(message-thread rebuild + audio-thread try-lock with dry passthrough for the colliding block).

**→ JM: neither.** The matrix is eight rows of POD — source enum, destination index, four floats,
two bools. Make it a **fixed-size, trivially-copyable `std::array<Row, kMaxRows>` in a
double-buffered pair with an atomic index**: the message thread writes the inactive buffer and
publishes it with one `std::atomic<int>::store(release)`; the audio thread does one
`load(acquire)` per block and reads that buffer for the whole block. No allocation, no lock, no
dropped block, and row edits take effect on a block boundary — which is what you want anyway,
since a routing that changes mid-block would step. Resolve destination *paths* to integer indices
on the message thread (paths are `juce::String`; the audio thread must never touch one).
**[INFER, standard practice.]**

Store `rowCount` in state, as decision 3 requires, and size the array generously (16) so growing
from 8 to 16 is a UI change only.

### 4.2 Sample-accurate MIDI in `processBlock`

Covered in §3.7. Two additions:

- `getPlayHead()->getPosition()` may **only** be called from `processBlock` **[SPEC]**. The current
  call site is correct; keep the `TransportClock` update there.
- `handleMidiAtBlockStart()` must become `handleMidi(offset, count)` or equivalent, driven by the
  sub-block loop, so timestamps survive. The `RoutedMidiQueue` events carry no timestamp (they come
  from device threads, asynchronously) — those legitimately stay at offset 0; only the host buffer's
  events and the arp's own output need offsets. **[INFER.]**

### 4.3 Denormals and smoothing

`ScopedNoDenormals` is already at the top of `processBlock`, which covers the whole graph including
new LFO and matrix code. Nothing further is needed there; a per-voice or per-LFO flush is
redundant. The existing conventions — `SmoothedValue` 20 ms on master gain, one-pole tau 12 ms on
pitch bend, 20 ms crossfade on FX bypass — are the right precedents for §1.5's table; reuse the
numbers rather than inventing new ones.

One caveat worth repeating **[SPEC]**: `SmoothedValue` with `Multiplicative` *"cannot ever reach a
target value of zero"*, so gain and depth destinations must use `Linear`.

---

## 5. Traps

The specific mistakes this research exists to avoid.

1. **Reusing the 30 Hz macro timer for modulation.** It is a destructive parameter writer on the
   message thread. LFO motion through it would zipper, destroy the player's knob value, and (on
   `host.*` paths) write into host automation — the exact shape of a known Bitwig VST3 modulation
   feedback bug. §0.
2. **Integrating Hz instead of deriving phase from `ppq`.** An LFO that accumulates
   `phase += hz·dt` drifts across a tempo ramp and cannot survive a locate. The anchored form
   `frac(ppq / beatsPerCycle)` is stateless and therefore drift-free and bounce-reproducible.
   Vital has this bug; Surge does not. §2.4.
3. **Trusting the host's ppq blindly.** Logic reports it ahead by thousands of samples and negative
   during count-in. Clamp against your own prediction; never difference consecutive blocks. §2.4.
4. **Free-running on the host's ppq when the transport is stopped.** Latch ppq only while playing,
   free-run your own otherwise — otherwise the LFO freezes solid the moment the player stops the
   transport to practise. §2.4.
5. **Depth meaning different things for different sources.** Pick School A or School B (§1.1) and
   be consistent, or a velocity row and an LFO row at the same depth will differ by 2×.
6. **Applying the parameter taper before summing.** Sum in normalised space, denormalise once. Two
   rows on a log-tapered cutoff will otherwise not add the way the arcs suggest. §1.2.
7. **Up/Down exclusive at n = 1 or n = 2.** Cycle length `2n − 2` is 0 and −… for one note. Special-
   case both. §3.1.
8. **Restarting the arp pattern when a note is added.** No instrument does this implicitly; clamp
   the index instead. §3.2.
9. **Ambiguous swing scale.** "0–75%" means three different things depending on whether it is the
   MPC 50–75 scale, a 0–100 amount, or a raw fraction of the step. Fix the definition in the spec
   before anyone writes code. §3.4.
10. **A note-off landing on the same sample index as its note-on.** At a 1% gate or a very fast
    step this happens; JUCE delivers same-position events in insertion order and receivers may drop
    the note. Enforce a ≥1-sample gate. §3.5.
11. **Forwarding CC64 to the sampler while the arp runs.** Every step's release tail sustains and
    the result is mush. Consume the pedal in the arp. §3.6.
12. **Stuck arp notes.** Track `(slot, note)` for every emitted note-on and flush explicitly on
    every discontinuity — including transport stop, which some hosts follow by ceasing to call
    `processBlock`. CC 123 is not a substitute: the MIDI spec says it does not release notes while
    the receiver's hold pedal is on. §3.8.
13. **Block-quantised note timing.** `handleMidiAtBlockStart()` throws timestamps away today. Sub-
    block splitting is needed for the arp to be worth anything, and fixes ordinary play too. §3.7.
14. **Mutating the routing table in place.** Double-buffer and swap; never let the audio thread see
    a half-written row or touch a `juce::String`. §4.1.
15. **Smoothing the wrong quantity.** Smooth pan *position* not the two channel gains; smooth pitch
    in *semitones* not as a ratio; smooth cutoff *multiplicatively*. §1.5.
16. **Extending `divisionBeats()` by inserting entries.** FX presets store the division as an
    integer index. Append only. §2.4.

---

## Sources

**Standards and SDK documentation**
- MIDI 1.0 Control Change messages (CC 64 damper, ≤63 off / ≥64 on) — https://midi.org/midi-1-0-control-change-messages
- MIDI 1.0 detailed spec, All Notes Off / All Sound Off vs Hold Pedal — http://midi.teragonaudio.com/tech/midispec/ntnoff.htm
- JUCE `AudioPlayHead` — https://docs.juce.com/master/classAudioPlayHead.html
- JUCE `AudioPlayHead::PositionInfo` — https://docs.juce.com/master/classAudioPlayHead_1_1PositionInfo.html
- JUCE `SmoothedValue` — https://docs.juce.com/master/classSmoothedValue.html
- JUCE `NormalisableRange` — https://docs.juce.com/master/classNormalisableRange.html
- VST3 `ProcessContext` — https://steinbergmedia.github.io/vst3_doc/vstinterfaces/structSteinberg_1_1Vst_1_1ProcessContext.html
- VST3 `ProcessContextRequirements` — https://steinbergmedia.github.io/vst3_doc/vstsdk/classSteinberg_1_1Vst_1_1ProcessContextRequirements.html

**Source code (primary)**
- Vital — mod connection processing: https://github.com/mtytel/vital/blob/main/src/synthesis/modules/modulation_connection_processor.cpp
- Vital — mod summing: https://github.com/mtytel/vital/blob/main/src/synthesis/framework/synth_module.cpp
- Vital — cycle detection / `Feedback` insertion: https://github.com/mtytel/vital/blob/main/src/synthesis/framework/processor_router.cpp
- Vital — LFO phase, sync, fade: https://github.com/mtytel/vital/blob/main/src/synthesis/modulators/synth_lfo.cpp
- Vital — random LFO / interpolators: https://github.com/mtytel/vital/blob/main/src/synthesis/modulators/random_lfo.cpp
- Vital — 128-sample block splitting, transport: https://github.com/mtytel/vital/blob/main/src/plugin/synth_plugin.cpp
- Vital — self-reference block in `createConnection`: https://github.com/mtytel/vital/blob/main/src/common/synth_types.cpp
- Surge XT — `lm_freerun` ppq→phase: https://github.com/surge-synthesizer/surge (`src/common/dsp/modulators/LFOModulationSource.cpp`)
- Pirkle SynthLab — https://github.com/willpirkleaudio/SynthLab (`source/lfocore.cpp`)
- JUCE `ArpeggiatorPluginDemo` — https://github.com/juce-framework/JUCE/blob/master/examples/Plugins/ArpeggiatorPluginDemo.h

**Reference designs and texts**
- Pirkle SynthLab, Modulation Matrix — https://www.willpirkle.com/synthlab/docs/html/mod_matrix.html
- Pirkle SynthLab, `ModMatrix` class — https://www.willpirkle.com/synthlab/docs/html/class_synth_lab_1_1_mod_matrix.html
- Pirkle SynthLab, block audio processing — https://www.willpirkle.com/synthlab/docs/html/the_block.html
- Pirkle, *Designing Software Synthesizer Plugins in C++*, 2nd ed., ch. "The Modulation Matrix" — https://www.taylorfrancis.com/chapters/mono/10.4324/9781003052203-14/modulation-matrix-pirkle
- Zölzer (ed.), *DAFX: Digital Audio Effects*, 2nd ed., Wiley 2011 (parameter interpolation / control signals)
- Reiss & McPherson, *Audio Effects: Theory, Implementation and Application*, CRC/AES 2014 — https://www.routledge.com/Audio-Effects-Theory-Implementation-and-Application/Reiss-McPherson/p/book/9781466560284
- musicdsp, "Smooth Random LFO Generator" — https://www.musicdsp.org/en/latest/Synthesis/269-smooth-random-lfo-generator.html
- Improved Perlin noise (quintic easing) — https://www.scratchapixel.com/lessons/procedural-generation-virtual-worlds/perlin-noise-part-2/improved-perlin-noise.html

**Instrument manuals**
- Serum manual (mod matrix Type, Rise/Delay, Anchor, sync switches) — https://s3.amazonaws.com/decembercymatics/Serum_Manual.pdf
- Xfer forum, Steve Duda on bipolar assignment defaults — https://xferrecords.com/forums/general/lfo-polarity
- Arturia Pigments 5 manual (§12.2.3.2, §12.3.2, §12.3.3) — https://dl.arturia.net/products/pigments/manual/pigments_Manual_5_0_0_EN.pdf
- Arturia KeyStep 37 manual, Inclusive/Exclusive — https://dl.arturia.net/products/keystep-37/manual/keystep-37_Manual_1_1_EN.pdf
- Bitwig, Unified Modulation System — https://www.bitwig.com/userguide/latest/the_unified_modulation_system/
- Bitwig, Note FX (arp octave behaviour) — https://www.bitwig.com/userguide/latest/note_fx/
- Massive X, Modulators — https://docs.native-instruments.com/ni-tech-manuals/massive-x-manual/en/modulators
- NI Komplete Kontrol S-mk3, Arpeggiator (gate, swing) — https://docs.native-instruments.com/ni-tech-manuals/kontrol-s-mk3-manual/en/arpeggiator
- Omnisphere, LFO trigger modes — https://support.spectrasonics.net/manual/Omnisphere2/25/en/topic/layer-page-lfos-page08 · LFO delay — https://support.spectrasonics.net/manual/Omnisphere2/25/en/topic/layer-page-lfos-page06 · Latch — https://support.spectrasonics.net/manual/Omnisphere3/30/en/topic/latch
- Ableton Live MIDI Effect Reference (Arpeggiator modes, Hold, Gate) — https://www.ableton.com/en/manual/live-midi-effect-reference/ · Grooves — https://www.ableton.com/en/live-manual/11/using-grooves/
- Logic Pro Arpeggiator control parameters (Latch modes) — https://support.apple.com/guide/logicpro/arpeggiator-control-parameters-lgce2a327604/mac · note order — https://support.apple.com/guide/logicpro/note-order-parameters-overview-lgce129c3fbe/mac · options/swing — https://support.apple.com/guide/logicpro/arpeggiator-options-parameters-lgce78607291/mac · Environment arpeggiator (Up/Down vs Up/Down2) — https://logicpro.skydocu.com/en/work-in-the-environment/environment-objects-reference/arpeggiator-objects/arpeggiator-parameters/
- Yamaha Montage arp KEY MODE and "the sustain pedal … holds the keys for you" — https://yamahasynth.com/community/montage-series-synthesizers/arp-settings-basics/
- Korg Arpeggiator / Key Sync — https://support.korguser.net/hc/en-us/articles/115005673883-Arpeggiator
- Nord Stage 2, Latch Pedal (sostenuto semantics) — https://www.nordkeyboards.com/wt/documents/257/Nord%20Stage%202%20English%20User%20Manual%20v1.7x%20Edition%20J.pdf
- Sequential Prophet-6 Operation Manual (LFO key sync, random/S&H) — https://www.sequential.com/wp-content/uploads/2021/02/Prophet-6-Operation-Manual-2.1.pdf
- Akai MPC60 Operator's Manual p.57, SHUFFLE 50/66/75% — https://www.manualslib.com/manual/207378/Akai-Mpc-60.html?page=57 · Roger Linn on swing — https://www.attackmagazine.com/features/interview/roger-linn-swing-groove-magic-mpc-timing/
- u-he Hive user guide — https://u-he.com/downloads/manuals/plugins/hive/Hive-user-guide.pdf · Zebra2 user guide — https://u-he.com/downloads/manuals/plugins/zebra/Zebra2-user-guide.pdf
- Yamaha DX7 Operation Manual (LFO delay) — https://homepages.abdn.ac.uk/d.j.benson/pages/dx7/manuals/dx7-man.pdf

**Forum threads (community unless noted)**
- JUCE, `Synthesiser` sub-block splitting — **Jules, JUCE** — https://forum.juce.com/t/sample-accuracy-of-synthesiser/17339
- JUCE, atomic swap of resources onto the audio thread — **Dave96, JUCE** — https://forum.juce.com/t/atomic-swap-resources-onto-the-audio-thread/65707
- JUCE, valid sample numbers for `MidiBuffer` events — https://forum.juce.com/t/processblock-what-are-valid-sample-numbers-for-newly-created-midimessages/22293
- JUCE, sample accuracy via `ppqPosition` phasor — https://forum.juce.com/t/performing-a-function-with-sample-accuracy-using-processblock/55077
- JUCE, playhead best practice / pseudo-transport — https://forum.juce.com/t/best-practice-on-plugins-which-require-audioplayhead-information-and-transport-management/60489
- JUCE, `getPosition` sync issues in Logic — https://forum.juce.com/t/syncing-to-getposition-issue/56030
- JUCE, tempo changes and accurate time — https://forum.juce.com/t/getting-accurate-time-from-daw-with-tempo-changes/64426
- JUCE, hosts that stop calling `processBlock` — https://forum.juce.com/t/is-there-a-way-to-allow-audio-out-when-the-transport-is-not-running-but-also-allow-audio-in/54462
- JUCE, all-notes-off on standalone exit — https://forum.juce.com/t/solved-send-all-notes-off-when-standalone-plugin-exits/61604
- JUCE, zipper after `SmoothedValue` (don't `reset()` per block) — https://forum.juce.com/t/still-glitchy-zipping-noises-after-using-smoothedvalue/59589
- KVR, implementing a modulation matrix — **Urs Heckmann (u-he)**, mystran, Jeff McClintock — https://www.kvraudio.com/forum/viewtopic.php?t=593782
- KVR, alias-free LFOs — https://www.kvraudio.com/forum/viewtopic.php?t=470275
- KVR, u-he stuck notes — https://www.kvraudio.com/forum/viewtopic.php?t=541167
- Sequential forum, arpeggiator hold / sustain pedal — https://forum.sequential.com/index.php?topic=5347.0
- Timur Doumler, using locks in real-time audio safely — https://timur.audio/using-locks-in-real-time-audio-processing-safely

---

*Prepared as input to the modulation implementation. Extends `docs/JM-MODULATION-SPEC.html`;
behaviour authority remains `docs/JM-BUILD-SPEC.md`.*
