// EditPage.h — THE EDIT PAGE (client, 2026-11-03).
//
// "Mod matrix, LFO, ARP and Macro edits: I wish they didn't pop out, rather
// they open a page that replaces the down section where the effects are, like
// the way the keyboard does." This is that page. It sits in the rack band, the
// effects rack comes back when it closes, and one strip of tabs across its top
// moves between MACROS, MOD MATRIX, LFO, ARP and SUPER KNOB without leaving
// the page. On MACROS a second strip picks the macro (double-clicking a macro
// knob lands here with that macro chosen). The panels are the same ones that
// used to open as overlays, docked: no plaque, no title, no CLOSE chip of
// their own, laid out wide and short across the band.
#pragma once
#include "Perform.h"

namespace jm {

class EditPage : public juce::Component
{
public:
    static constexpr int kTabH = 36;

    EditPage (JMSignatureProcessor& p, std::function<void()> onClose)
        : proc_ (p), onClose_ (std::move (onClose))
    {
        const char* names[] = { "MACROS", "MOD MATRIX", "LFO", "ARP", "SUPER KNOB" };
        addAndMakeVisible (strip_);
        for (int i = 0; i < 5; ++i)
        {
            auto* b = tabs_.add (new ux::TabCellButton (names[i]));
            b->setRadioGroupId (7401);
            b->setClickingTogglesState (true);
            b->setFirstCell (i == 0);
            b->setFontHeight (12.0f);
            b->onClick = [this, i] { open (i, macro_); };
            strip_.addAndMakeVisible (b);
        }
        addAndMakeVisible (macroStrip_);
        for (int i = 0; i < 4; ++i)
        {
            auto* b = macroTabs_.add (new ux::TabCellButton (juce::String (i + 1)));
            b->setRadioGroupId (7402);
            b->setClickingTogglesState (true);
            b->setFirstCell (i == 0);
            b->setFontHeight (12.0f);
            b->onClick = [this, i] { open (EditPageHost::macros, i); };
            macroStrip_.addAndMakeVisible (b);
        }
        close_.setFontHeight (12.0f);
        close_.setTooltip ("Back to the effects rack");
        close_.onClick = [this] { if (onClose_) onClose_(); };
        addAndMakeVisible (close_);
    }

    int tab() const { return tab_; }
    int macro() const { return macro_; }

    /// Shows `tab`; on MACROS, `macroIdx` picks the macro (-1 keeps the last).
    void open (int tab, int macroIdx = -1)
    {
        tab = juce::jlimit (0, 4, tab);
        if (macroIdx >= 0) macro_ = juce::jlimit (0, 3, macroIdx);
        const bool same = tab == tab_ && panel_ != nullptr
                          && (tab != EditPageHost::macros || macroEditor_ == nullptr || macroEditor_->macroIndex() == macro_);
        tab_ = tab;
        tabs_[tab_]->setToggleState (true, juce::dontSendNotification);
        macroTabs_[macro_]->setToggleState (true, juce::dontSendNotification);
        macroStrip_.setVisible (tab_ == EditPageHost::macros);
        if (! same) build();
        refresh();                                   // the macro names on the strip, now
        resized();
        repaint();
    }

