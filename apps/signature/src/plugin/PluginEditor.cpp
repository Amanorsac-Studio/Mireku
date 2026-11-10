#include "PluginEditor.h"

namespace jm {

//==============================================================================
// R3 (B/C/F): styled modal overlay inside the root coordinate system, so every
// dialog is drawn in the hardware language and scales with the UI instead of
// opening a bare OS window.
void JMSignatureEditor::RootComponent::showOverlay (std::unique_ptr<juce::Component> content, int w, int h)
{
    closeOverlay();
    scrim_ = std::make_unique<Scrim>();
    scrim_->onDismiss = [this] { closeOverlay(); };
    addAndMakeVisible (*scrim_);
    overlay_ = std::move (content);
    overlayW_ = w;
    overlayH_ = h;
    addAndMakeVisible (*overlay_);
    resized();
}

void JMSignatureEditor::RootComponent::closeOverlay()
{
    overlay_.reset();
    scrim_.reset();
}

void JMSignatureEditor::RootComponent::resized()
{
    if (scrim_ != nullptr) scrim_->setBounds (getLocalBounds());
    if (overlay_ != nullptr)
        overlay_->setBounds (juce::Rectangle<int> (overlayW_, overlayH_)
                                 .withCentre (getLocalBounds().getCentre()));
}

//==============================================================================
// CLIENT REVIEW ROUND 6 (issue 3) — opening size clamped to the display.
juce::Point<int> JMSignatureEditor::preferredSizeForArea (juce::Rectangle<int> availableArea)
{
    // A native-title-bar window is TALLER than its content by the caption and
    // borders, so the editor has to be smaller than the user area by that much
    // or the window still overhangs. These are the Windows 100%-scale values in
    // logical pixels (they do not grow with DPI, because logical pixels don't).
    constexpr int kCaptionH = 32;
    constexpr int kBorder   = 8;

    const int usableW = juce::jmax (160, availableArea.getWidth()  - 2 * kBorder);
    const int usableH = juce::jmax (120, availableArea.getHeight() - kCaptionH - 2 * kBorder);

    // uniform contain, never enlarged past the design size
    const double s = juce::jmin (1.0,
                                 (double) usableW / (double) Theme::M::designW,
                                 (double) usableH / (double) Theme::M::designH);

    const int w = juce::jmax (160, (int) std::floor (Theme::M::designW * s));
    // 3:2 preserved exactly, then re-clamped in case rounding pushed it over
    const int h = juce::jlimit (120, usableH,
                                (int) std::lround (w * (double) Theme::M::designH
                                                     / (double) Theme::M::designW));
    return { w, h };
}

juce::Point<int> JMSignatureEditor::preferredSize()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    if (displays.displays.isEmpty())                     // headless / offscreen render
        return { Theme::M::designW, Theme::M::designH };
    return preferredSizeForArea (displays.getPrimaryDisplay()->userBounds.toNearestInt());
}

