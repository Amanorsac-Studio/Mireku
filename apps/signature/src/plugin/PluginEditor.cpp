#include "PluginEditor.h"

namespace jm {

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
        keyboard_->setVisible (showKeyboard_);
    });
    bar_->setBounds (0, L::barY, Theme::M::designW, L::barH);
    root_.addAndMakeVisible (*bar_);

    keyboard_ = std::make_unique<juce::MidiKeyboardComponent> (proc_.keyboardState,
                                                               juce::MidiKeyboardComponent::horizontalKeyboard);
    keyboard_->setKeyWidth (22.0f);
    keyboard_->setBounds (L::rackX, L::rackY + 40, L::rackW, L::rackH - 60);
    keyboard_->setVisible (false);
    root_.addChildComponent (*keyboard_);      // shown only via the KEYBOARD button

    proc_.stateChanged.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (Theme::M::minW, Theme::M::minH, Theme::M::maxW, Theme::M::maxH);
    getConstrainer()->setFixedAspectRatio ((double) Theme::M::designW / (double) Theme::M::designH);
    setSize (Theme::M::designW, Theme::M::designH);
    setPage (juce::jlimit (0, 4, proc_.state.currentPage));
    startTimerHz (15);
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
}

void JMSignatureEditor::rebuildCenter()
{
    using L = Theme::L;
    const int page = proc_.state.currentPage;
    modelPage_.reset();
    performPage_.reset();
    if (page >= 0 && page <= 3)
    {
        // model page: centre stage + the persistent PERFORM dock on the right
        modelPage_ = std::make_unique<ModelPagePanel> (proc_, page);
        modelPage_->setBounds (L::centerX, L::centerY, L::centerW, L::centerH);
        root_.addAndMakeVisible (*modelPage_);
        dock_->setVisible (true);
    }
    else
    {
        // CLIENT CORRECTION: on PERFORM the four slots move to the middle, so
        // the right dock steps aside and the centre stage hosts the 2x2 grid.
        performPage_ = std::make_unique<PerformCenterPanel> (proc_);
        performPage_->setBounds (L::centerX, L::centerY, L::centerW + L::dockW + 6, L::centerH);
        root_.addAndMakeVisible (*performPage_);
        dock_->setVisible (false);
    }
    header_->toFront (false);
    if (keyboard_ != nullptr) keyboard_->toFront (false);
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
}

void JMSignatureEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::chassis);      // letterbox
}

void JMSignatureEditor::resized()
{
    // root scaling law (kept from V2): uniform contain, centred, never stretched
    const float scale = juce::jmin ((float) getWidth() / (float) Theme::M::designW,
                                    (float) getHeight() / (float) Theme::M::designH);
    const float ox = ((float) getWidth() - Theme::M::designW * scale) * 0.5f;
    const float oy = ((float) getHeight() - Theme::M::designH * scale) * 0.5f;
    root_.setTransform (juce::AffineTransform::scale (scale).translated (ox, oy));
}

} // namespace jm
