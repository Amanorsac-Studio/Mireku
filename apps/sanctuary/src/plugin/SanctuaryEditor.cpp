// SanctuaryEditor.cpp — see header.
#include "SanctuaryEditor.h"

namespace sanctuary {

SanctuaryEditor::SanctuaryEditor (SanctuaryProcessor& p)
    : AudioProcessorEditor (p), proc_ (p), stage_ (p), edit_ (p),
      keyboard_ (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lnf_);
    addAndMakeVisible (stage_);
    addChildComponent (edit_);
    addChildComponent (keyboard_);
    keyboard_.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, Theme::text);
    keyboard_.setColour (juce::MidiKeyboardComponent::blackNoteColourId, Theme::recess);
    keyboard_.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::blue.withAlpha (0.7f));
    keyboard_.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::blue.withAlpha (0.3f));
    keyboard_.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::line);
    keyboard_.setAvailableRange (24, 108);

    for (auto* b : { &stageBtn_, &editBtn_, &deckBtn_, &panicBtn_, &kbBtn_ }) addAndMakeVisible (b);
    stageBtn_.onClick = [this] { showPage (0); };
    editBtn_.onClick = [this] { showPage (1); };
    deckBtn_.onClick = [this] { proc_.deck = true; proc_.stageChanged.sendChangeMessage(); };
    panicBtn_.onClick = [this] { proc_.panic(); };
    kbBtn_.setClickingTogglesState (true);
    kbBtn_.onClick = [this] { keyboardVisible_ = kbBtn_.getToggleState(); resized(); };

    addChildComponent (resumeBtn_); addChildComponent (discardBtn_);
    resumeBtn_.onClick = [this] { proc_.resumeSession(); resized(); repaint(); };
    discardBtn_.onClick = [this] { proc_.discardSession(); resized(); repaint(); };

    master_.setSliderStyle (juce::Slider::LinearHorizontal);
    master_.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    master_.setRange (-60.0, 6.0, 0.1);
    master_.setValue (proc_.apvts.getRawParameterValue ("masterVol")->load(), juce::dontSendNotification);
    master_.onValueChange = [this] { if (auto* pr = proc_.apvts.getParameter ("masterVol")) pr->setValueNotifyingHost (pr->convertTo0to1 ((float) master_.getValue())); };
    master_.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (master_);
    meterL_.vertical = false; meterR_.vertical = false;
    meterL_.read = [this] { return proc_.masterPeakL.load(); };
    meterR_.read = [this] { return proc_.masterPeakR.load(); };
    addAndMakeVisible (meterL_); addAndMakeVisible (meterR_);

    proc_.stageChanged.addChangeListener (this);
    setResizable (true, true);
    setResizeLimits (960, 620, 4096, 2400);
    setSize (Theme::baseW, Theme::baseH);
    setWantsKeyboardFocus (true);
    edit_.settings().audioSettingsHook = [] { if (auto& f = SanctuaryEditor::audioSettingsHook()) f(); };
    showPage (0);
    updateDeck();
    startTimerHz (10);
}

