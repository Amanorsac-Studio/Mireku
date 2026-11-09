// Sealed.inc — the encrypted library and the encrypted documents (2026-11-04).
// Three things are held: a sealed DOCUMENT round-trips and fails loudly when
// touched; the Node seal script and this reader agree byte for byte (the
// committed vector under the TEST key); a sealed INSTRUMENT loads through the
// same Library path as a folder bundle and plays.

static void testSealedDocuments()
{
    std::printf ("\n== SEALED DOCUMENTS (.jmi presets, performances, setlists) ==\n");
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mireku-sealed-doc-test");
    dir.deleteRecursively();
    dir.createDirectory();

    const juce::String text = "{\"name\":\"Warm Tine\",\"engine\":{\"tine\":44.0}}";
    auto f = sealed::documentFile (dir, "Warm Tine");
    CHECK (f.getFileName() == "Warm Tine.jmi", "a document is written as Name.jmi");
    juce::String err;
    CHECK (sealed::writeDocument (f, sealed::preset, text, &err), "a preset seals");
    CHECK (sealed::isDocumentFile (f) && ! sealed::isSealedBundleFile (f), "the file carries the document magic, not the instrument's");
    CHECK (! f.loadFileAsString().contains ("Warm Tine"), "the name is not readable in the file");

    juce::String back;
    CHECK (sealed::readDocument (f, sealed::preset, back) && back == text, "and reads back as the same text");
    CHECK (! sealed::readDocument (f, sealed::performance, back, &err), "read as a PERFORMANCE it is refused");
    std::printf ("    (%s)\n", err.toRawUTF8());

    // one flipped byte in the ciphertext: refused, nothing returned
    {
        juce::MemoryBlock mb;
        f.loadFileAsData (mb);
        auto* b = (uint8_t*) mb.getData();
        b[mb.getSize() - 3] ^= 0x40;
        auto bad = dir.getChildFile ("Tampered.jmi");
        bad.replaceWithData (mb.getData(), mb.getSize());
        CHECK (! sealed::readDocument (bad, sealed::preset, back, &err) && back.isEmpty(),
               "a flipped byte fails the tag and returns nothing");
    }

    // the old plain .json still reads, and a .jmi twin takes precedence
    auto legacy = dir.getChildFile ("Old One.json");
    legacy.replaceWithText ("{\"name\":\"Old One\"}");
    CHECK (sealed::readDocument (legacy, sealed::preset, back) && back.contains ("Old One"), "a 1.0.x .json preset still reads");
    CHECK (sealed::findDocument (dir, "Old One") == legacy, "findDocument falls back to the .json");
    CHECK (sealed::findDocument (dir, "Warm Tine") == f, "and finds the .jmi by name");
    sealed::writeDocument (sealed::documentFile (dir, "Old One"), sealed::preset, "{\"name\":\"Old One\",\"v\":2}");
    auto list = sealed::listDocuments (dir);
    juce::StringArray names;
    for (auto& d : list) names.add (d.getFileName());
    std::printf ("    listed: %s\n", names.joinIntoString (", ").toRawUTF8());
    CHECK (list.size() == 3 && names.contains ("Old One.jmi") && ! names.contains ("Old One.json"),
           "the list carries the .jmi, the .json only when it has no .jmi twin");
    CHECK (sealed::readDocumentVar (sealed::findDocument (dir, "Old One"), sealed::preset).getProperty ("v", 0) == juce::var (2),
           "the sealed twin is the one read");

    // the committed cross-check: sealed by scripts/seal-library.mjs under the TEST key
    {
        aofas::KeyRing test;
        test.set (1, std::array<uint8_t, 32> {});
        sealed::overrideKeysForTests (&test);
        auto vec = testdataDir().getChildFile ("sealed").getChildFile ("doc-vector.jmi");
        auto expected = testdataDir().getChildFile ("sealed").getChildFile ("doc-vector.expected.json").loadFileAsString();
        juce::String got;
        const bool ok = sealed::readDocument (vec, sealed::performance, got, &err);
        if (! ok) std::printf ("    %s\n", err.toRawUTF8());
        CHECK (ok && got == expected, "the Node-sealed vector decrypts to its expected JSON (both implementations agree)");
        sealed::overrideKeysForTests (nullptr);
        CHECK (! sealed::readDocument (vec, sealed::performance, got), "...and does not open under the studio key");
    }
    dir.deleteRecursively();
}

