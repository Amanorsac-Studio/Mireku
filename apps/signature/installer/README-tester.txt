MIREKU 1.0.0 - test build
Amanorsac Studio


1. WHAT THIS IS

   A test build of MIREKU, a sampled keyboard instrument. It is not the released
   product and must not be distributed or sold.

   Installed:
     Standalone application   C:\Program Files\Amanorsac Studio\MIREKU
     VST3 plug-in             C:\Program Files\Common Files\VST3\MIREKU.vst3


2. THE TEST KEY

   This build requires the key sent with it. MIREKU asks for the key when it
   first opens. Paste it whole, including the dot, and select UNLOCK.

   The key is stored, so it is asked for once per computer. It expires 30 days
   after it was issued. After that date MIREKU opens but produces no sound until
   a new key is entered. Request a new key when the one in use runs out.

   Setting the computer's clock back does not extend a key.


3. THE SAMPLE LIBRARY

   The library is delivered separately; it is not part of this installer.

   Put the .jmi folders anywhere, then open the SETTINGS tab and set LIBRARY
   FOLDER to the folder holding them. Select RESCAN. The four category tabs
   fill with whatever was found.

   Installing and uninstalling never touch the library, the presets or the
   performances.


4. AUDIO AND MIDI

   In the standalone, open the audio settings and choose the interface driver.
   Select ASIO where the interface provides one; it gives the lowest latency.
   A buffer of 128 or 256 samples is a reasonable starting point.

   Connect a MIDI keyboard before opening MIREKU, or enable it in the same
   settings panel afterwards.


5. BUILDING PRESETS

   Patches are saved into the library bundle they belong to.

     SAVE AS              saves what is on screen as a new preset. It never
                          replaces an existing one.
     OVERWRITE            replaces the preset that is loaded. It takes two
                          selections and keeps the previous version as a .bak
                          file beside it.
     SAVE PERFORMANCE     saves the whole stage: four slots, macros, modulation
                          and the arpeggiator. A patch cannot hold modulation;
                          a performance can.

   To rename a sound, right-click it in the list and select "Rename sound".
   To move a sound to another category, right-click it and select
   "Move to category".


6. SENDING WORK BACK

   Open the SETTINGS tab and select SHARE PRESETS. A folder is written
   containing every preset, performance, and the category and name each sound
   is filed under, with a manifest listing what is in it.

   Send that folder back. Nothing is uploaded: MIREKU makes no network request
   of any kind.


7. REPORTING A PROBLEM

   Note what was on screen, what was selected, and what happened. Include the
   audio driver and buffer size from the audio settings panel.

   hello@amanorsac.studio
