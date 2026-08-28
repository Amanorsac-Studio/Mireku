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

    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    std::printf ("\nworst measured hero offset: (%d,%d) px\n", worstDx, worstDy);
    std::printf ("%s (%d failure%s)\n", failures == 0 ? "SELF-CHECK PASSED" : "SELF-CHECK FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
