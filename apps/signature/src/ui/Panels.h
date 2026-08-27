// Panels.h — preset browser, V2 model pages, FX unit panels + rack strip.
// Geometry follows UX pack V2 (docs/svg-pack-v2): tokens/design-tokens.json and
// specs/screen-layouts.json are implemented EXACTLY for the visible layout;
// build-spec functions that V2 screens do not place (sub-tab content, preset
// browser, FX rack) live in overlays on the recessed surface (logged).
#pragma once
#include "Widgets.h"
#include "KenteTexture.h"
#include "UxAssets.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace jm {

//==============================================================================
inline int slotForWorld (const AppState& st, int world)
{
    for (int i = 0; i < 4; ++i)
        if (st.slots[(size_t) i].world == world) return i;
    return world;   // default 1:1 mapping when no slot hosts the world
}

//==============================================================================
class PresetBrowserPanel : public juce::Component,
                           private juce::ListBoxModel
{
public:
    PresetBrowserPanel (JMSignatureProcessor& p) : proc_ (p)
    {
        title_.setText ("PRESET BROWSER", juce::dontSendNotification);
        title_.setFont (Theme::display (14.0f));
        title_.setColour (juce::Label::textColourId, Theme::gold);
        addAndMakeVisible (title_);

        search_.setTextToShowWhenEmpty ("Search presets...", Theme::muted);
        search_.onTextChange = [this] { rebuild(); };
        addAndMakeVisible (search_);

        filterBtn_ = assets::makeIconButton ("filter", assets::Icon::filter, Theme::muted);
        filterBtn_->onClick = [this]
        {
            juce::PopupMenu m;
            const char* names[] = { "User", "Factory", "Favorites" };
            for (int i = 0; i < 3; ++i)
                m.addItem (i + 1, names[i], true, tabs_[(size_t) i].getToggleState());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (filterBtn_.get()), [this] (int r)
            {
                if (r >= 1 && r <= 3)
                {
                    tabs_[(size_t) (r - 1)].setToggleState (true, juce::dontSendNotification);
                    rebuild();
                }
            });
        };
        addAndMakeVisible (*filterBtn_);

        const char* names[] = { "USER", "FACTORY", "FAVORITES" };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = tabs_[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (777);
            b.onClick = [this] { rebuild(); };
            addAndMakeVisible (b);
        }
        tabs_[1].setToggleState (true, juce::dontSendNotification);

        list_.setModel (this);
        list_.setRowHeight (26);
        addAndMakeVisible (list_);

        saveName_.setTextToShowWhenEmpty ("New preset name", Theme::muted);
        addAndMakeVisible (saveName_);
        saveBtn_.setButtonText ("SAVE");
        saveBtn_.onClick = [this] { doSave(); };
        addAndMakeVisible (saveBtn_);
        rebuild();
    }

    void rebuild()
    {
        rows_.clear();
        auto filter = search_.getText().trim();
        const int page = proc_.state.currentPage;
        const int mode = tabs_[0].getToggleState() ? 0 : tabs_[2].getToggleState() ? 2 : 1;
        auto add = [&] (Row r)
        {
            if (filter.isNotEmpty() && ! r.name.containsIgnoreCase (filter)) return;
            if (mode == 2 && ! proc_.state.library.isFavorite (r.favKey())) return;
            rows_.push_back (std::move (r));
        };
        if (page >= 0 && page <= 3)
        {
            const int slot = slotForWorld (proc_.state, page);
            if (mode != 0)
                for (auto& b : proc_.state.library.bundlesForWorld (page))
                    add ({ Row::Bundle, juce::String::fromUTF8 ("\xE2\x96\xA0 ") + b.name, b.dir, slot, -1 });
            if (mode != 0)
            {
                if (auto lb = proc_.engines[(size_t) slot].bundle())
                    for (size_t i = 0; i < lb->inst.presets.size(); ++i)
                        add ({ Row::FactoryPreset, juce::String (lb->inst.presets[i].name), {}, slot, (int) i });
            }
            if (mode != 1)
                for (auto& f : proc_.worldPresetDir (page).findChildFiles (juce::File::findFiles, false, "*.json"))
                    add ({ Row::UserPreset, f.getFileNameWithoutExtension(), f, slot, -1 });
        }
        else
        {
            auto dir = JMSignatureProcessor::performancePresetDir();
            for (auto& f : dir.getChildFile ("Factory").findChildFiles (juce::File::findFiles, false, "*.json"))
                if (mode != 0) add ({ Row::Performance, f.getFileNameWithoutExtension(), f, -1, -1 });
            for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
                if (mode != 1) add ({ Row::Performance, f.getFileNameWithoutExtension(), f, -1, -1 });
        }
        list_.updateContent();
        list_.repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (Theme::M::pad);
        title_.setBounds (r.removeFromTop (22));
        auto searchRow = r.removeFromTop (26);
        filterBtn_->setBounds (searchRow.removeFromRight (24).reduced (2));
        searchIconArea_ = searchRow.removeFromLeft (22).reduced (3);
        search_.setBounds (searchRow);
        r.removeFromTop (4);
        auto tr = r.removeFromTop (24);
        const int tw = tr.getWidth() / 3;
        for (auto& t : tabs_) t.setBounds (tr.removeFromLeft (tw).reduced (1, 0));
        r.removeFromTop (4);
        auto save = r.removeFromBottom (26);
        saveBtn_.setBounds (save.removeFromRight (52));
        saveName_.setBounds (save.reduced (2, 0));
        r.removeFromBottom (4);
        list_.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        ux::drawFrame (g, getLocalBounds().toFloat(), true, (float) Theme::M::corner, 7.0f);
        if (searchIcon_ != nullptr && ! searchIconArea_.isEmpty())
            searchIcon_->drawWithin (g, searchIconArea_.toFloat(), juce::RectanglePlacement::centred, 0.9f);
    }