SanctuaryEditor::~SanctuaryEditor()
{
    proc_.stageChanged.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void SanctuaryEditor::showPage (int page)
{
    page_ = page;
    stage_.setVisible (page == 0); edit_.setVisible (page == 1);
    stageBtn_.setToggleState (page == 0, juce::dontSendNotification);
    editBtn_.setToggleState (page == 1, juce::dontSendNotification);
}

void SanctuaryEditor::updateDeck()
{
    if (proc_.deck && deck_ == nullptr)
    {
        deck_ = std::make_unique<DeckView> (proc_);
        addAndMakeVisible (*deck_);
        deck_->setBounds (getLocalBounds());
        deck_->toFront (true);
        deck_->grabKeyboardFocus();
    }
    else if (! proc_.deck && deck_ != nullptr) deck_.reset();
}

void SanctuaryEditor::changeListenerCallback (juce::ChangeBroadcaster*) { updateDeck(); repaint (header_); }

void SanctuaryEditor::timerCallback()
{
    const float mv = proc_.apvts.getRawParameterValue ("masterVol")->load();
    if (std::abs (mv - (float) master_.getValue()) > 0.05f) master_.setValue (mv, juce::dontSendNotification);
    repaint (status_);
    const bool banner = proc_.sessionAvailable();
    if (banner != resumeBtn_.isVisible()) { resumeBtn_.setVisible (banner); discardBtn_.setVisible (banner); resized(); repaint(); }
}

void SanctuaryEditor::resized()
{
    auto r = getLocalBounds();
    if (deck_ != nullptr) deck_->setBounds (r);
    header_ = r.removeFromTop (Theme::headerH);
    bottom_ = r.removeFromBottom (Theme::bottomH);
    if (proc_.sessionAvailable())
    {
        banner_ = r.removeFromTop (36);
        auto b = banner_.reduced (12, 5);
        discardBtn_.setBounds (b.removeFromRight (120)); b.removeFromRight (6); resumeBtn_.setBounds (b.removeFromRight (180));
    }
    else banner_ = {};
    if (keyboardVisible_) { keyboard_.setBounds (r.removeFromBottom (72)); keyboard_.setVisible (true); } else keyboard_.setVisible (false);
    stage_.setBounds (r); edit_.setBounds (r);

    auto h = header_.reduced (12, 10);
    h.removeFromLeft (170);                       // brand
    stageBtn_.setBounds (h.removeFromLeft (80)); h.removeFromLeft (6);
    editBtn_.setBounds (h.removeFromLeft (80)); h.removeFromLeft (6);
    deckBtn_.setBounds (h.removeFromLeft (80));
    auto right = h.removeFromRight (300);
    auto meters = right.removeFromRight (120).reduced (0, 6);
    meterL_.setBounds (meters.removeFromTop (meters.getHeight() / 2).reduced (0, 1));
    meterR_.setBounds (meters.reduced (0, 1));
    right.removeFromRight (8);
    master_.setBounds (right.removeFromRight (140));
    status_ = h;

    auto b = bottom_.reduced (12, 7);
    kbBtn_.setBounds (b.removeFromLeft (100)); b.removeFromLeft (6);
    panicBtn_.setBounds (b.removeFromRight (100));
}

void SanctuaryEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    // header
    g.setColour (Theme::panel); g.fillRect (header_);
    g.setColour (Theme::line); g.fillRect (header_.removeFromBottom (1));
    auto brand = header_.reduced (14, 8).removeFromLeft (160);
    auto mark = brand.removeFromLeft (30).withSizeKeepingCentre (22, 22).toFloat();
    juce::Path arch; arch.addRoundedRectangle (mark, 4.0f);
    g.setColour (Theme::blueDeep); g.fillPath (arch);
    g.setColour (Theme::blueHi); g.drawRoundedRectangle (mark, 4.0f, 1.0f);
    g.setColour (Theme::ice); g.setFont (Theme::display (13.0f)); g.drawText ("S", mark.toNearestInt(), juce::Justification::centred);
    brand.removeFromLeft (8);
    drawDisplayText (g, "SANCTUARY", brand.removeFromTop (brand.getHeight() / 2 + 2), 15.0f, Theme::ice);
    paint::label (g, brand, "Amanorsac Studio", 9.0f, Theme::textDim);
    // status
    g.setColour (Theme::textDim); g.setFont (Theme::mono (11.0f));
    juce::String st = proc_.set.name + "   ·   " + proc_.currentSongTitle();
    st += "   ·   cpu " + juce::String ((int) (proc_.cpuLoad.load() * 100)) + "%   voices " + juce::String (proc_.totalVoices.load());
    if (proc_.overruns.load() > 0) st += "   ·   overruns " + juce::String (proc_.overruns.load());
    g.drawText (st, status_.reduced (16, 0), juce::Justification::centredLeft);
    paint::label (g, master_.getBounds().translated (0, -14).withHeight (12), "Master", 9.0f);
    // banner
    if (! banner_.isEmpty())
    {
        g.setColour (Theme::blueDeep.withAlpha (0.35f)); g.fillRect (banner_);
        g.setColour (Theme::ice); g.setFont (Theme::ui (13.0f));
        g.drawText ("The last session did not close cleanly (" + proc_.sessionName + ").", banner_.reduced (16, 0), juce::Justification::centredLeft);
    }
    // bottom
    g.setColour (Theme::panel); g.fillRect (bottom_);
    g.setColour (Theme::line); g.fillRect (bottom_.removeFromTop (1));
    g.setColour (Theme::textMute); g.setFont (Theme::ui (11.0f));
    g.drawText ("F1 stage · F2 edit · F3 deck · space next moment", bottom_.reduced (130, 0), juce::Justification::centred);
}

bool SanctuaryEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::F1Key) { showPage (0); return true; }
    if (k == juce::KeyPress::F2Key) { showPage (1); return true; }
    if (k == juce::KeyPress::F3Key) { proc_.deck = true; proc_.stageChanged.sendChangeMessage(); return true; }
    if (k == juce::KeyPress::spaceKey && page_ == 0) { if (k.getModifiers().isShiftDown()) proc_.prevMoment(); else proc_.nextMoment(); return true; }
    if (k == juce::KeyPress::pageDownKey) { proc_.nextSong(); return true; }
    if (k == juce::KeyPress::pageUpKey) { proc_.prevSong(); return true; }
    return false;
}

} // namespace sanctuary