JMSignatureEditor::JMSignatureEditor (JMSignatureProcessor& p)
    : juce::AudioProcessorEditor (p), proc_ (p)
{
    using L = Theme::L;
    setLookAndFeel (&lnf_);

    root_.setBounds (0, 0, Theme::M::designW, Theme::M::designH);
    addAndMakeVisible (root_);

    header_ = std::make_unique<HeaderBar> (proc_, [this] (int page) { setPage (page); });
    header_->setBounds (0, 0, Theme::M::designW, L::headerH);
    root_.addAndMakeVisible (*header_);

    browser_ = std::make_unique<PresetBrowserPanel> (proc_);
    browser_->setBounds (L::browserX, L::browserY, L::browserW, L::browserH);
    root_.addAndMakeVisible (*browser_);

    dock_ = std::make_unique<PerformDock> (proc_);
    dock_->setBounds (L::dockX, L::dockY, L::dockW, L::dockH);
    root_.addAndMakeVisible (*dock_);

    rack_ = std::make_unique<FxRackStrip> (proc_);
    rack_->setBounds (L::rackX, L::rackY, L::rackW, L::rackH);
    root_.addAndMakeVisible (*rack_);

    bar_ = std::make_unique<BottomBar> (proc_, [this]
    {
        showKeyboard_ = ! showKeyboard_;
        if (showKeyboard_) closeEditPage();          // the keyboard and the page share the band
        keyboard_->setVisible (showKeyboard_);
        bar_->setKeyboardEngaged (showKeyboard_);
    });
    root_.onOpenEditPage = [this] (int tab, int macroIdx) { showEditPage (tab, macroIdx); };
    root_.onCloseEditPage = [this] { closeEditPage(); };
    bar_->setBounds (0, L::barY, Theme::M::designW, L::barH);
    root_.addAndMakeVisible (*bar_);

    // R4(2): the on-screen keyboard now carries a spring-return PITCH wheel and
    // a MOD wheel (CC1) beside the keys, as on a real keyboard.
    keyboard_ = std::make_unique<KeyboardStrip> (proc_);
    keyboard_->setBounds (L::rackX, L::rackY + 40, L::rackW, L::rackH - 60);
    keyboard_->setVisible (false);
    root_.addChildComponent (*keyboard_);      // shown only via the KEYBOARD button

    proc_.stateChanged.addChangeListener (this);

    // CLIENT REVIEW ROUND 3 (E): the window may now be MAXIMISED. A fixed
    // aspect-ratio constrainer would have stopped a maximised window from
    // filling the screen, so the ratio lock is gone; the root still scales
    // UNIFORMLY (never stretched) and the remainder is chassis letterbox, which
    // is exactly what maximising should look like. The limits are widened so a
    // large display can be filled.
    setResizable (true, true);
    // R6(3): the opening size is the design size CLAMPED TO THE DISPLAY, and the
    // lower resize limit has to come down with it — a 1152x768 minimum is itself
    // taller than the usable area of a 1366x768 laptop, so leaving it fixed would
    // just move the overhang from "at launch" to "the first time you resize".
    const auto opening = preferredSize();
    setResizeLimits (juce::jmin (Theme::M::minW, opening.x),
                     juce::jmin (Theme::M::minH, opening.y),
                     Theme::M::maxW, Theme::M::maxH);
    setSize (opening.x, opening.y);
    setPage (juce::jlimit (0, 4, proc_.state.currentPage));

    // TEST BUILDS ONLY. Constructed last so it is the frontmost child, and only
    // when this build is gated at all; a release build never makes one.
    if (proc_.testerGated())
    {
        gate_ = std::make_unique<TesterGatePanel> (proc_);
        addAndMakeVisible (*gate_);
        gate_->setVisible (TesterGatePanel::shouldShow (proc_));
        gate_->setBounds (getLocalBounds());
    }

    startTimerHz (15);

    // FIRST LAUNCH: no instruments anywhere means the library has not been
    // installed yet, so the panel that installs it is the first thing shown.
    // (The self-checks run against the test library and never reach this.)
    if (proc_.state.library.bundles().empty())
    {
        juce::Component::SafePointer<JMSignatureEditor> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr || ! safe->proc_.state.library.bundles().empty()) return;
            safe->root_.showOverlay (std::make_unique<LibraryInstallPanel> (safe->proc_),
                                     LibraryInstallPanel::kWidth, LibraryInstallPanel::kHeight);
        });
    }
}