private:
    struct Row
    {
        enum Kind { Bundle, FactoryPreset, UserPreset, Performance } kind;
        juce::String name;
        juce::File file;
        int slot;
        int presetIndex;
        juce::String favKey() const { return juce::String ((int) kind) + "|" + name; }
    };

    int getNumRows() override { return (int) rows_.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) rows_.size()) return;
        auto& r = rows_[(size_t) row];
        if (selected)
            ux::drawPresetRowActive (g, juce::Rectangle<float> (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f));
        g.setColour (selected ? Theme::gold : r.kind == Row::Bundle ? Theme::cream : Theme::muted.brighter (0.25f));
        g.setFont (Theme::ui (13.0f));
        g.drawText (r.name, 8, 0, w - 30, h, juce::Justification::centredLeft);
        if (proc_.state.library.isFavorite (r.favKey()) && starIcon_ != nullptr)
            starIcon_->drawWithin (g, juce::Rectangle<float> ((float) w - 22.0f, (float) h * 0.5f - 7.0f, 14.0f, 14.0f),
                                   juce::RectanglePlacement::centred, 1.0f);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) rows_.size()) return;
        auto& r = rows_[(size_t) row];
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Toggle Favorite");
            if (r.kind == Row::UserPreset || r.kind == Row::Performance) m.addItem (2, "Delete preset");
            m.showMenuAsync ({}, [this, r] (int res)
            {
                if (res == 1) proc_.state.library.toggleFavorite (r.favKey());
                else if (res == 2) { r.file.moveToTrash(); }
                rebuild();
            });
            return;
        }
        switch (r.kind)
        {
            case Row::Bundle:
                for (auto& b : proc_.state.library.bundles())
                    if (b.dir == r.file) { proc_.loadBundleIntoSlot (r.slot, b); break; }
                break;
            case Row::FactoryPreset:
                if (auto lb = proc_.engines[(size_t) r.slot].bundle())
                    if (r.presetIndex >= 0 && r.presetIndex < (int) lb->inst.presets.size())
                        proc_.applyJmiPresetToSlot (r.slot, lb->inst.presets[(size_t) r.presetIndex]);
                break;
            case Row::UserPreset:
                proc_.applyUserPresetToSlot (r.slot, r.file);
                break;
            case Row::Performance:
                proc_.loadPerformancePreset (r.file);
                break;
        }
    }

    void doSave()
    {
        auto name = saveName_.getText().trim();
        if (name.isEmpty()) return;
        if (proc_.state.currentPage <= 3)
            proc_.saveUserPreset (slotForWorld (proc_.state, proc_.state.currentPage), name);
        else
            proc_.savePerformancePreset (name);
        saveName_.clear();
        tabs_[0].setToggleState (true, juce::dontSendNotification);
        rebuild();
    }

    JMSignatureProcessor& proc_;
    juce::Label title_;
    juce::TextEditor search_, saveName_;
    std::array<juce::TextButton, 3> tabs_;
    juce::TextButton saveBtn_;
    std::unique_ptr<juce::DrawableButton> filterBtn_;
    std::unique_ptr<juce::Drawable> searchIcon_ { assets::makeIcon (assets::Icon::search, Theme::muted) };
    std::unique_ptr<juce::Drawable> starIcon_ { ux::makePackIcon (ux::PackIcon::star, Theme::gold) };
    juce::Rectangle<int> searchIconArea_;
    juce::ListBox list_;
    std::vector<Row> rows_;
};

