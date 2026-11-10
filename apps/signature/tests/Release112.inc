// Release112.inc — 1.1.2 (2026-11-10), the engine side: the library pack the
// app installs itself, the sostenuto pedal, a rack that rests when silent, and
// the controller map's lock-free "is it learned" flags.

static void testLibraryPack()
{
    std::printf ("\n== LIBRARY PACK: one file in, the library in its place ==\n");
    auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mireku-pack-test");
    root.deleteRecursively();
    auto src = root.getChildFile ("src"), dest = root.getChildFile ("dest");
    src.getChildFile ("Performance").getChildFile ("Factory").createDirectory();
    juce::Random rnd (7);
    auto fill = [&] (const juce::String& rel, int bytes)
    {
        juce::MemoryBlock mb ((size_t) bytes);
        for (int i = 0; i < bytes; ++i) mb[i] = (char) rnd.nextInt (256);
        src.getChildFile (rel).replaceWithData (mb.getData(), mb.getSize());
    };
    fill ("Grand Piano.jmi", 3 * 1024 * 1024 + 17);          // crosses the 1 MB copy buffer
    fill ("Small Bell .jmi", 777);                            // James's names carry spaces
    fill ("Performance/Factory/Sunday Set.jmi", 9000);
    const juce::StringArray rels { "Grand Piano.jmi", "Small Bell .jmi", "Performance/Factory/Sunday Set.jmi" };

    auto pack = root.getChildFile ("Test Library.mirekupack");
    juce::String err;
    CHECK (librarypack::write (pack, src, rels, err), "a pack is written");
    librarypack::Index idx;
    CHECK (librarypack::readIndex (pack, idx, err) && idx.entries.size() == 3, "its index reads back: three files");
    CHECK (idx.instruments() == 2 && idx.performances() == 1, "counted as 2 instruments and 1 performance");

    librarypack::Progress pr;
    auto res = librarypack::install (pack, dest, pr);
    std::printf ("    %s\n", res.message.toRawUTF8());
    CHECK (res.ok && res.written == 3, "installing writes all three");
    bool same = true;
    for (auto& rel : rels)
        same = same && dest.getChildFile (rel).hasIdenticalContentTo (src.getChildFile (rel));
    CHECK (same, "every installed file is byte for byte the source, in its sub-folder");
    CHECK (pr.bytesDone.load() == idx.totalBytes && pr.filesDone.load() == 3, "progress reached the end");
    CHECK (dest.findChildFiles (juce::File::findFiles, true, "*.part").isEmpty(), "no half-written file is left behind");

    librarypack::Progress pr2;
    auto again = librarypack::install (pack, dest, pr2);
    CHECK (again.ok && again.written == 0 && again.unchanged == 3, "installing again rewrites nothing: an update merges");

    // a download that stopped early
    {
        juce::MemoryBlock mb;
        pack.loadFileAsData (mb);
        auto cut = root.getChildFile ("Cut.mirekupack");
        cut.replaceWithData (mb.getData(), mb.getSize() - 5000);
        librarypack::Progress p3;
        auto d3 = root.getChildFile ("dest3");
        auto r3 = librarypack::install (cut, d3, p3);
        std::printf ("    %s\n", r3.message.toRawUTF8());
        CHECK (! r3.ok && ! d3.exists(), "a short download is refused before anything is written");
    }
    // one damaged byte inside a file
    {
        juce::MemoryBlock mb;
        pack.loadFileAsData (mb);
        mb[mb.getSize() - 100] ^= 0x20;                       // inside the last file
        auto bad = root.getChildFile ("Bad.mirekupack");
        bad.replaceWithData (mb.getData(), mb.getSize());
        librarypack::Progress p4;
        auto d4 = root.getChildFile ("dest4");
        auto r4 = librarypack::install (bad, d4, p4);
        std::printf ("    %s\n", r4.message.toRawUTF8());
        CHECK (! r4.ok && ! d4.getChildFile ("Performance/Factory/Sunday Set.jmi").existsAsFile(),
               "a damaged file fails its check and is not installed");
        CHECK (d4.getChildFile ("Grand Piano.jmi").existsAsFile(), "the files before it are complete and stay");
    }
    // not a pack at all
    {
        auto notPack = root.getChildFile ("Not.mirekupack");
        notPack.replaceWithText ("hello");
        CHECK (! librarypack::readIndex (notPack, idx, err), "a file that is not a library pack is refused");
    }
    CHECK (librarypack::pathIsSafe ("Performance/Factory/A B.jmi") && librarypack::pathIsSafe ("Mireku Grand c7 .jmi"),
           "ordinary library paths are accepted");
    CHECK (! librarypack::pathIsSafe ("../outside.jmi") && ! librarypack::pathIsSafe ("/abs.jmi")
           && ! librarypack::pathIsSafe ("C:\\x.jmi") && ! librarypack::pathIsSafe ("a/../../b.jmi") && ! librarypack::pathIsSafe (""),
           "a path that could leave the library folder is refused");
    root.deleteRecursively();
}