JMSignatureEditor::~JMSignatureEditor()
{
    proc_.stateChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void JMSignatureEditor::setPage (int page)
{
    proc_.state.currentPage = page;
    header_->setPage (page);
    rebuildCenter();
    if (browser_ != nullptr) browser_->rebuild();
    if (rack_ != nullptr) rack_->rebuild();
    repaint();                                   // the end cheeks carry the page's kente
}

void JMSignatureEditor::rebuildCenter()
{
    const int page = proc_.state.currentPage;
    modelPage_.reset();
    performPage_.reset();
    if (page >= 0 && page <= 3)
    {
        // model page: centre stage + the persistent PERFORM dock on the right
        modelPage_ = std::make_unique<ModelPagePanel> (proc_, page);
        root_.addAndMakeVisible (*modelPage_);
        dock_->setVisible (true);
    }
    else
    {
        // CLIENT CORRECTION: on PERFORM the four slots move to the middle, so
        // the right dock steps aside and the centre stage hosts the 2x2 grid.
        performPage_ = std::make_unique<PerformCenterPanel> (proc_, [this] { showSetlist (true); });
        root_.addAndMakeVisible (*performPage_);
        dock_->setVisible (false);
    }
    layoutRoot (rootW_);                          // the new page takes the current width
    header_->toFront (false);
    if (editPage_ != nullptr) editPage_->toFront (false);
    if (keyboard_ != nullptr) keyboard_->toFront (false);
    if (proc_.setlist.viewOpen && setlist_ == nullptr) showSetlist (true);
    if (setlist_ != nullptr) setlist_->toFront (false);
    if (root_.scrim_ != nullptr) root_.scrim_->toFront (false);      // modal stays on top
    if (root_.overlay_ != nullptr) root_.overlay_->toFront (false);
}

void JMSignatureEditor::showSetlist (bool on)
{
    proc_.setlist.viewOpen = on;
    if (on)
    {
        if (setlist_ == nullptr)
        {
            setlist_ = std::make_unique<SetlistPanel> (proc_, [this] { showSetlist (false); });
            root_.addAndMakeVisible (*setlist_);
        }
        setlist_->setBounds (0, 0, rootW_, Theme::M::designH);
        setlist_->toFront (false);
        if (root_.scrim_ != nullptr) root_.scrim_->toFront (false);
        if (root_.overlay_ != nullptr) root_.overlay_->toFront (false);
        setlist_->refresh();
    }
    else
        setlist_.reset();
}

void JMSignatureEditor::showEditPage (int tab, int macroIdx)
{
    using L = Theme::L;
    if (editPage_ == nullptr)
    {
        editPage_ = std::make_unique<EditPage> (proc_, [this] { closeEditPage(); });
        root_.addAndMakeVisible (*editPage_);
        editPage_->setBounds (L::rackX, L::rackY, rootW_ - 12 - L::rackX, L::rackH);
    }
    // the page takes the band the keyboard would use
    if (showKeyboard_)
    {
        showKeyboard_ = false;
        keyboard_->setVisible (false);
        bar_->setKeyboardEngaged (false);
    }
    editPage_->open (tab, macroIdx);
    editPage_->toFront (false);
    if (setlist_ != nullptr) setlist_->toFront (false);
    if (root_.scrim_ != nullptr) root_.scrim_->toFront (false);
    if (root_.overlay_ != nullptr) root_.overlay_->toFront (false);
    bar_->setEditTab (editPage_->tab());
}

void JMSignatureEditor::closeEditPage()
{
    editPage_.reset();
    if (bar_ != nullptr) bar_->setEditTab (-1);
    if (rack_ != nullptr) rack_->rebuild();        // the rack comes back as it is now
}

void JMSignatureEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // model state changed (bundle/preset/rack edits): rebuild the affected views
    rebuildCenter();
    if (performPage_ != nullptr) performPage_->rebuild();
    if (dock_ != nullptr) dock_->rebuild();
    if (rack_ != nullptr) rack_->rebuild();
    if (browser_ != nullptr) browser_->rebuild();
}

void JMSignatureEditor::timerCallback()
{
    header_->refresh();
    if (dock_ != nullptr && dock_->isVisible()) dock_->refresh();
    if (rack_ != nullptr) rack_->refresh();
    if (bar_ != nullptr) bar_->refresh();
    if (modelPage_ != nullptr) modelPage_->refresh();
    if (performPage_ != nullptr) performPage_->refresh();
    // the super knob fans out to its macros, and a program change steps the
    // setlist, from here: both are message-thread work that must run on
    // every page, not only while PERFORM is showing
    proc_.pollSuperKnob();
    if (editPage_ != nullptr) editPage_->refresh();
    if (keyboard_ != nullptr && showKeyboard_) keyboard_->followMidi();
    if (proc_.pollPendingProgram()) return;           // the load rebuilt the centre
    if (setlist_ != nullptr) setlist_->refresh();
}