    /// Timer: the live parts.
    void refresh()
    {
        if (superPanel_ != nullptr) superPanel_->refresh();
        if (macroEditor_ != nullptr && macroEditor_->macroIndex() == macro_)
        {
            const int n = (int) proc_.state.macros[(size_t) macro_].targets.size();
            if (n != macroRows_) { macroRows_ = n; macroEditor_->rebuild(); }
        }
        if (matrixPanel_ != nullptr)
        {
            const int n = juce::jlimit (1, kMaxMatrixRows, proc_.state.mod.rowCount);
            if (n != matrixRows_) { matrixRows_ = n; matrixPanel_->rebuild(); }
        }
        // the macro names on the strip follow renames
        for (int i = 0; i < 4; ++i)
        {
            const auto& m = proc_.state.macros[(size_t) i];
            const auto want = m.name.isNotEmpty() ? m.name.toUpperCase() : "MACRO " + juce::String (i + 1);
            if (macroTabs_[i]->getButtonText() != want) { macroTabs_[i]->setButtonText (want); macroTabs_[i]->repaint(); }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);
        auto head = r.removeFromTop (kTabH);
        close_.setBounds (head.removeFromRight (96).withSizeKeepingCentre (84, 26));
        strip_.setBounds (head.removeFromLeft (juce::jmin (640, head.getWidth() - 120)).reduced (0, 3));
        const int cw = strip_.getWidth() / 5;
        for (int i = 0; i < 5; ++i) tabs_[i]->setBounds (i * cw, 0, cw, strip_.getHeight());
        head.removeFromLeft (24);
        macroStrip_.setBounds (head.removeFromLeft (juce::jmin (480, head.getWidth())).reduced (0, 3));
        const int mw = macroStrip_.getWidth() / 4;
        for (int i = 0; i < 4; ++i) macroTabs_[i]->setBounds (i * mw, 0, mw, macroStrip_.getHeight());
        r.removeFromTop (4);
        content_ = r;
        if (panel_ != nullptr) panel_->setBounds (content_);
    }

    void paint (juce::Graphics& g) override
    {
        // the same body as the keyboard strip: the band's own dark plate with a
        // gold hairline, no plaque inside it
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient body (juce::Colour (0xff110F0C), 0.0f, r.getY(),
                                   juce::Colour (0xff070706), 0.0f, r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 8.0f);
        ux::drawMicroWeave (g, r.reduced (2.0f), 0.07f, 8.0f);
        g.setColour (Theme::goldDark.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        kit::drawDivider (g, { r.getX() + 10.0f, (float) (kTabH + 6), r.getWidth() - 20.0f, 3.0f }, 0.5f);
    }

private:
    void build()
    {
        panel_ = nullptr;
        macroEditor_ = nullptr;
        matrixPanel_ = nullptr;
        superPanel_ = nullptr;
        owned_.reset();
        switch (tab_)
        {
            case EditPageHost::macros:
            {
                auto p = std::make_unique<MacroEditorPanel> (proc_, macro_);
                macroEditor_ = p.get();
                macroRows_ = (int) proc_.state.macros[(size_t) macro_].targets.size();
                p->setDocked (true);
                panel_ = p.get();
                owned_ = std::move (p);
                break;
            }
            case EditPageHost::matrix:
            {
                auto p = std::make_unique<MatrixPanel> (proc_);
                matrixPanel_ = p.get();
                matrixRows_ = juce::jlimit (1, kMaxMatrixRows, proc_.state.mod.rowCount);
                p->setDocked (true);
                panel_ = p.get();
                owned_ = std::move (p);
                break;
            }
            case EditPageHost::lfo:
            {
                auto p = std::make_unique<LfoPanel> (proc_);
                p->setDocked (true);
                panel_ = p.get();
                owned_ = std::move (p);
                break;
            }
            case EditPageHost::arp:
            {
                auto p = std::make_unique<ArpPanel> (proc_);
                p->setDocked (true);
                panel_ = p.get();
                owned_ = std::move (p);
                break;
            }
            default:
            {
                auto p = std::make_unique<SuperKnobPanel> (proc_);
                superPanel_ = p.get();
                p->setDocked (true);
                panel_ = p.get();
                owned_ = std::move (p);
                break;
            }
        }
        addAndMakeVisible (*panel_);
        panel_->setBounds (content_);
    }

    JMSignatureProcessor& proc_;
    std::function<void()> onClose_;
    ux::SegmentStrip strip_, macroStrip_;
    juce::OwnedArray<ux::TabCellButton> tabs_, macroTabs_;
    ux::PlateButton close_ { "CLOSE" };
    std::unique_ptr<juce::Component> owned_;
    juce::Component* panel_ = nullptr;
    MacroEditorPanel* macroEditor_ = nullptr;
    MatrixPanel* matrixPanel_ = nullptr;
    SuperKnobPanel* superPanel_ = nullptr;
    int macroRows_ = -1, matrixRows_ = -1;
    juce::Rectangle<int> content_;
    int tab_ = 0, macro_ = 0;
};

} // namespace jm
