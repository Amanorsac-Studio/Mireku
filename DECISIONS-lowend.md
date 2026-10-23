# DECISIONS-lowend.md — judgment calls made while building Amanorsac Low End

Low End lives in `apps/lowend`. Spec: [docs/LOWEND-BUILD-SPEC.md](docs/LOWEND-BUILD-SPEC.md).

## Separation from the other products

- **Own copy of the shared engine headers.** `apps/lowend/src/engine/{Antialias,FxParams,FxDsp,Limiter}.h`
  are verbatim copies of the Sanctuary versions, kept in the `jm::` namespace so future
  diffs against the source stay trivial. Same trade as Sanctuary made against JM
  Signature: a deliberate fork, in exchange for a guarantee that Low End work can never
  change another product's sound.
- **The reused FX are genuinely reused, not re-implemented.** Nine of the twenty blocks
  (chorus, flanger, phaser, tremolo, studio EQ, both delays, both reverbs) are a thin
  `ReusedBlock` adapter over `jm::FxUnit`. Their parameter lists are *copied from the
  shared registry at startup*, so index i in Low End is index i in the FX unit and the
  adapter's forwarding is a pass-through. A test asserts the two lists still match — if
  someone adds a parameter to the shared chorus, the test fails rather than the Low End
  chorus silently moving the wrong knob.
- Nothing here writes into `apps/signature` or `apps/sanctuary`. JUCE is read from the
  checkout next to JM Signature.

## Naming

- **"Low End"**, not "Rumble". Rumble is a Fender product name; using it internally is
  fine but it would be a trademark problem the day this is sold, and renaming a shipped
  product is worse than naming it right now.

## Engine

- **The tone stack is voiced, not SPICE.** A real Fender TMB network is one passive RC
  section whose three controls interact — scooping the mid lifts the ends and cuts the
  middle, which is the sound people mean by "Bassman". That interaction is modelled
  explicitly (a low shelf, a mid peak and a high shelf whose gains, corners and Q are
  functions of all three positions), not by discretising the actual ladder's transfer
  function. The component-level model is a third-order transfer function whose
  coefficients are easy to get subtly wrong and impossible to verify by ear here; the
  voiced version is testable, and `tests/LowEndTests.cpp` asserts the scoop appears in
  the mids *and* lifts the lows. An earlier version also walked the bass corner down as
  the mid was cut, which cancelled most of the low-end lift — the test caught it.
- **Cabs are analytic, IRs are optional.** The four built-in cabs are filter networks
  (resonant low corner, cone-breakup peak, 4th-order top rolloff, mic colour and
  placement), so the app ships with no audio data, starts instantly and has no licensing
  question about whose speakers were recorded. A player who wants a specific cab drops a
  `.wav` IR in the folder.
- **The octaver is the analog design**, a hysteresis comparator driving flip-flop
  dividers, not a pitch shifter. It tracks instantly, costs almost nothing, and only
  works on single notes — which is exactly how an octave pedal behaves and what a bass
  player expects.
- **Every drive has a clean blend.** A low B's fundamental is 31 Hz and a waveshaper
  turns it into harmonics; distorting a bass without a parallel clean path removes the
  instrument's job from the mix. The drive path is high-passed at 120 Hz *before* the
  shaper and the clean path is untouched.
- **Fuzz is oversampled 8×, the overdrives 2×.** A near-square wave is broadband by
  construction and its aliases fold back into the bass register, where they sound like a
  ring modulator rather than like fuzz. The softer shapers do not need it.
- **Pitch detection is YIN, not autocorrelation.** On a bass the second harmonic is
  frequently stronger than the fundamental, and autocorrelation's peak-picking reports an
  octave down. An octave error in an arpeggiator is not a subtle artefact. The detector
  runs on a 4×-decimated signal (bass fundamentals are all below 500 Hz), which is what
  makes a two-period window affordable on the audio thread.
- **The tracked synth is a layer, never a replacement.** Pitch detection needs to see two
  periods before it can name a note — 64 ms on a low B. That is physics. So the direct
  bass path is never delayed to match it; the synth arrives late and sits alongside.

## Threading

- **The audio thread never reads the Rig.** The model is a `std::map<std::string,float>`
  per section owned by the message thread. Every engine object that is configured from
  one keeps its own flat array of atomics (`ParamStore`), indexed by an enum that is
  asserted against the registry's order in `prepare`. This replaced an earlier version
  where the amp, cab, synth, arp and looper held plain scalars written by the UI and read
  by the audio thread — which worked in practice on x86 and was still a data race.
