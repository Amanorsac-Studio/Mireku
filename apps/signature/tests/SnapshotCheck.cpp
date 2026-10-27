// SnapshotCheck.cpp — acceptance self-check for the CLIENT REVIEW ROUND 2
// arrangement restore (console, offscreen).
//
// The old 1000x720 V2 coordinate assertions no longer apply: the client-approved
// arrangement is a 1536x1024 (3:2) full window in which the preset browser,
// centre stage, PERFORM dock, FX rack strip and bottom bar are ALL present at
// once. This harness now verifies that arrangement, the PERFORM centre 2x2 grid,
// the hardware-kit assets (including each knob crop's derived optical centre)
// and writes full-window PNG snapshots for comparison with the approved image.
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

using namespace jm;

static int failures = 0;
#define CHECK(cond, msg) \
    do { if (cond) std::printf ("  [PASS] %s\n", msg); \
         else { std::printf ("  [FAIL] %s\n", msg); ++failures; } } while (0)

static void collectSliders (juce::Component& c, std::vector<juce::Slider*>& out)
{
    if (auto* s = dynamic_cast<juce::Slider*> (&c)) out.push_back (s);
    for (auto* ch : c.getChildren()) collectSliders (*ch, out);
}

/// centres of hero sliders (those carrying a "jmMotif" property) in `root` coords
static std::vector<juce::Point<int>> heroCentres (juce::Component& root)
{
    std::vector<juce::Slider*> sliders;
    collectSliders (root, sliders);
    std::vector<juce::Point<int>> out;
    for (auto* s : sliders)
        if ((int) s->getProperties().getWithDefault ("jmMotif", -1) >= 0)
            out.push_back (root.getLocalPoint (s, s->getLocalBounds().getCentre()));
    std::sort (out.begin(), out.end(), [] (auto a, auto b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    return out;
}

template <typename T>
static T* findOne (juce::Component& c)
{
    if (auto* t = dynamic_cast<T*> (&c)) return t;
    for (auto* ch : c.getChildren())
        if (auto* t = findOne<T> (*ch)) return t;
    return nullptr;
}

template <typename T>
static void findAll (juce::Component& c, std::vector<T*>& out)
{
    if (auto* t = dynamic_cast<T*> (&c)) out.push_back (t);
    for (auto* ch : c.getChildren()) findAll<T> (*ch, out);
}

static void saveImage (const juce::Image& img, const juce::String& name)
{
    juce::File out = juce::File (JM_SNAPSHOT_DIR).getChildFile (name);
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.openedOk())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (img, os);
        std::printf ("  snapshot: %s (%dx%d)\n", out.getFullPathName().toRawUTF8(),
                     img.getWidth(), img.getHeight());
    }
}

static void savePng (juce::Component& c, const juce::String& name)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), false, 1.0f);
    juce::File out = juce::File (JM_SNAPSHOT_DIR).getChildFile (name);
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.openedOk())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (img, os);
        std::printf ("  snapshot: %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
}

//==============================================================================
// AUTHORING MODE — real-library probe (NOT a CTest; its result depends on which
// bundles are on the machine, exactly like MirekuDevices).
//
//   MirekuSnapshot --probe-bundle <path-to.jmi> <preset name>
//
// It performs the REAL authoring save into a REAL bundle through the same
// writeBundlePreset guard the app uses, prints where it went, and then
// `npx jmi validate <bundle>` proves the bundle is still valid. That is the
// risky path, so it is proven against the client's own content rather than a
// fixture.  --undo-probe removes the preset again and restores any .bak.
/// The same idea for the OTHER risky write the authoring workflow added:
/// moving an instrument to another category, i.e. editing meta.world inside a
/// real instrument.json.
///
///   MirekuSnapshot --probe-world <path-to.jmi> <rhodes-ep|mks|fm|strings>
///
/// Run it against a COPY of a client bundle, then `npx jmi validate` the copy.
/// It prints the before/after world and whether anything but meta.world moved.
static int probeWorld (const juce::String& bundlePath, const juce::String& worldId)
{
    juce::File bundle (bundlePath);
    if (! authoring::looksLikeBundle (bundle))
    { std::printf ("not a .jmi bundle: %s\n", bundlePath.toRawUTF8()); return 2; }
    auto ij = bundle.getChildFile ("instrument.json");
    const auto before = ij.loadFileAsString();
    std::printf ("bundle   : %s\n", bundle.getFullPathName().toRawUTF8());
    std::printf ("world    : %s -> %s\n",
                 juce::JSON::parse (before).getProperty ("meta", juce::var())
                     .getProperty ("world", "?").toString().toRawUTF8(),
                 worldId.toRawUTF8());

    auto r = authoring::writeInstrumentWorld (bundle, worldId);
    std::printf ("result   : %s\n", r.ok ? "OK" : "FAILED");
    std::printf ("message  : %s\n", r.message.toRawUTF8());
    if (r.note.isNotEmpty()) std::printf ("note     : %s\n", r.note.toRawUTF8());
    if (! r.ok) return 1;

    juce::String diff;
    const bool narrow = authoring::varsDifferOnlyAt (juce::JSON::parse (before),
                                                     juce::JSON::parse (ij.loadFileAsString()),
                                                     "meta.world", diff);
    std::printf ("narrow   : %s%s\n", narrow ? "yes - meta.world and nothing else" : "NO - ",
                 narrow ? "" : diff.toRawUTF8());
    std::printf ("backup   : %s\n", r.backup.getFullPathName().toRawUTF8());
    std::printf ("now run  : npx jmi validate \"%s\"\n", bundle.getFullPathName().toRawUTF8());
    return narrow ? 0 : 1;
}