//==============================================================================
class FxUnitPanel : public juce::Component, private juce::Timer
{
public:
    FxUnitPanel (JMSignatureProcessor& p, juce::String pathPrefix, int unitIndex,
                 const FxUnitState& st, FxRackState* rackState, FxRackDsp* rackDsp)
        : proc_ (p), prefix_ (std::move (pathPrefix)), idx_ (unitIndex),
          type_ (st.type), rackState_ (rackState), rackDsp_ (rackDsp)
    {
        auto& spec = fxSpec (type_);
        title_.setText (juce::String (spec.name).toUpperCase(), juce::dontSendNotification);
        title_.setFont (Theme::display (12.0f));
        title_.setJustificationType (juce::Justification::centredLeft);
        title_.setColour (juce::Label::textColourId, Theme::cream);
        addAndMakeVisible (title_);

        menuBtn_ = ux::makePackIconButton ("more", ux::PackIcon::kebab, Theme::muted);
        menuBtn_->onClick = [this] { showMenu(); };
        addAndMakeVisible (*menuBtn_);

        for (auto& ps : spec.params)
        {
            auto path = prefix_ + ".fx" + juce::String (idx_) + "." + ps.id;
            if (ps.kind == ParamKind::Float)
            {
                auto* k = new ParamKnob (proc_, path, ps.label, &ps);
                knobs_.add (k);
                addAndMakeVisible (k);
            }
            else
            {
                auto* c = new ParamChoiceButton (proc_, path, ps.label, ps);
                choices_.add (c);
                addAndMakeVisible (c);
            }
        }
        bypass_.setButtonText ("BYPASS");
        bypass_.setClickingTogglesState (true);
        bypass_.setToggleState (st.bypassed, juce::dontSendNotification);
        bypass_.onClick = [this]
        {
            proc_.setParamNormalized (prefix_ + ".fx" + juce::String (idx_) + ".bypass",
                                      bypass_.getToggleState() ? 1.0f : 0.0f);
        };
        addAndMakeVisible (bypass_);
        if (type_ == FxType::widener) startTimerHz (10);
    }

    // V2 fx-module family (personality mapping, logged in DECISIONS):
    // chorus + vintage EQ teal · tape delay/tremolo/EP drive orange ·
    // phaser/flanger purple · shimmer + digital delay blue · compressor red ·
    // plate green · widener green + gold overlay. Never world colors.
    static juce::Colour accent (FxType t)
    {
        switch (t)
        {
            case FxType::jmChorus:       return Theme::fxTeal;
            case FxType::vintageEq:      return Theme::fxTeal;
            case FxType::tremoloAutoPan: return Theme::fxOrange;
            case FxType::tapeDelay:      return Theme::fxOrange;
            case FxType::epPreamp:       return Theme::fxOrange;
            case FxType::phaser:         return Theme::fxPurple;
            case FxType::flanger:        return Theme::fxPurple;
            case FxType::hallShimmer:    return Theme::fxBlue;
            case FxType::digitalDelay:   return Theme::fxBlue;
            case FxType::compressor:     return Theme::fxRed;
            case FxType::plateReverb:    return Theme::fxGreen;
            default:                     return Theme::fxGreen;   // widener (+ gold overlay)
        }
    }