- **Footswitches that touch the model are handed over.** A CC arrives on the audio
  thread; looper transport, arp hold and panic are applied there (they touch no model
  state), while preset changes, tap tempo and learned parameter moves go into a lock-free
  ring and are applied by an async update on the message thread.
- **MIDI binding lists are swapped, never edited in place**, under a spin lock the audio
  thread only ever *tries*. A dropped buffer of CC is not a bug; a priority inversion on
  stage is.
- **Scene changes crossfade two amps.** The pedalboard has the same clickless dry
  hand-over `jm::FxRackDsp` uses, but the amp and cab are filter networks whose
  coefficients jump. So there are two amp+cab instances: the incoming preset is set up on
  the idle one and the two are equal-power crossfaded over 25 ms. It costs a second amp
  for one buffer per scene change and nothing the rest of the time.
- **The looper's undo is bounded per block.** Undo used to restore the whole overdubbed
  span inside one audio callback — on a long loop that is millions of float copies, and
  the soak test measured it as a 7 ms block, i.e. a dropout. It now arms, and restores
  64k frames per block. Related: the pedal chain's dry buffer used to be grown lazily
  inside `process()`, so the first block after `prepareToPlay` called the allocator on
  the audio thread (a 17 ms first block). Both were found by the soak test, not by
  reading the code.

## Model

- **One path space for everything.** `in.trim`, `pre.2.drive`, `amp.gain`, `arp.division`,
  `loop.level` — the UI, MIDI learn, footswitches, presets and the factory content all go
  through one `setPath`. A control added to the UI is automatable, learnable and saveable
  the moment it exists. A path that does not resolve returns false and is logged; it is
  never silently applied to the wrong control.
- **The factory presets are written as path edits** over the default rig, which means
  every launch that generates them exercises `setPath` on every path they use. A typo in
  a factory preset shows up as a log line on first run rather than as a wrong-sounding
  preset a month later.
- **Host parameters are deliberately four, not two hundred.** Master, limiter — and
  nothing per block. Exposing every pedal parameter would make the DAW's automation list
  change shape every time a block is swapped, which is what breaks a session six months
  later. Everything else is a path saved with the rig.
- **Unknown blocks in a preset are dropped and reported**, never guessed at. A preset
  written by a future build that has a pedal this one does not is loaded without that
  pedal, with a log line naming it.

## UI

- **The Board draws real module boxes**, in the same visual language as the JM Signature
  asset pack (`docs/svg-pack-v2`): dark chassis, coloured faceplate over a woven
  microtexture, gold hairline and screws, gold-ringed knobs with a value arc. They are
  drawn as inline SVG rather than shipped as images, so a new pedal needs no new asset —
  it inherits the box from its category — and the whole board stays sharp at any window
  size. The amp is a head with a control strip and a grille; the cab shows the right
  number of speaker cones for its model.
- **The knobs on the box are the real controls.** Dragging a knob on the faceplate edits
  the parameter directly, so the common case (nudge the drive) needs no dialog. A box
  shows its first six knobs; blocks with more (the delays) list the rest in the panel
  below. The footswitch and LED toggle the pedal, as they would on the floor.
- **Faceplate labels are abbreviated, not truncated.** A 132 px faceplate gives about
  44 px per knob, and full names collided ("THRESHOLDRELEASE"). Long ones map to the
  abbreviation a real pedal would print (THRESH, SENS, RESO).
- **A bypassed box desaturates.** Real pedals only drop their LED, but on a screen at a
  distance the colour is what the eye reads first.

## Preset model, and the wording of it

The first build shipped a "Scenes" panel that listed every preset when no song was
loaded, with a dropdown reading "All presets". That is three concepts wearing one label,
and it was reported as making no sense. The model is now stated explicitly in the UI:

- A **preset** is one whole rig — board, amp, cab, synth, arp, looper settings.
- A **song** is an ordered list of presets, with its own tempo.
- A **scene** is one entry in a song. The same preset can be a scene in several songs;
  editing the preset changes it everywhere, which is what a setlist should do.

