MIREKU 1.2.1 - test build
Amanorsac Studio


0. WHAT CHANGED IN 1.2.1

   The release accepts MIREKU licence keys only (they start with MIRE-).
   Nothing changes in a test build.

   WHAT CHANGED IN 1.2.0

   Nothing in how a test build plays. The RELEASE of MIREKU now asks for a
   licence key and activates online. A test build does not: it is opened
   by its test key, as before, and makes no network request.

   WHAT CHANGED IN 1.1.3

   Installs over any 1.1 build. Presets, performances, the setlist, the
   library and the test key are kept.

     - CHANGING PERFORMANCE WITH THE SUSTAIN PEDAL DOWN no longer makes the
       held sound jump in volume. The notes you are holding keep the volume,
       effects and tone they were played with until you let them go; new
       notes play the new performance.
     - CHANGING SCENE no longer flashes the screen. A scene that only moves
       knobs updates them in place.

   WHAT CHANGED IN 1.1.2

   Installs over 1.1.1 or 1.1.0. Presets, performances, the setlist and the
   test key are kept; the key keeps the days it has left.

     - THE LIBRARY INSTALLS ITSELF. It is one file now. Choose it in the
       INSTALL LIBRARY panel and MIREKU puts everything in place. See 3.
     - CHANGING PERFORMANCE IS SMOOTH. The next performance is prepared in
       the background while the current one keeps playing; the window no
       longer stops. Going back to a performance you just used is instant.
     - LESS PROCESSOR USE. With nothing playing, the effects rest. Loading
       sounds in the background takes about a quarter of the work it did.
       Large piano performances no longer reload their samples while you
       play.
     - THE MASTER VOLUME AND THE LIMITER STAY PUT when you change performance
       or scene. They are yours, not the performance's.
     - THE METERS READ TRUE. 0 dB is the top; green to -18, yellow to -6,
       red only at the very top. An instrument's meter now shows what it
       sends to the mix, after its VOL knob.
     - ADD UNIT no longer lands on the OUTPUT meter when the rack is full.
     - MIDI CONTROLS, under the gear: every assignment in one list, a LEARN
       button, and a line showing the last message received. Assignments now
       stay through every performance. Sostenuto and soft pedals, master
       volume on CC 7 and the macros on CC 16 to 19 work with nothing to
       learn. See 4.

   WHAT CHANGED IN 1.1.1

   Installs over 1.1.0. Presets, performances, the setlist and the test key
   are kept; the key keeps the days it has left.

     - MAC: performances now play the instruments they were saved with. On a
       Mac, a performance made on Windows played the first sound of each
       category instead.
     - If an instrument really is missing from the library, the status bar
       now says which one and what is playing in its place.

   WHAT CHANGED IN 1.1.0

   Installs over any earlier build. Presets, performances, the setlist and
   the test key are kept; the key keeps the days it has left.

     - THE LIBRARY IS SEALED. Each instrument is now ONE file, Name.jmi,
       encrypted so that only MIREKU can play it. Set LIBRARY FOLDER to the
       "MIREKU Library" folder you were sent and select RESCAN. The folders
       of an earlier library still load on a machine that has them.
     - EVERYTHING YOU SAVE IS A .jmi FILE. Presets, performances and the
       setlist (with its scenes) are written as encrypted .jmi documents in
       MIREKU's own folder; nothing is a .json file any more. Files written
       by an earlier build are still read, and are left where they are.
     - THIS IS THE PLAYER. Saving goes to your own presets and performances;
       the library itself is never written. IMPORT LIBRARY, Measure levels,
       Move to category and Rename sound are studio tools and are not here.
     - THE TEST KEY now runs for 14 days from the day it was issued.

   WHAT CHANGED IN 1.0.5

     - OCTAVE and KEY RANGE PER ENGINE, on the BACK of each PERFORM card.
       Click the card's letter (A, B, C, D) and the card turns over: OCT
       (-2..+2), LOW and HIGH (the keys that engine answers), LEARN LOW and
       LEARN HIGH (the next key you play), ALL KEYS. Click the letter again
       to turn it back. Two engines whose ranges meet are a split; two that
       overlap are a layer. The slot menu (three dots) has the same on the
       model pages. The global TRANSPOSE and OCTAVE still move everything
       together, on top. Saved with the performance.
     - SEAMLESS SWITCHING. Changing performance no longer cuts the sound.
       Notes you are holding, or still holding with the pedal, keep
       sounding from the previous instrument until you let go, as on a
       Yamaha Montage (SSS); the effects hand over without a click. New
       notes go to the new sound.
     - SETLIST. The SETLIST plate between MACRO 2 and MACRO 3 opens a page
       over the whole instrument, on a kente backdrop. ADD SONG names a
       song and picks the performance it plays; EDIT and REMOVE change it;
       drag a song up or down the list to reorder the show. PREV and NEXT
       step through it, a double-click loads a song, and a MIDI program
       change n loads song n. The SUPER KNOB moves every macro linked to
       it: right-click a macro on PERFORM to link it, and press SETTINGS
       under the super knob (or double-click it) for its settings, which
       open in the knob's own place: each macro gets LINK and its travel
       (MIN to MAX, or inverted). The super knob can be MIDI-learned and
       automated like a macro.
     - SCENES. Along the foot of the setlist page. Set the song the way you
       want it (worlds on or off, knobs, effects, macros, the super knob)
       and press SAVE SCENE: everything about that moment is kept as a
       scene of the song. Tap a scene to bring it all back; held notes
       keep sounding. Right-click a scene to update it with what you hear
       now, rename it or remove it. Scenes are saved with the setlist.
     - THE EDIT PAGE. MOD MATRIX, LFO and ARP (bottom bar), a macro's
       targets (double-click the macro) and the super knob settings no
       longer pop out. They open in the band where the effects rack is,
       the way the keyboard does, with tabs across the top (MACROS, MOD
       MATRIX, LFO, ARP, SUPER KNOB) so you move between them without
       leaving the page. On MACROS a second strip picks the macro. CLOSE,
       or the lit bottom-bar plate, brings the rack back.
     - THE KEYS PAGE WHEELS follow your controller: pitch bend and the mod
       wheel from a MIDI keyboard move the on-screen PITCH and MOD wheels.
     - INTELLIGENT BROWSER. Every sound carries INSTRUMENT, MOOD and USE tags
       (Piano, Pad, Organ... Warm, Dark, Big... Worship, Jazz, Cinematic...)
       read from its name and its world. The search box reads the tags, so
       "warm pad" finds a sound named neither. The filter button (beside
       the search box) ticks tags; every ticked tag must be carried. Beside the
       search box, CLEAR drops the typed search and every ticked tag at
       once. A performance takes the INSTRUMENT and MOOD tags of the sounds
       in its slots. Right-click a row, TAGS, to see or correct a sound's
       tags; corrections are kept in settings.json, never written into the
       library.
     - PERFORMANCES DELIVERED IN THE LIBRARY are listed. The PERFORM page's
       FACTORY tab now reads the library folder's Performance\Factory and
       Performance folders as well as MIREKU's own. A performance saved in
       MIREKU under the same name takes the row; the library file is never
       written.

   WHAT CHANGED IN 1.0.4

   Installs over any earlier build. The library, presets, performances and
   the test key are kept; the key keeps the days it has left.

     - JM ROTARY, a new unit in ADD UNIT, for organs. SPEED is the half-moon
       switch: SLOW (chorale), FAST (tremolo) and BRAKE. DRIVE is the valve
       amplifier in front of the cabinet. HORN and DRUM set how hard each
       rotor moves the sound; MIC WIDTH sets how far apart the two
       microphones sit (0 is one microphone, mono); MIX is dry to wet. The
       light horn changes speed in about a second, the heavy drum takes four
       to spin up and six to slow down, as on the real cabinet.
     - The OUTPUT plate now shows two meters, L and R, one per channel.
     - AUDIO / MIDI is a window of MIREKU's own, opened from the
       gear's panel: driver, output device, sample rate, buffer size and the MIDI
       inputs. The grey system dialog is gone.
     - MULTI OUT. Each world (JM Set, MKS, FM, Strings) can be sent to its
       own output, OUT 1 to OUT 4, in the same window. In the standalone each
       OUT is mapped to a pair of channels on the interface; in a DAW the
       plug-in offers four stereo outputs and the host routes them. A world
       sent to an output the host has not enabled plays through OUT 1.
     - The browser shows the names James gave his sounds again ("Mireku
       Velvet Pads vip", not the folder name).
     - A sound with TINE or BODY off centre started slightly differently
       depending on the buffer size, and re-opening the audio device let the
       previous note's tail ring into the next. Both fixed; every sound now
       renders identically at every buffer size and sample rate.

   WHAT CHANGED IN 1.0.3

   Installs over any earlier build. The library, presets, performances and
   the test key are kept; the key keeps the days it has left.

     - JM MODULATION, a new unit in ADD UNIT: one INTENSITY knob, RATE, MIX,
       four modes (chorus, flanger, phaser, wow+flut) and a MONO switch. In
       WOW+FLUT the mix is locked full wet, as on the pedal.
     - WOW/FLUTTER on the TAPE MACHINE now moves the pitch for real. At 50 it
       is a tape wobble; at 100 it is seasick. Before, it barely moved.
     - IMPORT LIBRARY reads the JM Signature Sound Library (Full Expansion):
       stereo pairs, one-shot sounds and the named velocity layers (EP2C
       Velocity 1..8 and the like) all come in as one instrument each.
     - IMPORT LIBRARY asks WHERE each instrument goes (JM Set, MKS, FM or
       Strings) before it writes anything. ALL sets every row at once.
     - A sound that cannot be laid out is skipped and named in the report;
       the rest of the file still imports.

   WHAT CHANGED IN 1.0.2

   Installs over 1.0.1 or 1.0.0. The library, presets, performances and the
   test key are kept; the key keeps the days it has left.

     - VELOCITY: you can hear it now. The old law only moved 9 dB from the
       softest touch to the hardest; sounds now follow the standard sampler
       law (about 40 dB of range). Every sound will feel more dynamic.
     - ATTACK is 0 by default on every sound. Saved presets keep their own.
     - PLATE and HALL/SHIMMER are a new reverb. DECAY now means seconds, the
       level no longer rises with DECAY, stereo is kept, and SHIMMER is a
       real shimmer (a proper pitch shifter, and a much denser tail). It no
       longer crashes at extreme settings. Reverb presets will sound
       different - that is the point.
     - Two new units in ADD UNIT: TAPE MACHINE (drive, wow/flutter, age,
       hiss, and a DETUNE in cents that spreads two copies left/right) and
       MODULATOR (ring modulation or frequency shifting, with an LFO).
     - Third-party licence notices are installed as THIRD-PARTY-LICENCES.txt.

   WHAT CHANGED IN 1.0.1

   Installs over 1.0.0. The library, presets, performances and the test key
   are kept; the key keeps the days it has left.

     - The AMP ENV page was blank. Fixed.
     - Effect boxes are sized to their controls instead of all one width.
     - VELOCITY RESPONSE, on the SETTINGS tab of every sound. CURVE is for
       the whole instrument and is the same five as a Montage keyboard:
       Normal, Soft, Hard, Wide, Fixed. If the keyboard already has a curve
       set, leave this on NORMAL. DEPTH and OFFSET are per sound (64 is
       neutral) and are saved with the preset.
     - ANCHORED PLAYBACK for recorded sounds. Right-click a sound in the list
       and select "Measure levels (anchored playback)". MIREKU reads every
       sample, and from then on that sound plays ONE layer per note instead of
       blending two recordings, with the level following one smooth curve
       through the measured layers. It takes a moment on a big piano. The
       previous instrument.json is kept as a .bak beside it. Sounds not yet
       measured play exactly as before.
     - The categories are JM SET, MKS, FM and STRINGS.


1. WHAT THIS IS

   A test build of MIREKU, a sampled keyboard instrument. It is not the released
   product and must not be distributed or sold.

   Installed:
     Standalone application   C:\Program Files\Amanorsac Studio\MIREKU
     VST3 plug-in             C:\Program Files\Common Files\VST3\MIREKU.vst3


2. THE TEST KEY

   This build requires the key sent with it. MIREKU asks for the key when it
   first opens. Paste it whole, including the dot, and select UNLOCK.

   The key is stored, so it is asked for once per computer. It expires 14 days
   after it was issued. After that date MIREKU opens but produces no sound until
   a new key is entered. Request a new key when the one in use runs out.

   Setting the computer's clock back does not extend a key.


3. THE SAMPLE LIBRARY

   The library is ONE file, "MIREKU Library.mirekupack", downloaded
   separately. There is nothing to unzip and nothing to move.

   The first time MIREKU opens without a library it shows INSTALL LIBRARY.
   Select CHOOSE THE LIBRARY FILE and pick the file you downloaded. MIREKU
   checks it, puts every instrument and performance in its own folder
   (Documents\MIREKU\Library) and shows them. To use another disk, select
   CHANGE first.

   The same panel is under the gear, "Install library or update". A small
   update file with new performances installs the same way and adds to what
   is there.

   Each instrument is one sealed .jmi file that only MIREKU can open. If you
   already unpacked a library folder for an earlier build it still works:
   gear, Preferences, BROWSE to the folder.

   Installing and uninstalling never touch the library, the presets or the
   performances.


4. AUDIO AND MIDI

   In the standalone, select the gear, then AUDIO / MIDI..., and choose
   the interface driver. Select ASIO where the interface provides one; it
   gives the lowest latency. A buffer of 128 or 256 samples is a reasonable
   starting point.

   MIDI inputs are listed in the same window. Connect a keyboard before
   opening MIREKU, or select RESCAN MIDI afterwards.

   MIDI CONTROLS. The gear, MIDI controls, lists every assignment with its
   controller number, and shows the last message that arrived so you can see
   what a knob sends. LEARN A CONTROL picks a control from a list; then move
   a knob, fader or pedal. Right-clicking any knob and selecting MIDI Learn
   does the same. Assignments are yours: they stay through every performance.

   These work with nothing learned: pitch bend, mod wheel (CC 1), sustain
   (64), sostenuto (66), soft pedal (67), expression (11), master volume
   (7), macros 1 to 4 (16 to 19), all sound off (120), all notes off (123),
   and program change n for song n of the setlist. An assignment of your
   own to one of those numbers takes its place.

   MULTI OUT. Each world (JM Set, MKS, FM, Strings) can be sent to OUT 1,
   OUT 2, OUT 3 or OUT 4. In the standalone each OUT is mapped to a pair of
   channels on the interface in the same window. In a DAW the plug-in offers
   four stereo outputs; enable them in the host and route each as a track.
   A world sent to an output the host has not enabled plays through OUT 1.


5. BUILDING PRESETS

   Presets and performances are saved as .jmi files in MIREKU's own folder
   (Documents\MIREKU\Presets), never into the library.

     SAVE AS              saves what is on screen as a new preset. It never
                          replaces an existing one.
     OVERWRITE            replaces the preset that is loaded. It takes two
                          selections and keeps the previous version as a .bak
                          file beside it.
     SAVE PERFORMANCE     saves the whole stage: four slots, macros, modulation
                          and the arpeggiator. A patch cannot hold modulation;
                          a performance can.

   To rename one of your own presets or performances, right-click it in the
   list and select "Rename".


6. SENDING WORK BACK

   Everything you save lives in Documents\MIREKU\Presets: your presets by
   world, your performances in Performance, and the setlist with its scenes
   in Setlists. Zip that folder and send it back with your notes.

   Nothing is uploaded: MIREKU makes no network request of any kind.


7. REPORTING A PROBLEM

   Note what was on screen, what was selected, and what happened. Include the
   audio driver and buffer size from the audio settings panel.

   hello@amanorsac.studio
