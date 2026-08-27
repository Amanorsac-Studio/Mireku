#include "PluginEditor.h"

namespace jm {

JMSignatureEditor::JMSignatureEditor (JMSignatureProcessor& p)
    : juce::AudioProcessorEditor (p), proc_ (p)
{
    setLookAndFeel (&lnf_);

    topBar_ = std::make_unique<TopBar> (proc_, [this] (int page) { setPage (page); });
    addAndMakeVisible (*topBar_);
    browser_ = std::make_unique<PresetBrowserPanel> (proc_);
    addAndMakeVisible (*browser_);
    dock_ = std::make_unique<PerformDock> (proc_);
    addAndMakeVisible (*dock_);
    rack_ = std::make_unique<FxRackStrip> (proc_);
    addAndMakeVisible (*rack_);
    status_ = std::make_unique<StatusBar> (proc_, [this]
    {
        showKeyboard_ = ! showKeyboard_;
        keyboard_->setVisible (showKeyboard_);
        resized();
    });
    addAndMakeVisible (*status_);
    keyboard_ = std::make_unique<juce::MidiKeyboardComponent> (proc_.keyboardState,
                                                               juce::MidiKeyboardComponent::horizontalKeyboard);
    keyboard_->setKeyWidth (14.0f);
    keyboard_->setVisible (false);
    addAndMakeVisible (*keyboard_);

    proc_.stateChanged.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (1152, 768, 3072, 2048);   // UX pack reference viewport 1536x1024
    getConstrainer()->setFixedAspectRatio ((double) Theme::M::designW / (double) Theme::M::designH);
    setSize (Theme::M::designW, Theme::M::designH);
    setPage (juce::jlimit (0, 4, proc_.state.currentPage));
    topBar_->setPage (proc_.state.currentPage);
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
    rebuildCenter();
    browser_->rebuild();
    rack_->rebuild();
    resized();
}

void JMSignatureEditor::rebuildCenter()
{
    const int page = proc_.state.currentPage;
    modelPage_.reset();
    performPage_.reset();
    if (page >= 0 && page <= 3)
    {
        modelPage_ = std::make_unique<ModelPagePanel> (proc_, page);
        addAndMakeVisible (*modelPage_);
    }
    else
    {
        performPage_ = std::make_unique<PerformCenterPanel> (proc_);
        addAndMakeVisible (*performPage_);
    }
    resized();
}

void JMSignatureEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // model state changed (bundle/preset/rack edits): rebuild the affected views
    rebuildCenter();
    rack_->rebuild();
    browser_->rebuild();
    dock_->rebuild();
}

void JMSignatureEditor::timerCallback()
{
    topBar_->refresh();
    dock_->refresh();
    rack_->refresh();
    status_->refresh();
    if (modelPage_ != nullptr) modelPage_->refresh();
    if (performPage_ != nullptr) performPage_->refresh();
}

void JMSignatureEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::chassis);
}

void JMSignatureEditor::resized()
{
    const float scale = (float) getWidth() / (float) Theme::M::designW;
    auto S = [scale] (int v) { return juce::roundToInt ((float) v * scale); };

    auto r = getLocalBounds();
    topBar_->setBounds (r.removeFromTop (S (Theme::M::topBarH)));
    status_->setBounds (r.removeFromBottom (S (Theme::M::statusBarH)));
    if (showKeyboard_)
        keyboard_->setBounds (r.removeFromBottom (S (72)));
    else
        keyboard_->setBounds ({});
    auto rackArea = r.removeFromBottom (S (Theme::M::fxStripH));
    rack_->setBounds (rackArea.reduced (S (4)));
    browser_->setBounds (r.removeFromLeft (S (Theme::M::leftColW)).reduced (S (4)));
    dock_->setBounds (r.removeFromRight (S (Theme::M::rightColW)).reduced (S (4)));
    auto center = r.reduced (S (4));
    if (modelPage_ != nullptr) modelPage_->setBounds (center);
    if (performPage_ != nullptr) performPage_->setBounds (center);
}

} // namespace jm