The Stage panel now says either "Presets" or "Scenes — <song>", the dropdown reads
"No song — all presets", and creating things is a visible button rather than something
you have to know about: Save changes / Save as new / Rename / Blank preset / Duplicate,
plus a Library page with a song builder (add the current preset as a scene, reorder,
remove) and per-preset rename, duplicate and delete. An **unsaved** marker sits next to
the rig name, computed by comparing the live rig against what is on disk, so a player
never has to guess whether a tweak survived the next scene change.

Deleting a preset also removes it from any song that referenced it, so a song can never
list a scene that will not load.

## September 2026 revision — simpler, cleaner, iPad-ready

Requested by the owner after using the app: the UI felt clumsy and cheap, was not
usable on an iPad, and carried features and text it did not need.

- **The arpeggiator is gone** — engine (`Arp.h` deleted), registry, path space
  (`arp.*`), the `arpHold` footswitch role, the UI, and its tests. The pitch tracker and
  the synth voice stay: the tracker feeds the tuner, and the "Synth Bass" factory preset
  uses the synth. The synth's `trackDry` switch went with the arp, because without an
  arp there is nothing else for the synth to follow. The factory "Arp Pulse" preset was
  replaced by "Dub Echo" (Flip Top, 1×15, analog delay after the cab). **Existing
  libraries are not rewritten**: a user who already has "Arp Pulse" keeps the file, which
  now loads as a compressor-and-delay rig with a silent synth, and can delete it.
- **The Amp page is gone.** The Board is the one place a rig is edited. Tapping the amp
  head shows the four heads as drawn cards plus its front panel; tapping the cab shows
  all six cabinets as drawn cards *at their real relative sizes* (a 2×10 is short, an
  8×10 is a fridge) plus mic and filters. The jacks and a Settings button open the rig
  settings — input trim and gate, DI blend, master and limiter, and the synth.
- **The boxes are drawn as hardware**: anodized faceplates with a brushed texture and a
  specular sheen, nickel side jacks the cable plugs into, black knurled knobs with a lit
  indicator and min/max dots, a jewel LED, a footswitch and a lined name band. The amp
  heads differ by model (black tolex, tweed, blue check, a black modern head), and the
  cabinets carry grille cloth, corner protectors and the speakers faintly visible
  through the cloth. Everything is centred on the cable, as on a real board.
- **No instructional copy on working screens.** "Drag a knob…", "double-click the
  footswitch…", "these are all your presets…" — all removed. The few things worth
  explaining live in Settings → Help.
- **Type**: Bahnschrift (Windows) / DIN Alternate (iPad, macOS) for names and numbers,
  the platform UI face for text. Both ship with the OS, so nothing is downloaded and the
  app looks the same offline on a stage.
- **iPad**: every target ≥ 44 px; nothing depends on hover (the insert "+" on the cable
  is always visible); right-click has a long-press equivalent; `prompt()`/`confirm()`
  are replaced by in-app sheets, because the iPad web view has neither; the navigation
  becomes a bottom bar at 920 px and narrower; the editor's minimum size dropped from
  1040 to 760 px wide so that layout is reachable. **This makes the UI iPad-ready; it
  does not produce an iPad build** — that needs an iOS/AUv3 target built on a Mac.
- **Preset management moved to the preset name** in the header (tap it: switch, save,
  save as, rename, duplicate, new). A Save button appears only when there are unsaved
  changes. The Library is a list and detail pane instead of two crowded tables.
- **The tuner is a full-screen overlay** instead of a permanent panel; the output mutes
  while it is open, as it always did.
- **The looper has an explicit CLEAR button** instead of "hold STOP to clear", which was
  an undiscoverable gesture that needed a caption to explain it.

## Learn tab (September 2026)

The owner asked for a music-tutor tab: YouTube inside the app; import a song or video,
hear its chords and melody and put them on a piano roll with the chord name on top;
loop, slow down, speed up; for bass, hear the notes being played and show them on the
instrument in the context of the key.

- **Analysis runs in JavaScript, in a Web Worker, not in the C++ engine.** The web view
  already decodes every format a user will bring (MP4/MOV video included, where the
  JUCE readers on Windows would need a Media Foundation shim), already plays video with
  pitch-preserving speed, and can hand the file to both the player and the analyser
  without the file ever going through the plugin's resource provider (which serves whole
  bodies and cannot seek a large video). The analyser has no dependencies, is tested
  under Node (`tests/learn/analyzer.test.js`, wired into ctest when Node is present), and
  analyses a four-minute song in about 15 s.