static void testSostenuto (const BundlePtr& bundle)
{
    std::printf ("\n== SOSTENUTO (CC 66): holds the notes down when it is pressed, not later ones ==\n");
    SamplerEngine eng;
    eng.setWorld (0);
    eng.prepare (48000.0, 512);
    eng.setBundle (bundle);
    juce::AudioBuffer<float> out (2, 512);
    auto renderMs = [&] (double ms)
    {
        int total = (int) (48.0 * ms);
        while (total > 0) { const int n = juce::jmin (512, total); out.clear(); eng.render (out, n); total -= n; }
    };

    eng.noteOn (60, 100);
    renderMs (40);
    eng.sostenutoPedal (true);                          // catches the C
    eng.noteOn (64, 100);                               // played AFTER the pedal
    renderMs (40);
    eng.noteOff (60);
    eng.noteOff (64);
    renderMs (30);
    CHECK (eng.activeVoices() == 2 && eng.releasingVoices() == 1, "the caught note holds; the later note releases");
    eng.sostenutoPedal (false);
    renderMs (10);
    CHECK (eng.releasingVoices() == eng.activeVoices(), "lifting the pedal lets the held note go");

    // with the sustain pedal on top
    eng.allNotesOff (false);
    eng.noteOn (60, 100);
    renderMs (20);
    eng.sostenutoPedal (true);
    eng.sustainPedal (true);
    eng.noteOff (60);
    eng.sostenutoPedal (false);
    renderMs (10);
    CHECK (eng.activeVoices() == 1 && eng.releasingVoices() == 0 && eng.sustainedVoices() == 1,
           "lifted while SUSTAIN is down, the note passes to the sustain pedal");
    eng.sustainPedal (false);
    renderMs (10);
    CHECK (eng.releasingVoices() == 1, "and sustain then lets it go");
    eng.allNotesOff (false);
    eng.setBundle (nullptr);
}

static void testRackRestsWhenSilent()
{
    std::printf ("\n== A SILENT RACK RESTS, AND WAKES ON SOUND OR ON A CHANGE ==\n");
    FxRackDsp rack;
    rack.prepare (48000.0, 512);
    FxRackState st;
    st.push_back (defaultUnitState (FxType::plateReverb, false));
    st.push_back (defaultUnitState (FxType::jmChorus, false));
    rack.setState (st);
    juce::AudioBuffer<float> b (2, 512);
    auto run = [&] (int blocks, float level)
    {
        float peak = 0;
        for (int k = 0; k < blocks; ++k)
        {
            b.clear();
            if (level > 0) for (int i = 0; i < 512; ++i) { b.setSample (0, i, level * std::sin (0.05f * (float) i)); b.setSample (1, i, b.getSample (0, i)); }
            rack.process (b, 120.0);
            peak = juce::jmax (peak, b.getMagnitude (0, 512));
        }
        return peak;
    };
    CHECK (run (40, 0.5f) > 0.05f && ! rack.isAsleep(), "with sound going in it is awake");
    run (94, 0.0f);                                      // one second of silence: the reverb tail is still there
    CHECK (! rack.isAsleep(), "a second into silence it is still awake: the tail must finish");
    run (94 * 40, 0.0f);                                 // forty seconds
    CHECK (rack.isAsleep(), "once the tail has died and two silent seconds have passed, it rests");
    CHECK (run (4, 0.0f) == 0.0f, "resting, it passes silence through untouched");
    CHECK (run (4, 0.5f) > 0.05f && ! rack.isAsleep(), "the first sound wakes it and is processed");
    run (94 * 40, 0.0f);
    CHECK (rack.isAsleep(), "it rests again");
    rack.setParam (0, "mix", 60.0f);
    run (1, 0.0f);
    CHECK (! rack.isAsleep(), "a change to a unit wakes it");

    // a unit that makes sound on its own never produces silence, so never rests
    FxRackDsp hiss;
    hiss.prepare (48000.0, 512);
    FxRackState hs;
    auto tape = defaultUnitState (FxType::tapeMachine, false);
    tape.params["hiss"] = 80.0f;
    hs.push_back (tape);
    hiss.setState (hs);
    float pk = 0;
    for (int k = 0; k < 94 * 6; ++k) { b.clear(); hiss.process (b, 120.0); pk = juce::jmax (pk, b.getMagnitude (0, 512)); }
    std::printf ("    tape hiss peak with nothing played: %.5f\n", pk);
    CHECK (pk > 1.0e-6f ? ! hiss.isAsleep() : true, "a rack with tape hiss turned up keeps running");
}

static void testMidiMapFlags()
{
    std::printf ("\n== CONTROLLER MAP: learned numbers are visible without the lock ==\n");
    MidiMap m;
    CHECK (! m.isLearned (74) && m.size() == 0 && ! m.dirty.load(), "an empty map has nothing learned");
    m.set (74, "host.macro1");
    CHECK (m.isLearned (74) && m.dirty.exchange (false), "learning a controller marks it and marks the map changed");
    m.set (75, "host.macro1");                           // one controller per control
    CHECK (! m.isLearned (74) && m.isLearned (75) && m.size() == 1, "re-learning a control moves it off its old number");
    m.set (7, "host.slotVol0");
    auto v = m.toVar();
    MidiMap n;
    n.fromVar (v);
    CHECK (n.isLearned (75) && n.isLearned (7) && n.pathForCc (7) == "host.slotVol0", "the map survives a round trip, flags included");
    n.removeCc (7);
    CHECK (! n.isLearned (7) && n.size() == 1, "CLEAR removes one assignment");
    n.clear();
    CHECK (n.size() == 0 && ! n.isLearned (75), "CLEAR ALL empties it");
}
