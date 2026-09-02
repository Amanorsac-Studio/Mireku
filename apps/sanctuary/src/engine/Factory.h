// Factory.h — Sanctuary's factory content (SANCTUARY-BUILD-SPEC.md §10).
//
// Twelve Songs, three Sets and seven Surfaces are GENERATED on first run from
// whatever bundles the library holds, so the Stage is fully exercisable before
// any captured instruments exist. Everything is written as plain JSON files in
// the user's Songs / Sets / Surfaces folders and can be edited or deleted.
#pragma once
#include "StageModel.h"
#include "Library.h"

namespace sanctuary {
namespace factory {

//==============================================================================
struct BundlePick
{
    std::array<std::optional<jm::BundleInfo>, 4> byWorld;   // first bundle per world
    explicit BundlePick (const std::vector<jm::BundleInfo>& all)
    {
        for (auto& b : all)
            if (b.world >= 0 && b.world < 4 && ! byWorld[(size_t) b.world]) byWorld[(size_t) b.world] = b;
    }
    bool any() const { for (auto& b : byWorld) if (b) return true; return false; }
};

inline void assignSlot (SlotSpec& s, const BundlePick& pick, int world, const juce::String& label,
                        float volDb = 0.0f, int keyLo = 0, int keyHi = 127)
{
    s.world = world;
    s.label = label;
    s.engineParams = jm::defaultEngineParams (world);
    s.rack = jm::buildDefaultRack (world);
    s.keyLo = keyLo; s.keyHi = keyHi;
    s.volumeDb = volDb;
    if (pick.byWorld[(size_t) world])
    {
        s.bundleDir = pick.byWorld[(size_t) world]->dir.getFullPathName();
        s.bundleName = pick.byWorld[(size_t) world]->name;
        s.presetName = "Factory";
        s.enabled = true;
    }
    else { s.bundleDir.clear(); s.enabled = false; }
}
inline void unbypass (FxRackState& rack, FxType t, std::map<std::string, float> params = {})
{
    for (auto& u : rack) if (u.type == t) { u.bypassed = false; for (auto& [k, v] : params) u.params[k] = v; }
}
inline Moment moment (const juce::String& name, juce::uint32 c, float bars, std::map<juce::String, float> diff = {})
{
    Moment m; m.id = juce::Uuid().toString(); m.name = name; m.colour = c; m.transitionBars = bars; m.diff = std::move (diff);
    return m;
}
inline float n01 (float v, float lo, float hi) { return (v - lo) / (hi - lo); }      // normalise helper
inline float volN (float db) { return n01 (db, -60.0f, 6.0f); }

// Moment colours
constexpr juce::uint32 cVerse = 0xff3D8BFF, cChorus = 0xff35D3F2, cBridge = 0xff8E7CFF, cBig = 0xff4FE0C2, cDown = 0xff2C4270, cOutro = 0xff8A9BB8;

//==============================================================================
inline std::vector<Song> songs (const BundlePick& pick)
{
    std::vector<Song> out;
    auto base = [&] (const juce::String& title, int key, bool minor, double bpm, const juce::String& meter,
                     juce::StringArray tags, const juce::String& notes)
    {
        Song s; s.title = title; s.songKey = key; s.minor = minor; s.playIn = key; s.soundIn = key;
        s.tempo = bpm; s.meter = meter; s.tags = tags; s.notes = notes; s.artist = "Amanorsac Studio";
        s.moments.clear();
        s.baseRig.drone.key = key; s.baseRig.drone.minor = minor;
        return s;
    };

    // 1 — Piano-led contemporary
    {
        auto s = base ("Open Skies", 7, false, 74, "4/4", { "piano-led", "moderate", "contemporary" },
                       "Piano carries the verse. Pad (slot B) rides under everything; bring the Strings in for the chorus with MACRO 1 (Swell).\nDrone follows the song key.");
        assignSlot (s.baseRig.slots[0], pick, 0, "Piano", 0.0f);
        assignSlot (s.baseRig.slots[1], pick, 1, "Pad", -9.0f);
        assignSlot (s.baseRig.slots[2], pick, 3, "Strings", -12.0f);
        assignSlot (s.baseRig.slots[3], pick, 2, "Bell", -18.0f, 72, 127);
        s.baseRig.slots[3].enabled = false;
        unbypass (s.baseRig.slots[0].rack, FxType::plateReverb, { { "mix", 18.0f } });
        unbypass (s.baseRig.slots[1].rack, FxType::hallShimmer, { { "mix", 30.0f } });
        s.baseRig.macros[0].name = "SWELL";
        s.baseRig.macros[0].links = { { "slot2.vol", volN (-30.0f), volN (-3.0f) }, { "slot2.engine.swell", 0.2f, 0.8f } };
        s.baseRig.drone.on = true; s.baseRig.drone.level = 55; s.baseRig.drone.character = 1;
        s.moments = { moment ("Verse", cVerse, 1, {}),
                      moment ("Chorus", cChorus, 2, { { "slot2.vol", volN (-4.0f) }, { "slot1.vol", volN (-5.0f) }, { "drone.brightness", 0.65f } }),
                      moment ("Bridge", cBridge, 1, { { "slot3.enabled", 1.0f }, { "slot0.vol", volN (-6.0f) }, { "drone.air", 0.7f } }),
                      moment ("Big", cBig, 2, { { "slot2.vol", volN (0.0f) }, { "slot3.enabled", 1.0f }, { "drone.level", 0.75f }, { "drone.brightness", 0.8f } }),
                      moment ("Outro", cOutro, 4, { { "slot0.vol", volN (-12.0f) }, { "slot2.vol", volN (-20.0f) }, { "drone.level", 0.8f } }) };
        out.push_back (s);
    }
    // 2 — Pad-led ballad
    {
        auto s = base ("Still Waters", 1, false, 66, "4/4", { "pad-led", "simple", "ballad" },
                       "Almost nothing but pad and drone. Keep the piano soft (velocity ceiling 100). Chorus adds the strings.");
        assignSlot (s.baseRig.slots[0], pick, 1, "Pad", -3.0f);
        assignSlot (s.baseRig.slots[1], pick, 0, "Soft Piano", -8.0f); s.baseRig.slots[1].velHi = 100;
        assignSlot (s.baseRig.slots[2], pick, 3, "Strings", -16.0f);
        assignSlot (s.baseRig.slots[3], pick, 2, "FM", -24.0f); s.baseRig.slots[3].enabled = false;
        unbypass (s.baseRig.slots[0].rack, FxType::hallShimmer, { { "mix", 35.0f }, { "shimmer", 20.0f } });
        s.baseRig.drone.on = true; s.baseRig.drone.level = 65; s.baseRig.drone.character = 4; s.baseRig.drone.shimmer = 40;
        s.moments = { moment ("Verse", cVerse, 1), moment ("Chorus", cChorus, 2, { { "slot2.vol", volN (-6.0f) }, { "drone.air", 0.6f } }),
                      moment ("Prayer", cDown, 4, { { "slot1.vol", volN (-20.0f) }, { "slot2.vol", volN (-30.0f) }, { "drone.level", 0.85f } }) };
        out.push_back (s);
    }
    // 3 — Hymn
    {
        auto s = base ("Great Is Thy Faithfulness", 3, false, 84, "3/4", { "hymn", "piano-led", "simple" },
                       "Traditional hymn. Piano only in verse 1, strings from verse 2, everything for the last verse. Wheel: play in Eb, congregation sings in Eb.");
        assignSlot (s.baseRig.slots[0], pick, 0, "Piano", 0.0f);
        assignSlot (s.baseRig.slots[1], pick, 3, "Strings", -40.0f);
        assignSlot (s.baseRig.slots[2], pick, 1, "Pad", -40.0f);
        assignSlot (s.baseRig.slots[3], pick, 2, "FM", -40.0f); s.baseRig.slots[3].enabled = false;
        s.baseRig.drone.on = false; s.baseRig.drone.character = 2;
        s.moments = { moment ("Verse 1", cVerse, 0), moment ("Verse 2", cVerse, 2, { { "slot1.vol", volN (-10.0f) } }),
                      moment ("Verse 3", cChorus, 2, { { "slot1.vol", volN (-6.0f) }, { "slot2.vol", volN (-12.0f) } }),
                      moment ("Last Verse", cBig, 1, { { "slot1.vol", volN (-2.0f) }, { "slot2.vol", volN (-8.0f) }, { "drone.on", 1.0f }, { "drone.level", 0.6f } }) };
        out.push_back (s);
    }
    // 4 — Gospel shout
    {
        auto s = base ("Shout Break", 10, false, 132, "12/8", { "gospel", "shout", "organ", "complex" },
                       "Praise break. Bright EP (slot A) doubles the organ line; left hand bass below F2 on slot D. Tempo 132, swung 12/8. No drone.");
        assignSlot (s.baseRig.slots[0], pick, 0, "Bright EP", 0.0f, 53, 127);
        assignSlot (s.baseRig.slots[1], pick, 2, "FM Stab", -6.0f, 53, 127);
        assignSlot (s.baseRig.slots[2], pick, 1, "Pad", -40.0f); s.baseRig.slots[2].enabled = false;
        assignSlot (s.baseRig.slots[3], pick, 1, "Synth Bass", -4.0f, 0, 52); s.baseRig.slots[3].octaveShift = -1;
        s.baseRig.slots[0].engineParams["bark"] = 70; s.baseRig.slots[0].engineParams["tine"] = 75;
        unbypass (s.baseRig.slots[0].rack, FxType::epPreamp, { { "drive", 35.0f } });
        unbypass (s.baseRig.slots[0].rack, FxType::tremoloAutoPan, { { "rate", 6.5f }, { "depth", 45.0f } });
        s.baseRig.drone.on = false;
        s.moments = { moment ("Groove", cVerse, 0), moment ("Shout", cBig, 0, { { "slot1.vol", volN (0.0f) }, { "slot0.engine.bark", 0.9f } }),
                      moment ("Breakdown", cDown, 1, { { "slot1.vol", volN (-20.0f) }, { "slot3.vol", volN (-12.0f) } }) };
        out.push_back (s);
    }
    // 5 — Prayer underscore
    {
        auto s = base ("Underscore (Prayer)", 0, false, 60, "4/4", { "underscore", "pad-led", "simple", "drone" },
                       "For prayer and altar moments. Mostly drone; slot A pad under held chords. Drone follows CHORDS here — play a chord, hold it, the bed re-voices after 600 ms.");
        assignSlot (s.baseRig.slots[0], pick, 1, "Pad", -6.0f);
        assignSlot (s.baseRig.slots[1], pick, 3, "Strings", -18.0f);
        assignSlot (s.baseRig.slots[2], pick, 0, "Piano", -40.0f); s.baseRig.slots[2].enabled = false;
        assignSlot (s.baseRig.slots[3], pick, 2, "FM", -40.0f); s.baseRig.slots[3].enabled = false;
        s.baseRig.drone.on = true; s.baseRig.drone.level = 80; s.baseRig.drone.follow = DroneFollow::chords;
        s.baseRig.drone.character = 5; s.baseRig.drone.brightness = 30; s.baseRig.drone.shimmer = 45;
        s.moments = { moment ("Hold", cDown, 2), moment ("Lift", cChorus, 4, { { "drone.brightness", 0.6f }, { "drone.air", 0.7f }, { "slot1.vol", volN (-10.0f) } }) };
        out.push_back (s);
    }
    // 6 — Youth / EDM-leaning
    {
        auto s = base ("Lightning", 9, true, 128, "4/4", { "youth", "synth", "synced", "complex" },
                       "Synth-led. Slot A FM lead above C4 with synced digital delay; slot D synth bass below. MACRO 1 opens the filter-ish brightness on the lead.");
        assignSlot (s.baseRig.slots[0], pick, 2, "Lead", -2.0f, 60, 127);
        assignSlot (s.baseRig.slots[1], pick, 1, "Pad", -10.0f);
        assignSlot (s.baseRig.slots[2], pick, 3, "Strings", -40.0f); s.baseRig.slots[2].enabled = false;
        assignSlot (s.baseRig.slots[3], pick, 1, "Bass", -3.0f, 0, 59); s.baseRig.slots[3].octaveShift = -1;
        unbypass (s.baseRig.slots[0].rack, FxType::digitalDelay, { { "mix", 28.0f } });
        s.baseRig.macros[0].name = "OPEN";
        s.baseRig.macros[0].links = { { "slot0.engine.brightness", 0.3f, 1.0f }, { "slot0.engine.glass", 0.2f, 0.9f } };
        s.baseRig.drone.on = true; s.baseRig.drone.level = 40; s.baseRig.drone.character = 7;
        s.moments = { moment ("Intro", cDown, 0, { { "slot0.vol", volN (-40.0f) }, { "slot3.vol", volN (-40.0f) } }),
                      moment ("Verse", cVerse, 1, { { "slot3.vol", volN (-6.0f) } }),
                      moment ("Drop", cBig, 0, { { "slot0.vol", volN (0.0f) }, { "slot3.vol", volN (-1.0f) }, { "drone.level", 0.6f } }) };
        out.push_back (s);
    }
    // 7 — One rig for the whole set (Moments only)
    {
        auto s = base ("One Rig — Whole Set", 2, false, 72, "4/4", { "go-to", "moderate", "piano-led", "pad-led" },
                       "The one-patch philosophy: never change Songs, only Moments. DOWN = piano + drone, MID = + pad, BIG = + strings + bells, OUT = drone only.\nSet the Wheel per song; everything follows.");
        assignSlot (s.baseRig.slots[0], pick, 0, "Piano", 0.0f);
        assignSlot (s.baseRig.slots[1], pick, 1, "Pad", -9.0f);
        assignSlot (s.baseRig.slots[2], pick, 3, "Strings", -12.0f);
        assignSlot (s.baseRig.slots[3], pick, 2, "Bells", -16.0f, 72, 127);
        s.baseRig.drone.on = true; s.baseRig.drone.level = 55; s.baseRig.drone.character = 0;
        s.moments = { moment ("Down", cDown, 2, { { "slot1.vol", volN (-40.0f) }, { "slot2.vol", volN (-40.0f) }, { "slot3.enabled", 0.0f } }),
                      moment ("Mid", cVerse, 2, { { "slot2.vol", volN (-40.0f) }, { "slot3.enabled", 0.0f } }),
                      moment ("Big", cBig, 2, {}),
                      moment ("Out", cOutro, 4, { { "slot0.vol", volN (-40.0f) }, { "slot1.vol", volN (-40.0f) }, { "slot2.vol", volN (-40.0f) }, { "slot3.enabled", 0.0f }, { "drone.level", 0.85f } }) };
        out.push_back (s);
    }
    // 8 — Minor-key worship
    {
        auto s = base ("Deep Calls", 4, true, 70, "6/8", { "minor", "pad-led", "moderate" },
                       "E minor, 6/8. Drone voiced minor. Rhodes with tremolo; strings swell into the bridge.");
        assignSlot (s.baseRig.slots[0], pick, 0, "Rhodes", -2.0f);
        assignSlot (s.baseRig.slots[1], pick, 1, "Pad", -8.0f);
        assignSlot (s.baseRig.slots[2], pick, 3, "Strings", -20.0f);
        assignSlot (s.baseRig.slots[3], pick, 2, "FM", -40.0f); s.baseRig.slots[3].enabled = false;
        unbypass (s.baseRig.slots[0].rack, FxType::tremoloAutoPan, { { "depth", 55.0f }, { "rate", 4.0f } });
        s.baseRig.drone.on = true; s.baseRig.drone.minor = true; s.baseRig.drone.level = 60; s.baseRig.drone.character = 5;
        s.moments = { moment ("Verse", cVerse, 1), moment ("Chorus", cChorus, 2, { { "slot2.vol", volN (-8.0f) } }),
                      moment ("Bridge", cBridge, 3, { { "slot2.vol", volN (-3.0f) }, { "slot2.engine.swell", 0.8f }, { "drone.brightness", 0.7f } }) };
        out.push_back (s);
    }
    // 9 — Split: bass left, keys right
    {
        auto s = base ("Two Hands", 5, false, 96, "4/4", { "split", "bass", "moderate" },
                       "Left hand below C3 is synth bass (slot D, −1 oct, ignores the Wheel? no — it follows). Right hand piano + pad.");
        assignSlot (s.baseRig.slots[0], pick, 0, "Piano", 0.0f, 48, 127);
        assignSlot (s.baseRig.slots[1], pick, 1, "Pad", -10.0f, 48, 127);
        assignSlot (s.baseRig.slots[2], pick, 3, "Strings", -40.0f); s.baseRig.slots[2].enabled = false;
        assignSlot (s.baseRig.slots[3], pick, 1, "Bass", -2.0f, 0, 47); s.baseRig.slots[3].octaveShift = -1;
        s.baseRig.drone.on = false;
        s.moments = { moment ("Verse", cVerse, 1), moment ("Chorus", cChorus, 1, { { "slot1.vol", volN (-5.0f) } }) };
        out.push_back (s);
    }
    // 10 — Christmas / seasonal bells
    {
        auto s = base ("Bells at Midnight", 7, false, 90, "3/4", { "seasonal", "bells", "simple" },
                       "Bright FM bells above C5 layered on piano; hall reverb on the master.");
        assignSlot (s.baseRig.slots[0], pick, 0, "Piano", -1.0f);
        assignSlot (s.baseRig.slots[1], pick, 2, "Bells", -8.0f, 72, 127);
        assignSlot (s.baseRig.slots[2], pick, 3, "Strings", -16.0f);
        assignSlot (s.baseRig.slots[3], pick, 1, "Pad", -40.0f); s.baseRig.slots[3].enabled = false;
        s.baseRig.masterRack = { jm::defaultUnitState (FxType::hallShimmer, false) };
        s.baseRig.masterRack[0].params["mix"] = 18.0f;
        s.baseRig.drone.on = true; s.baseRig.drone.level = 45; s.baseRig.drone.character = 0;
        s.moments = { moment ("Verse", cVerse, 1), moment ("Refrain", cChorus, 1, { { "slot1.vol", volN (-3.0f) } }) };
        out.push_back (s);
    }
    // 11 — Altar call / spontaneous
    {
        auto s = base ("Spontaneous", 9, false, 68, "4/4", { "spontaneous", "drone", "pad-led", "moderate" },
                       "Free worship. Drone in CHORD-FOLLOW; play slowly and let it move under you. Bring piano up with the Live Trim if the leader starts a song.");
        assignSlot (s.baseRig.slots[0], pick, 1, "Pad", -4.0f);
        assignSlot (s.baseRig.slots[1], pick, 0, "Piano", -14.0f);
        assignSlot (s.baseRig.slots[2], pick, 3, "Strings", -22.0f);
        assignSlot (s.baseRig.slots[3], pick, 2, "FM", -40.0f); s.baseRig.slots[3].enabled = false;
        s.baseRig.drone.on = true; s.baseRig.drone.level = 75; s.baseRig.drone.follow = DroneFollow::chords; s.baseRig.drone.character = 2;
        s.moments = { moment ("Flow", cVerse, 2), moment ("Build", cBig, 4, { { "slot1.vol", volN (-4.0f) }, { "slot2.vol", volN (-8.0f) }, { "drone.brightness", 0.75f } }) };
        out.push_back (s);
    }
    // 12 — Communion
    {
        auto s = base ("Communion", 8, false, 62, "4/4", { "communion", "simple", "piano-led" },
                       "Quiet. Felt-style piano (velocity ceiling 90) and a low, dark drone. Nothing else.");
        assignSlot (s.baseRig.slots[0], pick, 0, "Piano", -4.0f); s.baseRig.slots[0].velHi = 90;
        assignSlot (s.baseRig.slots[1], pick, 1, "Pad", -40.0f); s.baseRig.slots[1].enabled = false;
        assignSlot (s.baseRig.slots[2], pick, 3, "Strings", -40.0f); s.baseRig.slots[2].enabled = false;
        assignSlot (s.baseRig.slots[3], pick, 2, "FM", -40.0f); s.baseRig.slots[3].enabled = false;
        s.baseRig.drone.on = true; s.baseRig.drone.level = 50; s.baseRig.drone.character = 5; s.baseRig.drone.reg = DroneRegister::low; s.baseRig.drone.brightness = 25;
        s.moments = { moment ("Serve", cDown, 2), moment ("Close", cOutro, 4, { { "slot0.vol", volN (-14.0f) }, { "drone.level", 0.7f } }) };
        out.push_back (s);
    }
    return out;
}

//==============================================================================
inline std::vector<Set> sets (const std::vector<Song>& songList)
{
    auto id = [&] (int i) { return i >= 0 && i < (int) songList.size() ? songList[(size_t) i].id : juce::String(); };
    std::vector<Set> out;
    {
        Set s; s.name = "Morning service sample"; s.serviceType = "Morning";
        s.notes = "Open Skies → Still Waters → hymn → Underscore for prayer → Spontaneous → Communion.";
        for (int i : { 0, 1, 2, 4, 10, 11 }) s.songs.push_back ({ id (i), std::nullopt, std::nullopt });
        out.push_back (s);
    }
    {
        Set s; s.name = "Midweek sample"; s.serviceType = "Midweek";
        s.notes = "Shorter set; Deep Calls moved to F#m for the leader.";
        s.songs.push_back ({ id (7), 6, std::nullopt });
        s.songs.push_back ({ id (8), std::nullopt, std::nullopt });
        s.songs.push_back ({ id (4), std::nullopt, std::nullopt });
        out.push_back (s);
    }
    {
        Set s; s.name = "Rehearsal"; s.serviceType = "Rehearsal";
        s.notes = "Everything, in order, for checking sounds.";
        for (int i = 0; i < (int) songList.size(); ++i) s.songs.push_back ({ id (i), std::nullopt, std::nullopt });
        out.push_back (s);
    }
    return out;
}

//==============================================================================
inline Surface generic8 (const juce::String& name, const juce::String& device,
                         std::vector<int> knobCcs, std::vector<int> faderCcs, std::vector<int> buttonCcs, std::vector<int> padNotes)
{
    Surface s; s.name = name; s.deviceMatch = device;
    int n = 1;
    for (int cc : knobCcs)   { s.controls.push_back ({ "knob" + juce::String (n), ControlKind::knob, cc, -1, 0, "K" + juce::String (n), ControlMode::absolute }); ++n; }
    n = 1;
    for (int cc : faderCcs)  { s.controls.push_back ({ "fader" + juce::String (n), ControlKind::fader, cc, -1, 0, "F" + juce::String (n), ControlMode::absolute }); ++n; }
    n = 1;
    for (int cc : buttonCcs) { s.controls.push_back ({ "btn" + juce::String (n), ControlKind::button, cc, -1, 0, "B" + juce::String (n), ControlMode::momentary }); ++n; }
    n = 1;
    for (int note : padNotes){ s.controls.push_back ({ "pad" + juce::String (n), ControlKind::pad, -1, note, 0, "P" + juce::String (n), ControlMode::momentary }); ++n; }
    s.controls.push_back ({ "pedal1", ControlKind::pedal, 66, -1, 0, "Pedal 1", ControlMode::momentary });
    s.controls.push_back ({ "pedal2", ControlKind::pedal, 67, -1, 0, "Pedal 2", ControlMode::momentary });
    // default bindings — the eight knobs mirror the Live Trim, buttons move the stage
    for (int i = 1; i <= juce::jmin (8, (int) knobCcs.size()); ++i) s.bind ("knob" + juce::String (i), "trim" + juce::String (i));
    for (int i = 1; i <= juce::jmin (4, (int) faderCcs.size()); ++i) s.bind ("fader" + juce::String (i), "slot" + juce::String (i - 1) + ".vol");
    if (faderCcs.size() >= 5) s.bind ("fader5", "drone.level");
    if (buttonCcs.size() >= 1) s.bind ("btn1", "stage.prevMoment");
    if (buttonCcs.size() >= 2) s.bind ("btn2", "stage.nextMoment");
    if (buttonCcs.size() >= 3) s.bind ("btn3", "stage.prevSong");
    if (buttonCcs.size() >= 4) s.bind ("btn4", "stage.nextSong");
    if (buttonCcs.size() >= 5) s.bind ("btn5", "stage.droneToggle");
    if (buttonCcs.size() >= 6) s.bind ("btn6", "tempo.tap");
    for (int i = 1; i <= juce::jmin (8, (int) padNotes.size()); ++i) s.bind ("pad" + juce::String (i), "stage.moment" + juce::String (i));
    s.bind ("pedal1", "stage.nextMoment");
    s.bind ("pedal2", "stage.droneToggle");
    return s;
}

inline std::vector<Surface> surfaces()
{
    return {
        generic8 ("nanoKONTROL2", "nanoKONTROL", { 16, 17, 18, 19, 20, 21, 22, 23 }, { 0, 1, 2, 3, 4, 5, 6, 7 },
                  { 43, 44, 58, 59, 41, 42, 45, 46 }, {}),
        generic8 ("Launchkey", "Launchkey", { 21, 22, 23, 24, 25, 26, 27, 28 }, { 41, 42, 43, 44, 45, 46, 47, 48 },
                  { 104, 105, 102, 103, 108, 109 }, { 36, 37, 38, 39, 40, 41, 42, 43 }),
        generic8 ("KeyStep", "KeyStep", { 74, 71, 76, 77, 93, 73, 75, 114 }, {}, { 51, 52 }, {}),
        generic8 ("MiniLab 3", "MiniLab", { 74, 71, 76, 77, 93, 18, 19, 16 }, { 82, 83, 85, 17 }, {}, { 36, 37, 38, 39, 40, 41, 42, 43 }),
        generic8 ("MPK Mini", "MPK", { 70, 71, 72, 73, 74, 75, 76, 77 }, {}, {}, { 36, 37, 38, 39, 40, 41, 42, 43 }),
        generic8 ("Generic 8 knobs", "", { 20, 21, 22, 23, 24, 25, 26, 27 }, { 12, 13, 14, 15, 16 }, { 80, 81, 82, 83, 84, 85 }, {}),
        generic8 ("Generic 2 pedals", "", {}, {}, {}, {}),
    };
}

} // namespace factory
} // namespace sanctuary