- **What the tests prove, and what they do not.** On synthesized progressions with
  realistic harmonic content: I–vi–IV–V and iv–V–i named exactly, both keys right, a
  melody over chords found note for note, a solo bass line transcribed 12/12, the chords
  and key surviving a drum kit on top, silence silent. They do not prove accuracy on
  commercial recordings: chord recognition from a chromagram is reliable on clear
  material and weaker on dense, distorted or jazz-voiced music, and note transcription
  from a full mix is a guide to play along with, not a score. Every fix made while
  building it is in the analyser's comments — each was a real failure the tests caught:
  triads read as major-seven chords (a note's own third harmonic), octave-down bass notes
  (subharmonic summation), a melody lost under its own chord tones (over-eager overtone
  pruning), a key read off the relative minor (profile-only detection), and melody
  passing tones renaming the chord.
- **The key comes from the chords.** A pitch profile cannot separate a key from its
  relative minor; the progression can.
- **YouTube is browsed, not analysed.** A second native web view shows youtube.com
  itself (an `<iframe>` of youtube.com is refused by the site); the Learn transport drives
  its player's speed and loop by script, as a browser extension would. Capturing or
  downloading YouTube audio for analysis would breach YouTube's terms, so it is not done.
  A later option that stays inside the rules is analysing the computer's own output
  while a video plays (WASAPI loopback) — real-time, not ahead of time.
- **Lessons live in the web view's IndexedDB** (file + analysis + chosen key), so a song
  reopens without re-analysis. A file too large to store keeps its analysis and asks to
  be re-imported to play.
- **Live bass notes** come from the engine's existing pitch tracker: a note number, its
  cents, and a note-on counter so "A A A" lists three times. **Piano** live input is a
  MIDI keyboard; polyphonic piano from a microphone is not attempted.

- **Verified in the running app**, driven through WebView2's DevTools port (opened only
  for the test run): a 16 s stereo WAV imported, analysed by the worker the app serves
  and stored in 1.2 s (C major; C–Am–F–G ×2); playback at 80 % advanced 1.55 s in 2 s with
  pitch preserved; the A–B loop wrapped and held; the chord display read the right chord
  and the next one during playback; the lesson reopened from storage without re-analysis;
  YouTube loaded in the tab and a search typed mid-load reached the results page.
  Two bugs were found this way and fixed: the A–B loop lived in the drawing loop, which
  the browser pauses when the window is covered, so it silently stopped looping — it now
  has its own timer; and JUCE replaced an aborted YouTube navigation with a plain-text
  "Error code: 9" page that then cancelled the search the user had asked for — the
  YouTube view now leaves failed navigations to WebView2. YouTube speed and looping were
  not exercised on a playing video during the test run (it would have played sound
  unattended); they use the same injected-script path that navigation does.

### Stem separation — built, after the first attempt proved too slow

The answer given before building was "not yet; it is feasible". It is now built, with one
sizeable detour worth recording.

**The model.** Meta's HT-Demucs (Demucs v4): drums / bass / other / vocals. Nothing
DSP-only (centre extraction, filtering) was offered, because it would not be separation.

**First attempt — demucs.cpp, abandoned on speed.** demucs.cpp (MIT) is a C++17 + Eigen
port of the same network, and it built under MSVC first try and separated correctly. It
was also about **thirty times slower than real time** on this laptop: a 30-second clip was
60 % done after eleven minutes, which puts a song at over an hour. The cause is not the
port but its arithmetic — Eigen compiled by MSVC with no BLAS behind it, on a low-power
CPU. Options considered: linking OpenBLAS, or building that translation unit with
clang-cl (not installed here).

**What shipped — the same model on ONNX Runtime.** Intel publishes an MIT-licensed ONNX
export of HT-Demucs v4 with the STFT and inverse STFT stripped out (`Intel/demucs-openvino`,
171 MB in two files). Measured on the same machine: **0.5x real time**, about a sixty-fold
improvement, so a four-minute song separates in two to three minutes. Same weights, same
output — only the matrix multiplications changed hands.