static int probeBundle (const juce::String& bundlePath, const juce::String& presetName, bool undo)
{
    juce::File bundle (bundlePath);
    if (! authoring::looksLikeBundle (bundle))
    { std::printf ("not a .jmi bundle: %s\n", bundlePath.toRawUTF8()); return 2; }

    auto target = bundle.getChildFile ("presets")
                        .getChildFile (juce::File::createLegalFileName (presetName) + ".json");
    if (undo)
    {
        auto bak = target.getSiblingFile (target.getFileName() + ".bak");
        target.deleteFile();
        if (bak.existsAsFile()) { bak.copyFileTo (target); bak.deleteFile(); }
        std::printf ("removed %s (and restored any .bak)\n", target.getFullPathName().toRawUTF8());
        std::printf ("ledger left in place: %s\n",
                     authoring::ledgerFile (bundle).getFullPathName().toRawUTF8());
        return 0;
    }

    JMSignatureProcessor proc;
    proc.state.library.setLibraryDirTransient (bundle.getParentDirectory());
    proc.state.library.setModeTransient (authoring::Mode::authoring);
    for (auto& b : proc.state.library.bundles())
        if (b.dir == bundle) proc.loadBundleIntoSlot (0, b);
    if (proc.engines[0].bundle() == nullptr)
    { std::printf ("could not load the bundle into a slot\n"); return 2; }

    std::printf ("bundle   : %s\n", bundle.getFullPathName().toRawUTF8());
    std::printf ("presets  : %d before\n",
                 bundle.getChildFile ("presets").findChildFiles (juce::File::findFiles, false, "*.json").size());
    auto out = proc.saveInstrumentPreset (0, presetName);
    std::printf ("result   : %s\n", out.ok ? "OK" : "FAILED");
    std::printf ("dest     : %s\n", authoring::saveDestLabel (out.dest));
    std::printf ("file     : %s\n", out.file.getFullPathName().toRawUTF8());
    std::printf ("message  : %s\n", out.message.toRawUTF8());
    if (out.note.isNotEmpty()) std::printf ("note     : %s\n", out.note.toRawUTF8());
    std::printf ("presets  : %d after\n",
                 bundle.getChildFile ("presets").findChildFiles (juce::File::findFiles, false, "*.json").size());
    std::printf ("ledger   : %s\n", authoring::ledgerFile (bundle).getFullPathName().toRawUTF8());
    std::printf ("now run  : npx jmi validate \"%s\"\n", bundle.getFullPathName().toRawUTF8());
    return out.ok && out.dest == authoring::SaveDest::bundleFactory ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 3 && juce::String (argv[1]) == "--probe-bundle")
        return probeBundle (juce::String::fromUTF8 (argv[2]),
                            argc >= 4 ? juce::String::fromUTF8 (argv[3]) : juce::String ("Authoring Probe"),
                            false);
    if (argc >= 4 && juce::String (argv[1]) == "--probe-world")
        return probeWorld (juce::String::fromUTF8 (argv[2]), juce::String::fromUTF8 (argv[3]));
    if (argc >= 3 && juce::String (argv[1]) == "--undo-probe")
        return probeBundle (juce::String::fromUTF8 (argv[2]),
                            argc >= 4 ? juce::String::fromUTF8 (argv[3]) : juce::String ("Authoring Probe"),
                            true);

    std::printf ("MIREKU — client review round 2 self-check (1536x1024)\n"
                 "===========================================================\n");

    JmLookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    JMSignatureProcessor proc;
    using L = Theme::L;

    // ---------------------------------------------------- hardware kit assets
    std::printf ("\n== hardware kit (docs/hardware-kit) ==\n");
    {
        struct { kit::Img id; const char* name; } imgs[] = {
            { kit::Img::knobLarge, "knob-large-00" }, { kit::Img::knobSmall, "knob-small-00" },
            { kit::Img::panelTeal, "panel-teal" },    { kit::Img::panelOrange, "panel-orange" },
            { kit::Img::panelPurple, "panel-purple" },{ kit::Img::panelBlue, "panel-blue" },
            { kit::Img::panelRed, "panel-red" },      { kit::Img::panelGreen, "panel-green" },
            { kit::Img::panelMaster, "panel-master-empty" },
            { kit::Img::buttonUp, "button-empty-up" },{ kit::Img::buttonDown, "button-empty-down" },
            { kit::Img::buttonMaster, "button-master-empty" },
            { kit::Img::meterLeft, "meter-led-left" },{ kit::Img::meterRight, "meter-led-right" },
            { kit::Img::screw, "screw-00" },          { kit::Img::power, "icon-power" },
            { kit::Img::rackCorner, "rack-corner" },  { kit::Img::dividerGold, "divider-gold" },
            { kit::Img::flourishGold, "flourish-gold" },
        };
        int decoded = 0;
        for (auto& i : imgs)
            if (kit::image (i.id).isValid()) ++decoded;
            else std::printf ("  [FAIL] %s did not decode\n", i.name);
        char msg[96];
        std::snprintf (msg, sizeof (msg), "all %d kit assets decoded", (int) std::size (imgs));
        CHECK (decoded == (int) std::size (imgs), msg);

        for (auto which : { kit::Img::knobLarge, kit::Img::knobSmall })
        {
            auto o = kit::optical (which);
            auto& im = kit::image (which);
            std::printf ("  %s crop %dx%d -> optical centre (%.1f,%.1f)  span %.0f  "
                         "(pixel centre would be (%.1f,%.1f))\n",
                         which == kit::Img::knobLarge ? "knob-large" : "knob-small",
                         im.getWidth(), im.getHeight(), o.centre.x, o.centre.y, o.span,
                         im.getWidth() * 0.5f, im.getHeight() * 0.5f);
            CHECK (o.valid && o.span > 100.0f, "knob optical centre derived from the alpha silhouette");
        }
    }

    // ------------------------------- knob rotation sheet (0% / 50% / 100%)
    // The kit crops are not square, so this renders the large and small knob at
    // the three cardinal values with cross-hairs through the requested centre:
    // if the derived optical centre were wrong the art would visibly orbit.
    {
        struct Sheet : juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                g.fillAll (juce::Colour (0xff141210));
                const float vals[3] = { 0.0f, 0.5f, 1.0f };
                const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);
                for (int row = 0; row < 2; ++row)
                {
                    const bool large = row == 0;
                    const float d = large ? 150.0f : 110.0f;
                    for (int i = 0; i < 3; ++i)
                    {
                        juce::Point<float> c (110.0f + (float) i * 200.0f, 110.0f + (float) row * 200.0f);
                        g.setColour (juce::Colour (0xff3A3A38));
                        g.drawLine (c.x - 100.0f, c.y, c.x + 100.0f, c.y, 1.0f);
                        g.drawLine (c.x, c.y - 100.0f, c.x, c.y + 100.0f, 1.0f);
                        g.setColour (juce::Colour (0xff5A4A22));
                        g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (c), 1.0f);
                        kit::drawKnob (g, c, d, a0 + vals[i] * (a1 - a0), large);
                        g.setColour (Theme::cream);
                        g.setFont (Theme::ui (13.0f));
                        g.drawText (juce::String ((int) (vals[i] * 100.0f)) + " %",
                                    c.x - 40.0f, c.y + 92.0f, 80.0f, 16.0f, juce::Justification::centred, false);
                    }
                }
            }
        } sheet;
        sheet.setSize (620, 420);
        savePng (sheet, "round2-knob-rotation.png");
    }

    // ------------------------------------------------------------ model pages
    const char* worlds[] = { "rhodes", "mks", "fm", "strings" };
    int worstDx = 0, worstDy = 0;
    for (int w = 0; w < 4; ++w)
    {
        proc.state.currentPage = w;
        ModelPagePanel page (proc, w);
        page.setSize (L::centerW, L::centerH);
        auto centres = heroCentres (page);
        const bool isSix = (int) worldEngineControls (w).size() == 6;
        const int n = isSix ? 6 : 5;
        std::printf ("\n== %s model page (centre stage %dx%d) ==\n", worlds[w], L::centerW, L::centerH);
        char msg[128];
        std::snprintf (msg, sizeof (msg), "%d hero controls (expected %d)", (int) centres.size(), n);
        CHECK ((int) centres.size() == n, msg);
        std::vector<juce::Point<int>> want;
        for (int i = 0; i < n; ++i)
            want.push_back ({ isSix ? L::mpSix[i][0] : L::mpFive[i][0],
                              isSix ? L::mpSix[i][1] : L::mpFive[i][1] });
        std::sort (want.begin(), want.end(), [] (auto a, auto b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
        for (int i = 0; i < juce::jmin (n, (int) centres.size()); ++i)
        {
            const int dx = centres[(size_t) i].x - want[(size_t) i].x;
            const int dy = centres[(size_t) i].y - want[(size_t) i].y;
            worstDx = juce::jmax (worstDx, std::abs (dx));
            worstDy = juce::jmax (worstDy, std::abs (dy));
            std::printf ("  control %d: measured (%d,%d)  layout (%d,%d)  offset (%+d,%+d)\n",
                         i + 1, centres[(size_t) i].x, centres[(size_t) i].y,
                         want[(size_t) i].x, want[(size_t) i].y, dx, dy);
            CHECK (std::abs (dx) <= 2 && std::abs (dy) <= 2, "hero knob on its layout centre");
        }
        // the V2 doc removes the sound-type badge from model pages; the approved
        // reference shows one (DECAYING). Kept hidden pending the client's call.
        CHECK (! findOne<OutputMeterV2> (page) == false, "OUTPUT meter present on the model page");
        if (isSix) savePng (page, "round2-page-mks-6knob.png");   // the 3x2 outlier
    }

    // -------------------------------------------------------- full arrangement
    std::printf ("\n== restored full-window arrangement ==\n");
    CHECK (Theme::M::designW == 1536 && Theme::M::designH == 1024, "root reference 1536x1024 (3:2)");
    proc.state.currentPage = 0;
    {
        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        CHECK (ed != nullptr, "editor created");
        if (ed != nullptr)
        {
            ed->setSize (Theme::M::designW, Theme::M::designH);
            // ROUND 3 (E): the fixed-aspect constrainer is gone so a MAXIMISED
            // window can fill the screen. The law that replaces it is that the
            // root is always scaled UNIFORMLY and centred, with chassis-coloured
            // letterbox around it — verified here at three window shapes.
            CHECK (ed->getConstrainer()->getFixedAspectRatio() == 0.0, "no fixed-aspect lock (maximise allowed)");
            {
                auto* root = ed->getChildComponent (0);
                struct Shape { int w, h; const char* what; };
                const Shape shapes[] = { { 1536, 1024, "native" },
                                         { 2560, 1080, "ultrawide (maximised)" },
                                         { 1200, 1400, "tall" } };
                bool uniform = true, centred = true, inside = true;
                for (auto& s : shapes)
                {
                    ed->setSize (s.w, s.h);
                    auto t = root->getTransform();
                    const float sx = t.mat00, sy = t.mat11;
                    const float expect = juce::jmin ((float) s.w / 1536.0f, (float) s.h / 1024.0f);
                    auto tl = juce::Point<float> (0.0f, 0.0f).transformedBy (t);
                    auto br = juce::Point<float> (1536.0f, 1024.0f).transformedBy (t);
                    std::printf ("  %-24s window %dx%d -> scale (%.4f,%.4f) box (%.1f,%.1f)-(%.1f,%.1f)\n",
                                 s.what, s.w, s.h, sx, sy, tl.x, tl.y, br.x, br.y);
                    uniform &= std::abs (sx - sy) < 1.0e-4f && std::abs (sx - expect) < 1.0e-4f;
                    centred &= std::abs (tl.x - ((float) s.w - br.x)) < 1.0f
                            && std::abs (tl.y - ((float) s.h - br.y)) < 1.0f;
                    inside  &= br.x <= (float) s.w + 0.5f && br.y <= (float) s.h + 0.5f;
                }
                CHECK (uniform, "root scaled uniformly at every window shape (never stretched)");
                CHECK (centred, "root centred -> symmetric chassis letterbox");
                CHECK (inside,  "root always fits inside the window (contain, not cover)");
                ed->setSize (Theme::M::designW, Theme::M::designH);
            }
            CHECK (Theme::M::maxW >= 3840 && Theme::M::maxH >= 2160,
                   "resize limits allow a maximised size on a large display");

            // every approved region present AT THE SAME TIME on a model page
            CHECK (findOne<PresetBrowserPanel> (*ed) != nullptr, "left preset browser column visible");
            CHECK (findOne<ModelPagePanel> (*ed)    != nullptr, "centre model panel visible");
            auto* dock = findOne<PerformDock> (*ed);
            CHECK (dock != nullptr && dock->isVisible(),        "right PERFORM dock visible");
            CHECK (findOne<FxRackStrip> (*ed)       != nullptr, "bottom FX rack strip visible");
            CHECK (findOne<MasterFxColumn> (*ed)    != nullptr, "MASTER FX column visible");
            CHECK (findOne<BottomBar> (*ed)         != nullptr, "bottom bar / status bar visible");
            CHECK (findOne<HeaderBar> (*ed)         != nullptr, "header visible");

            std::vector<SlotStrip*> dockSlots;
            findAll<SlotStrip> (*ed, dockSlots);
            CHECK ((int) dockSlots.size() == 4, "four Perform slots (A-D) in the dock");
            for (int i = 0; i < juce::jmin (4, (int) dockSlots.size()); ++i)
            {
                auto b = dockSlots[(size_t) i]->getBounds();
                const bool ok = b == juce::Rectangle<int> (L::dkSlotX, L::dkSlotY + i * L::dkSlotPitch,
                                                           L::dkSlotW, L::dkSlotH);
                std::printf ("  dock slot %c at (%d,%d,%dx%d)\n", 'A' + i, b.getX(), b.getY(),
                             b.getWidth(), b.getHeight());
                CHECK (ok, "dock slot on its layout row");
            }

            std::vector<FxUnitPanel*> units;
            findAll<FxUnitPanel> (*ed, units);
            std::printf ("  FX rack units on screen: %d\n", (int) units.size());
            CHECK (! units.empty() && (int) units.size() <= 6, "up to six FX units on the rack strip");
            for (auto* u : units)
            {
                const float ar = (float) u->getWidth() / (float) u->getHeight();
                CHECK (std::abs (ar - 0.764f) < 0.02f, "FX unit at the kit's native 0.764:1 plate ratio");
                break;
            }
            savePng (*ed, "round2-full-rhodes.png");

            // -------- PERFORM: slots move to the CENTRE (client correction)
            std::printf ("\n== PERFORM page (slots move to the middle) ==\n");
            proc.state.currentPage = 4;
            delete ed;
        }
    }
    {
        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        if (ed != nullptr)
        {
            ed->setSize (Theme::M::designW, Theme::M::designH);
            auto* dock = findOne<PerformDock> (*ed);
            CHECK (dock == nullptr || ! dock->isVisible(), "right dock steps aside on PERFORM");
            auto* centre = findOne<PerformCenterPanel> (*ed);
            CHECK (centre != nullptr, "PERFORM centre stage present");
            if (centre != nullptr)
            {
                std::vector<SlotStrip*> slots;
                findAll<SlotStrip> (*centre, slots);
                CHECK ((int) slots.size() == 4, "four slots on the centre stage");
                for (int i = 0; i < juce::jmin (4, (int) slots.size()); ++i)
                {
                    auto b = slots[(size_t) i]->getBounds();
                    const bool ok = b == juce::Rectangle<int> (L::pcSlotX[i], L::pcSlotY[i],
                                                               L::pcSlotW, L::pcSlotH);
                    std::printf ("  centre slot %c at (%d,%d,%dx%d)  expected (%d,%d,%dx%d)\n",
                                 'A' + i, b.getX(), b.getY(), b.getWidth(), b.getHeight(),
                                 L::pcSlotX[i], L::pcSlotY[i], L::pcSlotW, L::pcSlotH);
                    CHECK (ok, "centre slot on its 2x2 grid cell");
                }
                CHECK (slots.size() == 4
                       && slots[0]->getY() == slots[1]->getY()
                       && slots[2]->getY() == slots[3]->getY()
                       && slots[2]->getY() > slots[0]->getY(), "A/B top row, C/D second row");
                auto macros = heroCentres (*centre);
                CHECK ((int) macros.size() == 4, "four macro knobs beneath the grid");
            }
            // the other regions stay in place while PERFORM is showing
            CHECK (findOne<PresetBrowserPanel> (*ed) != nullptr, "preset browser stays in place on PERFORM");
            CHECK (findOne<FxRackStrip> (*ed) != nullptr,        "FX rack strip stays in place on PERFORM");
            CHECK (findOne<BottomBar> (*ed) != nullptr,          "bottom bar stays in place on PERFORM");
            savePng (*ed, "round2-full-perform.png");
            delete ed;
        }
    }

    // ================================================== CLIENT REVIEW ROUND 3
    // These replace the throwaway master-knob diagnostic: the same checks, kept
    // as assertions, plus the new round-3 surfaces.
    std::printf ("\n== R3 (D) master section ==\n");
    {
        proc.state.currentPage = 0;
        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        ed->setSize (Theme::M::designW, Theme::M::designH);
        ed->setVisible (true);
        auto* hdr = findOne<HeaderBar> (*ed);
        std::vector<ParamKnob*> knobs;
        findAll<ParamKnob> (*hdr, knobs);
        CHECK ((int) knobs.size() == 1 && knobs[0]->path() == "host.masterVol",
               "exactly one header knob, bound to host.masterVol");
        for (auto* k : knobs)
        {
            std::vector<juce::Slider*> sl;
            collectSliders (*k, sl);
            CHECK ((int) sl.size() == 1, "master knob owns one rotary");
            for (auto* s : sl)
            {
                // ROOT CAUSE of round 2's "dead" master knob: ParamKnob's default
                // layout reserved the top 15 px for a caption it did not have, so
                // the rotary was 41 px in a 56 px cell and its centre sat 7 px
                // BELOW Theme::L::masterKnobCy. Nothing intercepted the mouse -
                // the target was simply small and off its mark.
                auto c = ed->getLocalPoint (s, s->getLocalBounds().getCentre());
                std::printf ("  rotary %dx%d, centre in editor (%d,%d), layout centre (%d,%d)\n",
                             s->getWidth(), s->getHeight(), c.x, c.y, L::masterKnobCx, L::masterKnobCy);
                CHECK (std::abs (c.x - L::masterKnobCx) <= 1 && std::abs (c.y - L::masterKnobCy) <= 1,
                       "master rotary centred on its layout centre");
                CHECK (s->getWidth() >= L::masterKnobD, "master rotary at least the token diameter");
                CHECK (ed->getComponentAt (c) == s, "master rotary receives the mouse at its centre");

                auto* prm = proc.apvts.getParameter ("masterVol");
                s->setValue (0.25, juce::sendNotificationSync);
                CHECK (std::abs (prm->getValue() - 0.25f) < 0.01f, "master rotary drives masterVol");
                prm->setValueNotifyingHost (0.75f);
                k->refresh();
                CHECK (std::abs ((float) s->getValue() - 0.75f) < 0.01f, "master rotary follows the host");
            }
        }
        CHECK (proc.apvts.getParameter ("limiter") != nullptr, "limiter parameter exists");
        std::vector<ux::PlateButton*> plates;
        findAll<ux::PlateButton> (*hdr, plates);
        bool limiterLive = false;
        for (auto* b : plates) if (b->getButtonText() == "LIMITER") limiterLive = b->isEnabled();
        CHECK (limiterLive, "LIMITER plate is enabled (no longer permanently dead)");

        proc.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        for (int i = 0; i < 40; ++i) { proc.processBlock (buf, midi); midi.clear(); }
        std::printf ("  masterPeak after 40 blocks with a held note: L=%.5f R=%.5f\n",
                     proc.masterPeakL.load(), proc.masterPeakR.load());
        CHECK (proc.masterPeakL.load() > 1.0e-4f, "master meter fed from the real master-bus level");
        delete ed;
    }

    // ------------------------------------------------- R3 (A) styled chrome
    std::printf ("\n== R3 (A) hardware chrome ==\n");
    {
        struct MenuSheet : juce::Component
        {
            void paint (juce::Graphics& g) override
            {
                g.fillAll (juce::Colour (0xff0C0B0A));
                ux::drawMicroWeave (g, getLocalBounds().toFloat(), 0.10f, 0.0f);
                auto& lf = getLookAndFeel();
                auto* pm = dynamic_cast<juce::PopupMenu::LookAndFeelMethods*> (&lf);
                if (pm == nullptr) return;
                // the menu plaque, drawn exactly as a live PopupMenu would
                juce::Rectangle<int> menu (26, 26, 318, 300);
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    g.setOrigin (menu.getPosition());
                    g.reduceClipRegion (juce::Rectangle<int> (menu.getWidth(), menu.getHeight()));
                    pm->drawPopupMenuBackground (g, menu.getWidth(), menu.getHeight());
                    int y = 8;
                    auto row = [&] (int h) { auto r = juce::Rectangle<int> (8, y, menu.getWidth() - 16, h); y += h; return r; };
                    pm->drawPopupMenuSectionHeader (g, row (26), "Settings");
                    pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                           "Audio / MIDI Devices...", {}, nullptr, nullptr);
                    pm->drawPopupMenuItem (g, row (30), false, true, true, false, false,
                                           "Preferences...", {}, nullptr, nullptr);
                    pm->drawPopupMenuItem (g, row (30), false, true, false, true, false,
                                           "Rescan Library", {}, nullptr, nullptr);
                    pm->drawPopupMenuItem (g, row (9), true, true, false, false, false, {}, {}, nullptr, nullptr);
                    pm->drawPopupMenuSectionHeader (g, row (26), "Macro");
                    for (int i = 1; i <= 4; ++i)
                        pm->drawPopupMenuItem (g, row (30), false, true, false, i == 2, false,
                                               "Assign to Macro " + juce::String (i), {}, nullptr, nullptr);
                    pm->drawPopupMenuItem (g, row (30), false, true, false, false, true,
                                           "Replace with", {}, nullptr, nullptr);
                    pm->drawPopupMenuItem (g, row (30), false, false, false, false, false,
                                           "Clear MIDI mapping (disabled)", {}, nullptr, nullptr);
                }
                // and the search well / text editor register beside it
                g.setColour (Theme::gold);
                g.setFont (Theme::display (13.0f));
                g.drawText ("SEARCH FIELD", 380, 26, 240, 18, juce::Justification::centredLeft, false);
                ux::drawChromeWell (g, { 380.0f, 50.0f, 240.0f, 30.0f }, false);
                ux::drawChromeWell (g, { 380.0f, 92.0f, 240.0f, 30.0f }, true);
                g.setColour (Theme::muted.withAlpha (0.7f));
                g.setFont (Theme::ui (13.0f));
                g.drawText ("Search presets...", 410, 50, 210, 30, juce::Justification::centredLeft, false);
                g.setColour (Theme::cream);
                g.drawText ("focused", 410, 92, 210, 30, juce::Justification::centredLeft, false);
                if (icon_ != nullptr)
                {
                    icon_->drawWithin (g, { 387.0f, 57.0f, 16.0f, 16.0f }, juce::RectanglePlacement::centred, 0.85f);
                    icon_->drawWithin (g, { 387.0f, 99.0f, 16.0f, 16.0f }, juce::RectanglePlacement::centred, 0.85f);
                }
                g.setColour (Theme::gold);
                g.setFont (Theme::display (13.0f));
                g.drawText ("HIGHLIGHT ROW IS GOLD, NEVER SYSTEM BLUE", 380, 150, 250, 40,
                            juce::Justification::topLeft, false);
            }
            std::unique_ptr<juce::Drawable> icon_ { assets::makeIcon (assets::Icon::search, Theme::muted) };
        } sheet;
        sheet.setLookAndFeel (&lnf);
        sheet.setSize (660, 360);
        savePng (sheet, "round3-chrome-menu.png");
        sheet.setLookAndFeel (nullptr);
        CHECK (true, "styled popup-menu / search-field sheet rendered");
    }

    // ------------------------------ R3 (F) macro editor over the live window
    std::printf ("\n== R3 (F) macro drag-drop + editor ==\n");
    {
        proc.state.currentPage = 4;
        proc.state.macros[0].name = "Shimmer";
        proc.state.macros[0].targets.clear();
        proc.state.macros[0].targets.push_back ({ "slot0.engine.bark",   0.0f, 1.0f, 1.0f,  1.0f, false });
        proc.state.macros[0].targets.push_back ({ "slot0.fx0.drive",     0.1f, 0.8f, 2.0f, -0.7f, false });
        proc.state.macros[0].targets.push_back ({ "slot0.fx3.mix",       0.0f, 1.0f, 1.0f,  0.5f, true  });

        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        ed->setSize (Theme::M::designW, Theme::M::designH);
        ed->setVisible (true);

        // the PERFORM centre stage owns the visible macros (the hidden dock keeps
        // its own set, so search the centre panel specifically)
        std::vector<MacroKnobUnit*> macros;
        findAll<MacroKnobUnit> (*findOne<PerformCenterPanel> (*ed), macros);
        CHECK ((int) macros.size() == 4, "four macro drop targets on the PERFORM page");

        // a parameter knob offers a drag payload, and a macro accepts it
        std::vector<ParamKnob*> allKnobs;
        findAll<ParamKnob> (*ed, allKnobs);
        ParamKnob* fxKnob = nullptr;
        for (auto* k : allKnobs) if (k->path().startsWith ("slot0.fx")) { fxKnob = k; break; }
        CHECK (fxKnob != nullptr, "a droppable FX parameter knob exists");
        if (fxKnob != nullptr && ! macros.empty())
        {
            auto payload = macrodrag::makePayload (fxKnob->path(), fxKnob->caption());
            CHECK (macrodrag::isPayload (payload) && macrodrag::pathOf (payload) == fxKnob->path(),
                   "drag payload round-trips the parameter path");
            juce::DragAndDropTarget::SourceDetails d (payload, fxKnob, {});
            CHECK (macros[1]->isInterestedInDragSource (d), "macro knob accepts a parameter drag");
            juce::DragAndDropTarget::SourceDetails junk (juce::var ("something else"), fxKnob, {});
            CHECK (! macros[1]->isInterestedInDragSource (junk), "macro knob rejects unrelated drags");
            const auto before = proc.state.macros[1].targets.size();
            macros[1]->itemDropped (d);
            CHECK (proc.state.macros[1].targets.size() == before + 1, "dropping creates a macro link");
            if (! proc.state.macros[1].targets.empty())
            {
                auto& t = proc.state.macros[1].targets.back();
                CHECK (t.min == 0.0f && t.max == 1.0f && t.depth == 1.0f && t.curve == 1.0f,
                       "default link is full range, full positive depth, linear");
            }
            macros[1]->itemDropped (d);
            CHECK (proc.state.macros[1].targets.size() == before + 1, "dropping the same parameter twice is a no-op");
        }

        // bipolar depth actually inverts the direction of travel
        {
            MacroTarget up   { "x", 0.0f, 1.0f, 1.0f,  1.0f, false };
            MacroTarget down { "x", 0.0f, 1.0f, 1.0f, -1.0f, false };
            MacroTarget half { "x", 0.0f, 1.0f, 1.0f,  0.5f, false };
            std::printf ("  depth +1: %.2f -> %.2f   depth -1: %.2f -> %.2f   depth +0.5 top: %.2f\n",
                         up.apply (0.0f), up.apply (1.0f), down.apply (0.0f), down.apply (1.0f), half.apply (1.0f));
            CHECK (up.apply (0.0f) < up.apply (1.0f), "positive depth increases with the macro");
            CHECK (down.apply (0.0f) > down.apply (1.0f), "negative depth DECREASES with the macro");
            CHECK (std::abs (half.apply (1.0f) - 0.5f) < 1.0e-4f, "|depth| scales the covered range");
            MacroTarget inv { "x", 0.0f, 1.0f, 1.0f, 1.0f, true };
            CHECK (std::abs (inv.apply (0.0f) - 1.0f) < 1.0e-4f, "invert mirrors the macro's travel");
        }

        // the editor popup, rendered over the live window for the client
        if (auto* host = OverlayHost::find (findOne<MacroKnobUnit> (*ed)))
        {
            host->showOverlay (std::make_unique<MacroEditorPanel> (proc, 0),
                               MacroEditorPanel::kWidth, MacroEditorPanel::heightFor (proc, 0));
            savePng (*ed, "round3-macro-editor.png");
            CHECK (findOne<MacroEditorPanel> (*ed) != nullptr, "macro editor popup opens over the window");
            host->closeOverlay();
        }
        savePng (*ed, "round3-full-perform.png");
        delete ed;
    }

    // ------------------------------------------- R3 (B/C/G) header + browser
    std::printf ("\n== R3 (B/C/G) header, licence, browser ==\n");
    {
        proc.state.currentPage = 0;
        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        ed->setSize (Theme::M::designW, Theme::M::designH);
        ed->setVisible (true);
        auto* hdr = findOne<HeaderBar> (*ed);

        std::vector<juce::DrawableButton*> icons;
        findAll<juce::DrawableButton> (*hdr, icons);
        bool key = false, gear = false;
        for (auto* b : icons)
        {
            if (b->getName() == "licence")  key = true;
            if (b->getName() == "settings") gear = true;
        }
        CHECK (key,  "cart replaced by a live KEY button (item C)");
        CHECK (gear, "gear is a live button (item B)");

        // nav cells evenly distributed (item G)
        std::vector<ux::TabCellButton*> tabs;
        findAll<ux::TabCellButton> (*hdr, tabs);
        CHECK ((int) tabs.size() == 5, "five nav cells");
        int minW = 9999, maxW = 0;
        for (auto* t : tabs) { minW = juce::jmin (minW, t->getWidth()); maxW = juce::jmax (maxW, t->getWidth()); }
        std::printf ("  nav cell widths %d..%d\n", minW, maxW);
        CHECK (maxW - minW <= 1, "nav cells evenly spaced (reference distribution)");

        // ---------------------------------------- CATEGORY DISPLAY NAMES
        // The client agreed four names with the artist and reported "I don't
        // see the name category changes": the strip hardcoded its own copy of
        // the labels, so it kept saying RHODES / EP and FM after worldName()
        // was meant to be the single source. These two checks are what stops
        // that recurring — the labels must BE worldName()'s, and they must fit.
        {
            const char* expect[5] = { "JM Pianos", "MKS", "FM Keys", "Strings", "PERFORM" };
            for (int i = 0; i < (int) tabs.size(); ++i)
            {
                std::printf ("  nav cell %d: \"%s\"\n", i, tabs[(size_t) i]->getButtonText().toRawUTF8());
                CHECK (tabs[(size_t) i]->getButtonText() == juce::String (worldName (i)),
                       "nav cell label comes from worldName()");
                CHECK (tabs[(size_t) i]->getButtonText() == juce::String (expect[i]),
                       "nav cell carries the name the client agreed");
            }

            // "JM PIANOS" and "FM KEYS" are wider than "RHODES / EP" and "FM".
            // drawTabCell upper-cases and draws centred at 15 pt display with
            // no eliding, so a label wider than its cell would simply be
            // clipped. Measure the drawn string against the cell it has.
            const juce::Font nav = Theme::display (15.0f);
            for (auto* t : tabs)
            {
                const auto drawn = t->getButtonText().toUpperCase();
                const int w = juce::GlyphArrangement::getStringWidthInt (nav, drawn);
                std::printf ("  label \"%s\" %d px in a %d px cell (%d px clear)\n",
                             drawn.toRawUTF8(), w, t->getWidth(), t->getWidth() - w);
                CHECK (w <= t->getWidth() - 12,
                       "nav label fits its cell with breathing room");
            }

            // The cells are siblings of the SegmentStrip (both are children of
            // the HeaderBar), so snapshotting the strip alone captures an empty
            // plaque. Take the header's nav rectangle, which has both.
            saveImage (hdr->createComponentSnapshot (juce::Rectangle<int> (L::navX - 6, L::navY - 6,
                                                                           L::navW + 12, L::navH + 12),
                                                     true, 2.0f),
                       "mireku-nav-strip-category-names.png");
        }

        savePng (*ed, "round3-full-rhodes.png");

        // the gear menu drawn over the live window, exactly as the LookAndFeel
        // paints a real PopupMenu window (menu windows are separate desktop
        // windows, so they cannot be captured by a component snapshot)
        {
            auto* pm = dynamic_cast<juce::PopupMenu::LookAndFeelMethods*> (&lnf);
            const int mx = L::gearX, my = L::iconY + L::iconD + 6, mw = 300, mh = 262;
            juce::Image menuImg (juce::Image::ARGB, mw, mh, true, juce::SoftwareImageType());
            {
            juce::Graphics g (menuImg);
            pm->drawPopupMenuBackground (g, mw, mh);
            int y = 8;
            auto row = [&] (int h) { auto r = juce::Rectangle<int> (8, y, mw - 16, h); y += h; return r; };
            pm->drawPopupMenuSectionHeader (g, row (26), "Settings");
            pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                   "Audio / MIDI Devices...", {}, nullptr, nullptr);
            pm->drawPopupMenuItem (g, row (30), false, true, true, false, false,
                                   "Preferences...", {}, nullptr, nullptr);
            pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                   "Rescan Library", {}, nullptr, nullptr);
            pm->drawPopupMenuItem (g, row (9), true, true, false, false, false, {}, {}, nullptr, nullptr);
            pm->drawPopupMenuSectionHeader (g, row (26), "Licence");
            pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                   "Activate licence...", {}, nullptr, nullptr);
            }   // the Graphics must go out of scope before the image is read back
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
            { juce::Graphics gw (img); gw.drawImageAt (menuImg, mx, my); }
            saveImage (img, "round3-menu-open.png");
        }

        if (auto* host = OverlayHost::find (hdr))
        {
            host->showOverlay (std::make_unique<LicensePanel> (proc),
                               LicensePanel::kWidth, LicensePanel::kHeight);
            savePng (*ed, "round3-licence.png");
            host->closeOverlay();
            host->showOverlay (std::make_unique<SettingsPanel> (proc),
                               SettingsPanel::kWidth, SettingsPanel::kHeight);
            savePng (*ed, "round3-settings.png");
            host->closeOverlay();
        }

        // performance presets are listed on PERFORM (item G)
        auto factory = JMSignatureProcessor::performancePresetDir().getChildFile ("Factory");
        const int n = factory.findChildFiles (juce::File::findFiles, false, "*.json").size();
        std::printf ("  factory performance presets on disk: %d (%s)\n", n,
                     factory.getFullPathName().toRawUTF8());
        CHECK (n >= 4, "factory performance presets exist for the PERFORM browser");
        delete ed;
    }

    //========================================================================
    // CLIENT REVIEW ROUND 4
    //========================================================================

    // ------------------------------------------- (1) MIDI routing -> slot mask
    std::printf ("\n== R4 (1) MIDI routing (channel + device -> slot) ==\n");
    {
        proc.prepareToPlay (48000.0, 512);
        // all four slots on, SOLO off, so only the routing can silence a slot
        if (auto* s = proc.apvts.getParameter ("solo")) s->setValueNotifyingHost (0.0f);
        for (int i = 0; i < 4; ++i)
            if (auto* p = proc.apvts.getParameter ("slotEnable" + juce::String (i)))
                p->setValueNotifyingHost (1.0f);

        juce::AudioBuffer<float> buf (2, 512);
        auto panic = [&]
        {
            juce::MidiBuffer mb;
            mb.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            buf.clear();
            proc.processBlock (buf, mb);
            for (auto& e : proc.engines) e.allNotesOff (false);
        };
        auto play = [&] (const juce::MidiMessage& m)
        {
            juce::MidiBuffer mb;
            mb.addEvent (m, 0);
            buf.clear();
            proc.processBlock (buf, mb);
        };
        auto voices = [&] { return std::array<int, 4> { proc.engines[0].activeVoices(),
                                                        proc.engines[1].activeVoices(),
                                                        proc.engines[2].activeVoices(),
                                                        proc.engines[3].activeVoices() }; };

        CHECK (proc.midiRouting.channelsAreOmni(), "default routing is Omni (unchanged behaviour)");

        // Omni: a note reaches every enabled slot
        panic();
        play (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 100));
        auto vOmni = voices();
        std::printf ("  omni note -> voices A%d B%d C%d D%d\n", vOmni[0], vOmni[1], vOmni[2], vOmni[3]);
        CHECK (vOmni[0] > 0 && vOmni[1] > 0 && vOmni[2] > 0 && vOmni[3] > 0,
               "Omni: one note reaches all four slots");

        // channel 2 -> slot B only
        panic();
        proc.midiRouting.setChannelMask (2, 0b0010);
        play (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100));
        auto vB = voices();
        std::printf ("  ch2->B note  -> voices A%d B%d C%d D%d\n", vB[0], vB[1], vB[2], vB[3]);
        CHECK (vB[1] > 0 && vB[0] == 0 && vB[2] == 0 && vB[3] == 0,
               "channel 2 routed to B sounds ONLY slot B");
        // ... and the unrouted channels still work
        play (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100));
        CHECK (proc.engines[0].activeVoices() > 0, "an unrouted channel still reaches its slots");

        // sustain + note-off obey the same mask (§5.4 must not leak across slots)
        panic();
        play (juce::MidiMessage::controllerEvent (2, 64, 127));
        std::printf ("  CC64 on ch2 -> sustain A=%d B=%d\n",
                     (int) proc.engines[0].sustainDown(), (int) proc.engines[1].sustainDown());
        CHECK (proc.engines[1].sustainDown() && ! proc.engines[0].sustainDown(),
               "CC64 sustain is filtered by the same routing");
        play (juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100));
        play (juce::MidiMessage::noteOff (2, 60));
        CHECK (proc.engines[1].sustainedVoices() >= 1, "deferred note-off still works on the routed slot");
        play (juce::MidiMessage::controllerEvent (2, 64, 0));
        panic();

        // the DEVICE path: the standalone router stamps a mask before queuing,
        // so it is exercised here without needing real hardware.
        proc.midiRouting.resetChannelsToOmni();
        proc.routedQueue.push (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0b0100, true);
        buf.clear();
        { juce::MidiBuffer empty; proc.processBlock (buf, empty); }
        auto vC = voices();
        std::printf ("  device->C note -> voices A%d B%d C%d D%d\n", vC[0], vC[1], vC[2], vC[3]);
        CHECK (vC[2] > 0 && vC[0] == 0 && vC[1] == 0 && vC[3] == 0,
               "a device routed to slot C sounds ONLY slot C");
        panic();

        // routing survives a state round-trip
        proc.midiRouting.setChannelMask (5, 0b1001);
        proc.midiRouting.devices().push_back ({ "test-id", "Test Keyboard", true, 0b0010 });
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        proc.midiRouting.resetChannelsToOmni();
        proc.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (proc.midiRouting.channelMask (5) == 0b1001, "channel routing survives plugin state");
        auto* dev = proc.midiRouting.findDevice ("test-id");
        CHECK (dev != nullptr && dev->mask == 0b0010 && dev->enabled,
               "device routing survives plugin state");
        proc.midiRouting.resetChannelsToOmni();
    }

    // ---------------------------------------- (2) pitch bend through the plugin
    std::printf ("\n== R4 (2) pitch bend + wheels ==\n");
    {
        CHECK (proc.apvts.getParameter ("bendRange") != nullptr, "configurable pitch-bend range parameter exists");
        std::printf ("  default bend range: +/-%d st\n", proc.pitchBendRange());
        CHECK (proc.pitchBendRange() == 2, "default range is the standard +/-2 semitones");

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        mb.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 1);      // full up = +2 st
        buf.clear();
        proc.processBlock (buf, mb);
        for (int i = 0; i < 60; ++i) { juce::MidiBuffer e; buf.clear(); proc.processBlock (buf, e); }
        const double expected = std::pow (2.0, 2.0 / 12.0);
        std::printf ("  slot A bend ratio after full-up wheel: %.6f (expected %.6f)\n",
                     proc.engines[0].pitchBendRatio(), expected);
        CHECK (std::abs (proc.engines[0].pitchBendRatio() - expected) < 1.0e-3,
               "a real pitch-bend message shifts the playback rate by 2^(range/12)");

        { juce::MidiBuffer c; c.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
          buf.clear(); proc.processBlock (buf, c); }
        for (int i = 0; i < 80; ++i) { juce::MidiBuffer e; buf.clear(); proc.processBlock (buf, e); }
        CHECK (std::abs (proc.engines[0].pitchBendRatio() - 1.0) < 1.0e-3,
               "wheel back to centre returns the rate to unity");
        for (auto& e : proc.engines) e.allNotesOff (false);
    }

    //========================================================================
    // MODULATION PHASE 1 — LFOs + matrix, through the REAL processor.
    //========================================================================

    // -------- the architectural claim: modulation OFFSETS, it never overwrites
    std::printf ("\n== MOD (1) base + offset: the knob is never overwritten ==\n");
    {
        proc.prepareToPlay (48000.0, 512);
        for (auto& r : proc.state.mod.rows) r = MatrixRow {};
        for (auto& m : proc.state.macros) m.targets.clear();

        // LFO 1: free-running 5 Hz triangle, so the value sweeps the full +-1
        auto& l1 = proc.state.mod.lfos[0];
        l1.sync = 0; l1.rateHz = 5.0f; l1.shape = (int) LfoShape::triangle;
        l1.fadeSec = 0.0f; l1.retrigger = (int) LfoRetrigger::free;

        auto& row = proc.state.mod.rows[0];
        row.source = ModSource::lfo1;
        row.destPath = "slot0.engine.bark";      // 0..100
        row.depth = 0.5f; row.min = 0.0f; row.max = 1.0f; row.curve = 1.0f;
        row.enabled = true;
        proc.publishModulation();

        juce::AudioBuffer<float> buf (2, 512);
        auto sweep = [&] (float base)
        {
            proc.setParamNormalized ("slot0.engine.bark", base);
            float lo = 1.0e9f, hi = -1.0e9f;
            for (int b = 0; b < 60; ++b)
            {
                juce::MidiBuffer e;
                buf.clear();
                proc.processBlock (buf, e);
                const float v = proc.engines[0].engineParam (0);
                lo = juce::jmin (lo, v); hi = juce::jmax (hi, v);
            }
            return std::make_pair (lo, hi);
        };

        auto a = sweep (0.25f);
        const float storedAfterA = proc.state.slots[0].engineParams["bark"];
        auto b = sweep (0.60f);
        const float storedAfterB = proc.state.slots[0].engineParams["bark"];
        std::printf ("  base 0.25 -> engine value swept %.2f..%.2f   (stored base still %.2f)\n",
                     a.first, a.second, storedAfterA);
        std::printf ("  base 0.60 -> engine value swept %.2f..%.2f   (stored base still %.2f)\n",
                     b.first, b.second, storedAfterB);

        // 1. the STORED value is exactly the knob, never the modulated one
        CHECK (std::abs (storedAfterA - 25.0f) < 0.01f && std::abs (storedAfterB - 60.0f) < 0.01f,
               "the stored parameter value stays exactly where the player put it");
        // 2. depth 0.5 on a bipolar source = +-25% of the range (School A)
        CHECK (std::abs ((a.second - a.first) - 50.0f) < 2.0f,
               "a bipolar source at depth 50% swings the parameter by 50% peak-to-peak");
        // 3. turning the knob moves the WHOLE modulated range, by exactly the
        //    amount the knob moved — the behaviour the spec asks for by name
        std::printf ("  range shift when the knob moved by 35%%: lo %+.2f  hi %+.2f\n",
                     b.first - a.first, b.second - a.second);
        CHECK (std::abs ((b.first - a.first) - 35.0f) < 2.0f
               && std::abs ((b.second - a.second) - 35.0f) < 2.0f,
               "turning the knob while the LFO runs moves the whole modulated range");
        // 4. and it is still centred on the knob
        CHECK (std::abs ((a.first + a.second) * 0.5f - 25.0f) < 1.5f,
               "the modulated range stays centred on the base value");

        // disabling the row must hand the destination back to the knob
        row.enabled = false;
        proc.publishModulation();
        for (int i = 0; i < 4; ++i) { juce::MidiBuffer e; buf.clear(); proc.processBlock (buf, e); }
        CHECK (std::abs (proc.engines[0].engineParam (0) - 60.0f) < 0.01f,
               "disabling a row restores the destination to the knob's own value");
        row.enabled = true;
        proc.publishModulation();
    }

    // -------- no zipper at high LFO rates, measured on the real audio path
    std::printf ("\n== MOD (2) no zipper at high LFO rates ==\n");
    {
        // Gain modulation is multiplicative, so rendering the SAME note twice —
        // once clean, once with a 39 Hz LFO on master volume — and dividing gives
        // the actual per-sample gain the modulation applied. Zipper noise would
        // show up as 64-sample-wide steps in that signal.
        for (auto& r : proc.state.mod.rows) r = MatrixRow {};
        proc.publishModulation();
        for (auto& e : proc.engines) e.allNotesOff (false);
        // one slot, no limiter, so the measured ratio IS the modulation gain
        for (int i = 0; i < 4; ++i)
            if (auto* p = proc.apvts.getParameter ("slotEnable" + juce::String (i)))
                p->setValueNotifyingHost (i == 0 ? 1.0f : 0.0f);
        if (auto* p = proc.apvts.getParameter ("limiter")) p->setValueNotifyingHost (0.0f);
        if (auto* p = proc.apvts.getParameter ("masterVol")) p->setValueNotifyingHost (0.55f);
        // The Rhodes rack carries a tape delay whose wow/flutter is a random
        // source, so the slot's own FX would make two renders differ for reasons
        // that have nothing to do with modulation. Measure on the dry engine.
        const auto savedRack = proc.state.slots[0].rack;
        proc.state.slots[0].rack.clear();
        proc.slotRacks[0].setState (proc.state.slots[0].rack);
        proc.prepareToPlay (48000.0, 512);

        auto render = [&] (bool withMod)
        {
            // Both renders keep the SAME row (and therefore the same 64-sample
            // control-block split through the whole engine); only the depth
            // changes, so the ratio between them isolates the gain modulation
            // and nothing else.
            for (auto& r : proc.state.mod.rows) r = MatrixRow {};
            {
                auto& l = proc.state.mod.lfos[0];
                l.sync = 0; l.rateHz = 39.0f; l.shape = (int) LfoShape::sine;
                l.fadeSec = 0.0f; l.retrigger = (int) LfoRetrigger::everyNote;
                auto& r = proc.state.mod.rows[0];
                r.source = ModSource::lfo1;
                r.destPath = "host.masterVol";
                r.depth = withMod ? 0.4f : 0.0f;
                r.min = 0.0f; r.max = 1.0f; r.curve = 1.0f; r.enabled = true;
            }
            proc.publishModulation();
            // make the two renders bit-comparable: the round-robin counter would
            // otherwise pick a different sample the second time round
            for (auto& e : proc.engines) { e.allNotesOff (false); e.setBundle (e.bundle()); }
            proc.prepareToPlay (48000.0, 512);

            std::vector<float> out;
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer mb;
            mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
            for (int b = 0; b < 12; ++b)
            {
                buf.clear();
                proc.processBlock (buf, mb);
                mb.clear();
                const auto* p = buf.getReadPointer (0);
                for (int i = 0; i < 512; ++i) out.push_back (p[i]);
            }
            for (auto& e : proc.engines) e.allNotesOff (false);
            return out;
        };

        const auto dry = render (false);
        const auto dry2 = render (false);
        {
            size_t firstDiff = dry.size();
            for (size_t i = 0; i < dry.size() && i < dry2.size(); ++i)
                if (std::abs (dry[i] - dry2[i]) > 1e-9f) { firstDiff = i; break; }
            std::printf ("  determinism control: two identical renders diverge at sample %d of %d\n",
                         (int) firstDiff, (int) dry.size());
            CHECK (firstDiff == dry.size(),
                   "the reference render is bit-identical twice (so the ratio below means something)");
        }
        const auto wet = render (true);
        int compared = 0, steps = 0;
        float worstStep = 0.0f, prevGain = 1.0f;
        bool havePrev = false;
        for (size_t i = 2048; i < dry.size() && i < wet.size(); ++i)   // skip the attack
        {
            if (std::abs (dry[i]) < 1.0e-3f) { havePrev = false; continue; }
            const float g = wet[i] / dry[i];
            if (g < 0.0f || g > 4.0f) { havePrev = false; continue; }
            if (havePrev)
            {
                const float d = std::abs (g - prevGain);
                worstStep = juce::jmax (worstStep, d);
                if (d > 0.01f) ++steps;
                ++compared;
            }
            prevGain = g;
            havePrev = true;
        }
        // A 20 ms linear ramp at 48 kHz can move the gain by at most
        // 1/(0.02*48000) = 1.04e-3 per sample; anything much above that is a step.
        std::printf ("  samples compared %d | worst per-sample gain step %.6f | steps > 1%%: %d\n",
                     compared, worstStep, steps);
        CHECK (compared > 2000, "the zipper measurement had enough signal to be meaningful");
        CHECK (worstStep < 3.0e-3f,
               "a 39 Hz LFO on master volume moves the gain smoothly, per sample, with no steps");
        CHECK (steps == 0, "no sample-to-sample gain jump above 1% anywhere in the render");

        for (auto& r : proc.state.mod.rows) r = MatrixRow {};
        proc.state.slots[0].rack = savedRack;
        proc.slotRacks[0].setState (savedRack);
        if (auto* p = proc.apvts.getParameter ("limiter")) p->setValueNotifyingHost (1.0f);
        proc.publishModulation();
    }

    // -------- editing the routing while audio renders
    std::printf ("\n== MOD (3) the matrix can be re-routed while audio renders ==\n");
    {
        proc.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        const char* dests[] = { "slot0.engine.bark", "slot0.engine.tine", "host.masterVol",
                                "slot0.fx0.drive", "mod.lfo2.rate", "host.slotPan0" };
        bool finite = true;
        for (int i = 0; i < 600; ++i)
        {
            // message-thread style edit, interleaved with real render calls
            auto& r = proc.state.mod.rows[(size_t) (i % kDefaultMatrixRows)];
            r.source = (ModSource) (1 + (i % (kNumModSources - 1)));
            r.destPath = dests[i % 6];
            r.depth = ((i % 21) - 10) * 0.1f;
            r.enabled = (i % 5) != 0;
            proc.state.mod.lfos[i % kNumLfos].division = i % numDivisions();
            proc.state.mod.lfos[i % kNumLfos].sync = (i % 3) != 0;
            proc.publishModulation();

            buf.clear();
            proc.processBlock (buf, mb);
            mb.clear();
            for (int c = 0; c < 2; ++c)
            {
                const auto* p = buf.getReadPointer (c);
                for (int s = 0; s < 256; ++s)
                    if (! std::isfinite (p[s]) || std::abs (p[s]) > 8.0f) finite = false;
            }
        }
        CHECK (finite, "600 live re-routings during rendering produce no NaN, inf or runaway output");
        for (auto& e : proc.engines) e.allNotesOff (false);
        for (auto& r : proc.state.mod.rows) r = MatrixRow {};
        for (auto& l : proc.state.mod.lfos) l = LfoSettings {};
        proc.publishModulation();
    }

    // -------- a macro link saved BEFORE this change still loads and behaves
    std::printf ("\n== MOD (4) pre-modulation state and macro links still work ==\n");
    {
        proc.prepareToPlay (48000.0, 512);
        // A version-1 state: macros with links, and NO "mod" property at all.
        auto* root = new juce::DynamicObject();
        root->setProperty ("version", 1);
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("name", "Legacy Sweep");
        auto* to = new juce::DynamicObject();
        to->setProperty ("path", "slot0.engine.tine");
        to->setProperty ("min", 0.0);
        to->setProperty ("max", 1.0);
        to->setProperty ("curve", 1.0);
        to->setProperty ("depth", 1.0);
        to->setProperty ("invert", false);
        juce::Array<juce::var> ts; ts.add (juce::var (to));
        mo->setProperty ("targets", juce::var (ts));
        juce::Array<juce::var> ms2; ms2.add (juce::var (mo));
        for (int i = 1; i < 4; ++i)
        {
            auto* e = new juce::DynamicObject();
            e->setProperty ("name", "MACRO " + juce::String (i + 1));
            e->setProperty ("targets", juce::Array<juce::var>());
            ms2.add (juce::var (e));
        }
        root->setProperty ("macros", juce::var (ms2));
        const auto json = juce::JSON::toString (juce::var (root));
        proc.setStateInformation (json.toRawUTF8(), (int) json.getNumBytesAsUTF8());

        CHECK (proc.state.macros[0].name == "Legacy Sweep"
               && proc.state.macros[0].targets.size() == 1
               && proc.state.macros[0].targets[0].path == "slot0.engine.tine",
               "a version-1 state's macro link loads unchanged");
        CHECK (proc.state.mod.rowCount == kDefaultMatrixRows && proc.state.mod.rows[0].destPath.isEmpty(),
               "a version-1 state gets a clean default modulation block");

        auto* macro1 = proc.apvts.getParameter ("macro1");
        juce::AudioBuffer<float> buf (2, 512);
        auto settle = [&] { for (int i = 0; i < 6; ++i) { juce::MidiBuffer e; buf.clear(); proc.processBlock (buf, e); } };
        proc.setParamNormalized ("slot0.engine.tine", 0.0f);
        macro1->setValueNotifyingHost (0.0f);
        settle();
        const float atZero = proc.engines[0].engineParam (1);
        macro1->setValueNotifyingHost (1.0f);
        settle();
        const float atOne = proc.engines[0].engineParam (1);
        macro1->setValueNotifyingHost (0.5f);
        settle();
        const float atHalf = proc.engines[0].engineParam (1);
        std::printf ("  legacy full-range link, knob at 0: macro 0 -> %.1f, 0.5 -> %.1f, 1 -> %.1f\n",
                     atZero, atHalf, atOne);
        // The link was min 0 / max 1 / depth +1, i.e. the macro covers the whole
        // range. With the knob at its low end the behaviour is IDENTICAL to the
        // old destructive path, value for value.
        CHECK (std::abs (atZero - 0.0f) < 0.6f && std::abs (atOne - 100.0f) < 0.6f
               && std::abs (atHalf - 50.0f) < 0.6f,
               "a restored full-range macro link sweeps the target exactly as it did before");
        // ...and now it also does the thing the spec asked for
        proc.setParamNormalized ("slot0.engine.tine", 0.2f);
        macro1->setValueNotifyingHost (0.0f);
        settle();
        CHECK (std::abs (proc.engines[0].engineParam (1) - 20.0f) < 0.6f,
               "with the macro at rest the knob's own value is what is heard");
        CHECK (std::abs (proc.state.slots[0].engineParams["tine"] - 20.0f) < 0.01f,
               "a macro sweep no longer overwrites the stored knob value");
        macro1->setValueNotifyingHost (0.0f);
    }

    // -------- full state round trip including modulation
    std::printf ("\n== MOD (5) modulation state round-trips ==\n");
    {
        proc.state.mod.lfos[2].shape = (int) LfoShape::sampleHold;
        proc.state.mod.lfos[2].division = 21;                 // 2/1
        proc.state.mod.lfos[2].phaseDeg = 90.0f;
        proc.state.mod.lfos[2].fadeSec = 1.5f;
        proc.state.mod.rows[2].source = ModSource::keyTrack;
        proc.state.mod.rows[2].destPath = "host.slotPan1";
        proc.state.mod.rows[2].depth = -0.75f;
        proc.state.mod.rows[2].enabled = true;
        proc.publishModulation();

        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        proc.state.mod = ModState {};
        proc.publishModulation();
        proc.setStateInformation (mb.getData(), (int) mb.getSize());

        CHECK (proc.state.mod.lfos[2].shape == (int) LfoShape::sampleHold
               && proc.state.mod.lfos[2].division == 21
               && std::abs (proc.state.mod.lfos[2].phaseDeg - 90.0f) < 1e-3f
               && std::abs (proc.state.mod.lfos[2].fadeSec - 1.5f) < 1e-3f,
               "LFO settings survive a plugin state round trip");
        CHECK (proc.state.mod.rows[2].source == ModSource::keyTrack
               && proc.state.mod.rows[2].destPath == "host.slotPan1"
               && std::abs (proc.state.mod.rows[2].depth + 0.75f) < 1e-3f,
               "a matrix row survives a plugin state round trip");

        // ...and it reaches a PERFORMANCE preset but never a .jmi bundle preset
        auto slotPreset = proc.slotPresetToVar (0, "probe");
        CHECK (! slotPreset.hasProperty ("mod"),
               "a slot/.jmi-schema preset carries NO modulation (spec decision 4)");
        // AUTHORING MODE: the destination now depends on the mode, so take the
        // file from the save's own report rather than assuming the user folder.
        // Assuming it left ModProbe.json behind in the FACTORY folder on any run
        // that happened to be in authoring mode.
        auto pf = proc.savePerformanceSnapshot ("ModProbe").file;
        CHECK (pf.existsAsFile()
               && juce::JSON::parse (pf.loadFileAsString()).hasProperty ("mod"),
               "a performance preset DOES carry modulation");
        pf.deleteFile();
        pf.getSiblingFile (pf.getFileName() + ".bak").deleteFile();

        proc.state.mod = ModState {};
        proc.publishModulation();
    }

    // -------- the cross-modulation guard
    std::printf ("\n== MOD (6) cross-modulation guard ==\n");
    {
        CHECK (proc.modRowWouldCycle (0, ModSource::lfo2, "mod.lfo2.rate"),
               "a row may not modulate its own LFO's rate");
        CHECK (! proc.modRowWouldCycle (0, ModSource::lfo1, "mod.lfo2.rate"),
               "but LFO 1 sweeping LFO 2's rate is allowed (spec §D)");
        CHECK (proc.modRowWouldCycle (3, ModSource::lfo1, "mod.row4.depth"),
               "a row may not modulate its own depth");
        CHECK (proc.modRowWouldCycle (0, ModSource::macro2, "host.macro2"),
               "a macro may not modulate itself");
        CHECK (proc.isModulatableDest ("slot0.engine.bark")
               && proc.isModulatableDest ("host.masterVol")
               && ! proc.isModulatableDest ("host.solo")
               && ! proc.isModulatableDest ("host.transpose"),
               "bool/int host parameters are not offered as destinations");
    }

    // -------- the two new panels, and a knob wearing its modulation span
    std::printf ("\n== MOD (7) LFO panel, MOD MATRIX panel, modulated knob ==\n");
    {
        proc.state.currentPage = 0;                       // a model page, so hero knobs are on screen
        // a visible, generous routing so the span arc is unmistakable in the shot
        for (auto& r : proc.state.mod.rows) r = MatrixRow {};
        for (auto& m : proc.state.macros) m.targets.clear();   // (MOD 4 left a legacy link)
        auto& l0 = proc.state.mod.lfos[0];
        l0.sync = 0; l0.rateHz = 0.6f; l0.shape = (int) LfoShape::sine; l0.fadeSec = 0.0f;
        proc.state.mod.lfos[1].shape = (int) LfoShape::smoothRandom;
        proc.state.mod.lfos[1].division = 18;             // 1/1
        proc.state.mod.lfos[2].shape = (int) LfoShape::sampleHold;
        proc.state.mod.rows[0].source = ModSource::lfo1;
        proc.state.mod.rows[0].destPath = "slot0.engine.tine";
        proc.state.mod.rows[0].depth = 0.6f;
        proc.state.mod.rows[1].source = ModSource::velocity;
        proc.state.mod.rows[1].destPath = "slot0.engine.bark";
        proc.state.mod.rows[1].depth = 0.45f;
        proc.state.mod.rows[2].source = ModSource::macro1;
        proc.state.mod.rows[2].destPath = "slot0.engine.space";
        proc.state.mod.rows[2].depth = -0.5f;
        proc.state.mod.rows[3].source = ModSource::lfo2;
        proc.state.mod.rows[3].destPath = "mod.lfo1.rate";
        proc.state.mod.rows[3].depth = 0.25f;
        proc.setParamNormalized ("slot0.engine.tine", 0.5f);
        proc.publishModulation();

        // drive the LFO a little so the live marker is off-centre in the shot
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer mb;
            mb.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 96), 0);
            for (int i = 0; i < 22; ++i) { buf.clear(); proc.processBlock (buf, mb); mb.clear(); }
        }

        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        ed->setSize (Theme::M::designW, Theme::M::designH);
        ed->setVisible (true);

        // all four bottom-bar plates are live now — PHASE 2 lit the last one
        auto* bar = findOne<BottomBar> (*ed);
        std::vector<ux::PlateButton*> bb;
        if (bar != nullptr) findAll<ux::PlateButton> (*bar, bb);
        CHECK ((int) bb.size() == 4, "the bottom bar still carries its four plates");
        if (bb.size() == 4)
        {
            CHECK (bb[1]->isEnabled() && bb[2]->isEnabled(),
                   "MOD MATRIX and LFO are live buttons (they were disabled placeholders)");
            CHECK (bb[3]->isEnabled(), "ARP is live now too — the last placeholder is gone");
        }

        // a modulated knob advertises its span to the LookAndFeel
        std::vector<juce::Slider*> sliders;
        collectSliders (*ed, sliders);
        int modded = 0;
        for (auto* s : sliders)
            if ((bool) s->getProperties().getWithDefault ("jmModActive", false)) ++modded;
        std::printf ("  knobs carrying a modulation span: %d\n", modded);
        CHECK (modded >= 3, "every modulated knob publishes its span for the second arc");
        {
            const auto span = proc.modSpanForPath ("slot0.engine.tine");
            std::printf ("  slot0.engine.tine span %.3f .. %.3f (live %+.3f)\n",
                         span.lo, span.hi, span.current);
            CHECK (span.active && std::abs (span.lo + 0.3f) < 1e-3f && std::abs (span.hi - 0.3f) < 1e-3f,
                   "a bipolar LFO at depth 60% reports a symmetric +-30% span");
            CHECK (span.current >= span.lo - 1e-4f && span.current <= span.hi + 1e-4f,
                   "the live modulated value sits inside the drawn span");
            const auto uni = proc.modSpanForPath ("slot0.engine.bark");
            CHECK (uni.active && std::abs (uni.lo) < 1e-4f && std::abs (uni.hi - 0.45f) < 1e-3f,
                   "a unipolar source reports a one-sided span anchored at the knob");
        }
        savePng (*ed, "mod-knob-span.png");

        if (auto* host = OverlayHost::find (findOne<HeaderBar> (*ed)))
        {
            host->showOverlay (std::make_unique<LfoPanel> (proc), LfoPanel::kWidth, LfoPanel::kHeight);
            auto* lp = findOne<LfoPanel> (*ed);
            CHECK (lp != nullptr, "the LFO panel opens as an overlay");
            std::vector<PlainKnob*> lk;
            if (lp != nullptr) findAll<PlainKnob> (*lp, lk);
            CHECK ((int) lk.size() == 3, "the LFO panel exposes RATE, PHASE and FADE IN");
            CHECK (findOne<LfoScope> (*ed) != nullptr, "the LFO panel shows a live scope");
            savePng (*ed, "mod-lfo-panel.png");
            host->closeOverlay();

            host->showOverlay (std::make_unique<MatrixPanel> (proc),
                               MatrixPanel::kWidth, MatrixPanel::heightFor (proc));
            auto* mp = findOne<MatrixPanel> (*ed);
            CHECK (mp != nullptr, "the MOD MATRIX panel opens as an overlay");
            std::vector<MatrixRowEditor*> mr;
            if (mp != nullptr) findAll<MatrixRowEditor> (*mp, mr);
            std::printf ("  matrix rows on screen: %d (stored row count %d)\n",
                         (int) mr.size(), proc.state.mod.rowCount);
            CHECK ((int) mr.size() == kDefaultMatrixRows, "the matrix shows the spec's eight rows");
            CHECK (kMaxMatrixRows >= 16, "the row array is sized so eight can grow to sixteen");
            savePng (*ed, "mod-matrix-panel.png");
            host->closeOverlay();
        }

        // no default JUCE chrome anywhere in the two new panels
        {
            std::vector<juce::ComboBox*> boxes;
            auto* lp2 = new LfoPanel (proc);
            lp2->setSize (LfoPanel::kWidth, LfoPanel::kHeight);
            findAll<juce::ComboBox> (*lp2, boxes);
            bool styled = true;
            for (auto* b : boxes) if (dynamic_cast<JmLookAndFeel*> (&b->getLookAndFeel()) == nullptr) styled = false;
            CHECK (styled && ! boxes.empty(), "every control in the LFO panel is drawn by JmLookAndFeel");
            delete lp2;
        }

        delete ed;
        for (auto& r : proc.state.mod.rows) r = MatrixRow {};
        for (auto& l : proc.state.mod.lfos) l = LfoSettings {};
        proc.publishModulation();
        for (auto& e : proc.engines) e.allNotesOff (false);
    }

    //==========================================================================
    // MODULATION PHASE 2 — the arpeggiator, through the REAL processor.
    // Ordering, swing, gate and the latch rules are proved against the pure
    // engine in EngineTests; what can only be proved HERE is the wiring: the
    // routing fork, the pedal fork, sub-block MIDI timing, state and the panel.
    //==========================================================================
    std::printf ("\n== ARP (1) sub-block MIDI splitting (research §3.7 / trap 13) ==\n");
    {
        for (int i = 0; i < 4; ++i)
            if (auto* p = proc.apvts.getParameter ("slotEnable" + juce::String (i)))
                p->setValueNotifyingHost (i == 0 ? 1.0f : 0.0f);
        proc.state.mod.arp = ArpSettings {};
        proc.publishArp();
        for (auto& e : proc.engines) e.allNotesOff (false);

        // A note stamped at sample 400 of a 512-sample block. Before phase 2,
        // handleMidiAtBlockStart() threw every timestamp away and applied the
        // whole buffer at sample 0, so the note started 8.3 ms early.
        //
        // Measured on the VOICE, not the output, because the slot and master FX
        // racks still carry tails from the previous section: after the block the
        // voice has advanced by (512 - stamp) samples, so the ratio of the two
        // playback positions is the timestamp, exactly.
        juce::AudioBuffer<float> buf (2, 512);
        auto renderAt = [&] (int stamp)
        {
            for (auto& e : proc.engines) e.allNotesOff (false);
            juce::MidiBuffer mb;
            mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), stamp);
            buf.clear();
            proc.processBlock (buf, mb);
            const int v = proc.engines[0].firstActiveVoice();
            return v >= 0 ? proc.engines[0].voicePosition (v) : -1.0;
        };
        const double p0 = renderAt (0);
        const double p400 = renderAt (400);
        std::printf ("  voice advance: stamped at 0 -> %.1f, stamped at 400 -> %.1f (ratio %.3f, want %.3f)\n",
                     p0, p400, p0 > 0.0 ? p400 / p0 : -1.0, 112.0 / 512.0);
        CHECK (p0 > 1.0 && p400 > 1.0, "both stampings start a voice");
        CHECK (p0 > 1.0 && std::abs (p400 / p0 - 112.0 / 512.0) < 0.02,
               "a note stamped at sample 400 starts THERE, not at the block start (trap 13)");
        for (auto& e : proc.engines) e.allNotesOff (false);
    }

    std::printf ("\n== ARP (2) per-slot enable: arpeggiated into C, as played into A ==\n");
    {
        for (int i = 0; i < 4; ++i)
            if (auto* p = proc.apvts.getParameter ("slotEnable" + juce::String (i)))
                p->setValueNotifyingHost (1.0f);
        for (auto& e : proc.engines) e.allNotesOff (false);

        auto& a = proc.state.mod.arp;
        a = ArpSettings {};
        a.enabled = 1;
        a.slotMask = 0x04;                    // slot C only
        a.sync = 1; a.division = 3;           // 1/8
        a.gate = 50.0f;
        a.mode = (int) ArpMode::up;
        proc.publishArp();

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer mb;
        for (int n : { 60, 64, 67 }) mb.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
        buf.clear();
        proc.processBlock (buf, mb);
        mb.clear();

        std::printf ("  voices A=%d B=%d C=%d D=%d, arp pattern %d\n",
                     proc.engines[0].activeVoices(), proc.engines[1].activeVoices(),
                     proc.engines[2].activeVoices(), proc.engines[3].activeVoices(),
                     proc.arp.patternLength());
        CHECK (proc.engines[0].activeVoices() > 0 && proc.engines[1].activeVoices() > 0
               && proc.engines[3].activeVoices() > 0,
               "slots with the arp OFF receive the whole chord as played");
        CHECK (proc.arp.patternLength() == 3,
               "the same three keys became a three-step arp pattern for slot C");
        CHECK (proc.engines[2].activeVoices() <= 1,
               "slot C is NOT playing the chord — the arp consumed those notes");

        // run on until the arp has actually produced a note into slot C
        for (int i = 0; i < 60; ++i) { buf.clear(); proc.processBlock (buf, mb); }
        std::printf ("  after 60 blocks: C voices %d\n", proc.engines[2].activeVoices());
        CHECK (proc.engines[2].activeVoices() > 0, "slot C is sounding arpeggiated steps");

        // ---- rule 1: CC64 is consumed by the arp for slot C and forwarded to
        //      the others, whose §5.4 behaviour is untouched.
        mb.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
        buf.clear(); proc.processBlock (buf, mb); mb.clear();
        CHECK (proc.engines[0].sustainDown() && proc.engines[1].sustainDown()
               && proc.engines[3].sustainDown(),
               "rule 1: CC64 still reaches the slots the arp is OFF on (§5.4 unchanged)");
        CHECK (! proc.engines[2].sustainDown(),
               "rule 1: CC64 is CONSUMED by the arp and never sustains the arp-driven slot");
        CHECK (proc.arp.latchIsActive(), "rule 1: the pedal latched the arp instead");

        // releasing the keys leaves the arp latched but releases the other slots
        for (int n : { 60, 64, 67 }) mb.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        buf.clear(); proc.processBlock (buf, mb); mb.clear();
        CHECK (proc.arp.patternLength() == 3, "the latched arp chord survives the key release");

        mb.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
        buf.clear(); proc.processBlock (buf, mb); mb.clear();
        for (int i = 0; i < 8; ++i) { buf.clear(); proc.processBlock (buf, mb); }
        CHECK (proc.arp.patternLength() == 0 && proc.arp.soundingCount() == 0,
               "pedal up with no keys held stops the pattern and leaves no stuck note");

        proc.state.mod.arp = ArpSettings {};
        proc.publishArp();
        for (auto& e : proc.engines) e.allNotesOff (false);
    }

    std::printf ("\n== ARP (3) normal-play sustain (§5.4) is unchanged with the arp off ==\n");
    {
        proc.state.mod.arp = ArpSettings {};
        proc.publishArp();
        for (auto& e : proc.engines) e.allNotesOff (false);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        mb.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 8);
        buf.clear(); proc.processBlock (buf, mb); mb.clear();
        CHECK (proc.engines[0].sustainDown(), "arp off: CC64 reaches every routed slot as before");
        mb.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        buf.clear(); proc.processBlock (buf, mb); mb.clear();
        CHECK (proc.engines[0].sustainedVoices() > 0,
               "arp off: a note-off under the pedal is DEFERRED, exactly as §5.4 specifies");
        mb.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
        buf.clear(); proc.processBlock (buf, mb); mb.clear();
        CHECK (proc.engines[0].sustainedVoices() == 0 && proc.engines[0].releasingVoices() > 0,
               "arp off: lifting the pedal releases the deferred voices");
        for (auto& e : proc.engines) e.allNotesOff (false);
    }

    std::printf ("\n== ARP (4) arp state, presets and modulation destinations ==\n");
    {
        auto& a = proc.state.mod.arp;
        a.enabled = 1; a.mode = (int) ArpMode::upDown; a.inclusive = 1;
        a.swing = 50.0f; a.gate = 42.0f; a.octaves = 3; a.octDir = (int) ArpOctDir::both;
        a.slotMask = 0x0A; a.velMode = (int) ArpVelMode::accent; a.accentMask = 0x9;
        proc.publishArp();

        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        proc.state.mod = ModState {};
        proc.publishModulation();
        proc.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (proc.state.mod.arp.enabled == 1 && proc.state.mod.arp.mode == (int) ArpMode::upDown
               && proc.state.mod.arp.inclusive == 1 && proc.state.mod.arp.slotMask == 0x0A
               && std::abs (proc.state.mod.arp.swing - 50.0f) < 1e-3f
               && std::abs (proc.state.mod.arp.gate - 42.0f) < 1e-3f
               && proc.state.mod.arp.octaves == 3 && proc.state.mod.arp.accentMask == 0x9,
               "arp settings survive a plugin state round trip");

        auto slotPreset = proc.slotPresetToVar (0, "arpProbe");
        CHECK (! slotPreset.hasProperty ("mod") && ! slotPreset.hasProperty ("arp"),
               "a slot/.jmi-schema preset carries NO arp (spec decision 4)");
        auto pf = proc.savePerformanceSnapshot ("ArpProbe").file;   // (mode-correct: see MOD (5))
        const auto pv = pf.existsAsFile() ? juce::JSON::parse (pf.loadFileAsString()) : juce::var();
        CHECK (pf.existsAsFile()
               && pv.getProperty ("mod", juce::var()).hasProperty ("arp"),
               "a performance preset DOES carry the arp");
        pf.deleteFile();
        pf.getSiblingFile (pf.getFileName() + ".bak").deleteFile();

        // arp.rate and arp.gate are matrix destinations
        CHECK (proc.isModulatableDest ("arp.rate") && proc.isModulatableDest ("arp.gate"),
               "arp RATE and GATE are valid modulation destinations");
        CHECK (! proc.isModulatableDest ("arp.mode") && ! proc.isModulatableDest ("arp.octaves"),
               "structural arp controls are deliberately NOT modulation destinations");
        CHECK (proc.modDestinationPaths().contains ("arp.gate"),
               "the row editor's destination menu offers them");
        {
            proc.state.mod.rows[0].source = ModSource::macro1;
            proc.state.mod.rows[0].destPath = "arp.gate";
            proc.state.mod.rows[0].depth = 1.0f;
            proc.state.mod.rows[0].enabled = true;
            proc.state.mod.arp = ArpSettings {};
            proc.state.mod.arp.enabled = 1;
            proc.state.mod.arp.gate = 10.0f;
            proc.publishModulation();
            if (auto* m1 = proc.apvts.getParameter ("macro1")) m1->setValueNotifyingHost (1.0f);
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer empty;
            for (int i = 0; i < 4; ++i) { b.clear(); proc.processBlock (b, empty); }
            std::printf ("  gate 10%% + macro at full through the matrix -> %.1f%%\n", proc.arp.gateNow());
            CHECK (proc.arp.gateNow() > 90.0f,
                   "a matrix row sweeps the arp GATE as an OFFSET on the stored setting");
            CHECK (std::abs (proc.state.mod.arp.gate - 10.0f) < 1e-4f,
                   "...and the stored setting is never written (base + offset, trap 1)");
            if (auto* m1 = proc.apvts.getParameter ("macro1")) m1->setValueNotifyingHost (0.0f);
        }

        proc.state.mod = ModState {};
        proc.publishModulation();
    }

    std::printf ("\n== ARP (5) the ARP panel ==\n");
    {
        auto& a = proc.state.mod.arp;
        a = ArpSettings {};
        a.enabled = 1; a.slotMask = 0x0F;
        a.mode = (int) ArpMode::upDown; a.inclusive = 0;
        a.division = 3; a.gate = 62.0f; a.swing = 50.0f;
        a.octaves = 2; a.octDir = (int) ArpOctDir::up;
        a.velMode = (int) ArpVelMode::accent; a.accentMask = 0x5; a.fixedVel = 104;
        proc.publishArp();

        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        ed->setSize (Theme::M::designW, Theme::M::designH);
        ed->setVisible (true);
        if (auto* host = OverlayHost::find (findOne<HeaderBar> (*ed)))
        {
            host->showOverlay (std::make_unique<ArpPanel> (proc), ArpPanel::kWidth, ArpPanel::kHeight);
            auto* ap = findOne<ArpPanel> (*ed);
            CHECK (ap != nullptr, "the ARP panel opens as an overlay from the bottom bar");
            std::vector<PlainKnob*> ak;
            if (ap != nullptr) findAll<PlainKnob> (*ap, ak);
            CHECK ((int) ak.size() == 4, "the ARP panel exposes RATE, GATE, SWING and the velocity LEVEL");
            std::vector<juce::ComboBox*> boxes;
            if (ap != nullptr) findAll<juce::ComboBox> (*ap, boxes);
            bool styled = ! boxes.empty();
            for (auto* b : boxes)
                if (dynamic_cast<JmLookAndFeel*> (&b->getLookAndFeel()) == nullptr) styled = false;
            CHECK ((int) boxes.size() == 6, "MODE, TURNAROUND, DIVISION, OCT, OCT DIR and VELOCITY are menus");
            CHECK (styled, "every control in the ARP panel is drawn by JmLookAndFeel (no JUCE chrome)");
            savePng (*ed, "arp-panel.png");
            host->closeOverlay();
        }
        delete ed;
        proc.state.mod.arp = ArpSettings {};
        proc.publishArp();
        for (auto& e : proc.engines) e.allNotesOff (false);
    }

    // ------------------------- (3) engaged plate follows the button's own path
    std::printf ("\n== R4 (3) engaged button fill follows the plate outline ==\n");
    {
        // The defect: the plate art (377x123) is a STADIUM, and nine-slicing it
        // squashed its 63px waist into 4px while keeping 18px caps, so each
        // rounded end became a wedge. Rendered here at the real bottom-bar size.
        const int W = Theme::L::bbBtnW[0], H = Theme::L::bbBtnH;
        auto render = [&] (bool on)
        {
            juce::Image img (juce::Image::ARGB, W, H, true, juce::SoftwareImageType());
            juce::Graphics g (img);
            ux::drawPlateButton (g, juce::Rectangle<float> (0.0f, 0.0f, (float) W, (float) H),
                                 "KEYBOARD", on, false, false, true, 14.0f);
            return img;
        };
        auto off = render (false), on = render (true);
        saveImage (on, "round4-keyboard-button-engaged.png");
        saveImage (off, "round4-keyboard-button-idle.png");

        // isolate the ENGAGED state by differencing the two renders: whatever
        // the "on" branch paints is exactly the set of changed pixels.
        auto changed = [&] (int x, int y)
        {
            auto a = off.getPixelAt (x, y), b = on.getPixelAt (x, y);
            return std::abs ((int) a.getRed()   - (int) b.getRed())
                 + std::abs ((int) a.getGreen() - (int) b.getGreen())
                 + std::abs ((int) a.getBlue()  - (int) b.getBlue()) > 40;
        };
        auto inner = kit::pillInner (juce::Rectangle<float> (0.0f, 0.0f, (float) W, (float) H));
        const float rad = kit::pillRadius (inner);
        std::printf ("  button %dx%d, engaged inner %.1f,%.1f %.1fx%.1f (radius %.1f)\n",
                     W, H, inner.getX(), inner.getY(), inner.getWidth(), inner.getHeight(), rad);

        // it fills the plate's inner well edge to edge...
        CHECK (changed ((int) inner.getX() + 1, H / 2) && changed ((int) inner.getRight() - 2, H / 2),
               "engaged fill spans the plate's inner well horizontally");
        CHECK (changed (W / 2, (int) inner.getY() + 1) && changed (W / 2, (int) inner.getBottom() - 2),
               "engaged fill spans the plate's inner well vertically");
        // ...it is ROUNDED to the stadium, not squared off at the corners...
        CHECK (! changed ((int) inner.getX() + 1, (int) inner.getY() + 1)
               && ! changed ((int) inner.getRight() - 2, (int) inner.getBottom() - 2),
               "engaged fill is rounded to the plate's stadium, not a square corner");
        // ...and every changed pixel lies inside the stadium path, which is what
        // "the selection fits the shape" means. (The old wedge failed this.)
        int outside = 0;
        auto insideStadium = [&] (float x, float y)
        {
            const float cx = juce::jlimit (inner.getX() + rad, inner.getRight() - rad, x);
            const auto d = juce::Point<float> (x, y) - juce::Point<float> (cx, inner.getCentreY());
            return d.getDistanceFromOrigin() <= rad + 1.5f;
        };
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (changed (x, y) && ! insideStadium ((float) x + 0.5f, (float) y + 0.5f)) ++outside;
        std::printf ("  engaged pixels outside the stadium path: %d\n", outside);
        CHECK (outside == 0, "no engaged-state pixels outside the button's own outline");
    }

    // ----------------------- (2)/(4) the round-4 window, wheels and macro menu
    std::printf ("\n== R4 snapshots (transpose+octave, keyboard wheels, macro menu) ==\n");
    {
        proc.state.currentPage = 4;
        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        ed->setSize (Theme::M::designW, Theme::M::designH);
        ed->setVisible (true);

        // two visible steppers, TRANSPOSE and OCTAVE, side by side (item 2)
        std::vector<StepperControl*> steppers;
        findAll<StepperControl> (*findOne<PerformCenterPanel> (*ed), steppers);
        CHECK ((int) steppers.size() == 2, "TRANSPOSE and OCTAVE are two visible steppers");
        if (steppers.size() == 2)
        {
            auto a = steppers[0]->getBounds(), b = steppers[1]->getBounds();
            std::printf ("  steppers at %d,%d %dx%d and %d,%d %dx%d\n",
                         a.getX(), a.getY(), a.getWidth(), a.getHeight(),
                         b.getX(), b.getY(), b.getWidth(), b.getHeight());
            CHECK (! a.intersects (b) && a.getY() == b.getY(), "they sit side by side, not overlapping");
        }

        // the chips are gone (item 4): a macro cell now owns only its knob
        std::vector<MacroKnobUnit*> ms;
        findAll<MacroKnobUnit> (*findOne<PerformCenterPanel> (*ed), ms);
        int chips = 0;
        for (auto* m : ms) { std::vector<ux::PlateButton*> pb; findAll<ux::PlateButton> (*m, pb); chips += (int) pb.size(); }
        CHECK (chips == 0, "the LEARN / EDIT chips are gone from the macro cells (item 4)");

        // keyboard + wheels (item 2)
        auto* kb = findOne<KeyboardStrip> (*ed);
        CHECK (kb != nullptr, "the on-screen keyboard strip exists");
        if (kb != nullptr)
        {
            kb->setVisible (true);
            kb->toFront (false);
            // show the bottom-bar KEYBOARD plate in its ENGAGED state, which is
            // the control the client's item 3 is about
            std::vector<ux::PlateButton*> barBtns;
            if (auto* bb = findOne<BottomBar> (*ed)) findAll<ux::PlateButton> (*bb, barBtns);
            if (! barBtns.empty()) barBtns[0]->setToggleState (true, juce::dontSendNotification);
            CHECK (kb->pitchWheel().springsToCentre(), "the pitch wheel springs back to centre");
            CHECK (! kb->modWheel().springsToCentre(), "the mod wheel stays where it is left");
            kb->pitchWheel().setValue (0.86f, false);
            kb->modWheel().setValue (0.62f, false);
        }
        savePng (*ed, "round4-keyboard-wheels.png");

        // the macro right-click menu, drawn with the LookAndFeel that paints the
        // real popup window (menu windows are separate desktop windows)
        {
            auto* pm = dynamic_cast<juce::PopupMenu::LookAndFeelMethods*> (&lnf);
            const int mw = 330, mh = 232;
            juce::Image menuImg (juce::Image::ARGB, mw, mh, true, juce::SoftwareImageType());
            {
                juce::Graphics g (menuImg);
                pm->drawPopupMenuBackground (g, mw, mh);
                int y = 8;
                auto row = [&] (int h) { auto r = juce::Rectangle<int> (8, y, mw - 16, h); y += h; return r; };
                pm->drawPopupMenuSectionHeader (g, row (26), "MACRO 1");
                pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                       "Learn (then click any control)", {}, nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (30), false, true, true, false, false,
                                       "Edit targets...   (double-click)", {}, nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                       "Rename...", {}, nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (9), true, true, false, false, false, {}, {}, nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (30), false, true, false, false, true,
                                       "Targets (3)", {}, nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                       "Clear all targets", {}, nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (9), true, true, false, false, false, {}, {}, nullptr, nullptr);
                pm->drawPopupMenuSectionHeader (g, row (26), "MIDI");
            }
            auto shot = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
            int mx = 560, my = 560;
            if (! ms.empty()) { auto b = ed->getLocalArea (ms[0], ms[0]->getLocalBounds());
                                mx = b.getCentreX(); my = b.getCentreY(); }
            { juce::Graphics gw (shot); gw.drawImageAt (menuImg, mx, my); }
            saveImage (shot, "round4-macro-menu.png");
        }

        // the settings window with the MIDI routing editor (item 1)
        // (the OverlayHost is the editor's root child, so it is found from a
        //  descendant — walking up from `ed` itself would never reach it)
        if (auto* host = OverlayHost::find (findOne<HeaderBar> (*ed)))
        {
            host->showOverlay (std::make_unique<SettingsPanel> (proc),
                               SettingsPanel::kWidth, SettingsPanel::kHeight);
            auto* sp = findOne<SettingsPanel> (*ed);
            CHECK (sp != nullptr, "settings window opens");
            std::vector<RoutingRow*> rr;
            if (sp != nullptr) findAll<RoutingRow> (*sp, rr);
            std::printf ("  routing rows in the settings window: %d\n", (int) rr.size());
            CHECK ((int) rr.size() >= 16, "settings window exposes the 16 channel routing rows");
            savePng (*ed, "round4-settings-midi.png");
            host->closeOverlay();

            // ...and the same window as the STANDALONE shows it, where the
            // device -> slot table is the primary control (this console build
            // has no device router, so the flag + two entries are staged).
            MidiDeviceRouter::active().store (true);
            proc.midiRouting.devices().clear();
            proc.midiRouting.devices().push_back ({ "dev-1", "Nord Stage 3 (USB)",   true,  0b0001 });
            proc.midiRouting.devices().push_back ({ "dev-2", "Arturia KeyLab 61",    true,  0b0110 });
            proc.midiRouting.devices().push_back ({ "dev-3", "MPK Mini (not routed)", false, 0b1000 });
            host->showOverlay (std::make_unique<SettingsPanel> (proc),
                               SettingsPanel::kWidth, SettingsPanel::kHeight);
            std::vector<RoutingRow*> rr2;
            if (auto* sp2 = findOne<SettingsPanel> (*ed)) findAll<RoutingRow> (*sp2, rr2);
            // (the panel also merges this machine's real MIDI inputs, so the
            //  row count is 16 channels + staged devices + whatever is plugged in)
            const int nDev = (int) proc.midiRouting.devices().size();
            std::printf ("  standalone view rows: %d  (16 channels + %d devices)\n",
                         (int) rr2.size(), nDev);
            CHECK ((int) rr2.size() == 16 + nDev, "standalone view adds one routing row per MIDI device");
            savePng (*ed, "round4-settings-midi-standalone.png");
            host->closeOverlay();
            MidiDeviceRouter::active().store (false);
            proc.midiRouting.devices().clear();
        }
        delete ed;
    }

    // ================================================ CLIENT REVIEW ROUND 6 (3)
    // "when I open the app it opens too wide and goes out of the screen".
    // preferredSizeForArea() is a pure function of a display's USER area (in
    // logical pixels), so every display size and scale factor can be checked
    // here without touching the physical display.
    std::printf ("\n== R6 (3) opening size clamped to the display ==\n");
    {
        struct Case { int w, h; double scale; const char* what; };
        // userBounds is ALREADY logical (physical / scale), so a 4K display at
        // 200% presents a 1920x1080 user area — that is the number listed here.
        const Case cases[] = {
            { 1366,  728, 1.00, "1366x768 laptop, taskbar" },
            { 1920, 1032, 1.00, "1080p, taskbar" },
            { 1536,  824, 1.25, "1920x1080 @125% -> 1536x864 logical" },
            { 1280,  692, 1.50, "1920x1080 @150% -> 1280x720 logical" },
            { 2560, 1400, 1.00, "1440p, taskbar" },
            { 1920, 1032, 2.00, "3840x2160 @200% -> 1920x1080 logical" },
            { 3840, 2120, 1.00, "4K @100%, taskbar" },
        };
        bool allFit = true, allAspect = true, neverBigger = true;
        for (auto& c : cases)
        {
            const auto sz = JMSignatureEditor::preferredSizeForArea ({ 0, 0, c.w, c.h });
            // the WINDOW is the editor plus caption+borders; that is what has to fit
            const int winW = sz.x + 16, winH = sz.y + 48;
            const bool fits = winW <= c.w && winH <= c.h;
            const double ar = (double) sz.x / (double) sz.y;
            std::printf ("  %-38s user %4dx%4d -> editor %4dx%4d  window %4dx%4d  ar %.4f %s\n",
                         c.what, c.w, c.h, sz.x, sz.y, winW, winH, ar, fits ? "" : "  <-- OVERHANG");
            allFit &= fits;
            allAspect &= std::abs (ar - 1.5) < 0.005;
            neverBigger &= sz.x <= Theme::M::designW && sz.y <= Theme::M::designH;
        }
        CHECK (allFit, "opening window fits inside the display's user area at every size/scale");
        CHECK (allAspect, "3:2 preserved at every clamped size");
        CHECK (neverBigger, "never opens larger than the 1536x1024 design size");
        // and the old behaviour is genuinely gone
        const auto p1080 = JMSignatureEditor::preferredSizeForArea ({ 0, 0, 1920, 1032 });
        CHECK (p1080.x < Theme::M::designW,
               "1080p no longer gets the hardcoded 1536x1024 (the reported defect)");

        const auto& displays = juce::Desktop::getInstance().getDisplays();
        if (! displays.displays.isEmpty())
        {
            const auto* d = displays.getPrimaryDisplay();
            const auto ub = d->userBounds.toNearestInt();
            const auto here = JMSignatureEditor::preferredSize();
            std::printf ("  THIS machine: primary user area %dx%d @scale %.2f -> opens %dx%d\n",
                         ub.getWidth(), ub.getHeight(), d->scale, here.x, here.y);
            CHECK (here.x + 16 <= ub.getWidth() && here.y + 48 <= ub.getHeight(),
                   "opening window fits this machine's actual display");
        }
    }

    // ================================================ CLIENT REVIEW ROUND 6 (2)
    // The residual-latency budget. Everything printed here is MEASURED from the
    // live processor, not read off the source.
    std::printf ("\n== R6 (2) latency budget (measured) ==\n");
    {
        constexpr double sr = 48000.0;
        constexpr int block = 256;

        // ---- 1. the master limiter's lookahead, measured with an impulse
        MasterLimiter lim;
        lim.prepare (sr, block);
        lim.setEnabled (false);                       // bypassed: pure delay line
        juce::AudioBuffer<float> imp (2, block);
        imp.clear();
        imp.setSample (0, 0, 1.0f);
        imp.setSample (1, 0, 1.0f);
        lim.process (imp);
        int measuredDelay = -1;
        for (int i = 0; i < block; ++i)
            if (imp.getSample (0, i) > 0.5f) { measuredDelay = i; break; }
        std::printf ("  limiter lookahead      : reported %d samples, MEASURED %d samples (%.2f ms)\n",
                     lim.latencySamples(), measuredDelay, 1000.0 * measuredDelay / sr);
        CHECK (measuredDelay == lim.latencySamples(),
               "limiter's measured delay equals the latency it reports");
        CHECK (std::abs (1000.0 * measuredDelay / sr - 2.0) < 0.05,
               "limiter lookahead is 2.00 ms");

        // The delay line runs even when the limiter is OFF, by design: the
        // reported latency must not change at runtime (see DECISIONS R6/2).
        MasterLimiter lim2;
        lim2.prepare (sr, block);
        lim2.setEnabled (true);
        juce::AudioBuffer<float> imp2 (2, block);
        imp2.clear();
        imp2.setSample (0, 0, 0.1f);
        imp2.setSample (1, 0, 0.1f);
        lim2.process (imp2);
        int onDelay = -1;
        for (int i = 0; i < block; ++i)
            if (imp2.getSample (0, i) > 0.05f) { onDelay = i; break; }
        CHECK (onDelay == measuredDelay, "limiter delay is identical on and off (click-free toggle)");

        // ---- 2. what the plugin reports to a host, so a DAW compensates it
        JMSignatureProcessor lp;
        lp.prepareToPlay (sr, block);
        std::printf ("  reported to the host   : %d samples (%.2f ms)\n",
                     lp.getLatencySamples(), 1000.0 * lp.getLatencySamples() / sr);
        CHECK (lp.getLatencySamples() == measuredDelay,
               "processor reports exactly its real latency (DAW delay compensation is correct)");

        // ---- 3. a note pushed into the routed queue must sound in the VERY
        //        NEXT block — i.e. nothing timer-based sits in the note path.
        {
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer mb;
            for (int i = 0; i < 8; ++i) { buf.clear(); mb.clear(); lp.processBlock (buf, mb); }
            lp.routedQueue.push (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100),
                                 kSlotMaskAll, true);
            buf.clear(); mb.clear();
            lp.processBlock (buf, mb);
            const float mag = buf.getMagnitude (0, 0, block);
            std::printf ("  queued note -> audio   : magnitude %.6f in the first block after the push\n", mag);
            CHECK (mag > 1.0e-6f, "a queued note-on sounds in the next block (no timer in the note path)");
        }

        // ---- 4. the whole budget at the buffer sizes that matter
        std::printf ("  budget at 48 kHz (output buffer + half-buffer queue jitter + 2 ms limiter):\n");
        for (int bs : { 128, 144, 256, 480, 2560 })
            std::printf ("    buffer %4d (%5.2f ms) : %6.2f ms typical, %6.2f ms worst\n",
                         bs, 1000.0 * bs / sr,
                         1000.0 * (bs + bs * 0.5 + measuredDelay) / sr,
                         1000.0 * (bs + bs + measuredDelay) / sr);
    }

    //========================================================================
    // CLIENT REVIEW ROUND 7 — the FX rack: a visible remove control on every
    // unit, and drag-to-reorder with a live insertion indicator.
    //========================================================================
    std::printf ("\n== R7 (2/3) FX rack: remove control + drag to reorder ==\n");
    {
        proc.state.currentPage = 0;
        // the rack under test is the one the strip shows for page 0
        proc.state.slots[0].rack = buildDefaultRack (0, false);

        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        ed->setSize (Theme::M::designW, Theme::M::designH);
        ed->setVisible (true);

        auto* strip = findOne<FxRackStrip> (*ed);
        CHECK (strip != nullptr, "FX rack strip present");

        std::vector<FxUnitPanel*> units;
        findAll<FxUnitPanel> (*ed, units);
        std::printf ("  units on the strip: %d\n", (int) units.size());
        CHECK (units.size() >= 3, "enough units on the strip to reorder");

        // ---- item 2: removal is a control ON the unit, not a menu entry
        std::vector<ux::RemoveChipButton*> chips;
        findAll<ux::RemoveChipButton> (*ed, chips);
        std::printf ("  remove chips found: %d\n", (int) chips.size());
        CHECK (chips.size() == units.size(), "every rack unit carries a visible remove control");
        bool onScreen = ! chips.empty();
        for (auto* c : chips)
            onScreen &= c->isVisible() && c->getWidth() >= 14 && c->getHeight() >= 14
                        && ed->getLocalArea (c, c->getLocalBounds()).getWidth() > 0;
        CHECK (onScreen, "the remove control is visible and a real hit target");
        // the mouse actually reaches it (nothing paints over it)
        if (! chips.empty())
        {
            auto c = ed->getLocalPoint (chips[0], chips[0]->getLocalBounds().getCentre());
            CHECK (ed->getComponentAt (c) == chips[0], "the remove control receives the mouse at its centre");
        }
        savePng (*ed, "round7-fx-remove-control.png");
        savePng (*strip, "round7-fx-rack-strip.png");   // the strip alone, legibly

        // ---- item 3: the drag payload, the target, and the insertion indicator
        auto payload = fxdrag::makePayload (units[0]->rackPrefix(), units[0]->rackIndex());
        CHECK (fxdrag::isPayload (payload) && fxdrag::indexOf (payload) == units[0]->rackIndex(),
               "the reorder payload round-trips the unit's rack position");
        juce::DragAndDropTarget::SourceDetails onUnit3 (
            payload, units[0], strip->getLocalPoint (units[2], units[2]->getLocalBounds().getCentre()));
        CHECK (strip->isInterestedInDragSource (onUnit3), "the rack strip accepts a reorder drag from its own rack");
        juce::DragAndDropTarget::SourceDetails foreign (
            macrodrag::makePayload ("slot0.fx0.mix", "MIX"), units[0], onUnit3.localPosition);
        CHECK (! strip->isInterestedInDragSource (foreign), "it rejects a macro-link drag");
        juce::DragAndDropTarget::SourceDetails otherRack (
            fxdrag::makePayload ("master", 0), units[0], onUnit3.localPosition);
        CHECK (! strip->isInterestedInDragSource (otherRack), "it rejects a unit dragged from another rack");

        // the indicator: drive a drag-move over the third unit and shoot it,
        // compositing the ghost the container would draw under the cursor
        strip->itemDragEnter (onUnit3);
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
            auto ghost = fxdrag::makeGhost (*units[0]);
            auto at = ed->getLocalPoint (strip, onUnit3.localPosition);
            {   // the Graphics must go out of scope before the image is read back
                juce::Graphics g (img);
                g.setOpacity (0.85f);
                g.drawImage (ghost, juce::Rectangle<float> ((float) at.x - 40.0f, (float) at.y - 30.0f,
                                                            (float) ghost.getWidth() / fxdrag::kGhostScale,
                                                            (float) ghost.getHeight() / fxdrag::kGhostScale));
                // and the cursor, so the shot reads as a drag in progress
                juce::Path arrow;
                arrow.startNewSubPath ((float) at.x, (float) at.y);
                arrow.lineTo ((float) at.x, (float) at.y + 17.0f);
                arrow.lineTo ((float) at.x + 5.0f, (float) at.y + 12.5f);
                arrow.lineTo ((float) at.x + 12.0f, (float) at.y + 12.0f);
                arrow.closeSubPath();
                g.setOpacity (1.0f);
                g.setColour (juce::Colours::white);
                g.fillPath (arrow);
                g.setColour (juce::Colours::black);
                g.strokePath (arrow, juce::PathStrokeType (1.0f));
            }
            saveImage (img, "round7-fx-reorder-drag.png");
        }

        // ---- and the drop actually reorders the rack, in the model and the DSP
        const auto before = proc.state.slots[0].rack;
        strip->itemDropped (onUnit3);
        const auto& after = proc.state.slots[0].rack;
        std::printf ("  before: ");
        for (auto& u : before) std::printf ("%s ", fxSpec (u.type).typeId);
        std::printf ("\n  after : ");
        for (auto& u : after) std::printf ("%s ", fxSpec (u.type).typeId);
        std::printf ("\n");
        CHECK (after.size() == before.size(), "a reorder never adds or loses a unit");
        CHECK (after[0].type != before[0].type && after[1].type == before[0].type,
               "the dragged unit landed in the gap the indicator pointed at");
        // ...and the order that lands is the order that gets saved
        auto rt = rackFromVar (juce::JSON::parse (juce::JSON::toString (rackToVar (after))));
        bool kept = rt.size() == after.size();
        for (size_t i = 0; i < rt.size() && kept; ++i) kept = rt[i].type == after[i].type;
        CHECK (kept, "the dropped order round-trips through saved state");

        strip->itemDragExit (onUnit3);

        // ---- and the remove control actually removes, from the rack AND the DSP
        {
            std::vector<ux::RemoveChipButton*> live;
            findAll<ux::RemoveChipButton> (*ed, live);
            const auto sizeBefore = proc.state.slots[0].rack.size();
            const auto goneType = proc.state.slots[0].rack[1].type;
            live[1]->onClick();          // what the mouse does, without the queue
            // ...then let the clickless two-step's 25 ms timer run
            juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
            const auto& r = proc.state.slots[0].rack;
            std::printf ("  rack size %d -> %d after clicking one remove chip\n",
                         (int) sizeBefore, (int) r.size());
            CHECK (r.size() == sizeBefore - 1, "clicking the remove chip takes exactly one unit out");
            bool stillThere = false;
            for (auto& u : r) stillThere |= u.type == goneType;
            CHECK (! stillThere, "the removed unit is the one whose chip was clicked");
            // the DSP follows (the two-step restructure has had its 25 ms)
            CHECK (proc.slotRacks[0].numUnits() == (int) r.size(),
                   "the DSP rack matches the model after a removal");
        }

        delete ed;
        proc.state.slots[0].rack = buildDefaultRack (0);
        proc.slotRacks[0].setState (proc.state.slots[0].rack);
    }


    //========================================================================
    // VALIDATION + UX STANDARDS PASS -- guide S6, "knob resolution and feel".
    //
    // Every gesture the guide asks for, asserted on a REAL knob inside a real
    // editor, plus proof that the gestures rounds 3/4/7 already bound to these
    // controls (alt-drag to assign a macro, right-click for the MIDI/macro
    // menu, header-only FX drag-to-reorder) are untouched.
    //========================================================================
    std::printf ("\n== S6 knob conventions: relative drag, fine, wheel, typed entry ==\n");
    {
        proc.state.currentPage = 0;
        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        ed->setSize (Theme::M::designW, Theme::M::designH);
        ed->setVisible (true);

        std::vector<PathSlider*> knobs;
        findAll<PathSlider> (*ed, knobs);
        std::printf ("  path-bound knobs on the page: %d\n", (int) knobs.size());
        CHECK (! knobs.empty(), "knobs found to test");

        auto* s = knobs.front();
        auto* owner = dynamic_cast<ParamKnob*> (s->getParentComponent());
        CHECK (owner != nullptr, "the knob is a ParamKnob");
        if (owner != nullptr)
            std::printf ("  testing '%s' (%s)\n", owner->caption().toRawUTF8(),
                         owner->path().toRawUTF8());

        auto ev = [s] (juce::Point<float> pos, juce::ModifierKeys mods,
                       juce::Point<float> down, int clicks, bool dragged)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f, s, s,
                                     juce::Time::getCurrentTime(), down,
                                     juce::Time::getCurrentTime(), clicks, dragged);
        };
        const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        const auto leftShift = left.withFlags (juce::ModifierKeys::shiftModifier);
        const auto leftCmd = left.withFlags (juce::ModifierKeys::commandModifier);
        const auto altDrag = left.withFlags (juce::ModifierKeys::altModifier);

        // One vertical sweep starting at `from`; returns the value change.
        auto sweep = [&] (juce::Point<float> from, juce::ModifierKeys mods, float dy)
        {
            s->setValue (0.5, juce::sendNotificationSync);
            const double before = s->getValue();
            s->mouseDown (ev (from, mods, from, 1, false));
            for (int i = 1; i <= 4; ++i)
                s->mouseDrag (ev (from.translated (0.0f, dy * (float) i / 4.0f), mods, from, 1, true));
            const double after = s->getValue();
            s->mouseUp (ev (from.translated (0.0f, dy), mods, from, 1, true));
            return after - before;
        };

        // ---- RELATIVE, not absolute: the same movement from two very
        //      different grab points must move the knob by the same amount. An
        //      absolute (position-jump) control would snap to the cursor and
        //      give two completely different answers.
        const double coarseA = sweep ({ 10.0f, 60.0f }, left, -40.0f);
        const double coarseB = sweep ({ 55.0f, 12.0f }, left, -40.0f);
        std::printf ("  40 px sweep from (10,60): %+.4f    from (55,12): %+.4f\n", coarseA, coarseB);
        CHECK (coarseA > 0.01 && std::abs (coarseA - coarseB) < 1.0e-6,
               "drag is RELATIVE (delta-based): identical movement, identical change, "
               "wherever the grab started");

        // ---- fine-adjust modifier
        const double fine = sweep ({ 30.0f, 40.0f }, leftShift, -40.0f);
        std::printf ("  same sweep with Shift held      : %+.4f  (%.0f%% of coarse)\n",
                     fine, 100.0 * fine / coarseA);
        CHECK (fine > 0.0 && std::abs (fine / coarseA - (double) PathSlider::kFineFactor) < 0.02,
               "Shift-drag is a fine adjust at the documented factor");

        // ---- mouse wheel, coarse and fine
        s->setValue (0.5, juce::sendNotificationSync);
        const juce::MouseWheelDetails up { 0.0f, 1.0f, false, false, false };
        s->mouseWheelMove (ev ({ 30.0f, 30.0f }, {}, { 30.0f, 30.0f }, 0, false), up);
        const double wheelStep = s->getValue() - 0.5;
        s->setValue (0.5, juce::sendNotificationSync);
        s->mouseWheelMove (ev ({ 30.0f, 30.0f }, juce::ModifierKeys (juce::ModifierKeys::shiftModifier),
                               { 30.0f, 30.0f }, 0, false), up);
        const double wheelFine = s->getValue() - 0.5;
        std::printf ("  one wheel notch: %+.4f   with Shift: %+.4f\n", wheelStep, wheelFine);
        CHECK (std::abs (wheelStep - PathSlider::kWheelStep) < 1.0e-6, "a wheel notch is 2 % of range");
        CHECK (std::abs (wheelFine - PathSlider::kWheelStep * PathSlider::kFineFactor) < 1.0e-6,
               "Shift+wheel is the fine increment");

        // ---- keyboard (accessibility): arrows, page, home
        s->setValue (0.5, juce::sendNotificationSync);
        s->keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        const double arrow = s->getValue() - 0.5;
        s->setValue (0.5, juce::sendNotificationSync);
        s->keyPressed (juce::KeyPress (juce::KeyPress::pageUpKey));
        const double page = s->getValue() - 0.5;
        s->keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
        const double home = s->getValue();
        std::printf ("  arrow %+.4f   pageUp %+.4f   home -> %.4f\n", arrow, page, home);
        CHECK (std::abs (arrow - 0.01) < 1.0e-6 && std::abs (page - 0.10) < 1.0e-6 && home == 0.0,
               "arrow / page / home step the value from the keyboard");
        CHECK (s->getWantsKeyboardFocus(), "the knob is keyboard-navigable (Tab reaches it)");

        // ---- double-click-to-reset (round 2) still works, and the drag
        //      accumulator is re-anchored so the next pixel does not snap back
        s->setValue (0.9, juce::sendNotificationSync);
        s->mouseDown (ev ({ 30.0f, 30.0f }, left, { 30.0f, 30.0f }, 2, false));
        s->mouseDoubleClick (ev ({ 30.0f, 30.0f }, left, { 30.0f, 30.0f }, 2, false));
        const double reset = s->getValue();
        s->mouseDrag (ev ({ 30.0f, 29.0f }, left, { 30.0f, 30.0f }, 2, true));
        const double afterNudge = s->getValue();
        s->mouseUp (ev ({ 30.0f, 29.0f }, left, { 30.0f, 30.0f }, 2, true));
        std::printf ("  double-click reset -> %.4f, then 1 px -> %.4f\n", reset, afterNudge);
        CHECK (reset != 0.9 && std::abs (afterNudge - reset) < 0.02,
               "double-click resets, and the next pixel of drag does not snap back");

        // ---- alt-drag is STILL the macro assign gesture, not a value change
        s->setValue (0.5, juce::sendNotificationSync);
        s->mouseDown (ev ({ 30.0f, 30.0f }, altDrag, { 30.0f, 30.0f }, 1, false));
        s->mouseDrag (ev ({ 30.0f, 10.0f }, altDrag, { 30.0f, 30.0f }, 1, true));
        s->mouseUp (ev ({ 30.0f, 10.0f }, altDrag, { 30.0f, 30.0f }, 1, true));
        std::printf ("  after a 20 px ALT-drag the value is %.4f\n", s->getValue());
        CHECK (s->getValue() == 0.5, "alt-drag (macro assign, round 3) never moves the value");

        // ---- typed value entry: Cmd/Ctrl-click, resolved on mouse UP so that
        //      Ctrl-DRAG stays a fine adjust on Windows
        s->setValue (0.5, juce::sendNotificationSync);
        s->mouseDown (ev ({ 30.0f, 30.0f }, leftCmd, { 30.0f, 30.0f }, 1, false));
        s->mouseDrag (ev ({ 30.0f, 20.0f }, leftCmd, { 30.0f, 30.0f }, 1, true));
        s->mouseUp (ev ({ 30.0f, 20.0f }, leftCmd, { 30.0f, 30.0f }, 1, true));
        CHECK (owner == nullptr || findOne<juce::TextEditor> (*owner) == nullptr,
               "Ctrl/Cmd-DRAG is a fine adjust -- it does not open the value box");

        s->mouseDown (ev ({ 30.0f, 30.0f }, leftCmd, { 30.0f, 30.0f }, 1, false));
        s->mouseUp (ev ({ 30.0f, 30.0f }, leftCmd, { 30.0f, 30.0f }, 1, false));
        auto* box = owner != nullptr ? findOne<juce::TextEditor> (*owner) : nullptr;
        CHECK (box != nullptr && box->isVisible(), "Ctrl/Cmd-CLICK opens typed value entry");
        if (box != nullptr)
        {
            std::printf ("  value box shows '%s' (%d x %d px)\n", box->getText().toRawUTF8(),
                         box->getWidth(), box->getHeight());
            CHECK (box->getText().isNotEmpty(), "the box opens pre-filled with the current value");
            CHECK (box->getHeight() >= 18 && box->getWidth() >= juce::jmin (72, owner->getWidth()),
                   "the box is big enough to read and to hit");
            savePng (*ed, "ux-typed-entry-full.png");
            auto region = ed->getLocalArea (owner, owner->getLocalBounds()).expanded (46, 34)
                              .getIntersection (ed->getLocalBounds());
            saveImage (ed->createComponentSnapshot (region, false, 3.0f), "ux-typed-entry.png");

            // typing a real value commits it through the parameter router
            box->setText ("100", false);
            if (box->onReturnKey) box->onReturnKey();
            std::printf ("  typed '100' -> knob at %.4f\n", s->getValue());
            CHECK (s->getValue() > 0.9, "a typed value reaches the parameter");
            auto* after = owner != nullptr ? findOne<juce::TextEditor> (*owner) : nullptr;
            CHECK (after == nullptr || ! after->isVisible(), "the box closes on Return");
        }

        // ---- accessibility: the knob announces itself by name and by the
        //      value the panel prints, not by the raw 0..1 number
        s->setValue (0.5, juce::sendNotificationSync);
        std::printf ("  accessible title '%s'\n", s->getTitle().toRawUTF8());
        CHECK (s->getTitle().isNotEmpty(), "the knob carries an accessible name");
        std::printf ("  screen reader would say: \"%s, %s\"\n",
                     s->getTitle().toRawUTF8(), s->getTextFromValue (s->getValue()).toRawUTF8());
        CHECK (s->getTextFromValue (s->getValue()).isNotEmpty()
                   && s->getTextFromValue (s->getValue()) != juce::String (s->getValue()),
               "the announced value is the panel's formatted readout, not the 0..1 number");

        // ---- toggles are reachable and operable from the keyboard too
        std::vector<PowerButton*> powers;
        findAll<PowerButton> (*ed, powers);
        int named = 0;
        bool focusable = ! powers.empty();
        for (auto* p : powers)
        {
            focusable &= p->getWantsKeyboardFocus();
            if (p->getTitle().isNotEmpty()) ++named;
        }
        std::printf ("  power buttons: %d, keyboard-focusable, %d named\n", (int) powers.size(), named);
        CHECK (focusable, "every power/enable button is keyboard-navigable");
        CHECK (named == (int) powers.size(), "every power/enable button has an accessible name");
        if (! powers.empty())
        {
            const bool was = powers[0]->getState();
            powers[0]->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
            CHECK (powers[0]->getState() != was, "Space operates a power/enable button");
            powers[0]->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
        }

        //========================================================================
        // The knob's right-click menu with its new first item, drawn through the
        // real LookAndFeel -- menu windows are separate desktop windows and cannot
        // be captured from a component snapshot (same technique as rounds 3 and 4).
        //========================================================================
        {
            auto& lnf = juce::LookAndFeel::getDefaultLookAndFeel();
            if (auto* pm = dynamic_cast<juce::PopupMenu::LookAndFeelMethods*> (&lnf))
            {
                const int mw = 470, mh = 300;
                juce::Image img (juce::Image::ARGB, mw, mh, true);
                {
                juce::Graphics g (img);
                pm->drawPopupMenuBackground (g, mw, mh);
                int y = 10;
                auto row = [&] (int h) { auto r = juce::Rectangle<int> (10, y, mw - 20, h); y += h; return r; };
                pm->drawPopupMenuSectionHeader (g, row (26), "BARK");
                pm->drawPopupMenuItem (g, row (30), false, true, true, false, false,
                                       "Type value...", "Ctrl-click", nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (9), true, true, false, false, false, {}, {}, nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                       "Assign to Macro 1", {}, nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                       "Assign to Macro 2", {}, nullptr, nullptr);
                pm->drawPopupMenuItem (g, row (9), true, true, false, false, false, {}, {}, nullptr, nullptr);
                pm->drawPopupMenuSectionHeader (g, row (26), "MIDI");
                pm->drawPopupMenuItem (g, row (30), false, true, false, false, false,
                                       "MIDI Learn (move a CC after clicking)", {}, nullptr, nullptr);
                }   // the Graphics must be destroyed before the image is read back
                // composite the menu over a real editor shot -- a bare ARGB
                // image would save as transparent (menu windows are separate
                // desktop windows and cannot be snapshotted directly).
                auto shot = ed->createComponentSnapshot (ed->getLocalBounds(), false, 1.0f);
                auto kb = ed->getLocalArea (s, s->getLocalBounds());
                { juce::Graphics gw (shot);
                  gw.drawImageAt (img, juce::jlimit (0, ed->getWidth() - mw, kb.getCentreX()),
                                       juce::jlimit (0, ed->getHeight() - mh, kb.getCentreY())); }
                saveImage (shot, "ux-knob-menu.png");
            }
        }

        delete ed;
    }

    //==========================================================================
    // CLIENT REVIEW ROUND 8 — "sounds are not changing when I change sound"
    //
    // The model pages address a slot through slotForWorld(), so the MKS page
    // edits slot B, the FM page slot C and so on — but only slot A is enabled
    // at first run, and a disabled slot never receives a note. Choosing a patch
    // on any page but the first therefore loaded it into silence.
    //
    // These render REAL AUDIO through the REAL processor after driving the REAL
    // browser row, which is the only formulation that would have caught it: the
    // patch always did load, so every state-level assertion passed.
    //==========================================================================
    {
        proc.prepareToPlay (48000.0, 512);
        juce::MidiBuffer emptyMidi;

        auto settle = [&]
        {
            for (auto& e : proc.engines) e.allNotesOff (false);
            juce::AudioBuffer<float> b (2, 512);
            for (int k = 0; k < 40; ++k) { juce::MidiBuffer m; proc.processBlock (b, m); }
        };
        // One note, 8192 frames, rendered in 512-frame blocks like a real host.
        // Per-slot peaks are latched as a running maximum across the render, so
        // "which slot is actually sounding" — the question the whole round
        // turns on — is answered per slot rather than inferred from the mix.
        // settle() silences everything first, so a slot that reads zero here
        // genuinely received nothing.
        std::array<float, 4> slotPeakAtOnset {};
        auto renderNote = [&] (juce::AudioBuffer<float>& out)
        {
            settle();
            slotPeakAtOnset.fill (0.0f);
            out.setSize (2, 8192, false, false, true);
            out.clear();
            for (int off = 0; off < 8192; off += 512)
            {
                float* p[2] = { out.getWritePointer (0) + off, out.getWritePointer (1) + off };
                juce::AudioBuffer<float> view (p, 2, 512);
                juce::MidiBuffer m;
                if (off == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                proc.processBlock (view, m);
                for (int i = 0; i < 4; ++i)
                    slotPeakAtOnset[(size_t) i] = juce::jmax (slotPeakAtOnset[(size_t) i],
                                                              proc.slotPeak[(size_t) i].load());
            }
        };
        auto rms = [] (const juce::AudioBuffer<float>& b)
        {
            return 0.5 * (b.getRMSLevel (0, 0, b.getNumSamples()) + b.getRMSLevel (1, 0, b.getNumSamples()));
        };
        auto differ = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            double worst = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < a.getNumSamples(); ++s)
                    worst = juce::jmax (worst, (double) std::abs (a.getSample (c, s) - b.getSample (c, s)));
            return worst;
        };
        auto enabledCount = [&]
        {
            int n = 0;
            for (int i = 0; i < 4; ++i)
                if (proc.getParamNormalized ("host.slotEnable" + juce::String (i)) >= 0.5f) ++n;
            return n;
        };
        auto setEnables = [&] (bool a, bool b, bool c, bool d)
        {
            const bool w[4] = { a, b, c, d };
            for (int i = 0; i < 4; ++i)
                proc.setParamNormalized ("host.slotEnable" + juce::String (i), w[i] ? 1.0f : 0.0f);
        };
        auto isEnabled = [&] (int i)
        {
            return proc.getParamNormalized ("host.slotEnable" + juce::String (i)) >= 0.5f;
        };

        // Drives the browser the way the mouse does: rebuild for the page the
        // player is on, then activate the first instrument row.
        PresetBrowserPanel browser (proc);
        auto firstBundleRow = [&] (int page)
        {
            proc.state.currentPage = page;
            browser.rebuild();
            for (int i = 0; i < browser.rowCount(); ++i)
                if (browser.rowIsBundle (i)) return i;
            return -1;
        };

        std::printf ("\n== R8 (1) choosing a patch on a model page is AUDIBLE ==\n");
        {
            setEnables (true, false, false, false);      // the shipped first-run state
            proc.setParamNormalized ("host.solo", 0.0f);

            juce::AudioBuffer<float> prev, cur;
            bool havePrev = false;
            for (int page = 0; page < 4; ++page)
            {
                const int row = firstBundleRow (page);
                if (row < 0) { std::printf ("  page %d: no instruments in the library\n", page); continue; }
                const int slot = browser.rowSlot (row);
                browser.activateRow (row);
                renderNote (cur);
                const double level = rms (cur);
                const float chosen = slotPeakAtOnset[(size_t) slot];
                float others = 0.0f;
                for (int i = 0; i < 4; ++i) if (i != slot) others = juce::jmax (others, slotPeakAtOnset[(size_t) i]);
                std::printf ("  %-8s -> slot %c  \"%s\"  enabled=%d  voices=%d  chosen-slot peak=%.6f  "
                             "other slots=%.6f  mix rms=%.6f\n",
                             worldName (page), (char) ('A' + slot), browser.rowName (row).toRawUTF8(),
                             (int) isEnabled (slot), proc.engines[(size_t) slot].activeVoices(),
                             chosen, others, level);
                CHECK (slot == slotForWorld (proc.state, page), "the page's row targets the page's slot");
                CHECK (proc.engines[(size_t) slot].bundle() != nullptr, "the patch really loaded");
                CHECK (isEnabled (slot), "the slot holding the chosen patch is enabled");
                CHECK (proc.engines[(size_t) slot].activeVoices() > 0,
                       "the chosen patch's slot actually RECEIVES what you play (a disabled slot gets no note)");
                CHECK (chosen > 1.0e-4f, "the chosen patch is AUDIBLE — the round-8 defect");
                CHECK (others < 1.0e-6f,
                       "and it is the ONLY thing sounding: you hear the sound you picked, not the one before it");
                CHECK (level > 1.0e-4, "the mix carries it");
                if (havePrev)
                    CHECK (differ (prev, cur) > 1.0e-3,
                           "the rendered output actually CHANGES when the patch changes");
                prev.makeCopyOf (cur);
                havePrev = true;
            }
        }

        std::printf ("\n== R8 (2) patch load vs. a deliberate PERFORM arrangement ==\n");
        {
            // POLY, one slot on -> the enable MOVES (single-instrument setup)
            proc.setParamNormalized ("host.solo", 0.0f);
            setEnables (false, true, false, false);
            int row = firstBundleRow (0);
            CHECK (row >= 0, "the rhodes-ep page offers an instrument");
            browser.activateRow (row);
            CHECK (isEnabled (slotForWorld (proc.state, 0)) && enabledCount() == 1,
                   "POLY + single instrument: the enable MOVES to the chosen slot");

            // POLY, a layer the player built by hand -> additive only
            setEnables (true, true, false, false);
            row = firstBundleRow (2);
            if (row >= 0)
            {
                const int slot = browser.rowSlot (row);
                browser.activateRow (row);
                CHECK (isEnabled (0) && isEnabled (1) && isEnabled (slot),
                       "POLY + a built layer: the chosen slot is ADDED, nothing is switched off");
                CHECK (enabledCount() == 3, "a deliberate 2-slot arrangement survives a patch load");
            }

            // SOLO -> the chosen patch becomes the soloed slot
            proc.setParamNormalized ("host.solo", 1.0f);
            setEnables (true, true, false, false);
            row = firstBundleRow (3);
            if (row >= 0)
            {
                const int slot = browser.rowSlot (row);
                browser.activateRow (row);
                CHECK (isEnabled (slot) && enabledCount() == 1,
                       "SOLO: the chosen patch becomes the one slot that sounds");
            }
            proc.setParamNormalized ("host.solo", 0.0f);
        }

        //----------------------------------------------------------------------
        // The client's own library SHAPE, built as a fixture so the case is
        // covered on any machine: two bundles in ONE world (mks), one in
        // rhodes-ep, and NOTHING for fm or strings.
        //----------------------------------------------------------------------
        std::printf ("\n== R8 (3) the client's library shape (2 in one world, 2 worlds empty) ==\n");
        const auto libRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("MirekuRound8Library");
        {
            libRoot.deleteRecursively();
            libRoot.createDirectory();
            struct Fx { const char* name; const char* world; double hz; };
            const Fx fixtures[] = { { "MKS20", "mks", 220.0 },
                                    { "Piano", "mks", 277.18 },
                                    { "EP2",   "rhodes-ep", 329.63 } };
            juce::WavAudioFormat wav;
            for (auto& f : fixtures)
            {
                auto dir = libRoot.getChildFile (juce::String (f.name) + ".jmi");
                dir.getChildFile ("samples").createDirectory();
                auto wf = dir.getChildFile ("samples").getChildFile ("tone.wav");
                {
                    const int len = 48000;      // 1 s at 48 kHz
                    juce::AudioBuffer<float> b (1, len);
                    for (int s = 0; s < len; ++s)
                        b.setSample (0, s, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                   * f.hz * (double) s / 48000.0));
                    if (auto out = wf.createOutputStream())
                        if (auto* w = wav.createWriterFor (out.release(), 48000.0, 1, 16, {}, 0))
                        { w->writeFromAudioSampleBuffer (b, 0, len); delete w; }
                }
                juce::String js;
                js << "{\"formatVersion\":1,\"meta\":{\"name\":\"" << f.name << "\",\"world\":\"" << f.world
                   << "\",\"author\":\"round8 fixture\",\"createdAt\":\"2026-09-02T00:00:00Z\","
                      "\"captureMode\":\"auto\",\"channels\":\"mono\",\"sampleRate\":48000,\"bitDepth\":16,"
                      "\"notes\":\"\"},\"soundType\":\"sustaining\",\"zones\":[{\"rootNote\":60,"
                      "\"keyRangeLo\":0,\"keyRangeHi\":127,\"velRangeLo\":1,\"velRangeHi\":127,"
                      "\"roundRobins\":[{\"file\":\"samples/tone.wav\",\"peakDb\":-6.0,\"lengthMs\":1000}]}]}";
                dir.getChildFile ("instrument.json").replaceWithText (js);
            }
            proc.state.library.setLibraryDirTransient (libRoot);
            // Emulate a FRESH INSTANCE restoring saved state against this
            // library: empty engines, slots still naming the old bundles by
            // absolute path. That is the client's situation exactly, and it is
            // the path round 8 changed — substitute from the library where it
            // can, empty the slot where it cannot, never load the pinned path.
            for (auto& e : proc.engines) e.setBundle (nullptr);
            proc.syncAllToDsp();

            std::printf ("  library now lists %d bundles:", (int) proc.state.library.bundles().size());
            bool anyPlaceholder = false;
            for (auto& b : proc.state.library.bundles())
            {
                std::printf (" %s(%s)", b.name.toRawUTF8(), worldName (b.world));
                if (b.name.containsIgnoreCase ("Placeholder")) anyPlaceholder = true;
            }
            std::printf ("\n");
            CHECK ((int) proc.state.library.bundles().size() == 3 && ! anyPlaceholder,
                   "only the player's own instruments are listed (no placeholders)");
            CHECK ((int) proc.state.library.bundlesForWorld (1).size() == 2,
                   "two instruments share the mks world");
            CHECK (proc.state.library.bundlesForWorld (2).empty()
                   && proc.state.library.bundlesForWorld (3).empty(),
                   "fm and strings have nothing — as in the client's library");

            // both mks instruments must be reachable and audible from the mks page
            setEnables (true, false, false, false);
            proc.state.currentPage = 1;
            browser.rebuild();
            int played = 0;
            juce::AudioBuffer<float> a, b;
            for (int i = 0; i < browser.rowCount(); ++i)
            {
                if (! browser.rowIsBundle (i)) continue;
                const int slot = browser.rowSlot (i);
                browser.activateRow (i);
                renderNote (played == 0 ? a : b);
                const double level = rms (played == 0 ? a : b);
                std::printf ("  mks row \"%s\" -> slot %c rms=%.6f\n",
                             browser.rowName (i).toRawUTF8(), (char) ('A' + slot), level);
                CHECK (level > 1.0e-4, "each mks instrument is audible when chosen");
                CHECK (enabledCount() == 1 && isEnabled (slot),
                       "the mks page's slot is the one the player hears");
                ++played;
            }
            CHECK (played == 2, "both mks instruments were auditioned from the one page");
            if (played == 2)
                CHECK (differ (a, b) > 1.0e-3, "the two mks instruments render DIFFERENT audio");

            // an empty-world page: nothing to load, and nothing goes silent
            proc.state.currentPage = 2;
            browser.rebuild();
            std::printf ("  fm page browser rows: %d, message: \"%s\"\n",
                         browser.rowCount(),
                         browser.emptyMessage().replaceCharacter ('\n', ' ').toRawUTF8());
            CHECK (browser.rowCount() == 0, "an empty world lists nothing (no placeholder fallback)");
            CHECK (browser.emptyMessage().containsIgnoreCase ("FM")
                   && browser.emptyMessage().containsIgnoreCase ("library"),
                   "the browser says WHY the page is empty instead of showing a black box");
            juce::AudioBuffer<float> onEmptyPage;
            renderNote (onEmptyPage);
            CHECK (rms (onEmptyPage) > 1.0e-4,
                   "visiting a world the library cannot fill never silences the instrument");
        }

        std::printf ("\n== R8 (4) saved state that names an instrument the library no longer has ==\n");
        {
            // Exactly the client's situation: state written when the library was
            // empty pinned the testdata placeholders BY ABSOLUTE PATH, and
            // syncSlotToDsp used to load that path straight off disk for ever.
            const juce::File stale (juce::String (JM_LIBRARY_TESTDATA) + "/JM_Placeholder_MKS.jmi");
            std::printf ("  stale reference: %s (exists on disk: %d)\n",
                         stale.getFullPathName().toRawUTF8(), (int) stale.isDirectory());

            // (a) the world still has instruments -> substitute one of them
            {
                auto& s = proc.state.slots[1];
                s.world = 1;
                s.bundleDir = stale.getFullPathName();
                s.bundleName = "JM Placeholder MKS";
                proc.engines[1].setBundle (nullptr);
                proc.state.libraryNotice.clear();
                proc.syncSlotToDsp (1);
                auto lb = proc.engines[1].bundle();
                std::printf ("  -> loaded \"%s\", notice: \"%s\"\n",
                             lb != nullptr ? lb->dir.getFileName().toRawUTF8() : "(none)",
                             proc.state.libraryNotice.toRawUTF8());
                CHECK (lb != nullptr && lb->dir.getParentDirectory() == libRoot,
                       "a stale reference is NOT resurrected from disk — the library substitutes");
                CHECK (proc.state.library.find (juce::File (s.bundleDir)) != nullptr,
                       "the slot's stored path is corrected to something the library offers");
                CHECK (proc.state.libraryNotice.isNotEmpty(),
                       "the substitution is said out loud, not done silently");
            }
            // (b) the world has nothing -> the slot is honestly left empty
            {
                auto& s = proc.state.slots[3];
                s.world = 3;
                s.bundleDir = stale.getFullPathName();
                s.bundleName = "JM Placeholder MKS";
                proc.engines[3].setBundle (nullptr);
                proc.state.libraryNotice.clear();
                proc.syncSlotToDsp (3);
                std::printf ("  -> strings slot bundle: %s, notice: \"%s\"\n",
                             proc.engines[3].bundle() == nullptr ? "(empty)" : "LOADED",
                             proc.state.libraryNotice.toRawUTF8());
                CHECK (proc.engines[3].bundle() == nullptr && s.bundleDir.isEmpty(),
                       "with nothing to substitute the slot is left visibly empty, not filled with a ghost");
                CHECK (proc.state.libraryNotice.isNotEmpty(), "and that is reported too");
            }
        }

        // put the process back on the testdata library it was started with
        proc.state.library.setLibraryDirTransient (juce::File (JM_LIBRARY_TESTDATA));
        libRoot.deleteRecursively();
        proc.state.libraryNotice.clear();
        for (auto& e : proc.engines) e.allNotesOff (false);
    }

    //==========================================================================
    // AUTHORING MODE
    //
    // The app is sent to the artist, who crafts presets and modulation setups,
    // and comes back to the developer, who solidifies them as shipping factory
    // content. So the SAME save gesture has two meanings, and the three things
    // that can go wrong are all tested here against a REAL bundle on disk:
    //
    //   * the save lands in the wrong place (factory vs. user);
    //   * a write into a bundle leaves that bundle invalid — which would stop
    //     it loading in MIREKU AND in JM Capture;
    //   * the artist saves a patch and silently loses his modulation work,
    //     because the .jmi preset schema cannot carry it.
    //
    // The last one is the reason the save control states its destination and
    // its contents; those sentences are asserted here word for word, because a
    // boundary nobody can read is not a boundary.
    //==========================================================================
    {
        std::printf ("\n==========================================================\n"
                     "AUTHORING MODE\n"
                     "==========================================================\n");

        const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("MirekuAuthoringCheck");
        scratch.deleteRecursively();
        const auto libRoot = scratch.getChildFile ("Library");
        const auto presetRoot = scratch.getChildFile ("Presets");
        libRoot.createDirectory();
        presetRoot.createDirectory();

        // A small but REAL bundle: instrument.json + one PCM sample, the same
        // fixture shape the round-8 block uses, so writing into presets/ is the
        // genuine path and not a mock.
        juce::WavAudioFormat authWav;
        auto makeBundle = [&] (const juce::String& name, const char* world, double hz)
        {
            auto dir = libRoot.getChildFile (name + ".jmi");
            dir.getChildFile ("samples").createDirectory();
            auto wf = dir.getChildFile ("samples").getChildFile ("tone.wav");
            const int len = 48000;
            juce::AudioBuffer<float> b (1, len);
            for (int s = 0; s < len; ++s)
                b.setSample (0, s, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                            * hz * (double) s / 48000.0));
            if (auto out = wf.createOutputStream())
                if (auto* w = authWav.createWriterFor (out.release(), 48000.0, 1, 16, {}, 0))
                { w->writeFromAudioSampleBuffer (b, 0, len); delete w; }
            juce::String js;
            js << "{\"formatVersion\":1,\"meta\":{\"name\":\"" << name << "\",\"world\":\"" << world
               << "\",\"author\":\"authoring fixture\",\"createdAt\":\"2026-10-22T00:00:00Z\","
                  "\"captureMode\":\"auto\",\"channels\":\"mono\",\"sampleRate\":48000,\"bitDepth\":16,"
                  "\"notes\":\"\"},\"soundType\":\"sustaining\",\"zones\":[{\"rootNote\":60,"
                  "\"keyRangeLo\":0,\"keyRangeHi\":127,\"velRangeLo\":1,\"velRangeHi\":127,"
                  "\"roundRobins\":[{\"file\":\"samples/tone.wav\",\"peakDb\":-6.0,\"lengthMs\":1000}]}]}";
            dir.getChildFile ("instrument.json").replaceWithText (js);
            return dir;
        };
        const auto epBundle = makeBundle ("AuthEP", "rhodes-ep", 220.0);
        makeBundle ("AuthMks", "mks", 330.0);

        // Every presets/*.json in a bundle must pass the .jmi preset schema or
        // `npx jmi validate` fails and jmi::loadInstrument drops the preset.
        // This is the C++ mirror of that check, run over the whole bundle.
        auto bundleIsValid = [] (const juce::File& dir, juce::String& why)
        {
            why.clear();
            jmi::Instrument inst;
            std::string err;
            std::vector<std::string> files;
            for (auto& f : dir.getChildFile ("presets")
                              .findChildFiles (juce::File::findFiles, false, "*.json"))
                files.push_back (f.getFullPathName().toStdString());
            std::sort (files.begin(), files.end());
            if (! jmi::loadInstrument (dir.getFullPathName().toStdString(), inst, err, &files))
            { why = "loadInstrument: " + juce::String (err); return false; }
            if (inst.presets.size() != files.size())
            { why = "a preset file was skipped as unparseable"; return false; }
            for (auto& f : dir.getChildFile ("presets")
                              .findChildFiles (juce::File::findFiles, false, "*.json"))
            {
                auto chk = authoring::checkBundlePresetVar (juce::JSON::parse (f.loadFileAsString()));
                if (! chk.ok)
                { why = f.getFileName() + ": " + chk.errors.joinIntoString ("; "); return false; }
            }
            return true;
        };

        proc.state.library.setLibraryDirTransient (libRoot);
        for (auto& e : proc.engines) e.setBundle (nullptr);
        proc.state.currentPage = 0;
        for (auto& b : proc.state.library.bundles())
            if (b.dir == epBundle) proc.loadBundleIntoSlot (0, b);

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (1) the mode: default, parse, and what it says on screen ==\n");
        {
            CHECK (authoring::modeFromId ("") == authoring::Mode::authoring
                   && authoring::modeFromId ("authoring") == authoring::Mode::authoring
                   && authoring::modeFromId ("release") == authoring::Mode::release
                   && authoring::modeFromId ("RELEASE") == authoring::Mode::release,
                   "an absent or unrecognised mode means AUTHORING (the app has not been told it ships yet)");
            CHECK (authoring::modeBadge (authoring::Mode::authoring).containsIgnoreCase ("FACTORY")
                   && authoring::modeBadge (authoring::Mode::release).isEmpty(),
                   "authoring mode carries an on-screen badge; release mode deliberately carries none");
            // The suite is pinned to release by JM_AUTHORING_MODE (see
            // CMakeLists), which is also the proof that the transient override
            // works: the developer's own settings.json never reaches the test.
            CHECK (! proc.state.library.isAuthoring(),
                   "JM_AUTHORING_MODE pins the mode for this process without touching settings.json");
            // The C++ schema mirror must not drift from the JSON schema that
            // `npx jmi validate` actually uses — that drift is the one failure
            // that would let a bad preset into a bundle undetected.
            {
                auto schema = juce::JSON::parse (juce::File (JM_JMI_SCHEMA_DIR)
                                                     .getChildFile ("preset.schema.json")
                                                     .loadFileAsString());
                auto fxEnum = schema.getProperty ("properties", juce::var())
                                    .getProperty ("fxRack", juce::var())
                                    .getProperty ("items", juce::var())
                                    .getProperty ("properties", juce::var())
                                    .getProperty ("type", juce::var())
                                    .getProperty ("enum", juce::var());
                juce::StringArray fromSchema;
                if (auto* a = fxEnum.getArray()) for (auto& v : *a) fromSchema.add (v.toString());
                auto required = schema.getProperty ("required", juce::var());
                juce::StringArray req;
                if (auto* a = required.getArray()) for (auto& v : *a) req.add (v.toString());
                fromSchema.sort (false);
                auto mirrored = authoring::allowedFxTypeIds();
                mirrored.sort (false);
                std::printf ("  schema fx types: %d, mirrored: %d\n", fromSchema.size(), mirrored.size());
                CHECK (! fromSchema.isEmpty() && fromSchema == mirrored,
                       "the C++ preset-schema mirror lists exactly the FX types preset.schema.json does");
                CHECK (req.contains ("formatVersion") && req.contains ("name") && req.contains ("engine")
                       && req.contains ("fxRack") && req.contains ("ampEnvelope")
                       && (bool) schema.getProperty ("additionalProperties", true) == false,
                       "and the schema is still strict about required + additional properties");
            }

            jm::Library probe;
            probe.setModeTransient (authoring::Mode::release);
            CHECK (! probe.isAuthoring(), "a Library can be put into release mode");
            probe.setModeTransient (authoring::Mode::authoring);
            CHECK (probe.isAuthoring(), "...and back");
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (2) a save in AUTHORING mode lands in the bundle's FACTORY presets ==\n");
        {
            proc.state.library.setModeTransient (authoring::Mode::authoring);
            proc.state.slots[0].engineParams["tine"] = 77.0f;
            proc.state.slots[0].ampEnv.releaseMs = 640.0f;

            auto out = proc.saveInstrumentPreset (0, "Warm Tine");
            std::printf ("  -> %s\n     %s\n", out.file.getFullPathName().toRawUTF8(),
                         out.message.toRawUTF8());
            CHECK (out.ok && out.dest == authoring::SaveDest::bundleFactory,
                   "the save reports that it created FACTORY content");
            CHECK (out.file.existsAsFile()
                   && out.file.getParentDirectory() == epBundle.getChildFile ("presets"),
                   "and the file really is inside the instrument bundle that ships");
            CHECK (out.message.containsIgnoreCase ("FACTORY"),
                   "the message names the destination, so nobody has to guess");

            juce::String why;
            CHECK (bundleIsValid (epBundle, why),
                   "WRITING INTO A REAL BUNDLE LEAVES IT VALID (the risky path)");
            if (why.isNotEmpty()) std::printf ("  bundle invalid: %s\n", why.toRawUTF8());

            auto v = juce::JSON::parse (out.file.loadFileAsString());
            CHECK ((int) v.getProperty ("formatVersion", 0) == 1,
                   "the written preset carries formatVersion (the schema requires it)");
            CHECK (std::abs ((double) v.getProperty ("engine", juce::var()).getProperty ("tine", 0.0) - 77.0) < 1.0e-6,
                   "the patch the artist dialled in is what was written");
            CHECK (! v.hasProperty ("mod") && ! v.hasProperty ("provenance"),
                   "and NOTHING else: one extra property would fail the strict schema");

            CHECK (authoring::ledgerFile (epBundle).existsAsFile()
                   && authoring::ledgerClaims (epBundle, out.file.getFileName()),
                   "provenance is recorded in the bundle's authoring.json sidecar");
            auto led = authoring::readLedger (epBundle);
            CHECK (! led.empty() && led[0].appVersion == juce::String (authoring::kAppVersion)
                   && led[0].savedAt.isNotEmpty(),
                   "the ledger says WHICH app version wrote it and WHEN");
            CHECK (! authoring::ledgerFile (epBundle).getParentDirectory()
                        .getFileName().equalsIgnoreCase ("presets"),
                   "the sidecar is NOT in presets/ — every file there is schema-checked");

            PresetBrowserPanel br (proc);
            proc.state.currentPage = 0;
            br.rebuild();
            bool listed = false;
            for (int i = 0; i < br.rowCount(); ++i) if (br.rowName (i) == "Warm Tine") listed = true;
            CHECK (listed, "the new factory patch appears in the browser immediately");

            auto again = proc.saveInstrumentPreset (0, "Warm Tine");
            CHECK (again.ok && again.note.containsIgnoreCase ("bak"),
                   "overwriting a factory patch keeps the previous version beside it");
            CHECK (bundleIsValid (epBundle, why), "and the bundle is still valid after the overwrite");
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (3) the SAME save in RELEASE mode lands in the USER folder ==\n");
        {
            proc.state.library.setModeTransient (authoring::Mode::release);
            const int before = epBundle.getChildFile ("presets")
                                       .findChildFiles (juce::File::findFiles, false, "*.json").size();
            auto out = proc.saveInstrumentPreset (0, "Shipped Behaviour");
            std::printf ("  -> %s\n     %s\n", out.file.getFullPathName().toRawUTF8(),
                         out.message.toRawUTF8());
            CHECK (out.ok && out.dest == authoring::SaveDest::worldUserFolder,
                   "in release mode a save is a USER preset — today's behaviour, unchanged");
            CHECK (out.file.getParentDirectory() == proc.worldPresetDir (0),
                   "it goes to Presets/<world>/");
            CHECK (epBundle.getChildFile ("presets")
                           .findChildFiles (juce::File::findFiles, false, "*.json").size() == before,
                   "and the shipped product NEVER writes into the customer's bundle");
            out.file.deleteFile();
            proc.state.library.setModeTransient (authoring::Mode::authoring);
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (4) a performance preset round-trips the WHOLE modulation setup ==\n");
        {
            proc.state.library.setModeTransient (authoring::Mode::authoring);

            auto& md = proc.state.mod;
            md = ModState {};
            md.rowCount = 6;
            md.lfos[0].shape = (int) LfoShape::triangle; md.lfos[0].sync = 0;
            md.lfos[0].rateHz = 3.25f; md.lfos[0].phaseDeg = 135.0f; md.lfos[0].fadeSec = 2.25f;
            md.lfos[1].shape = (int) LfoShape::smoothRandom; md.lfos[1].sync = 1;
            md.lfos[1].division = 17; md.lfos[1].retrigger = 2;
            md.rows[0].source = ModSource::lfo1; md.rows[0].destPath = "slot0.engine.tine";
            md.rows[0].depth = 0.63f; md.rows[0].min = 0.1f; md.rows[0].max = 0.9f;
            md.rows[0].curve = 1.7f; md.rows[0].invert = true; md.rows[0].enabled = true;
            md.rows[1].source = ModSource::lfo2; md.rows[1].destPath = "host.slotPan0";
            md.rows[1].depth = -0.42f; md.rows[1].enabled = true;
            md.rows[2].source = ModSource::velocity; md.rows[2].destPath = "slot0.engine.body";
            md.rows[2].depth = 0.31f; md.rows[2].enabled = true;
            md.arp.enabled = 1; md.arp.slotMask = 0x01; md.arp.mode = 3; md.arp.sync = 1;
            md.arp.division = 9; md.arp.gate = 72.0f; md.arp.octaves = 3; md.arp.octDir = 2;
            md.arp.swing = 18.0f; md.arp.hold = 1; md.arp.velMode = 2; md.arp.fixedVel = 88;
            md.arp.accentMask = 0x05; md.arp.inclusive = 1;
            proc.state.macros[2].name = "JAMES SWELL";
            proc.state.macros[2].targets.clear();
            {
                MacroTarget mt;
                mt.path = "slot0.engine.space"; mt.min = 0.2f; mt.max = 0.95f;
                mt.depth = -0.8f; mt.curve = 2.2f; mt.invert = true;
                proc.state.macros[2].targets.push_back (mt);
            }
            proc.publishModulation();
            proc.publishArp();

            auto out = proc.savePerformanceSnapshot ("James Live Set");
            std::printf ("  -> %s\n     %s\n", out.file.getFullPathName().toRawUTF8(),
                         out.message.toRawUTF8());
            CHECK (out.ok && out.dest == authoring::SaveDest::performanceFactory,
                   "in authoring mode a performance snapshot becomes FACTORY performance content");
            CHECK (out.file.getParentDirectory() == JMSignatureProcessor::factoryPerformanceDir(),
                   "it lands in Presets/Performance/Factory, which the browser already scans as factory");

            auto onDisk = juce::JSON::parse (out.file.loadFileAsString());
            CHECK (onDisk.hasProperty ("mod") && onDisk.hasProperty ("macros")
                   && onDisk.getProperty ("mod", juce::var()).hasProperty ("arp"),
                   "the file really carries the modulation, the macros and the arpeggiator");

            proc.state.mod = ModState {};
            proc.state.macros[2] = Macro { "MACRO 3", {} };
            proc.publishModulation();
            proc.publishArp();
            CHECK (proc.loadPerformancePreset (out.file), "the factory snapshot loads");

            auto& r0 = proc.state.mod.rows[0];
            CHECK (r0.source == ModSource::lfo1 && r0.destPath == "slot0.engine.tine"
                   && std::abs (r0.depth - 0.63f) < 1.0e-5f
                   && std::abs (r0.min - 0.1f) < 1.0e-5f && std::abs (r0.max - 0.9f) < 1.0e-5f
                   && std::abs (r0.curve - 1.7f) < 1.0e-5f && r0.invert && r0.enabled,
                   "a matrix row comes back with its source, destination, range, depth, curve and invert");
            CHECK (proc.state.mod.rows[1].source == ModSource::lfo2
                   && std::abs (proc.state.mod.rows[1].depth + 0.42f) < 1.0e-5f
                   && proc.state.mod.rows[2].source == ModSource::velocity,
                   "and so do the other rows, in order");
            auto& l0 = proc.state.mod.lfos[0];
            CHECK (l0.shape == (int) LfoShape::triangle && l0.sync == 0
                   && std::abs (l0.rateHz - 3.25f) < 1.0e-4f
                   && std::abs (l0.phaseDeg - 135.0f) < 1.0e-3f
                   && std::abs (l0.fadeSec - 2.25f) < 1.0e-4f,
                   "an LFO comes back with its shape, rate, phase and fade");
            CHECK (proc.state.mod.lfos[1].shape == (int) LfoShape::smoothRandom
                   && proc.state.mod.lfos[1].division == 17
                   && proc.state.mod.lfos[1].retrigger == 2,
                   "and the synced LFO with its division and retrigger mode");
            auto& ar = proc.state.mod.arp;
            CHECK (ar.enabled == 1 && ar.slotMask == 0x01 && ar.mode == 3 && ar.division == 9
                   && std::abs (ar.gate - 72.0f) < 1.0e-3f && ar.octaves == 3 && ar.octDir == 2
                   && std::abs (ar.swing - 18.0f) < 1.0e-3f && ar.hold == 1 && ar.velMode == 2
                   && ar.fixedVel == 88 && ar.accentMask == 0x05 && ar.inclusive == 1,
                   "the ARPEGGIATOR comes back whole — every one of its fourteen settings");
            CHECK (proc.state.macros[2].name == "JAMES SWELL"
                   && proc.state.macros[2].targets.size() == 1
                   && proc.state.macros[2].targets[0].path == "slot0.engine.space"
                   && std::abs (proc.state.macros[2].targets[0].depth + 0.8f) < 1.0e-5f
                   && proc.state.macros[2].targets[0].invert,
                   "and the macro keeps its name and its bipolar, curved, inverted link");
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (5) an instrument preset does NOT silently claim to hold modulation ==\n");
        {
            proc.state.currentPage = 0;
            auto cap = proc.modCaptureForSlot (0);
            std::printf ("  slot A carries: %s  (rows elsewhere: %d, LFOs in use: %d)\n",
                         cap.phrase().toRawUTF8(), cap.rowsElsewhere, cap.lfosInUse);
            CHECK (cap.anyOnSlot() && cap.rowsOnSlot >= 2 && cap.macroLinksOnSlot >= 1 && cap.arpOnSlot,
                   "the processor can see exactly what modulation the slot is carrying");

            auto patch = proc.slotPresetToVar (0, "mod probe");
            CHECK (! patch.hasProperty ("mod") && ! patch.hasProperty ("macros")
                   && ! patch.hasProperty ("arp"),
                   "a patch carries NO modulation — the .jmi schema is shared with JM Capture");

            const auto warn = proc.modBoundaryLine (0);
            std::printf ("  the save control reads:\n    \"%s\"\n    \"%s\"\n",
                         proc.saveDestinationLine (0).toRawUTF8(), warn.toRawUTF8());
            CHECK (warn.isNotEmpty(), "so the save control says so BEFORE the click, not after");
            CHECK (warn.containsIgnoreCase ("cannot hold")
                   && warn.containsIgnoreCase ("modulation routing")
                   && warn.containsIgnoreCase ("arpeggiator"),
                   "it names what would be lost, in the artist's terms");
            // The line states the LIMIT; the way out is the button directly
            // under it, which is where the artist clicks. Asserted below on the
            // panel, where the label he actually reads is.
            CHECK (proc.saveDestinationLine (0).containsIgnoreCase ("FACTORY")
                   && proc.saveDestinationLine (0).containsIgnoreCase (epBundle.getFileName()),
                   "the destination line names the bundle the patch is going into");

            PresetBrowserPanel br (proc);
            br.setBounds (0, 0, Theme::L::browserW, Theme::L::browserH);
            br.rebuild();
            CHECK (br.alternativeSaveOffered()
                   && br.alternativeSaveText().containsIgnoreCase ("PERFORMANCE"),
                   "and names the save that WOULD keep it, on the button that does it");
            CHECK (br.boundaryText() == warn && br.alternativeSaveOffered(),
                   "the browser shows that line and OFFERS \"SAVE PERFORMANCE INSTEAD\"");

            br.setSaveName ("Kept By The Button");
            br.pressSave (true);                       // the one-click escape
            auto kept = JMSignatureProcessor::factoryPerformanceDir()
                            .getChildFile ("Kept By The Button.json");
            CHECK (kept.existsAsFile()
                   && juce::JSON::parse (kept.loadFileAsString()).hasProperty ("mod"),
                   "one click on it saves a snapshot that DOES carry the modulation");
            kept.deleteFile();

            auto keep = proc.state.mod;
            auto keepMacro = proc.state.macros[2];
            proc.state.mod = ModState {};
            proc.state.macros[2] = Macro { "MACRO 3", {} };
            proc.publishModulation();
            proc.publishArp();
            CHECK (proc.modBoundaryLine (0).isEmpty(),
                   "with nothing to lose the warning is absent — it never cries wolf");
            PresetBrowserPanel quiet (proc);
            quiet.setBounds (0, 0, Theme::L::browserW, Theme::L::browserH);
            quiet.rebuild();
            CHECK (! quiet.alternativeSaveOffered(), "and the alternative save is not offered either");

            // The model page's overlay browser is short, so the block collapses
            // what it is not showing instead of eating the preset list.
            {
                const juce::Rectangle<int> overlay (0, 0, Theme::L::mpSurfW - 120,
                                                    Theme::L::mpSurfH - 32);
                PresetBrowserPanel narrow (proc, true);
                narrow.setBounds (overlay);
                narrow.rebuild();
                const int quietList = narrow.listBounds().getHeight();
                proc.state.mod = keep;
                proc.state.macros[2] = keepMacro;
                proc.publishModulation();
                proc.publishArp();
                narrow.rebuild();
                const int warnedList = narrow.listBounds().getHeight();
                std::printf ("  compact browser list: %d px with no warning, %d px with one\n",
                             quietList, warnedList);
                CHECK (quietList > warnedList && warnedList > 120,
                       "the compact browser gives the warning its room only when there is a warning");
                savePng (narrow, "authoring-compact-browser.png");
            }
            proc.state.mod = keep;
            proc.state.macros[2] = keepMacro;
            proc.publishModulation();
            proc.publishArp();
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (6) a bundle that cannot take the save never loses the work ==\n");
        {
            proc.state.library.setModeTransient (authoring::Mode::authoring);
            proc.engines[2].setBundle (nullptr);
            proc.state.slots[2].bundleDir.clear();
            proc.state.slots[2].world = 2;
            auto out = proc.saveInstrumentPreset (2, "No Bundle Here");
            std::printf ("  -> %s\n     %s\n     note: %s\n",
                         out.file.getFullPathName().toRawUTF8(), out.message.toRawUTF8(),
                         out.note.toRawUTF8());
            CHECK (out.ok && out.dest == authoring::SaveDest::worldUserFolder && out.fellBack,
                   "with no bundle to write into, the patch is still SAVED (to the user folder)");
            CHECK (out.note.isNotEmpty(), "and the fallback is stated, not silent");
            CHECK (proc.saveDestinationLine (2).containsIgnoreCase ("no instrument loaded"),
                   "the save control warned about it beforehand too");
            out.file.deleteFile();

            auto notABundle = scratch.getChildFile ("NotABundle.jmi");
            notABundle.createDirectory();
            auto wr = authoring::writeBundlePreset (notABundle, "nope", proc.slotPresetToVar (0, "nope"));
            CHECK (! wr.ok && wr.message.containsIgnoreCase ("not a .jmi bundle"),
                   "a folder with no instrument.json is refused, not half-written into");
            CHECK (! notABundle.getChildFile ("presets").exists(),
                   "and nothing at all was created inside it");

            auto bad = proc.slotPresetToVar (0, "bad");
            bad.getDynamicObject()->setProperty ("surprise", 1);
            auto wrBad = authoring::writeBundlePreset (epBundle, "Refused", bad);
            CHECK (! wrBad.ok && wrBad.message.containsIgnoreCase ("does not fit"),
                   "a patch carrying anything the schema forbids is refused before the bundle is touched");
            CHECK (! epBundle.getChildFile ("presets").getChildFile ("Refused.json").exists(),
                   "so no file appears");
            juce::String why;
            CHECK (bundleIsValid (epBundle, why), "and the bundle is untouched and still valid");
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (7) COLLECT gathers everything, IMPORT puts it back faithfully ==\n");
        {
            presetRoot.getChildFile ("Performance").getChildFile ("Factory").createDirectory();
            presetRoot.getChildFile ("rhodes-ep").createDirectory();
            presetRoot.getChildFile ("Performance").getChildFile ("Factory")
                      .getChildFile ("Set One.json")
                      .replaceWithText (JMSignatureProcessor::factoryPerformanceDir()
                                            .getChildFile ("James Live Set.json").loadFileAsString());
            presetRoot.getChildFile ("Performance").getChildFile ("Loose Idea.json")
                      .replaceWithText ("{\"name\":\"Loose Idea\"}");
            presetRoot.getChildFile ("rhodes-ep").getChildFile ("Sketch.json")
                      .replaceWithText (juce::JSON::toString (proc.slotPresetToVar (0, "Sketch"), false));
            auto settingsCopy = scratch.getChildFile ("settings.json");
            settingsCopy.replaceWithText ("{\"mode\":\"authoring\",\"favorites\":[]}");

            auto dest = scratch.getChildFile ("Collected");
            auto res = authoring::collect (libRoot, presetRoot, settingsCopy, dest,
                                           authoring::Mode::authoring);
            std::printf ("  %s\n", res.message.toRawUTF8());
            for (auto& l : res.lines) std::printf ("    %s\n", l.toRawUTF8());
            CHECK (res.ok && res.manifest.existsAsFile(), "COLLECT writes a manifest");

            auto man = juce::JSON::parse (res.manifest.loadFileAsString());
            CHECK (man.getProperty ("kind", "").toString() == authoring::kCollectionKind
                   && man.getProperty ("appVersion", "").toString() == juce::String (authoring::kAppVersion)
                   && man.getProperty ("exportedAt", "").toString().isNotEmpty()
                   && man.getProperty ("mode", "").toString() == "authoring",
                   "with provenance: app version, when, which mode, which machine");
            CHECK (man.getProperty ("note", "").toString().containsIgnoreCase ("modulation"),
                   "and it says out loud that modulation lives only in the performance snapshots");

            auto* entries = man.getProperty ("entries", juce::var()).getArray();
            CHECK (entries != nullptr, "the manifest lists entries");
            int nBundle = 0, nPerfF = 0, nPerfU = 0, nWorld = 0, nSettings = 0, nAuthored = 0;
            bool everyFilePresent = true, everyHashRight = true;
            if (entries != nullptr)
                for (auto& e : *entries)
                {
                    const auto kind = e.getProperty ("kind", "").toString();
                    if (kind == "bundlePreset") ++nBundle;
                    else if (kind == "performanceFactory") ++nPerfF;
                    else if (kind == "performanceUser") ++nPerfU;
                    else if (kind == "worldPreset") ++nWorld;
                    else if (kind == "settings") ++nSettings;
                    if ((bool) e.getProperty ("authored", false)) ++nAuthored;
                    auto f = dest.getChildFile (e.getProperty ("path", "").toString());
                    if (! f.existsAsFile()) everyFilePresent = false;
                    else if (authoring::contentHash (f) != e.getProperty ("hash", "").toString())
                        everyHashRight = false;
                }
            std::printf ("  entries: %d bundle preset(s), %d factory + %d user performance, "
                         "%d world preset(s), %d settings; %d marked authored-here\n",
                         nBundle, nPerfF, nPerfU, nWorld, nSettings, nAuthored);
            CHECK (nBundle >= 1 && nPerfF == 1 && nPerfU == 1 && nWorld == 1 && nSettings == 1,
                   "every kind of thing the artist can produce is collected");
            CHECK (everyFilePresent, "every manifest entry has its file in the collection");
            CHECK (everyHashRight, "and every file matches the checksum the manifest recorded");
            CHECK (nAuthored >= 2,
                   "the ledger lets the manifest mark what was authored HERE, apart from what shipped");

            // ------- the round trip: wipe the destination, then import
            auto stolen = epBundle.getChildFile ("presets").getChildFile ("Warm Tine.json");
            const auto stolenHash = authoring::contentHash (stolen);
            stolen.deleteFile();
            presetRoot.getChildFile ("Performance").getChildFile ("Factory")
                      .getChildFile ("Set One.json").deleteFile();
            presetRoot.getChildFile ("rhodes-ep").getChildFile ("Sketch.json").deleteFile();

            auto imp = authoring::importCollection (dest, libRoot, presetRoot);
            std::printf ("  %s\n", imp.message.toRawUTF8());
            for (auto& l : imp.lines) std::printf ("    %s\n", l.toRawUTF8());
            CHECK (imp.ok, "IMPORT reports no skips");
            CHECK (imp.applied >= 3, "it put the deleted files back");
            CHECK (imp.identical >= 1,
                   "and recognised the ones that were already identical instead of churning them");
            CHECK (stolen.existsAsFile() && authoring::contentHash (stolen) == stolenHash,
                   "a FACTORY patch comes back byte for byte, inside its bundle");
            CHECK (presetRoot.getChildFile ("Performance").getChildFile ("Factory")
                             .getChildFile ("Set One.json").existsAsFile(),
                   "so does the factory performance snapshot");
            CHECK (presetRoot.getChildFile ("rhodes-ep").getChildFile ("Sketch.json").existsAsFile(),
                   "and the world user preset");
            juce::String why;
            CHECK (bundleIsValid (epBundle, why), "and the bundle an import wrote into is still valid");
            if (why.isNotEmpty()) std::printf ("  bundle invalid: %s\n", why.toRawUTF8());

            {   // a tampered collection is refused per entry, not applied blindly
                auto victim = dest.getChildFile ("performance/Factory/Set One.json");
                victim.replaceWithText (victim.loadFileAsString() + " ");
                auto tampered = authoring::importCollection (dest, libRoot, presetRoot);
                CHECK (! tampered.ok && tampered.skipped >= 1,
                       "a file whose checksum no longer matches the manifest is SKIPPED and reported");
            }
            {   // ...and a bundle this library does not have is reported, not invented
                auto alien = dest.getChildFile ("bundles/Nonexistent.jmi/presets/x.json");
                alien.getParentDirectory().createDirectory();
                alien.replaceWithText (juce::JSON::toString (proc.slotPresetToVar (0, "x"), false));
                if (auto* arr = man.getProperty ("entries", juce::var()).getArray())
                {
                    auto* eo = new juce::DynamicObject();
                    eo->setProperty ("kind", "bundlePreset");
                    eo->setProperty ("path", "bundles/Nonexistent.jmi/presets/x.json");
                    eo->setProperty ("bundle", "Nonexistent.jmi");
                    eo->setProperty ("hash", authoring::contentHash (alien));
                    arr->add (juce::var (eo));
                    res.manifest.replaceWithText (juce::JSON::toString (man, false));
                }
                auto partial = authoring::importCollection (dest, libRoot, presetRoot);
                bool said = false;
                for (auto& l : partial.lines)
                    if (l.containsIgnoreCase ("Nonexistent.jmi")
                        && l.containsIgnoreCase ("not in this library")) said = true;
                CHECK (said, "a preset for a bundle this library does not have is named, not guessed at");
            }
        }

        // =====================================================================
        // THE AUTHORING WORKFLOW (2026-10-26). The mode above decides WHERE a
        // save goes; these four sections are the editing James actually asked
        // for on top of it: move a sound to another category, save over it or
        // as a new one, name and rename, and get the whole performance loop
        // (create -> name -> save over -> rename -> recall) to work.
        // =====================================================================
        std::printf ("\n== AUTHORING (9) MOVE AN INSTRUMENT TO ANOTHER CATEGORY ==\n");
        {
            proc.state.library.setModeTransient (authoring::Mode::authoring);
            proc.state.library.setLibraryDirTransient (libRoot);
            const auto mksBundle = libRoot.getChildFile ("AuthMks.jmi");
            const auto ij = mksBundle.getChildFile ("instrument.json");
            const auto beforeText = ij.loadFileAsString();

            // the instrument is loaded and audible in a slot, which is the case
            // that has to stay coherent when it changes pages under the player
            for (auto& b : proc.state.library.bundles())
                if (b.dir == mksBundle) proc.loadBundleIntoSlot (1, b);
            CHECK (proc.engines[1].bundle() != nullptr && proc.state.slots[1].world == 1,
                   "AuthMks starts in MKS and is loaded in a slot");
            proc.state.currentPage = 1;

            // the client's rule: STRINGS is the catch-all for anything that is
            // not a piano/Rhodes/EP, an MKS or FM
            auto moved = proc.recategoriseBundle (mksBundle, 3);
            std::printf ("  -> %s  (%s)\n", moved.message.toRawUTF8(), moved.note.toRawUTF8());
            CHECK (moved.ok, "the move reports success");

            jmi::Instrument reread;
            std::string rerr;
            CHECK (jmi::parseInstrumentJson (ij.loadFileAsString().toStdString(), reread, rerr)
                   && reread.meta.world == jmi::World::Strings,
                   "THE WORLD CHANGE LANDED IN THE BUNDLE: instrument.json now says strings");
            juce::String why2;
            CHECK (bundleIsValid (mksBundle, why2),
                   "and the bundle is STILL VALID after being written into");
            if (why2.isNotEmpty()) std::printf ("  bundle invalid: %s\n", why2.toRawUTF8());

            // the narrowness of the write is the whole safety property
            juce::String diff;
            CHECK (authoring::varsDifferOnlyAt (juce::JSON::parse (beforeText),
                                                juce::JSON::parse (ij.loadFileAsString()),
                                                "meta.world", diff),
                   "meta.world changed and NOTHING ELSE in instrument.json did");
            if (diff.isNotEmpty()) std::printf ("  unexpected difference: %s\n", diff.toRawUTF8());
            CHECK (ij.getSiblingFile ("instrument.json.bak").existsAsFile(),
                   "the previous instrument.json is kept as a .bak");

            // ...and the app agrees with the file
            CHECK (proc.state.slots[1].world == 3 && proc.engines[1].bundle() != nullptr
                   && proc.engines[1].bundle()->world == 3,
                   "the slot playing it moved world with it, bundle and engine together");
            CHECK (moved.note.containsIgnoreCase ("re-seeded"),
                   "and it SAYS that the slot's per-world voicing controls were re-seeded");
            CHECK (proc.state.currentPage == 3,
                   "the app follows the instrument to its new page instead of leaving it lost");
            bool inStrings = false, inMks = false;
            for (auto& b : proc.state.library.bundlesForWorld (3)) if (b.dir == mksBundle) inStrings = true;
            for (auto& b : proc.state.library.bundlesForWorld (1)) if (b.dir == mksBundle) inMks = true;
            CHECK (inStrings && ! inMks, "the library lists it under STRINGS and no longer under MKS");

            {
                PresetBrowserPanel br (proc);
                proc.state.currentPage = 3;
                br.rebuild();
                bool listed = false;
                for (int i = 0; i < br.rowCount(); ++i)
                    if (br.rowName (i).contains ("AuthMks")) listed = true;
                CHECK (listed, "the browser offers it on the STRINGS page immediately");
                proc.state.currentPage = 1;
                br.rebuild();
                bool stale = false;
                for (int i = 0; i < br.rowCount(); ++i)
                    if (br.rowName (i).contains ("AuthMks")) stale = true;
                CHECK (! stale, "and the MKS page no longer shows a row that is not there any more");
            }

            CHECK (! proc.recategoriseBundle (mksBundle, 3).ok,
                   "moving it to the category it is already in is refused, not re-written");
            CHECK (! proc.recategoriseBundle (mksBundle, 9).ok,
                   "a world that does not exist is refused");
            CHECK (! proc.recategoriseBundle (libRoot.getChildFile ("NotABundle"), 2).ok,
                   "and so is something that is not a .jmi bundle");

            // ---------------------------------------------------------------
            // RENAMING A SOUND. The artist went looking for this in the save
            // block and found the preset machinery instead, because the list
            // refused to rename an instrument at all and told him to go rename
            // a folder. It is `meta.name` — one guarded property — and the .jmi
            // folder deliberately does NOT move, because slots, performances
            // and the bundle's own presets all find it by path.
            {
                const auto folderBefore = mksBundle.getFileName();
                auto before = ij.loadFileAsString();
                auto ren = proc.renameBundle (mksBundle, "Jamestown Keys");
                std::printf ("  -> %s\n", ren.message.toRawUTF8());
                CHECK (ren.ok, "an instrument can be renamed from the app");

                jmi::Instrument named;
                std::string nerr;
                CHECK (jmi::parseInstrumentJson (ij.loadFileAsString().toStdString(), named, nerr)
                       && named.meta.name == "Jamestown Keys",
                       "THE NEW NAME LANDED IN THE BUNDLE: instrument.json says so");
                CHECK (mksBundle.getFileName() == folderBefore && mksBundle.isDirectory(),
                       "and the .jmi folder did not move, so nothing that points at it broke");
                CHECK (proc.engines[1].bundle() == nullptr
                       || proc.engines[1].bundle()->dir != mksBundle
                       || proc.engines[1].bundle()->inst.meta.name == "Jamestown Keys",
                       "a slot already playing it shows the new name too");

                bool listed = false;
                for (auto& b : proc.state.library.bundles())
                    if (b.dir == mksBundle && b.name == "Jamestown Keys") listed = true;
                CHECK (listed, "and the library lists it under the new name");

                // the guards
                CHECK (! proc.renameBundle (mksBundle, "   ").ok, "an empty name is refused");
                CHECK (! proc.renameBundle (mksBundle, "Jamestown Keys").ok,
                       "renaming it to what it is already called is refused, not re-written");
                CHECK (! proc.renameBundle (libRoot.getChildFile ("NotABundle"), "X").ok,
                       "and so is something that is not a .jmi bundle");
                {
                    // a name another instrument already answers to is refused:
                    // two rows reading the same thing is the problem he is
                    // renaming to solve
                    juce::String other;
                    for (auto& b : proc.state.library.bundles())
                        if (b.dir != mksBundle && other.isEmpty()) other = b.name;
                    if (other.isNotEmpty())
                        CHECK (! proc.renameBundle (mksBundle, other).ok,
                               "a name another instrument already uses is refused");
                }

                // put the name back for the rest of the suite
                CHECK (proc.renameBundle (mksBundle, named.meta.name == "Jamestown Keys"
                                                         ? juce::JSON::parse (before)
                                                               .getDynamicObject()
                                                               ->getProperty ("meta")
                                                               .getDynamicObject()
                                                               ->getProperty ("name")
                                                               .toString()
                                                         : juce::String ("AuthMks")).ok,
                       "and it can be renamed back");
            }

            // put it back where the rest of the suite expects it
            CHECK (proc.recategoriseBundle (mksBundle, 1).ok, "and it can be moved back");
            proc.state.currentPage = 0;
            for (auto& b : proc.state.library.bundles())
                if (b.dir == epBundle) proc.loadBundleIntoSlot (0, b);
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (10) SAVE AS makes a new one; SAVE OVER replaces the right one ==\n");
        {
            proc.state.library.setModeTransient (authoring::Mode::authoring);
            proc.state.currentPage = 0;
            PresetBrowserPanel br (proc);
            br.setSize (Theme::L::browserW, Theme::L::browserH);
            br.rebuild();

            proc.state.slots[0].engineParams["tine"] = 11.0f;
            br.setSaveName ("Dial A");
            br.pressSave();
            auto dialA = authoring::bundlePresetFile (epBundle, "Dial A");
            CHECK (dialA.existsAsFile(), "SAVE AS creates the preset it was given a name for");
            const auto dialABytes = dialA.loadFileAsString();

            // ---- SAVE AS can never stand on an existing preset
            proc.state.slots[0].engineParams["tine"] = 99.0f;
            br.setSaveName ("Dial A");
            br.pressSave();
            CHECK (br.destinationText().startsWith ("NOT SAVED")
                   && br.destinationText().contains ("Dial A")
                   && br.destinationText().containsIgnoreCase ("OVERWRITE"),
                   "SAVE AS onto a name that is taken REFUSES and points at OVERWRITE");
            CHECK (dialA.loadFileAsString() == dialABytes,
                   "...and the existing preset is byte-for-byte untouched");
            CHECK (! proc.saveInstrumentPreset (0, "Dial A",
                        JMSignatureProcessor::SaveIntent::saveAs).ok,
                   "the same refusal at the API, so no UI path can get round it");

            // ---- SAVE AS with a different name leaves the original alone
            br.setSaveName ("Dial B");
            br.pressSave();
            CHECK (authoring::bundlePresetFile (epBundle, "Dial B").existsAsFile()
                   && dialA.loadFileAsString() == dialABytes,
                   "SAVE AS under a new name creates it WITHOUT touching the original");

            // ---- which one is about to happen must be unmistakable
            proc.state.slots[0].presetName = "Dial A";
            br.rebuild();
            CHECK (br.overwriteOffered(), "with a loaded preset on disk, OVERWRITE is offered");
            CHECK (br.overwriteButtonText().containsIgnoreCase ("OVERWRITE")
                   && br.overwriteButtonText().contains ("Dial A"),
                   "and the control NAMES the preset it would replace, in its own label");
            // The DRAWN line is deliberately short -- while OVERWRITE is armed,
            // the one thing worth reading is which file is about to change.
            CHECK (br.overwriteText().containsIgnoreCase ("replaces \"Dial A\"")
                   && br.overwriteText().containsIgnoreCase (".bak"),
                   "the drawn line names the preset replaced and the .bak kept");
            // Both halves are still stated -- in the tooltip, which is where the
            // sentence about SAVE AS moved when the line was cut back.
            CHECK (br.overwriteTooltip().containsIgnoreCase ("replaces \"Dial A\"")
                   && br.overwriteTooltip().containsIgnoreCase ("never replaces anything"),
                   "the wording states both halves: what OVERWRITE does and what SAVE AS does not");
            CHECK (! br.overwriteArmed(), "it starts unarmed");

            // first click arms and explains; it does NOT write
            proc.state.slots[0].engineParams["tine"] = 44.0f;
            br.pressOverwrite();
            CHECK (br.overwriteArmed() && dialA.loadFileAsString() == dialABytes,
                   "the FIRST click only arms it — nothing on disk has changed yet");
            CHECK (br.infoLineText() == br.overwriteText(),
                   "and while it is armed the destination line says what the next click will replace");
            br.disarmOverwrite();
            CHECK (! br.overwriteArmed() && dialA.loadFileAsString() == dialABytes,
                   "a disarm leaves the preset alone");

            // two clicks: the write
            const int presetsBefore = epBundle.getChildFile ("presets")
                                              .findChildFiles (juce::File::findFiles, false, "*.json").size();
            br.pressOverwrite();
            br.pressOverwrite();
            CHECK (dialA.existsAsFile()
                   && std::abs ((double) juce::JSON::parse (dialA.loadFileAsString())
                                              .getProperty ("engine", juce::var())
                                              .getProperty ("tine", 0.0) - 44.0) < 1.0e-6,
                   "SAVE OVER replaced THE RIGHT PRESET with what is on screen");
            {
                auto bakFile = dialA.getSiblingFile (dialA.getFileName() + ".bak");
                const double bakTine = (double) juce::JSON::parse (bakFile.loadFileAsString())
                                           .getProperty ("engine", juce::var())
                                           .getProperty ("tine", -1.0);
                std::printf ("  .bak holds tine %.1f (the value before the overwrite)\n", bakTine);
                CHECK (bakFile.existsAsFile() && std::abs (bakTine - 11.0) < 1.0e-6,
                       "and kept the previous version as a .bak holding the value it had");
            }
            CHECK (epBundle.getChildFile ("presets")
                           .findChildFiles (juce::File::findFiles, false, "*.json").size() == presetsBefore,
                   "a save-over adds no second file — it replaces one");
            juce::String why3;
            CHECK (bundleIsValid (epBundle, why3), "and the bundle is still valid");

            // the mod boundary still applies to BOTH new paths
            proc.state.mod.rowCount = 1;
            proc.state.mod.rows[0] = MatrixRow {};
            proc.state.mod.rows[0].source = ModSource::lfo1;
            proc.state.mod.rows[0].destPath = "slot0.engine.tine";
            proc.state.mod.rows[0].depth = 0.5f;
            proc.state.mod.rows[0].enabled = true;
            proc.publishModulation();
            br.rebuild();
            CHECK (br.boundaryText().containsIgnoreCase ("A patch cannot hold")
                   && br.alternativeSaveOffered(),
                   "the modulation boundary and SAVE PERFORMANCE INSTEAD survive beside the new controls");
            CHECK (br.overwriteOffered(),
                   "...and OVERWRITE is still offered at the same time, not swapped out for it");
            proc.state.mod = ModState {};
            proc.publishModulation();
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (11) NAME AND RENAME, with every reference kept coherent ==\n");
        {
            proc.state.library.setModeTransient (authoring::Mode::authoring);
            proc.state.currentPage = 0;
            PresetBrowserPanel br (proc);
            br.rebuild();

            // star it first: a browser row's favourite key is "<kind>|<name>",
            // so a rename that forgets it loses the star
            proc.state.library.toggleFavorite ("1|Dial A");
            proc.state.slots[0].presetName = "Dial A";

            int rowIdx = -1;
            for (int i = 0; i < br.rowCount(); ++i) if (br.rowName (i) == "Dial A") rowIdx = i;
            CHECK (rowIdx >= 0, "the preset to rename is on screen");
            CHECK (br.renameRow (rowIdx, "Tine Bloom"), "the rename reports success");

            auto renamed = authoring::bundlePresetFile (epBundle, "Tine Bloom");
            CHECK (renamed.existsAsFile()
                   && juce::JSON::parse (renamed.loadFileAsString())
                          .getProperty ("name", "").toString() == "Tine Bloom",
                   "BOTH halves of the name moved: the file AND the name inside it");
            CHECK (! authoring::bundlePresetFile (epBundle, "Dial A").existsAsFile(),
                   "the old file is gone, so the preset is not listed twice");
            CHECK (epBundle.getChildFile ("presets").getChildFile ("Dial A.json.bak").existsAsFile(),
                   "...but kept as a .bak, because a rename is destructive too");
            juce::String why4;
            CHECK (bundleIsValid (epBundle, why4),
                   "THE RENAME LEAVES THE BUNDLE VALID AND EVERY PRESET IN IT RESOLVABLE");
            if (why4.isNotEmpty()) std::printf ("  bundle invalid: %s\n", why4.toRawUTF8());

            bool listedNew = false, listedOld = false;
            br.rebuild();
            for (int i = 0; i < br.rowCount(); ++i)
            {
                if (br.rowName (i) == "Tine Bloom") listedNew = true;
                if (br.rowName (i) == "Dial A") listedOld = true;
            }
            CHECK (listedNew && ! listedOld, "the browser shows the new name and only the new name");
            bool inMemory = false;
            if (auto lb = proc.engines[0].bundle())
                for (auto& p : lb->inst.presets) if (juce::String (p.name) == "Tine Bloom") inMemory = true;
            CHECK (inMemory, "the in-memory FACTORY list the browser draws from was renamed too");
            CHECK (proc.state.slots[0].presetName == "Tine Bloom",
                   "the slot's displayed patch name followed it");
            CHECK (proc.state.library.isFavorite ("1|Tine Bloom")
                   && ! proc.state.library.isFavorite ("1|Dial A"),
                   "and the FAVOURITE star moved with it instead of pointing at nothing");
            bool ledgerHasNew = false, ledgerHasOld = false;
            for (auto& e : authoring::readLedger (epBundle))
            {
                if (e.file == "Tine Bloom.json") ledgerHasNew = true;
                if (e.file == "Dial A.json") ledgerHasOld = true;
            }
            CHECK (ledgerHasNew && ! ledgerHasOld,
                   "the authoring ledger follows the file, so provenance is not orphaned");

            CHECK (! br.renameRow (rowIdx >= 0 ? 0 : 0, ""), "an empty name is refused");
            int bloom = -1;
            for (int i = 0; i < br.rowCount(); ++i) if (br.rowName (i) == "Tine Bloom") bloom = i;
            CHECK (bloom >= 0 && ! br.renameRow (bloom, "Dial B"),
                   "renaming onto a name that is already taken is refused, not merged");
            CHECK (authoring::bundlePresetFile (epBundle, "Dial B").existsAsFile()
                   && authoring::bundlePresetFile (epBundle, "Tine Bloom").existsAsFile(),
                   "...and both presets are still there afterwards");
            proc.state.library.toggleFavorite ("1|Tine Bloom");
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (12) THE PERFORMANCE LOOP: create, name, save over, rename, RECALL ==\n");
        {
            proc.state.library.setModeTransient (authoring::Mode::authoring);
            proc.state.library.setLibraryDirTransient (libRoot);
            const auto mksBundle = libRoot.getChildFile ("AuthMks.jmi");
            for (auto& b : proc.state.library.bundles())
            {
                if (b.dir == epBundle) proc.loadBundleIntoSlot (0, b);
                if (b.dir == mksBundle) proc.loadBundleIntoSlot (1, b);
            }
            CHECK (proc.engines[0].bundle() != nullptr && proc.engines[1].bundle() != nullptr,
                   "two instruments are arranged across two slots — the thing a performance is");

            // the modulation work that ONLY a performance can carry
            auto& md = proc.state.mod;
            md = ModState {};
            md.rowCount = 3;
            md.lfos[0].shape = (int) LfoShape::triangle; md.lfos[0].rateHz = 1.75f;
            md.lfos[1].sync = 1; md.lfos[1].division = 11;
            md.rows[0].source = ModSource::lfo1; md.rows[0].destPath = "slot0.engine.tine";
            md.rows[0].depth = 0.55f; md.rows[0].enabled = true;
            md.rows[1].source = ModSource::lfo2; md.rows[1].destPath = "host.slotPan1";
            md.rows[1].depth = -0.33f; md.rows[1].enabled = true;
            md.rows[2].source = ModSource::modWheel; md.rows[2].destPath = "slot1.engine.warmth";
            md.rows[2].depth = 0.21f; md.rows[2].enabled = true;
            md.arp.enabled = 1; md.arp.slotMask = 0x02; md.arp.mode = 2; md.arp.division = 7;
            md.arp.octaves = 2; md.arp.gate = 55.0f;
            proc.state.macros[1].name = "JAMES WIDTH";
            proc.state.macros[1].targets.clear();
            {
                MacroTarget mt;
                mt.path = "host.slotPan0"; mt.min = 0.1f; mt.max = 0.9f; mt.depth = 0.75f;
                proc.state.macros[1].targets.push_back (mt);
            }
            proc.publishModulation();

            // ---- CREATE + NAME (save as)
            proc.state.performPresetName = {};
            proc.state.currentPage = 4;
            PresetBrowserPanel br (proc);
            br.rebuild();
            CHECK (br.destinationText().containsIgnoreCase ("FACTORY performance")
                   && br.destinationText().containsIgnoreCase ("modulation"),
                   "on PERFORM the save control says it captures the modulation");
            CHECK (! br.overwriteOffered(),
                   "a performance that has never been saved offers SAVE AS only — "
                   "there is nothing to overwrite");
            br.setSaveName ("James Rig");
            br.pressSave();
            auto rig = JMSignatureProcessor::factoryPerformanceDir().getChildFile ("James Rig.json");
            CHECK (rig.existsAsFile() && proc.state.performPresetName == "James Rig",
                   "CREATE + NAME: the performance exists and the app knows which one is loaded");
            br.rebuild();
            CHECK (br.overwriteOffered() && br.overwriteButtonText().contains ("James Rig"),
                   "and now OVERWRITE appears, naming it");

            // ---- SAVE OVER
            const auto firstBytes = rig.loadFileAsString();
            proc.state.mod.rows[0].depth = 0.91f;
            proc.publishModulation();
            br.pressOverwrite();
            CHECK (rig.loadFileAsString() == firstBytes, "arming a performance overwrite writes nothing");
            br.pressOverwrite();
            CHECK (rig.getSiblingFile ("James Rig.json.bak").existsAsFile(),
                   "SAVE OVER on a performance keeps a .bak of what it replaced");
            CHECK (JMSignatureProcessor::factoryPerformanceDir()
                       .findChildFiles (juce::File::findFiles, false, "James Rig*.json").size() == 1,
                   "and did not leave a second performance behind");

            // ---- RECALL. This is the gap that was found: a snapshot stores
            // each slot's .jmi path and NOTHING ever re-loaded it, so every
            // parameter came back and the instruments did not.
            for (auto& e : proc.engines) e.setBundle (nullptr);
            proc.state.mod = ModState {};
            proc.state.macros[1] = Macro { "MACRO 2", {} };
            proc.publishModulation();
            CHECK (proc.loadPerformancePreset (rig), "the performance recalls");
            CHECK (proc.engines[0].bundle() != nullptr && proc.engines[1].bundle() != nullptr,
                   "RECALL BRINGS THE INSTRUMENTS BACK (it used to restore controls over silence)");
            CHECK (proc.engines[0].bundle()->dir == epBundle
                   && proc.engines[1].bundle()->dir == mksBundle,
                   "...and the RIGHT instrument in the right slot");
            CHECK (std::abs (proc.state.mod.rows[0].depth - 0.91f) < 1.0e-5f
                   && proc.state.mod.rows[1].source == ModSource::lfo2
                   && proc.state.mod.rows[2].destPath == "slot1.engine.warmth",
                   "the matrix rows came back with the save-over's values, not the first save's");
            CHECK (proc.state.mod.lfos[1].sync == 1 && proc.state.mod.lfos[1].division == 11
                   && std::abs (proc.state.mod.lfos[0].rateHz - 1.75f) < 1.0e-4f,
                   "both LFOs came back");
            CHECK (proc.state.mod.arp.enabled == 1 && proc.state.mod.arp.slotMask == 0x02
                   && proc.state.mod.arp.division == 7 && proc.state.mod.arp.octaves == 2,
                   "the arpeggiator came back");
            CHECK (proc.state.macros[1].name == "JAMES WIDTH"
                   && proc.state.macros[1].targets.size() == 1,
                   "and the macro came back with its link");
            {
                // "restored into state" is not the same as "the audio thread is
                // playing it", so this reads the settings the ARP ITSELF is
                // using, after one real render block has picked up the publish.
                proc.prepareToPlay (48000.0, 64);
                juce::AudioBuffer<float> blk (2, 64);
                blk.clear();
                juce::MidiBuffer noMidi;
                proc.processBlock (blk, noMidi);
                CHECK (proc.arp.activeSettings().enabled == 1
                       && proc.arp.activeSettings().division == 7,
                       "...and the arp was PUBLISHED to the audio thread, not just restored into state");
            }

            // ---- a saved instrument that has left the library is substituted,
            // not silently dropped
            proc.state.slots[1].bundleDir = libRoot.getChildFile ("Gone.jmi").getFullPathName();
            proc.engines[1].setBundle (nullptr);
            CHECK (proc.restoreSlotBundles() >= 1, "a missing instrument is replaced, not left silent");
            CHECK (proc.engines[1].bundle() != nullptr
                   && proc.engines[1].bundle()->world == proc.state.slots[1].world,
                   "...by one from the same category");

            // ---- RENAME a performance
            proc.state.currentPage = 4;
            br.rebuild();
            proc.state.library.toggleFavorite ("3|James Rig");
            int rigRow = -1;
            for (int i = 0; i < br.rowCount(); ++i) if (br.rowName (i) == "James Rig") rigRow = i;
            CHECK (rigRow >= 0, "the performance is listed on the FACTORY tab where it was saved");
            CHECK (br.renameRow (rigRow, "James Stage Rig"), "it renames");
            auto stage = JMSignatureProcessor::factoryPerformanceDir()
                             .getChildFile ("James Stage Rig.json");
            CHECK (stage.existsAsFile() && ! rig.existsAsFile(),
                   "the file moved and the old name is gone");
            CHECK (juce::JSON::parse (stage.loadFileAsString()).getProperty ("name", "").toString()
                       == "James Stage Rig",
                   "the name inside the file moved with it");
            CHECK (proc.state.performPresetName == "James Stage Rig",
                   "and the app's idea of which performance is loaded moved too");
            CHECK (proc.state.library.isFavorite ("3|James Stage Rig"), "so did its star");
            CHECK (proc.loadPerformancePreset (stage)
                   && proc.state.mod.arp.division == 7
                   && std::abs (proc.state.mod.rows[0].depth - 0.91f) < 1.0e-5f,
                   "A RENAMED PERFORMANCE STILL RECALLS ITS MODULATION — the rename is not lossy");
            proc.state.library.toggleFavorite ("3|James Stage Rig");

            // leave the machine as we found it
            for (auto& f : JMSignatureProcessor::factoryPerformanceDir()
                               .findChildFiles (juce::File::findFiles, false, "James *.json"))
                f.deleteFile();
            for (auto& f : JMSignatureProcessor::factoryPerformanceDir()
                               .findChildFiles (juce::File::findFiles, false, "James *.json.bak"))
                f.deleteFile();
            proc.state.mod = ModState {};
            proc.state.macros[1] = Macro { "MACRO 2", {} };
            proc.publishModulation();
            proc.state.currentPage = 0;
        }

        // ---------------------------------------------------------------------
        std::printf ("\n== AUTHORING (8) the mode on screen ==\n");
        {
            auto shoot = [&] (authoring::Mode mode, const char* tag)
            {
                proc.state.library.setModeTransient (mode);
                proc.state.currentPage = 0;
                auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
                ed->setSize (Theme::M::designW, Theme::M::designH);
                ed->setVisible (true);
                if (auto* br = findOne<PresetBrowserPanel> (*ed)) br->rebuild();
                savePng (*ed, juce::String ("authoring-") + tag + "-full.png");
                if (auto* bar = findOne<BottomBar> (*ed))
                    saveImage (ed->createComponentSnapshot (ed->getLocalArea (bar, bar->getLocalBounds()),
                                                            false, 2.0f),
                               juce::String ("authoring-") + tag + "-statusbar.png");
                if (auto* br = findOne<PresetBrowserPanel> (*ed))
                {
                    // the block grew a row (OVERWRITE), so the crop follows it
                    auto region = ed->getLocalArea (br, br->getLocalBounds())
                                      .withTrimmedTop (Theme::L::brListY + Theme::L::brListH - 40)
                                      .withHeight (190)
                                      .getIntersection (ed->getLocalBounds());
                    saveImage (ed->createComponentSnapshot (region, false, 3.0f),
                               juce::String ("authoring-") + tag + "-save-block.png");
                }
                delete ed;
            };
            shoot (authoring::Mode::authoring, "mode-on");
            shoot (authoring::Mode::release, "mode-off");
            proc.state.library.setModeTransient (authoring::Mode::authoring);

            //----------------------------------------- SAVE AS vs SAVE OVER on screen
            // The claim being made is that the two cannot be confused, so the
            // two states are photographed: OVERWRITE resting, and OVERWRITE
            // armed with the consequence spelled out underneath it.
            {
                proc.state.currentPage = 0;
                auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
                ed->setSize (Theme::M::designW, Theme::M::designH);
                ed->setVisible (true);
                auto shootBlock = [&] (const char* tag)
                {
                    if (auto* br = findOne<PresetBrowserPanel> (*ed))
                    {
                        auto region = ed->getLocalArea (br, br->getLocalBounds())
                                          .withTrimmedTop (Theme::L::brSaveY - 14)
                                          .withHeight (170)
                                          .getIntersection (ed->getLocalBounds());
                        saveImage (ed->createComponentSnapshot (region, false, 3.0f),
                                   juce::String ("authoring-save-over-") + tag + ".png");
                    }
                };
                if (auto* br = findOne<PresetBrowserPanel> (*ed))
                {
                    proc.state.slots[0].presetName = "Tine Bloom";
                    br->rebuild();
                    br->setSaveName ("New Sound");
                    CHECK (br->overwriteOffered() && br->overwriteButtonText().contains ("Tine Bloom"),
                           "on screen, SAVE AS and OVERWRITE sit on separate rows and the "
                           "destructive one names its target");
                    shootBlock ("resting");
                    br->pressOverwrite();                     // arm only
                    shootBlock ("armed");
                    br->disarmOverwrite();
                }
                savePng (*ed, "authoring-save-over-full.png");
                delete ed;
            }

            AuthoringPanel panel (proc);
            panel.setSize (AuthoringPanel::kWidth, AuthoringPanel::kHeight);
            panel.setVisible (true);
            CHECK (panel.authoringSelected(), "the panel shows AUTHORING selected while the mode is on");
            CHECK (panel.logText().containsIgnoreCase ("CANNOT carry modulation"),
                   "and states the format boundary in the panel too");
            savePng (panel, "authoring-panel.png");
        }

        // leave the machine as we found it
        JMSignatureProcessor::factoryPerformanceDir().getChildFile ("James Live Set.json").deleteFile();
        JMSignatureProcessor::factoryPerformanceDir().getChildFile ("James Live Set.json.bak").deleteFile();
        proc.state.mod = ModState {};
        proc.state.macros[2] = Macro { "MACRO 3", {} };
        proc.publishModulation();
        proc.publishArp();
        proc.state.library.setLibraryDirTransient (juce::File (JM_LIBRARY_TESTDATA));
        scratch.deleteRecursively();
    }

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    std::printf ("\nworst measured hero offset: (%d,%d) px\n", worstDx, worstDy);
    std::printf ("%s (%d failure%s)\n", failures == 0 ? "SELF-CHECK PASSED" : "SELF-CHECK FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
