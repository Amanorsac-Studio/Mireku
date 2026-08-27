// SnapshotCheck.cpp — UX pack V2 acceptance self-check (console, offscreen).
// Renders the editor root at exactly 1000x720, measures every hero-control
// centre against specs/screen-layouts.json values, verifies the layout rules
// (header 52px / content y=74, 3-over-2, no DECAYING on model pages, PERFORM
// 2x2), and writes PNG snapshots for the overlay record.
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

/// centres of hero sliders (those carrying a "jmMotif" property) in root coords
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

static bool findText (juce::Component& c, const juce::String& text)
{
    if (auto* l = dynamic_cast<juce::Label*> (&c))
        if (l->getText().containsIgnoreCase (text)) return true;
    if (auto* b = dynamic_cast<juce::Button*> (&c))
        if (b->getButtonText().containsIgnoreCase (text)) return true;
    for (auto* ch : c.getChildren())
        if (findText (*ch, text)) return true;
    return false;
}

static void savePng (juce::Component& c, const juce::String& name)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), false, 1.0f);
    juce::File out = juce::File (JM_SNAPSHOT_DIR).getChildFile (name);
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
    std::printf ("JM Signature V2 acceptance self-check (1000x720)\n================================================\n");

    JmLookAndFeel lnf;
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    JMSignatureProcessor proc;

    // ------------------------------------------------ model pages
    const int five[5][2] = { { 340, 284 }, { 500, 284 }, { 660, 284 }, { 420, 456 }, { 580, 456 } };
    const int six[6][2]  = { { 320, 270 }, { 500, 270 }, { 680, 270 }, { 320, 445 }, { 500, 445 }, { 680, 445 } };
    const char* worlds[] = { "rhodes", "mks", "fm", "strings" };
    int worstDx = 0, worstDy = 0;

    for (int w = 0; w < 4; ++w)
    {
        proc.state.currentPage = w;
        ModelPagePanel page (proc, w);
        page.setSize (1000, 720);
        auto centres = heroCentres (page);
        const bool isSix = w == 1;
        const int n = isSix ? 6 : 5;
        std::printf ("\n== %s model page ==\n", worlds[w]);
        char msg[128];
        std::snprintf (msg, sizeof (msg), "%d hero controls (expected %d)", (int) centres.size(), n);
        CHECK ((int) centres.size() == n, msg);
        // expected list sorted the same way (row-major)
        std::vector<juce::Point<int>> want;
        for (int i = 0; i < n; ++i)
            want.push_back ({ isSix ? six[i][0] : five[i][0], isSix ? six[i][1] : five[i][1] });
        std::sort (want.begin(), want.end(), [] (auto a, auto b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
        for (int i = 0; i < juce::jmin (n, (int) centres.size()); ++i)
        {
            const int dx = centres[(size_t) i].x - want[(size_t) i].x;
            const int dy = centres[(size_t) i].y - want[(size_t) i].y;
            worstDx = juce::jmax (worstDx, std::abs (dx));
            worstDy = juce::jmax (worstDy, std::abs (dy));
            std::printf ("  control %d: measured (%d,%d)  spec (%d,%d)  offset (%+d,%+d)\n",
                         i + 1, centres[(size_t) i].x, centres[(size_t) i].y,
                         want[(size_t) i].x, want[(size_t) i].y, dx, dy);
            CHECK (std::abs (dx) <= 2 && std::abs (dy) <= 2, "within 2px of screen spec");
        }
        CHECK (! findText (page, "DECAYING") && ! findText (page, "SUSTAINING"),
               "no DECAYING/SUSTAINING label on the model page");
        if (w == 0) savePng (page, "v2-check-rhodes.png");
    }

    // ------------------------------------------------ perform page
    std::printf ("\n== perform page ==\n");
    proc.state.currentPage = 4;
    PerformPagePanel perform (proc);
    perform.setSize (1000, 720);
    auto macros = heroCentres (perform);
    CHECK ((int) macros.size() == 4, "4 macro hero knobs");
    const int macroX[4] = { 230, 410, 590, 770 };
    for (int i = 0; i < juce::jmin (4, (int) macros.size()); ++i)
    {
        const int dx = macros[(size_t) i].x - macroX[i];
        const int dy = macros[(size_t) i].y - 594;
        worstDx = juce::jmax (worstDx, std::abs (dx));
        worstDy = juce::jmax (worstDy, std::abs (dy));
        std::printf ("  macro %d: measured (%d,%d)  spec (%d,594)  offset (%+d,%+d)\n",
                     i + 1, macros[(size_t) i].x, macros[(size_t) i].y, macroX[i], dx, dy);
        CHECK (std::abs (dx) <= 2 && std::abs (dy) <= 2, "macro within 2px");
    }
    // slot grid + mode + transpose geometry (component bounds are set from tokens)
    struct Probe { const char* name; juce::Rectangle<int> want; };
    const Probe probes[] = {
        { "slot A", { 94, 116, 390, 132 } },  { "slot B", { 512, 116, 390, 132 } },
        { "slot C", { 94, 264, 390, 132 } },  { "slot D", { 512, 264, 390, 132 } },
        { "mode toggle", { 236, 412, 184, 46 } }, { "transpose", { 480, 395, 284, 78 } },
    };
    int probeIdx = 0, slotHits = 0;
    for (auto& p : probes)
    {
        bool found = false;
        for (auto* ch : perform.getChildren())
            if (ch->getBounds() == p.want) { found = true; break; }
        char msg[96];
        std::snprintf (msg, sizeof (msg), "%s at (%d,%d,%dx%d)", p.name, p.want.getX(), p.want.getY(),
                       p.want.getWidth(), p.want.getHeight());
        CHECK (found, msg);
        if (probeIdx++ < 4 && found) ++slotHits;
    }
    CHECK (slotHits == 4, "PERFORM central 2x2 slot grid exact");
    savePng (perform, "v2-check-perform.png");

    // ------------------------------------------------ header + root rules
    std::printf ("\n== header / root ==\n");
    CHECK (Theme::V2::headerH == 52 && Theme::V2::contentY == 74, "header 52px, content starts at y=74");
    CHECK (Theme::M::designW == 1000 && Theme::M::designH == 720, "root reference 1000x720 (25:18)");
    {
        auto* ed = dynamic_cast<JMSignatureEditor*> (proc.createEditor());
        CHECK (ed != nullptr, "editor created");
        if (ed != nullptr)
        {
            ed->setSize (1000, 720);
            savePng (*ed, "v2-check-editor.png");
            CHECK (ed->getConstrainer()->getFixedAspectRatio() == 1000.0 / 720.0, "fixed 25:18 aspect");
            delete ed;
        }
    }

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    std::printf ("\nworst measured offset: (%d,%d) px\n", worstDx, worstDy);
    std::printf ("%s (%d failure%s)\n", failures == 0 ? "SELF-CHECK PASSED" : "SELF-CHECK FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