void JMSignatureEditor::paint (juce::Graphics& g)
{
    // The root is 3:2 and scales uniformly, so a 16:9 screen leaves a band at
    // each side. Those bands are the instrument's end cheeks, not empty space:
    // the same woven chassis as the panels, falling off into shadow at the
    // outer edge, with a gold hairline where the panel meets the cheek
    // (client, 2026-11-01: "empty spaces on the left and right").
    g.fillAll (Theme::chassis);
    const float scale = rootScale();
    const float rootW = (float) rootW_ * scale, rootH = Theme::M::designH * scale;
    const float ox = ((float) getWidth() - rootW) * 0.5f;
    const float oy = ((float) getHeight() - rootH) * 0.5f;
    auto cheek = [&] (juce::Rectangle<float> band, bool left)
    {
        if (band.getWidth() < 1.0f || band.getHeight() < 1.0f) return;
        juce::ColourGradient fade (juce::Colour (0xff14110D), left ? band.getRight() : band.getX(), 0.0f,
                                   juce::Colour (0xff050504), left ? band.getX() : band.getRight(), 0.0f, false);
        g.setGradientFill (fade);
        g.fillRect (band);
        // the page's own kente, dimmed, so the cheek belongs to the panel it
        // flanks rather than reading as a black bar
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (band.toNearestInt());
            g.setOpacity (0.28f);
            g.drawImageWithin (ux::kenteRaster (juce::jlimit (0, 3, proc_.state.currentPage)),
                               (int) band.getX(), (int) band.getY(), (int) band.getWidth(), (int) band.getHeight(),
                               juce::RectanglePlacement::fillDestination, false);
        }
        juce::ColourGradient dim (juce::Colours::transparentBlack, left ? band.getRight() : band.getX(), 0.0f,
                                  juce::Colours::black.withAlpha (0.75f), left ? band.getX() : band.getRight(), 0.0f, false);
        g.setGradientFill (dim);
        g.fillRect (band);
        ux::drawMicroWeave (g, band, 0.12f, 0.0f);
        g.setColour (Theme::goldDark.withAlpha (0.6f));
        g.fillRect (left ? band.getRight() - 1.0f : band.getX(), band.getY(), 1.0f, band.getHeight());
    };
    cheek (juce::Rectangle<float> (0.0f, 0.0f, ox, (float) getHeight()), true);
    cheek (juce::Rectangle<float> (ox + rootW, 0.0f, (float) getWidth() - ox - rootW, (float) getHeight()), false);
    if (oy >= 1.0f)
    {
        g.setColour (Theme::goldDark.withAlpha (0.35f));
        g.fillRect (ox, oy - 1.0f, rootW, 1.0f);
        g.fillRect (ox, oy + rootH, rootW, 1.0f);
    }
}

float JMSignatureEditor::rootScale() const
{
    return juce::jmin ((float) getWidth() / (float) Theme::M::designW,
                       (float) getHeight() / (float) Theme::M::designH);
}

void JMSignatureEditor::layoutRoot (int rootW)
{
    using L = Theme::L;
    rootW_ = rootW;
    root_.setBounds (0, 0, rootW, Theme::M::designH);
    // The extra width is shared: 45 % to the centre stage (its knob surface
    // spreads), the rest to the PERFORM dock, both anchored where they always
    // were. The browser keeps its column; the rack and the bars span the lot.
    const int extra = rootW - Theme::M::designW;
    const int centerExtra = juce::jmin (extra * 45 / 100, 460);
    const int centerW = L::centerW + centerExtra;
    const int dockX = L::centerX + centerW + 6;
    const int dockW = rootW - 12 - dockX;
    const int rackW = rootW - 12 - L::rackX;

    header_->setBounds (0, 0, rootW, L::headerH);
    browser_->setBounds (L::browserX, L::browserY, L::browserW, L::browserH);
    dock_->setBounds (dockX, L::dockY, dockW, L::dockH);
    rack_->setBounds (L::rackX, L::rackY, rackW, L::rackH);
    bar_->setBounds (0, L::barY, rootW, L::barH);
    bar_->setStageWidth (centerW);
    if (keyboard_ != nullptr) keyboard_->setBounds (L::rackX, L::rackY + 40, rackW, L::rackH - 60);
    if (editPage_ != nullptr) editPage_->setBounds (L::rackX, L::rackY, rackW, L::rackH);
    if (modelPage_ != nullptr) modelPage_->setBounds (L::centerX, L::centerY, centerW, L::centerH);
    if (performPage_ != nullptr) performPage_->setBounds (L::centerX, L::centerY, rootW - 12 - L::centerX, L::centerH);
    if (setlist_ != nullptr) setlist_->setBounds (0, 0, rootW, Theme::M::designH);
}

void JMSignatureEditor::resized()
{
    // root scaling law (kept from V2, and what makes maximise safe in R3/E):
    // uniform scale, centred, never stretched. The root's WIDTH follows the
    // window (see Theme::M::maxDesignW); whatever is left is chassis.
    const float scale = rootScale();
    const int rootW = juce::jlimit (Theme::M::designW, Theme::M::maxDesignW,
                                    (int) std::floor ((float) getWidth() / scale));
    layoutRoot (rootW);
    const float ox = ((float) getWidth() - (float) rootW * scale) * 0.5f;
    const float oy = ((float) getHeight() - Theme::M::designH * scale) * 0.5f;
    root_.setTransform (juce::AffineTransform::scale (scale).translated (ox, oy));

    // TEST BUILDS ONLY: the key screen is a sibling of the scaled root, not a
    // child of it, so it fills the window at 1:1 and is not letterboxed with the
    // chassis.
    if (gate_ != nullptr) gate_->setBounds (getLocalBounds());
}

} // namespace jm