Because the export has no STFT, the audio pipeline around the network is ours
(`Separator.cpp`): song normalisation, the sub-second shift, 7.8 s segments at 25 %
overlap, complex-as-channels, and the inverse transform summed with the network's time
branch. The STFT itself (`Stft.h`) is written on JUCE's FFT to Demucs' exact constants and
is checked by a test that round-trips it (139 dB) and verifies the forward scaling against
`torch.stft(normalized=True)`. One deliberate difference from demucs.cpp: it reflects the
edge sample twice where `torch.stft` does not, and we follow torch.

**Consequences of the switch**, all simplifications: no Eigen, no AVX2-only translation
unit and no CPU feature check (ONNX Runtime picks its kernels at run time), no
thread-per-piece scheme, and 1.9 GB of peak memory gone.

**What "best" means now.** The four fine-tuned Demucs models have no ONNX export, so
quality is no longer a different model: standard is one pass at 25 % overlap, best is two
time-shifted passes averaged at 50 % overlap — about four times the work for a few tenths
of a dB.

**Verified**, on a synthetic multitrack with the true bass kept as a reference: the four
stems sum back to the mix at 32.3 dB, bass SDR against the reference is 17.8 dB, and the
vocals stem comes back near-silent — correctly, since that track has no vocals.

**Playing the stems** is its own problem, and the reason for `stemplayer.js`. Muting the
bass to play along is easy; doing it while the song is SLOWED DOWN is not. Four `<audio>`
elements each stretching independently drift apart within seconds, and an
`AudioBufferSourceNode`'s `playbackRate` drops the pitch with the speed. So one
AudioWorklet stretches all four: the WSOLA alignment search runs on the summed mix, and
the window and read position it finds are applied identically to every stem, which makes
relative drift impossible rather than merely unlikely. At exactly 100 % the stretcher is
bypassed and the mix is bit-exact. The test asserts that a stem and its own negation
cancel to silence at four different speeds — which is only true if one time pointer feeds
them all.

**The bass is re-analysed from its own stem** once separation finishes. Chords and key
still come from the whole song: a bass line on its own is a poor witness to a chord.

### Audio settings: the app's own panel, and what "low latency" is allowed to mean

The audio settings were a single button that opened JUCE's native device dialog. It
works, but it belongs to a different program: another typeface, another layout, no idea
what any of it costs in milliseconds, and unusable on a tablet.

Replaced with a panel in Settings driven by `LowEndProcessor::AudioHooks` — plain
`juce::var` in, plain `juce::var` out, implemented in `StandaloneApp.cpp` because that
is the only build with a device manager. In a plugin every hook is null and the panel
says the host owns the audio, which is true and is better than showing controls that
would silently do nothing.

Three judgement calls worth recording:

- **The driver list is ordered, and annotated.** "Windows Audio (Exclusive Mode)" does
  not tell a bass player anything. Each driver carries a short note — *Native, lowest
  latency* / *Exclusive, low latency* / *Shared, higher latency* — and they are listed
  best-first, so the top of the list is the recommendation.
