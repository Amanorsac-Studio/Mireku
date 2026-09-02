// SanctuaryTests.cpp — unit tests for the stage model, paths, drone, chord detection.
#include <cstdio>
#include <cmath>
#include "engine/StageModel.h"
#include "engine/Drone.h"
#include "engine/ChordDetect.h"
#include "engine/Factory.h"

using namespace sanctuary;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) std::printf ("  [PASS] %s\n", msg); \
         else { std::printf ("  [FAIL] %s  (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } } while (0)

static void testKeys()
{
    std::printf ("\n== Keys and the Wheel ==\n");
    CHECK (keyFromName ("Db") == 1 && keyFromName ("C#") == 1 && keyFromName ("Bb") == 10 && keyFromName ("e") == 4, "key names parse (sharps, flats, case)");
    CHECK (juce::String (keyName (10)) == "Bb" && juce::String (keyName (10, false)) == "A#", "flat/sharp spelling");
    CHECK (wheelSemitones (7, 8) == 1, "G -> Ab = +1");
    CHECK (wheelSemitones (0, 11) == -1, "C -> B = -1 (nearest direction)");
    CHECK (wheelSemitones (0, 6) == 6, "tritone = +6");
    CHECK (keyDisplay (3, true) == "Ebm", "Eb minor display");
}

static void testPaths()
{
    std::printf ("\n== Parameter paths ==\n");
    Rig r;
    CHECK (pathInfo (r, "slot0.engine.bark").valid, "slot0.engine.bark is a path");
    CHECK (pathInfo (r, "slot0.fx0.drive").valid, "slot0.fx0.drive (EP preamp) is a path");
    CHECK (pathInfo (r, "slot0.fx0.bypass").kind == ParamKind::Bool, "bypass is a bool");
    CHECK (! pathInfo (r, "slot0.fx9.mix").valid, "out-of-range unit index is not a path");
    CHECK (! pathInfo (r, "slot7.vol").valid, "slot7 is not a path");
    CHECK (pathInfo (r, "drone.brightness").group == "Drone", "drone paths grouped");
    CHECK (rigSet (r, "slot1.vol", -12.0f) && rigGet (r, "slot1.vol") == -12.0f, "vol set/get");
    CHECK (rigSet (r, "drone.on", 1.0f) && r.drone.on, "drone.on set");
    CHECK (rigSet (r, "slot0.fx0.drive", 55.0f) && std::abs (*rigGet (r, "slot0.fx0.drive") - 55.0f) < 1e-4f, "fx param set/get");
    const auto pi = pathInfo (r, "slot0.vol");
    CHECK (std::abs (valueFromNorm (pi, normFromValue (pi, -20.0f)) + 20.0f) < 1e-3f, "vol norm round trip");
    const auto lp = pathInfo (r, "slot0.ampenv.attackMs");
    CHECK (lp.logTaper && std::abs (valueFromNorm (lp, normFromValue (lp, 300.0f)) - 300.0f) < 0.5f, "log taper round trip");
    auto all = allPaths (r);
    CHECK (all.size() > 60, "allPaths enumerates the rig");
    bool allValid = true; for (auto& p : all) if (! pathInfo (r, p).valid) { allValid = false; std::printf ("    bad path %s\n", p.toRawUTF8()); }
    CHECK (allValid, "every enumerated path is valid");
}

static void testSongRoundTrip()
{
    std::printf ("\n== Song / Set / Surface JSON round trip ==\n");
    Song s; s.title = "Test"; s.songKey = 3; s.minor = true; s.tempo = 91; s.meter = "6/8"; s.tags = { "a", "b" }; s.notes = "hello\nworld";
    s.baseRig.slots[0].bundleDir = "C:/x.jmi"; s.baseRig.slots[0].keyLo = 48; s.baseRig.slots[0].volumeDb = -7.5f;
    s.baseRig.slots[2].octaveShift = -1; s.baseRig.slots[2].ignoreWheel = true;
    s.baseRig.drone.on = true; s.baseRig.drone.follow = DroneFollow::chords; s.baseRig.drone.level = 66;
    s.baseRig.macros[1].name = "SWELL"; s.baseRig.macros[1].links = { { "slot2.vol", 0.2f, 0.9f } };
    s.moments.push_back ({ "m2", "Chorus", 0xff112233u, 2.5f, { { "slot0.vol", 0.8f }, { "drone.on", 1.0f } } });
    s.trimPaths = { "drone.level" };
    auto v = s.toVar();
    auto text = juce::JSON::toString (v);
    auto back = Song::fromVar (juce::JSON::parse (text));
    CHECK (back.title == "Test" && back.songKey == 3 && back.minor && back.meter == "6/8" && back.tags.size() == 2, "song meta survives");
    CHECK (back.baseRig.slots[0].keyLo == 48 && std::abs (back.baseRig.slots[0].volumeDb + 7.5f) < 1e-4f, "slot range + volume survive");
    CHECK (back.baseRig.slots[2].octaveShift == -1 && back.baseRig.slots[2].ignoreWheel, "octave + ignoreWheel survive");
    CHECK (back.baseRig.drone.follow == DroneFollow::chords && back.baseRig.drone.level == 66, "drone settings survive");
    CHECK (back.baseRig.macros[1].links.size() == 1 && back.baseRig.macros[1].links[0].path == "slot2.vol", "macro links survive");
    CHECK (back.moments.size() == 2 && back.moments[1].transitionBars == 2.5f && back.moments[1].diff.size() == 2, "moments + diffs survive");
    CHECK (back.trimPaths == s.trimPaths, "pinned trim paths survive");

    // rigForMoment applies the diff on top of the base
    auto r1 = back.rigForMoment (1);
    const auto pi = pathInfo (back.baseRig, "slot0.vol");
    CHECK (std::abs (*rigGet (r1, "slot0.vol") - valueFromNorm (pi, 0.8f)) < 1e-3f, "rigForMoment denormalises the diff");
    CHECK (r1.drone.on, "rigForMoment applies bool diff");
    CHECK (back.rigForMoment (0).drone.on, "moment 0 is the base state");
    auto trim = back.liveTrimPaths();
    CHECK (trim.size() <= 8 && trim[0] == "drone.level" && trim.contains ("macro2") && trim.contains ("slot0.vol"), "live trim: pinned, macros with links, moment paths");

    Set st; st.name = "Sun"; st.songs = { { s.id, 5, 100.0 }, { "other", std::nullopt, std::nullopt } }; st.padBetweenSongs = PadBetweenSongs::stop;
    auto st2 = Set::fromVar (juce::JSON::parse (juce::JSON::toString (st.toVar())));
    CHECK (st2.songs.size() == 2 && st2.songs[0].keyOverride == 5 && st2.songs[0].tempoOverride == 100.0 && ! st2.songs[1].keyOverride, "set entries + overrides survive");
    CHECK (st2.padBetweenSongs == PadBetweenSongs::stop, "pad policy survives");

    auto surf = factory::surfaces()[0];
    auto surf2 = Surface::fromVar (juce::JSON::parse (juce::JSON::toString (surf.toVar())));
    CHECK (surf2.controls.size() == surf.controls.size() && surf2.bindings.size() == surf.bindings.size(), "surface round trip");
    CHECK (surf2.controlForCc (16, 1) != nullptr && surf2.targetFor ("knob1") == "trim1", "surface lookup by CC + binding");
    CHECK (surf2.targetFor ("pedal1") == "stage.nextMoment", "pedal 1 = next moment by default");
}

static void testChords()
{
    std::printf ("\n== Chord detection ==\n");
    auto pcs = [] (std::initializer_list<int> notes) { uint32_t m = 0; for (int n : notes) m |= 1u << (n % 12); return m; };
    auto r = ChordDetector::analysePitchClasses (pcs ({ 0, 4, 7 }), 0);
    CHECK (r.root == 0 && r.quality == ChordQuality::major, "C E G = C major");
    r = ChordDetector::analysePitchClasses (pcs ({ 9, 0, 4 }), 9);
    CHECK (r.root == 9 && r.quality == ChordQuality::minor, "A C E = A minor");
    r = ChordDetector::analysePitchClasses (pcs ({ 4, 7, 0 }), 4);
    CHECK (r.root == 0 && r.quality == ChordQuality::major, "E G C (first inversion) = C major");
    r = ChordDetector::analysePitchClasses (pcs ({ 7, 11, 2, 5 }), 7);
    CHECK (r.root == 7 && r.quality == ChordQuality::dom7, "G B D F = G7");
    r = ChordDetector::analysePitchClasses (pcs ({ 0, 4, 7, 11 }), 0);
    CHECK (r.root == 0 && r.quality == ChordQuality::maj7, "C E G B = Cmaj7");
    r = ChordDetector::analysePitchClasses (pcs ({ 2, 7, 9 }), 2);
    CHECK (r.root == 2 && r.quality == ChordQuality::sus4, "D G A = Dsus4");
    r = ChordDetector::analysePitchClasses (pcs ({ 0, 7 }), 0);
    CHECK (r.root == 0 && r.quality == ChordQuality::power, "C G = C5");
    CHECK (chordColourTone (ChordQuality::minor) == 3 && chordColourTone (ChordQuality::dom7) == 10, "colour tones");

    ChordDetector d; d.prepare (48000.0);
    d.noteOn (60); d.noteOn (64); d.noteOn (67);
    d.advance (2000);
    CHECK (d.result().root < 0, "chord not reported before 120 ms");
    d.advance (6000);
    CHECK (d.result().root == 0 && d.result().quality == ChordQuality::major, "chord reported after 120 ms of stability");
    d.noteOff (64); d.noteOn (63); d.advance (100);
    CHECK (d.result().root == 0 && d.result().quality == ChordQuality::major, "a change is held back until stable");
    d.advance (7000);
    CHECK (d.result().quality == ChordQuality::minor, "…then reported");
}

static void testDrone()
{
    std::printf ("\n== Drone engine ==\n");
    DroneEngine e; e.prepare (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    DroneSettings s; s.on = true; s.level = 80; s.riseSeconds = 0.3f; s.keyChangeSeconds = 0.3f;
    e.setSettings (s); e.setTarget (0, false, -1);
    float peak = 0.0f;
    for (int i = 0; i < 200; ++i) { buf.clear(); e.render (buf, 512); peak = juce::jmax (peak, buf.getMagnitude (0, 512)); }
    CHECK (peak > 0.01f && peak < 1.0f, "drone produces bounded audio when on");
    CHECK (e.isSounding(), "drone reports sounding");
    // key change: crossfade, no discontinuity beyond a sane bound
    e.setTarget (7, false, -1);
    float maxStep = 0.0f; float last = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        buf.clear(); e.render (buf, 512);
        auto* L = buf.getReadPointer (0);
        for (int n = 0; n < 512; ++n) { maxStep = juce::jmax (maxStep, std::abs (L[n] - last)); last = L[n]; }
    }
    CHECK (maxStep < 0.5f, "key change has no hard discontinuity");
    CHECK (e.currentRoot() == 7, "current root follows the target");
    // off: fades out to silence
    s.on = false; s.fallSeconds = 0.3f; e.setSettings (s);
    for (int i = 0; i < 400; ++i) { buf.clear(); e.render (buf, 512); }
    buf.clear(); e.render (buf, 512);
    CHECK (buf.getMagnitude (0, 512) < 0.02f, "drone fades out after off (tail may remain)");
    // NaN safety
    bool finite = true;
    for (int i = 0; i < 100; ++i) { buf.clear(); e.render (buf, 512); for (int n = 0; n < 512; ++n) if (! std::isfinite (buf.getReadPointer (0)[n])) finite = false; }
    CHECK (finite, "output stays finite");
    CHECK (droneCharacters().size() == 8, "eight drone characters");
}

static void testFactory()
{
    std::printf ("\n== Factory content ==\n");
    factory::BundlePick pick ({});
    auto songs = factory::songs (pick);
    CHECK (songs.size() == 12, "twelve factory songs");
    bool ok = true;
    for (auto& s : songs)
    {
        if (s.moments.empty() || s.title.isEmpty() || s.notes.isEmpty()) ok = false;
        for (auto& m : s.moments) for (auto& [p, v] : m.diff) if (! pathInfo (s.baseRig, p).valid || v < 0.0f || v > 1.0f) { ok = false; std::printf ("    bad diff %s in %s\n", p.toRawUTF8(), s.title.toRawUTF8()); }
    }
    CHECK (ok, "every factory moment diff addresses a valid path with a 0..1 value");
    auto sets = factory::sets (songs);
    CHECK (sets.size() == 3 && sets[2].songs.size() == 12, "three factory sets; rehearsal has every song");
    CHECK (factory::surfaces().size() == 7, "seven factory surfaces");
}

int main()
{
    std::printf ("Sanctuary unit tests\n");
    testKeys(); testPaths(); testSongRoundTrip(); testChords(); testDrone(); testFactory();
    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
