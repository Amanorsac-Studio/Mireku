#include "PluginEditor.h"

namespace jm {

JMSignatureEditor::JMSignatureEditor (JMSignatureProcessor& p)
    : juce::AudioProcessorEditor (p), proc_ (p)
{
    setLookAndFeel (&lnf_);

    root_.setBounds (0, 0, Theme::M::designW, Theme::M::designH);
    addAndMakeVisible (root_);

    header_ = std::make_unique<HeaderBar> (proc_, [this] (int page) { setPage (page); });
    header_->setBounds (0, 0, Theme::M::designW, Theme::V2::contentY);
    root_.addAndMakeVisible (*header_);

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
}

void JMSignatureEditor::rebuildCenter()
{
    const int page = proc_.state.currentPage;
    modelPage_.reset();
    performPage_.reset();
    if (page >= 0 && page <= 3)
    {
        modelPage_ = std::make_unique<ModelPagePanel> (proc_, page);
        modelPage_->setBounds (0, 0, Theme::M::designW, Theme::M::designH);
        root_.addAndMakeVisible (*modelPage_);
    }
    else
    {
        performPage_ = std::make_unique<PerformPagePanel> (proc_);
        performPage_->setBounds (0, 0, Theme::M::designW, Theme::M::designH);
        root_.addAndMakeVisible (*performPage_);
    }
    header_->toFront (false);
}

void JMSignatureEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // model state changed (bundle/preset/rack edits): rebuild the current view
    rebuildCenter();
    if (performPage_ != nullptr) performPage_->rebuild();
}

void JMSignatureEditor::timerCallback()
{
    header_->refresh();
    if (modelPage_ != nullptr) modelPage_->refresh();
    if (performPage_ != nullptr) performPage_->refresh();
}

void JMSignatureEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::chassis);      // letterbox
}

void JMSignatureEditor::resized()
{
    // V2 root scaling law: uniform contain, centered, never stretched
    const float scale = juce::jmin ((float) getWidth() / (float) Theme::M::designW,
                                    (float) getHeight() / (float) Theme::M::designH);
    const float ox = ((float) getWidth() - Theme::M::designW * scale) * 0.5f;
    const float oy = ((float) getHeight() - Theme::M::designH * scale) * 0.5f;
    root_.setTransform (juce::AffineTransform::scale (scale).translated (ox, oy));
}

} // namespace jm