static void testSealedInstrument()
{
    std::printf ("\n== SEALED INSTRUMENT (AOFAS file named .jmi) ==\n");
    auto src = testdataDir().getChildFile ("aofas").getChildFile ("vector1.aofas");
    if (! src.existsAsFile()) { std::printf ("  [SKIP] %s not found\n", src.getFullPathName().toRawUTF8()); return; }
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mireku-sealed-lib-test");
    dir.deleteRecursively();
    dir.createDirectory();
    auto f = dir.getChildFile ("AOFAS Vector.jmi");
    src.copyFileTo (f);

    aofas::KeyRing test;
    test.set (1, std::array<uint8_t, 32> {});
    sealed::overrideKeysForTests (&test);

    CHECK (sealed::isSealedBundleFile (f), "the sealed instrument is told by its magic");
    CHECK (! sealed::isDocumentFile (f), "and is not a document");

    Library lib;
    lib.setLibraryDirTransient (dir);
    CHECK (lib.bundles().size() == 1 && lib.bundles()[0].dir == f, "the library scan lists the sealed file as an instrument");
    if (! lib.bundles().empty())
    {
        std::printf ("    listed as \"%s\", world %d, %d preset(s)\n", lib.bundles()[0].name.toRawUTF8(),
                     lib.bundles()[0].world, lib.bundles()[0].presetNames.size());
        CHECK (lib.bundles()[0].world == 0, "world read from the sealed index (rhodes-ep)");
    }
    CHECK (lib.findByFolderName ("AOFAS Vector.jmi") != nullptr, "a performance naming it by file name finds it");

    // A performance saved on Windows, opened anywhere. This suite runs on the
    // macOS release job too, which is the machine this bug lived on.
    CHECK (Library::leafOfStoredPath ("C:\\Users\\jmire\\Desktop\\MIREKU Libary\\@All\\Mireku Grand c7 .jmi") == "Mireku Grand c7 .jmi",
           "the instrument name comes out of a Windows path, trailing space kept");
    CHECK (Library::leafOfStoredPath ("/Users/james/MIREKU Library/JM MKS 20.jmi") == "JM MKS 20.jmi",
           "and out of a Mac path");
    CHECK (Library::leafOfStoredPath ("C:\\One\\Two/Three\\Name.jmi") == "Name.jmi" && Library::leafOfStoredPath ("Name.jmi") == "Name.jmi",
           "and out of a mixed path or a bare name");
    {
        const auto* winHit = lib.findByStoredPath ("C:\\Users\\jmire\\Desktop\\MIREKU Libary\\@All\\AOFAS Vector.jmi");
        const auto* macHit = lib.findByStoredPath ("/Users/james/Somewhere Else/aofas vector.JMI");
        CHECK (winHit != nullptr && winHit->dir == f, "a slot saved on Windows finds its instrument in this library");
        CHECK (macHit != nullptr && macHit->dir == f, "so does one saved on a Mac, whatever the case");
        CHECK (lib.findByStoredPath ("C:\\Users\\jmire\\Gone.jmi") == nullptr && lib.findByStoredPath ("") == nullptr,
               "an instrument the library does not have is reported missing, not matched");
    }

    juce::String err;
    auto b = lib.loadBundle (f, err);
    CHECK (b != nullptr, "the sealed instrument loads through Library::loadBundle");
    if (b == nullptr) { std::printf ("  error: %s\n", err.toRawUTF8()); sealed::overrideKeysForTests (nullptr); return; }
    CHECK (b->isSealed() && b->dir == f, "the loaded bundle knows it is sealed");
    CHECK (! b->inst.zones.empty() && b->samples.size() == b->inst.zones.size(), "zones and samples parallel");
    bool heads = true;
    for (auto& z : b->samples) for (auto& s : z) heads &= s->residentFrames() > 0 && s->sealed != nullptr;
    CHECK (heads, "every sample has a decrypted head resident and names its sealed source");

    // the body promotes on the loader thread through the same read path
    SampleLoader::instance().waitUntilSettled (5000);
    bool full = true;
    for (auto& z : b->samples) for (auto& s : z) full &= s->isFullyResident();
    CHECK (full, "the loader promoted every sample to full residency from the sealed file");

    // a note plays: audible, finite
    SamplerEngine eng;
    eng.setWorld (0);
    eng.prepare (48000.0, 512);
    eng.setBundle (b);
    const int note = b->inst.zones[0].rootNote;
    eng.noteOn (note, 100);
    juce::AudioBuffer<float> out (2, 512);
    float peak = 0; bool finite = true;
    for (int blk = 0; blk < 20; ++blk)
    {
        out.clear();
        eng.render (out, 512);
        for (int i = 0; i < 512; ++i)
        {
            const float v = out.getSample (0, i);
            if (! std::isfinite (v)) finite = false;
            peak = juce::jmax (peak, std::abs (v));
        }
    }
    std::printf ("    render peak %.4f\n", peak);
    CHECK (finite && peak > 1.0e-4f, "the sealed instrument sounds");
    eng.allNotesOff (true);
    eng.setBundle (nullptr);

    // under the wrong key the file is refused with a message, not noise
    sealed::overrideKeysForTests (nullptr);
    juce::String err2;
    CHECK (lib.loadBundle (f, err2) == nullptr && err2.isNotEmpty(), "under another key the file is refused");
    std::printf ("    (%s)\n", err2.toRawUTF8());
    b.reset();
    dir.deleteRecursively();
}