- **Latency is stated, not implied.** Every buffer size shows its own millisecond cost,
  and the round trip (buffer plus the interface's reported input and output latency) is
  written underneath. A player choosing between 64 and 128 should not have to do
  arithmetic.
- **The menus are trimmed.** ASIO4ALL offers 129 buffer sizes (every multiple of 16 from
  64 to 2048) and sample rates down to 8 kHz. Both are technically true and useless on
  stage, so the list is cut to the sizes players choose between — plus whatever is
  currently in use and the device's own minimum, so nothing legitimate disappears.

"Lowest latency" asks the machine rather than assuming: it takes the best driver
present, opens it, then reads the buffer sizes that driver actually offers and takes the
smallest at or above 64. Measured here, it moved shared Windows Audio at 480 samples
(10 ms block, 20 ms round trip) to ASIO at 64 samples (1.5 ms block, 13.1 ms round
trip).

One thing this does NOT do: it does not verify the machine can survive the buffer it
chose. 64 samples through a generic wrapper like ASIO4ALL can crackle. The button is
honestly named, the buffer menu is one control away, and guessing on the player's
behalf would be worse than letting them hear it and step up.

### A new preset starts empty

"New preset" used to hand over `defaultRig` — a noise gate, a compressor and an
overdrive. Someone building their own sound then has to delete three pedals before they
can start, which is the most tedious possible beginning. `emptyRig` gives the amp, the
cab and a bare board. The factory presets still ship fully built, and `testEmptyRig`
checks the two starting points against each other so a change that quietly put the
default chain back would fail rather than go unnoticed until someone made a preset.


### Bringing Low End under the studio standards (September 2026)

The standards pack arrived after most of the product was built, so this was an audit of
finished work rather than a start audit. The line-by-line result is
`apps/lowend/docs/DELIVERY-AUDIT-1.0.0.md`. What changed, and what it was chosen over:

- **The separation model is installed, not downloaded.** It used to be fetched from
  Hugging Face on first use. The published privacy policy says a product makes no network
  request other than licensing (B46), so the download code was removed outright and the
  171 MB model became an installer component the buyer can untick. Chosen over keeping
  the download behind a confirmation: a promise that says "none" is not kept by asking
  first. It goes to one FIXED path (`Program Files\Amanorsac Studio\Low End\Models`)
  rather than beside the app, because the plug-in has to find it without knowing where,
  or whether, the standalone was installed.
- **The YouTube browser is left in, and flagged, not quietly kept or quietly removed.**
  It is a network request other than licensing. It was asked for by the studio and is the
  studio's call: amend the policy or drop the feature. The README and the installer say
  plainly what it does.
- **ONNX Runtime is delay-loaded and loaded by full path.** Found by reading the header,
  not by testing: its C++ API calls into the DLL during static initialisation; a plug-in's
  folder is not on the DLL search path; and Windows ships an older `onnxruntime.dll` in
  System32 that a search by name finds first. `ORT_API_MANUAL_INIT` plus `/DELAYLOAD` plus
  `LoadLibraryEx` on our own copy. Without this the VST3 would have failed to load in a DAW
  on most machines, and nothing on a development machine would have shown it.
- **Static C++ runtime.** A plug-in is loaded into someone else's process on a machine that
  may never have had the redistributable. ONNX Runtime is prebuilt and still needs it, so
  the installer places the four runtime DLLs beside it (app-local deployment).
- **Machine state moved to `%LOCALAPPDATA%`** — both web-view profiles (they were in
  Documents) and the settings file (JUCE puts it in Roaming). The Learn tab's lessons live
  inside the web-view profile, so the folder is MOVED on first run, not abandoned;
  verified that existing lessons and stems survive.
- **JUCE's device dialog is gone**, including the "Advanced…" escape hatch added a day
  earlier. B53 is unambiguous: a buyer must never meet a panel that was not designed.
- **Dropdowns are drawn by the product** (B54, Master Standard §6). Rather than rewrite
  nine `<select>` controls, each keeps its `<select>` as the MODEL — every handler,
  value and option untouched — and gets a drawn face with full keyboard support. A select
  added later is covered the moment it exists.
- **Fonts are bundled**: Inter for the interface, JetBrains Mono for every number, and
  Barlow Semi Condensed as the product's display face. It replaces Bahnschrift, which is a
  Windows system font and cannot be shipped; Barlow is the closest OFL face, so the look
  barely moves.
- **Two contrast failures fixed**: `--faint` (3.3:1) and white text on the accent (3.7:1).
  Filled controls now use a deeper blue; lines and glows keep the accent.
- **Bundle id** is `com.amanorsac.lowend`, to match the identifier pattern in the signing
  standard. Free to change before a first release; expensive after.

**Exceptions to the installer standard, recorded rather than worked around:** on macOS the
model goes to `/Library/Application Support/Amanorsac Studio/Low End/` (not in the
standard's table — three bundles share one 170 MB file); Installer.app cannot offer the
"second copy" field (P14); and stock Inno Setup cannot put the product accent on the
primary button or set the text in Inter (P18) without a custom VCL style.

**A test-harness lesson.** The page-review build of the installer was first made only by
lowering `PrivilegesRequired`. A UI-automation script then clicked through it and it began
a real per-user install before being killed (nothing was written; verified). The
review build now has a `PrepareToInstall` that refuses, so it cannot install whatever is
clicked. A preview that depends on nobody pressing the wrong button is not a preview.


### Test builds: a stand-in key, and why it is not the studio's licensing (September 2026)

Asked for one key to hand to every tester, good for 30 days. The studio's own system cannot
supply it, and the reason is worth keeping. Under the License Integration Standard a key is
created by the store, and the proof that makes it work is signed with a private key that
exists only on the studio's server ("if you are ever handed a private key, something has
gone wrong"). So no client, and no developer, can mint one. Low End also has no licence
client at all and no app id or key prefix, both of which the studio assigns. The studio
confirmed it cannot get a master key for that system.

So test builds carry a separate, offline gate (`src/plugin/TesterGate.h`):

- **The key** is a signed token, `base64url(body).base64url(signature)`. The body says
  which product, when it was issued and when it expires. The signature is RSA-2048,
  PKCS#1 v1.5, SHA-256. One key works for everyone and stops on its own expiry date.
- **The build holds only the public key** (`TesterKey.h`). The private half lives in
  `Documents\Amanorsac Studio\Security\LowEnd-Tester\`, outside the repository, and issues
  keys through `scripts/tester-key.js`. There is no secret in the binary.
- **Standard RSA, not hand-written cryptography.** JUCE has `BigInteger` and `SHA256`, and
  Node signs the same format, so the C++ is checked against an independent implementation:
  45 tests, with tokens signed by Node under a throwaway key. Chosen over Ed25519, which is
  shorter to paste but would have meant writing curve arithmetic by hand for a stand-in.
  The cost is a key about 430 characters long. Chosen over a shared secret compiled into
  the binary, which Build Standard B38 forbids and which any tester could read out.
- **Without a valid key the audio is silent** (one relaxed atomic read on the audio thread;
  the verdict is decided on the message thread and re-checked every 30 seconds, so a key
  that expires mid-session takes effect without a restart). The page shows a full-window
  gate and says why in plain words; the header shows the days left, and turns amber in the
  last seven.
- **Time.** There is no server to ask, so the gate remembers the latest time it has ever
  seen and never lets "now" go backwards past it. Winding the clock back does not revive an
  expired key. Deleting the remembered time does; that is an accepted limit of an offline
  check on a test build, and it is tested.
- **No network request**, so B46 is untouched.

What this is deliberately NOT, and the departures it makes from the standards:

- **It is compiled in only with `-DLOWEND_TESTER_BUILD=ON`** and a release contains none
  of it. Test builds are made in their own folders (`build-tester`, `build-mac-tester`) and
  named `LowEnd-1.0.0-Tester-Windows.exe` / `-Tester-macOS.pkg`, so one can never be mistaken
  for, or overwrite, the other. The installer, its README and its licence page say plainly
  that it is a test build and needs a key.
- **It breaks R2** ("MUST NOT contain any other signing key") for the test binaries. It is
  acceptable there only because a test build is not sold. It is a release blocker if one
  ever reaches the store.
- **Keys cannot be revoked.** With no server, a key stops when its own date passes and not
  before. Do not issue a longer one than you would be content to see live that long.
- **Replaced, not extended, by real licensing.** When the studio assigns an app id and a
  key prefix, the standard's reference client (`LicenseClient.h`, `LicenseCrypto.h`, which
  the studio holds) replaces this file, and the test key goes away with it.


## Out of scope / deferred

- **The limiter's ceiling is not a control.** `jm::MasterLimiter` is a fixed −0.3 dBFS
  safety wall shared with the other products; a knob implying otherwise would be a lie,
  so the parameter was removed from the registry rather than left dead.
- **Song/setlist editing is read-only in the UI** for now: songs and setlists load,
  advance and save from the factory content and from hand-edited JSON, but there is no
  in-app song builder yet. Presets themselves save, rename and delete.
- **Expression-pedal curves** are a straight line between the binding's `lo` and `hi`;
  the `curve` field named in the spec's §8 is not implemented.
- **The `tuner` and `diBlend` utility blocks** listed in the spec's block catalog are not
  separate pedals — the tuner is always-on and read from the shared detector, and the DI
  blend is part of the input stage, which is where a player actually wants both.
- pluginval is a CI concern, as with the other products, not part of this repo's ctest.

## Verified

`cmake --build apps/lowend/build --config Release` builds VST3 + Standalone + both test
binaries with MSVC 19.51 / JUCE 8. `ctest` passes: 513 unit checks, and a soak that ran
3 minutes of continuous audio while presets changed, blocks were added, removed and
reordered, and the looper was driven through its whole state machine — 8.9 % of the
real-time budget on average, worst block 3.3 ms against a 5.33 ms budget, no NaN, no
denormals, no drift. The UI was rendered and driven with a mock state to check the Stage
and Board views.