    void refresh()
    {
        for (auto* k : knobs_) k->refresh();
        for (auto* c : choices_) c->refresh();
        if (rackState_ != nullptr && idx_ < (int) rackState_->size())
            bypass_.setToggleState ((*rackState_)[(size_t) idx_].bypassed, juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto head = r.removeFromTop (20);
        menuBtn_->setBounds (head.removeFromRight (22).reduced (2));
        title_.setBounds (head);
        bypass_.setBounds (r.removeFromBottom (22).withSizeKeepingCentre (juce::jmin (86, r.getWidth()), 20));
        r.removeFromBottom (2);

        juce::Array<juce::Component*> items;
        for (auto* k : knobs_) items.add (k);
        for (auto* c : choices_) items.add (c);
        if (items.isEmpty()) return;
        const int perRow = juce::jmax (2, (items.size() + 1) / 2);
        const int rows = (items.size() + perRow - 1) / perRow;
        const int cellH = r.getHeight() / rows;
        int idx = 0;
        for (int row = 0; row < rows && idx < items.size(); ++row)
        {
            auto rowArea = r.removeFromTop (cellH);
            const int inRow = juce::jmin (perRow, items.size() - idx);
            const int cellW = rowArea.getWidth() / inRow;
            for (int c = 0; c < inRow; ++c)
            {
                auto cell = rowArea.removeFromLeft (cellW);
                auto* comp = items[idx++];
                if (dynamic_cast<ParamChoiceButton*> (comp) != nullptr)
                    comp->setBounds (cell.reduced (2).withHeight (juce::jmin (38, cell.getHeight())));
                else
                    comp->setBounds (cell.reduced (1));
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        ux::drawFxModuleFrameV2 (g, r, accent (type_), type_ == FxType::widener);
        // opaque local plaque behind the label — never trade contrast for pattern
        g.setColour (juce::Colour (0xff0B0A08).withAlpha (0.92f));
        g.fillRoundedRectangle (r.reduced (9.0f).removeFromTop (22.0f), 3.0f);
        if (type_ == FxType::widener && rackDsp_ != nullptr)   // correlation meter
        {
            const float corr = rackDsp_->widenerCorrelation();
            auto bar = getLocalBounds().toFloat().removeFromBottom (34.0f).removeFromTop (5.0f).reduced (14.0f, 0.0f);
            g.setColour (Theme::black);
            g.fillRect (bar);
            const float t = juce::jlimit (0.0f, 1.0f, (corr + 1.0f) * 0.5f);
            g.setColour (corr < 0.0f ? Theme::warnRed : Theme::okGreen);
            g.fillRect (bar.getX() + (bar.getWidth() - 4.0f) * t, bar.getY(), 4.0f, bar.getHeight());
        }
    }

    std::function<void()> onRackEdit;   // set by FxRackStrip: rebuild after structural ops

private:
    void timerCallback() override { repaint(); }
    void showMenu()
    {
        juce::PopupMenu m;
        juce::PopupMenu repl;
        for (auto& s : fxRegistry()) repl.addItem (100 + (int) s.type, s.name);
        m.addSubMenu ("Replace with", repl);
        m.addItem (1, "Move left", idx_ > 0);
        m.addItem (2, "Move right", rackState_ != nullptr && idx_ + 1 < (int) rackState_->size());
        m.addItem (3, "Remove");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuBtn_.get()), [this] (int r)
        {
            if (rackState_ == nullptr || idx_ >= (int) rackState_->size() || r == 0) return;
            auto& rack = *rackState_;
            if (r == 1 && idx_ > 0) std::swap (rack[(size_t) idx_], rack[(size_t) idx_ - 1]);
            else if (r == 2 && idx_ + 1 < (int) rack.size()) std::swap (rack[(size_t) idx_], rack[(size_t) idx_ + 1]);
            else if (r == 3) rack.erase (rack.begin() + idx_);
            else if (r >= 100)
            {
                FxUnitState u;
                u.type = (FxType) (r - 100);
                u.params = defaultParams (u.type);
                rack[(size_t) idx_] = std::move (u);
            }
            rackDsp_->setState (rack);
            if (onRackEdit) onRackEdit();
            proc_.stateChanged.sendChangeMessage();
        });
    }

    JMSignatureProcessor& proc_;
    juce::String prefix_;
    int idx_;
    FxType type_;
    FxRackState* rackState_;
    FxRackDsp* rackDsp_;
    juce::Label title_;
    std::unique_ptr<juce::DrawableButton> menuBtn_;
    juce::TextButton bypass_;
    juce::OwnedArray<ParamKnob> knobs_;
    juce::OwnedArray<ParamChoiceButton> choices_;
};

//==============================================================================
class FxRackStrip : public juce::Component
{
public:
    FxRackStrip (JMSignatureProcessor& p) : proc_ (p)
    {
        title_.setFont (Theme::display (13.0f));
        title_.setColour (juce::Label::textColourId, Theme::gold);
        title_.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (title_);
        addBtn_.setButtonText ("+ ADD UNIT");
        addBtn_.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addBtn_);
        rebuild();
    }

    void rebuild()
    {
        panels_.clear();
        const int page = proc_.state.currentPage;
        if (page >= 0 && page <= 3)
        {
            const int slot = slotForWorld (proc_.state, page);
            prefix_ = "slot" + juce::String (slot);
            rackState_ = &proc_.state.slots[(size_t) slot].rack;
            rackDsp_ = &proc_.slotRacks[(size_t) slot];
            title_.setText ("FX RACK  -  SLOT " + juce::String::charToString ((juce::juce_wchar) ('A' + slot))
                            + "  (" + worldName (proc_.state.slots[(size_t) slot].world) + ")",
                            juce::dontSendNotification);
        }
        else
        {
            prefix_ = "master";
            rackState_ = &proc_.state.masterRack;
            rackDsp_ = &proc_.masterRack;
            title_.setText ("MASTER FX RACK  (POST-MIX)", juce::dontSendNotification);
        }
        for (size_t i = 0; i < rackState_->size(); ++i)
        {
            auto* pn = new FxUnitPanel (proc_, prefix_, (int) i, (*rackState_)[i], rackState_, rackDsp_);
            pn->onRackEdit = [this] { rebuild(); };
            panels_.add (pn);
            addAndMakeVisible (pn);
        }
        addBtn_.setVisible ((int) rackState_->size() < FxRackDsp::kMaxSlots);
        resized();
        repaint();
    }

