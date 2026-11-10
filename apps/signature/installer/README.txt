MIREKU 1.1.2
Amanorsac Studio


1. WHAT IS INSTALLED

     Standalone application   C:\Program Files\Amanorsac Studio\MIREKU
     VST3 plug-in             C:\Program Files\Common Files\VST3\MIREKU.vst3

   No licence key and no account are required.


2. THE SAMPLE LIBRARY

   The library is ONE file, "MIREKU Library.mirekupack", delivered
   separately. There is nothing to unzip and nothing to move.

   The first time MIREKU opens without a library it shows INSTALL LIBRARY.
   Select CHOOSE THE LIBRARY FILE and pick the file. MIREKU checks it, puts
   every instrument and performance in its own folder
   (Documents\MIREKU\Library) and shows them. To use another disk, select
   CHANGE first. The same panel is under the gear, "Install library or
   update"; an update file with new performances installs the same way.

   Each instrument is one sealed .jmi file that only MIREKU can open.

   Installing and uninstalling never touch the library, the presets or the
   performances.


3. AUDIO AND MIDI

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

   OCTAVE and TRANSPOSE. The two steppers on PERFORM move every engine.
   Click a card's letter on PERFORM and it turns over to that engine's own
   OCT and LOW / HIGH key range; on the model pages they are in the slot's
   menu (the three dots), which can also learn a key from the keyboard.

   SETLIST. The SETLIST plate on PERFORM opens the show page over the whole
   instrument: songs in order (drag them to reorder), each naming the
   performance it plays, PREV and NEXT, and the SUPER KNOB that moves every
   macro linked to it (SETTINGS under the knob sets each macro's travel).
   A MIDI program change n loads song n.

   SCENES. Along the foot of the setlist page. SAVE SCENE keeps everything
   about the moment (which worlds are on, every knob, the effects, the
   macros, the super knob) as a scene of the loaded song. Tap a scene to
   bring it all back; held notes keep sounding. Right-click a scene to
   update, rename or remove it.

   THE EDIT PAGE. MOD MATRIX, LFO and ARP on the bottom bar, a macro's
   targets (double-click the macro) and the super knob settings open in
   the band where the effects rack is, with tabs across the top. CLOSE
   brings the rack back.

   MULTI OUT. Each world (JM Set, MKS, FM, Strings) can be sent to OUT 1,
   OUT 2, OUT 3 or OUT 4. In the standalone each OUT is mapped to a pair of
   channels on the interface in the same window. In a DAW the plug-in offers
   four stereo outputs; enable them in the host and route each as a track.
   A world sent to an output the host has not enabled plays through OUT 1.


4. SAVING WORK

     SAVE AS              saves what is on screen as a new preset. It never
                          replaces an existing one.
     OVERWRITE            replaces the preset that is loaded. It takes two
                          selections and keeps the previous version as a .bak
                          file beside it.
     SAVE PERFORMANCE     saves the whole stage: four slots, macros, modulation
                          and the arpeggiator. A patch cannot hold modulation;
                          a performance can.

   Presets and performances are saved as .jmi files in Documents\MIREKU\Presets,
   never into the library. To rename one of your own presets or performances,
   right-click it in the list and select "Rename".


5. PRIVACY

   MIREKU makes no network request of any kind. Nothing is collected and
   nothing is reported.


6. SUPPORT

   hello@amanorsac.studio
