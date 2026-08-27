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

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::printf ("JM Signature — client review round 2 self-check (1536x1024)\n"
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
            CHECK (ed->getConstrainer()->getFixedAspectRatio() == 1536.0 / 1024.0, "fixed 3:2 aspect");

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

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    std::printf ("\nworst measured hero offset: (%d,%d) px\n", worstDx, worstDy);
    std::printf ("%s (%d failure%s)\n", failures == 0 ? "SELF-CHECK PASSED" : "SELF-CHECK FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