    void refresh() { for (auto* pn : panels_) pn->refresh(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (Theme::M::pad);
        title_.setBounds (r.removeFromTop (18));
        r.removeFromTop (2);
        if (panels_.isEmpty())
        {
            addBtn_.setBounds (r.withSizeKeepingCentre (140, 30));
            return;
        }
        const int n = panels_.size() + (addBtn_.isVisible() ? 1 : 0);
        const int w = juce::jmin (220, r.getWidth() / juce::jmax (1, n));
        for (auto* pn : panels_)
            pn->setBounds (r.removeFromLeft (w).reduced (3, 0));
        if (addBtn_.isVisible())
            addBtn_.setBounds (r.removeFromLeft (juce::jmin (110, juce::jmax (60, r.getWidth()))).withSizeKeepingCentre (100, 30));
    }

    void paint (juce::Graphics&) override {}   // hosted inside overlay plaques

private:
    void showAddMenu()
    {
        juce::PopupMenu m;
        for (auto& s : fxRegistry()) m.addItem (100 + (int) s.type, s.name);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addBtn_), [this] (int r)
        {
            if (r < 100 || rackState_ == nullptr) return;
            if ((int) rackState_->size() >= FxRackDsp::kMaxSlots) return;
            FxUnitState u;
            u.type = (FxType) (r - 100);
            u.params = defaultParams (u.type);
            rackState_->push_back (std::move (u));
            rackDsp_->setState (*rackState_);
            rebuild();
            proc_.stateChanged.sendChangeMessage();
        });
    }

    JMSignatureProcessor& proc_;
    juce::String prefix_;
    FxRackState* rackState_ = nullptr;
    FxRackDsp* rackDsp_ = nullptr;
    juce::Label title_;
    juce::TextButton addBtn_;
    juce::OwnedArray<FxUnitPanel> panels_;
};

//==============================================================================
// V2 output meter (rhodes-model-layout.svg: 54x330 frame at 830,185; OUTPUT
// caption; 14px inner track; single 8px level bar, #7EE126).
class OutputMeterV2 : public juce::Component
{
public:
    void setLevel (float l) { if (std::abs (l - lvl_) > 1.0e-4f) { lvl_ = l; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff070706));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (0xff644617));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colour (0xffCDBB93));
        g.setFont (Theme::ui (9.0f));
        g.drawText ("OUTPUT", r.withTrimmedTop (14.0f).withHeight (14.0f), juce::Justification::centred);
        auto track = juce::Rectangle<float> (r.getCentreX() - 7.0f, r.getY() + 41.0f, 14.0f, r.getHeight() - 66.0f);
        g.setColour (juce::Colour (0xff111111));
        g.fillRoundedRectangle (track, 5.0f);
        const float db = juce::Decibels::gainToDecibels (lvl_ + 1.0e-6f);
        const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        if (t > 0.005f)
        {
            g.setColour (juce::Colour (0xff7EE126));
            auto fill = track.reduced (3.0f, 0.0f);
            g.fillRect (fill.removeFromBottom (fill.getHeight() * t));
        }
    }
private:
    float lvl_ = 0;
};

//==============================================================================
// V2 model page — fills the whole 1000x720 root below the header. MAIN shows
// the exact pixel-spec layout; the other build-spec functions (amp env, EQ,
// slot FX rack, settings, info, preset browser) open as overlays on the
// recessed surface so the spec view stays pristine (arrangement logged).
class ModelPagePanel : public juce::Component
{
public:
    ModelPagePanel (JMSignatureProcessor& p, int world) : proc_ (p), world_ (world)
    {
        slot_ = slotForWorld (proc_.state, world_);

        patchBtn_.onClick = [this] { toggleBrowser(); };   // invisible hit-area over the title
        addAndMakeVisible (patchBtn_);

        prevBtn_ = ux::makePackIconButton ("prev", ux::PackIcon::chevronLeft);
        nextBtn_ = ux::makePackIconButton ("next", ux::PackIcon::chevronRight);
        prevBtn_->onClick = [this] { stepPreset (-1); };
        nextBtn_->onClick = [this] { stepPreset (1); };
        addAndMakeVisible (*prevBtn_);
        addAndMakeVisible (*nextBtn_);

        heartBtn_ = std::make_unique<juce::DrawableButton> ("fav", juce::DrawableButton::ImageFitted);
        {
            auto off = ux::makePackIcon (ux::PackIcon::heart, Theme::muted);
            auto over = ux::makePackIcon (ux::PackIcon::heart, Theme::goldHi);
            auto on = ux::makePackIcon (ux::PackIcon::heart, Theme::gold);
            heartBtn_->setImages (off.get(), over.get(), over.get(), nullptr, on.get(), on.get());
            heartBtn_->setColour (juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
            heartBtn_->setColour (juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
        }
        heartBtn_->onClick = [this]
        {
            proc_.state.library.toggleFavorite (currentPresetFavKey());
            proc_.stateChanged.sendChangeMessage();
        };
        addAndMakeVisible (*heartBtn_);

        const char* tabNames[] = { "MAIN", "AMP ENV", "EQ", "FX", "SETTINGS", "INFO" };
        for (int i = 0; i < 6; ++i)
        {
            auto& b = tabs_[(size_t) i];
            b.setButtonText (tabNames[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (888);
            b.onClick = [this, i] { if (tabs_[(size_t) i].getToggleState()) { tab_ = i; rebuildContent(); } };
            addAndMakeVisible (b);
        }
        tabs_[0].setToggleState (true, juce::dontSendNotification);

        addAndMakeVisible (meter_);
        rebuildContent();
    }

    void refresh()
    {
        meter_.setLevel (proc_.slotPeak[(size_t) slot_].load());
        heartBtn_->setToggleState (proc_.state.library.isFavorite (currentPresetFavKey()), juce::dontSendNotification);
        for (auto* k : knobs_) k->refresh();
        for (auto* c : choices_) c->refresh();
        if (rack_ != nullptr) rack_->refresh();
        repaint (300, 120, 400, 30);      // live patch subtitle
    }

    juce::String currentPresetFavKey() const
    {
        return "1|" + proc_.state.slots[(size_t) slot_].presetName;
    }

    void stepPreset (int delta)
    {
        auto lb = proc_.engines[(size_t) slot_].bundle();
        if (lb == nullptr || lb->inst.presets.empty()) return;
        const int n = (int) lb->inst.presets.size();
        int cur = 0;
        for (int i = 0; i < n; ++i)
            if (juce::String (lb->inst.presets[(size_t) i].name) == proc_.state.slots[(size_t) slot_].presetName)
                { cur = i; break; }
        proc_.applyJmiPresetToSlot (slot_, lb->inst.presets[(size_t) (((cur + delta) % n + n) % n)]);
    }

    void rebuildContent()
    {
        knobs_.clear();
        choices_.clear();
        settingsWidgets_.clear();
        rack_.reset();
        browser_.reset();
        infoText_ = nullptr;
        slot_ = slotForWorld (proc_.state, world_);
        auto slotStr = juce::String (slot_);

        if (tab_ == 0)          // MAIN — exact V2 pixel spec (3-over-2 / MKS 3x2)
        {
            auto& ctrls = worldEngineControls (world_);
            const bool six = (int) ctrls.size() == 6;
            const int d = six ? Theme::V2::knob6D : Theme::V2::knobD;
            int i = 0;
            for (auto& spec : ctrls)
            {
                const int* ctr = six ? Theme::V2::six[juce::jmin (i, 5)] : Theme::V2::five[juce::jmin (i, 4)];
                auto* k = new ParamKnob (proc_, "slot" + slotStr + ".engine." + spec.id, spec.label, &spec);
                k->setMotif (i % 5);
                k->setAccent (Theme::worldAccent (world_));
                k->setV2 (d, Theme::V2::labelOffsetY * d / 112, Theme::V2::valueOffsetY * d / 112 + 13);
                const int box = d + 60;                       // room for glow + label + value
                k->setBounds (juce::Rectangle<int> (box, box + 80).withCentre ({ ctr[0], ctr[1] }));
                knobs_.add (k);
                addAndMakeVisible (k);
                ++i;
            }
        }
        else                    // overlay plaque content on the recessed surface
        {
            if (tab_ == 1)      // AMP ENV
            {
                const char* ids[] = { "attackMs", "decayMs", "sustain", "releaseMs" };
                const char* labels[] = { "Attack", "Decay", "Sustain", "Release" };
                for (int i = 0; i < 4; ++i)
                {
                    auto* k = new ParamKnob (proc_, "slot" + slotStr + ".ampenv." + ids[i], labels[i]);
                    knobs_.add (k);
                    addAndMakeVisible (k);
                }
            }
            else if (tab_ == 2) // EQ — the slot rack's Vintage EQ unit
            {
                int eqIdx = -1;
                auto& rack = proc_.state.slots[(size_t) slot_].rack;
                for (size_t i = 0; i < rack.size(); ++i)
                    if (rack[i].type == FxType::vintageEq) eqIdx = (int) i;
                if (eqIdx < 0 && (int) rack.size() < FxRackDsp::kMaxSlots)
                {
                    FxUnitState u; u.type = FxType::vintageEq; u.params = defaultParams (FxType::vintageEq);
                    rack.push_back (std::move (u));
                    eqIdx = (int) rack.size() - 1;
                    proc_.slotRacks[(size_t) slot_].setState (rack);
                }
                if (eqIdx >= 0)
                    for (auto& spec : fxSpec (FxType::vintageEq).params)
                    {
                        auto path = "slot" + slotStr + ".fx" + juce::String (eqIdx) + "." + spec.id;
                        if (spec.kind == ParamKind::Float)
                        {
                            auto* k = new ParamKnob (proc_, path, spec.label, &spec);
                            knobs_.add (k); addAndMakeVisible (k);
                        }
                        else
                        {
                            auto* c = new ParamChoiceButton (proc_, path, spec.label, spec);
                            choices_.add (c); addAndMakeVisible (c);
                        }
                    }
            }
            else if (tab_ == 3) // FX — this slot's rack (build-spec §5.2)
            {
                rack_ = std::make_unique<FxRackStrip> (proc_);
                addAndMakeVisible (*rack_);
            }
            else if (tab_ == 4) // SETTINGS — library folder + rescan
            {
                auto* lbl = new juce::Label();
                lbl->setText ("LIBRARY FOLDER", juce::dontSendNotification);
                lbl->setFont (Theme::ui (12.0f));
                lbl->setColour (juce::Label::textColourId, Theme::muted);
                addAndMakeVisible (lbl);
                settingsWidgets_.add (lbl);

                auto* path = new juce::Label();
                path->setText (proc_.state.library.libraryDir().getFullPathName()
                               + (proc_.state.library.usedFallback() ? "   (empty - using testdata fallback)" : ""),
                               juce::dontSendNotification);
                path->setFont (Theme::value (12.0f));
                path->setColour (juce::Label::textColourId, Theme::cream);
                addAndMakeVisible (path);
                settingsWidgets_.add (path);

                auto* browse = new juce::TextButton ("BROWSE...");
                browse->onClick = [this]
                {
                    chooser_ = std::make_unique<juce::FileChooser> ("Choose library folder",
                                                                    proc_.state.library.libraryDir());
                    chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                        [this] (const juce::FileChooser& fc)
                        {
                            if (fc.getResult().isDirectory())
                            {
                                proc_.state.library.setLibraryDir (fc.getResult());
                                proc_.stateChanged.sendChangeMessage();
                            }
                        });
                };
                addAndMakeVisible (browse);
                settingsWidgets_.add (browse);

                auto* rescan = new juce::TextButton ("RESCAN LIBRARY");
                rescan->onClick = [this]
                {
                    proc_.state.library.rescan();
                    proc_.stateChanged.sendChangeMessage();
                };
                addAndMakeVisible (rescan);
                settingsWidgets_.add (rescan);
            }
            else                // INFO — bundle meta (incl. sound type: metadata only)
            {
                auto* info = new juce::Label();
                juce::String text;
                if (auto lb = proc_.engines[(size_t) slot_].bundle())
                {
                    auto& m = lb->inst.meta;
                    text << "Name:  " << m.name << "\n"
                         << "World:  " << jmi::worldId (m.world) << "\n"
                         << "Author:  " << m.author << "\n"
                         << "Created:  " << m.createdAt << "\n"
                         << "Capture mode:  " << m.captureMode << "\n"
                         << "Format:  " << juce::String (m.sampleRate) << " Hz / " << juce::String (m.bitDepth)
                         << "-bit / " << (m.stereo ? "stereo" : "mono") << "\n"
                         << "Sound type:  " << (lb->inst.soundType == jmi::SoundType::Sustaining ? "sustaining" : "decaying") << "\n"
                         << "Zones:  " << juce::String ((int) lb->inst.zones.size()) << "\n"
                         << "Factory presets:  " << juce::String ((int) lb->inst.presets.size()) << "\n"
                         << "Path:  " << lb->dir.getFullPathName();
                }
                else text = "No bundle loaded for this world.";
                info->setText (text, juce::dontSendNotification);
                info->setFont (Theme::value (13.0f));
                info->setColour (juce::Label::textColourId, Theme::cream);
                info->setJustificationType (juce::Justification::topLeft);
                addAndMakeVisible (info);
                settingsWidgets_.add (info);
                infoText_ = info;
            }
        }
        resized();
        refresh();
        repaint();
    }

    void toggleBrowser()
    {
        if (browser_ != nullptr) { browser_.reset(); setKnobsVisible (true); repaint(); return; }
        browser_ = std::make_unique<PresetBrowserPanel> (proc_);
        addAndMakeVisible (*browser_);
        setKnobsVisible (false);
        resized();
        repaint();
    }
    void setKnobsVisible (bool v)
    {
        for (auto* k : knobs_) k->setVisible (v);
        for (auto* c : choices_) c->setVisible (v);
    }

    void resized() override
    {
        using V = Theme::V2;
        // title row widgets around the patch subtitle (y=137 line)
        patchBtn_.setBounds (360, 96, 280, 48);
        prevBtn_->setBounds (330, 124, 24, 20);
        nextBtn_->setBounds (648, 124, 24, 20);
        heartBtn_->setBounds (676, 124, 22, 20);
        meter_.setBounds (V::outX, V::outY, V::outW, V::outH);
        // sub-tabs: discreet row at the bottom of the content area
        int tx = 190;
        for (auto& t : tabs_) { t.setBounds (tx, 682, 84, 20); tx += 88; }

        const juce::Rectangle<int> surface (190, 150, 652, 454);
        if (browser_ != nullptr)
            browser_->setBounds (surface.reduced (60, 20));
        if (rack_ != nullptr)
            rack_->setBounds (surface.reduced (8));

        auto area = surface.reduced (40);
        if (tab_ == 4 && settingsWidgets_.size() >= 4)
        {
            auto r = area.reduced (20);
            settingsWidgets_[0]->setBounds (r.removeFromTop (20));
            settingsWidgets_[1]->setBounds (r.removeFromTop (24));
            r.removeFromTop (10);
            auto row = r.removeFromTop (30);
            settingsWidgets_[2]->setBounds (row.removeFromLeft (120));
            row.removeFromLeft (10);
            settingsWidgets_[3]->setBounds (row.removeFromLeft (160));
            return;
        }
        if (infoText_ != nullptr) { infoText_->setBounds (area.reduced (16)); return; }
        if (tab_ == 1 || tab_ == 2)
        {
            juce::Array<juce::Component*> items;
            for (auto* c : choices_) items.add (c);
            for (auto* k : knobs_) items.add (k);
            if (items.isEmpty()) return;
            const int perRow = juce::jmin (3, items.size());
            const int rows = (items.size() + perRow - 1) / perRow;
            const int cellH = area.getHeight() / rows;
            int idx = 0;
            for (int row = 0; row < rows; ++row)
            {
                const int inRow = juce::jmin (perRow, items.size() - idx);
                auto rowArea = area.removeFromTop (cellH);
                const int cellW = rowArea.getWidth() / inRow;
                for (int c = 0; c < inRow; ++c)
                {
                    auto cell = rowArea.removeFromLeft (cellW);
                    auto* comp = items[idx++];
                    if (dynamic_cast<ParamChoiceButton*> (comp) != nullptr)
                        comp->setBounds (cell.withSizeKeepingCentre (juce::jmin (120, cell.getWidth() - 8), 50));
                    else
                    {
                        const int k = juce::jmin (140, juce::jmin (cell.getWidth() - 4, cell.getHeight() - 4));
                        comp->setBounds (cell.withSizeKeepingCentre (k, k));
                    }
                }
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        using V = Theme::V2;
        const juce::Rectangle<float> content ((float) V::contentX, (float) V::contentY,
                                              (float) V::contentW, (float) V::contentH);
        ux::drawWorldBackdropV2 (g, content, world_);

        // title + live patch subtitle (V2 title block at 500,115 / 500,137).
        // Dark plaque under the text per the design law (never label on undimmed
        // pattern) and the polished reference's title panel; it is also the
        // click affordance for the preset browser.
        g.setColour (juce::Colour (0xff0A0805).withAlpha (0.85f));
        g.fillRoundedRectangle (352.0f, 92.0f, 296.0f, 54.0f, 8.0f);
        g.setColour (Theme::goldDark.withAlpha (0.8f));
        g.drawRoundedRectangle (352.0f, 92.0f, 296.0f, 54.0f, 8.0f, 1.0f);
        g.setColour (Theme::cream);
        g.setFont (Theme::display (28.0f));
        g.drawText (worldName (world_), 200, 96, 600, 26, juce::Justification::centred);
        auto& s = proc_.state.slots[(size_t) slot_];
        g.setColour (Theme::goldHi);
        g.setFont (Theme::ui (11.0f));
        g.drawText (s.bundleName.isEmpty() ? "NO BUNDLE - CLICK TO BROWSE"
                                           : (s.bundleName + "  -  " + s.presetName).toUpperCase(),
                    200, 128, 600, 16, juce::Justification::centred);

        if (tab_ != 0 || browser_ != nullptr)   // overlay plaque over the surface
        {
            g.setColour (juce::Colour (0xff0A0805).withAlpha (0.96f));
            g.fillRoundedRectangle (190.0f, 150.0f, 652.0f, 454.0f, 12.0f);
            g.setColour (Theme::goldDark);
            g.drawRoundedRectangle (190.0f, 150.0f, 652.0f, 454.0f, 12.0f, 1.0f);
        }
    }

    int world() const { return world_; }

private:
    // paint-free clickable region (the live patch title acts as the browser button)
    struct HitArea : juce::Component
    {
        std::function<void()> onClick;
        void paint (juce::Graphics&) override {}
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mouseWasDraggedSinceMouseDown() && onClick) onClick();
        }
        void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    };

    JMSignatureProcessor& proc_;
    int world_, slot_ = 0, tab_ = 0;
    HitArea patchBtn_;
    std::array<juce::TextButton, 6> tabs_;
    juce::OwnedArray<ParamKnob> knobs_;
    juce::OwnedArray<ParamChoiceButton> choices_;
    juce::OwnedArray<juce::Component> settingsWidgets_;
    juce::Label* infoText_ = nullptr;
    OutputMeterV2 meter_;
    std::unique_ptr<FxRackStrip> rack_;
    std::unique_ptr<PresetBrowserPanel> browser_;
    std::unique_ptr<juce::DrawableButton> prevBtn_, nextBtn_, heartBtn_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace jm
