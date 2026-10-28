// Panels.h — preset browser column, model pages, FX rack strip.
//
// CLIENT REVIEW ROUND 2: the approved reference arrangement is restored — the
// preset browser column, centre model panel, right PERFORM dock, bottom FX rack
// strip, bottom bar and status bar are all on screen at once inside ONE
// 1536x1024 root coordinate system (uniform-contain scaled, never reflowed).
// Materials/colour tokens/state rules stay on UX pack V2; the hardware surfaces
// (knobs, FX faceplates, meters, buttons, screws) now come from the photoreal
// kit in docs/hardware-kit. Behaviour/DSP authority remains JM-BUILD-SPEC.md —
// no function was added or removed in this pass.
#pragma once
#include "../engine/import/X7lImportJob.h"
#include "Widgets.h"
#include "KenteTexture.h"
#include "UxAssets.h"
#include "Kit.h"
#include "Hardware.h"
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
// Left column, persistent (approved reference): title, search + filter,
// USER/FACTORY/FAVORITES segmented tabs in the mature language, scrolling list
// with the active row lifted gold + starred, and the JM brand art panel below.
class PresetBrowserPanel : public juce::Component,
                           private juce::ListBoxModel
{
public:
    PresetBrowserPanel (JMSignatureProcessor& p, bool compact = false)
        : proc_ (p), compact_ (compact)
    {
        title_.setText ("PRESET BROWSER", juce::dontSendNotification);
        title_.setFont (Theme::display (17.0f));
        title_.setJustificationType (juce::Justification::centred);
        title_.setColour (juce::Label::textColourId, Theme::gold);
        addAndMakeVisible (title_);

        search_.setTextToShowWhenEmpty ("Search presets...", Theme::muted.withAlpha (0.65f));
        search_.setFont (Theme::ui (14.0f));
        search_.setIndents (30, 5);                 // room for the inline search glyph
        search_.setColour (juce::TextEditor::focusedOutlineColourId, Theme::goldHi);
        search_.onTextChange = [this] { rebuild(); };
        addAndMakeVisible (search_);

        filterBtn_ = assets::makeIconButton ("filter", assets::Icon::filter, Theme::muted);
        filterBtn_->onClick = [this]
        {
            juce::PopupMenu m;
            const char* names[] = { "User", "Factory", "Favorites" };
            for (int i = 0; i < 3; ++i)
                m.addItem (i + 1, names[i], true, tabs_[(size_t) i].getToggleState());
            m.showMenuAsync (styledMenu (m, *filterBtn_), [this] (int r)
            {
                if (r >= 1 && r <= 3)
                {
                    tabs_[(size_t) (r - 1)].setToggleState (true, juce::dontSendNotification);
                    rebuild();
                }
            });
        };
        addAndMakeVisible (*filterBtn_);

        addAndMakeVisible (tabStrip_);
        const char* names[] = { "USER", "FACTORY", "FAVORITES" };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = tabs_[(size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (777);
            b.setFirstCell (i == 0);
            b.setFontHeight (12.0f);
            b.onClick = [this] { rebuild(); };
            addAndMakeVisible (b);
        }
        tabs_[1].setToggleState (true, juce::dontSendNotification);

        list_.setModel (this);
        list_.setRowHeight (30);
        list_.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list_);

        saveName_.setTextToShowWhenEmpty ("New preset name", Theme::muted);
        saveName_.setFont (Theme::ui (13.0f));
        // Typing a new name is also the gesture that means "I am not
        // overwriting", so it disarms a primed OVERWRITE.
        saveName_.onTextChange = [this] { if (overArmed_) { overArmed_ = false; syncOverButton(); } };
        addAndMakeVisible (saveName_);
        saveBtn_.setButtonText ("SAVE AS");
        saveBtn_.setFontHeight (11.0f);
        saveBtn_.setTooltip ("Saves what is on screen as a NEW preset under the typed name. "
                             "Never replaces an existing one.");
        saveBtn_.onClick = [this] { doSaveAs(); };
        addAndMakeVisible (saveBtn_);

        // ------------------------------- AUTHORING WORKFLOW: SAVE OVER vs SAVE AS
        // "save it over the sound or as a different preset" are two acts, and
        // overwriting someone's work by accident is the failure that loses
        // trust. So they are two controls that cannot be confused:
        //
        //   * SAVE AS reads the NAME FIELD and refuses to replace anything;
        //   * OVERWRITE reads nothing — it NAMES, in its own label, the preset
        //     that is loaded and about to be replaced — wears the destructive
        //     register (warn red, not gold), sits on its own row, and takes two
        //     clicks: the first arms it and spells out the consequence in the
        //     destination line, the second does it.
        //
        // It is hidden entirely when there is no preset of that name to
        // replace, so its presence alone means "there is something here to
        // stand on". The arm times out, so a stray click cannot stay live.
        overBtn_.setFontHeight (10.0f);
        overBtn_.setDanger (true);
        overBtn_.onClick = [this] { doSaveOver(); };
        addChildComponent (overBtn_);

        // ------------------------------------------------- AUTHORING MODE
        // The whole point of this control is that a save must not be a guess.
        // Two things are therefore drawn with it (in paint(), from the
        // processor's own wording):
        //   * WHERE it will land — factory or user — which is the mode made
        //     concrete at the one moment it changes what happens;
        //   * WHAT it captures, and when the patch format cannot hold the
        //     modulation the artist just built, what to save instead.
        //
        // The second is deliberately NOT a warning dialog. A dialog is read
        // once, dismissed for ever, and then the work is gone anyway. Instead
        // the save that KEEPS the modulation sits here as a live button next to
        // the one that would lose it, and only appears when there is something
        // to lose — so the right thing is the obvious thing, not a thing you
        // have to have read about.
        altSaveBtn_.setButtonText ("SAVE PERFORMANCE INSTEAD");
        altSaveBtn_.setFontHeight (10.0f);
        altSaveBtn_.setTooltip ("Saves slots, macros, modulation and the arpeggiator - "
                                "everything a patch cannot hold");
        altSaveBtn_.onClick = [this] { doSavePerformanceInstead(); };
        addChildComponent (altSaveBtn_);
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
        refreshSaveBlock();
        list_.updateContent();
        list_.repaint();
    }

    //======================================================== AUTHORING MODE
    /// Re-reads the two lines the save control shows. The WORDING lives in the
    /// processor (saveDestinationLine / modBoundaryLine) so the self-check can
    /// assert the exact sentence the artist will read, and so there is one
    /// place that decides what a save means.
    void refreshSaveBlock()
    {
        const int page = proc_.state.currentPage;
        destLine_ = proc_.saveDestinationLine (page);
        boundaryLine_ = proc_.modBoundaryLine (page);
        overTarget_ = proc_.saveOverTargetName (page);
        overLine_ = proc_.saveOverLine (page);
        overArmed_ = false;
        resultIsGood_ = false;
        altSaveBtn_.setVisible (boundaryLine_.isNotEmpty());
        syncOverButton();
        if (compact_) resized();        // the block's height depends on those lines
        repaint();
    }

    /// Height the save block needs: the name row, plus only the lines that are
    /// actually showing.
    int saveBlockHeight() const
    {
        using L = Theme::L;
        int h = L::brSaveH + 2 + 6;
        if (overTarget_.isNotEmpty()) h += L::brOverH + 2;
        if (destLine_.isNotEmpty()) h += L::brInfoH + 2;
        if (boundaryLine_.isNotEmpty()) h += L::brWarnH + 4 + L::brAltH;
        return h;
    }

    // Self-check hooks: the save block is the whole feature, so it has to be
    // assertable without synthesising clicks into a TextEditor.
    juce::String destinationText() const { return destLine_; }
    /// What is actually DRAWN in the info area: the destination normally, and
    /// the consequence of the next click while OVERWRITE is armed.
    juce::String infoLineText() const
    {
        return overArmed_ && overLine_.isNotEmpty() ? overLine_ : destLine_;
    }
    juce::String boundaryText() const { return boundaryLine_; }
    bool alternativeSaveOffered() const { return altSaveBtn_.isVisible(); }
    /// The escape route's own label. The boundary LINE states the limit; this
    /// button states the way out, so the self-check reads it here rather than
    /// expecting the line to spell out both.
    juce::String alternativeSaveText() const { return altSaveBtn_.getButtonText(); }
    /// Where the full save-over explanation lives now that the drawn line is
    /// cut back to the file that changes.
    juce::String overwriteTooltip() { return overBtn_.getTooltip(); }
    juce::Rectangle<int> listBounds() const { return list_.getBounds(); }
    void setSaveName (const juce::String& n) { saveName_.setText (n, false); }
    void pressSave (bool performanceInstead = false)
    {
        if (performanceInstead) doSavePerformanceInstead();
        else doSaveAs();
    }
    // --- SAVE OVER vs SAVE AS, assertable without synthesising mouse clicks
    bool overwriteOffered() const { return overBtn_.isVisible(); }
    juce::String overwriteButtonText() const { return overBtn_.getButtonText(); }
    bool overwriteArmed() const { return overBtn_.isArmed(); }
    juce::String overwriteText() const { return overLine_; }
    void pressOverwrite() { doSaveOver(); }
    void disarmOverwrite() { overArmed_ = false; syncOverButton(); }
    bool renameRow (int index, const juce::String& newName) { return applyRename (index, newName); }
    bool recategoriseRow (int index, int world) { return applyRecategorise (index, world); }

    void resized() override
    {
        using L = Theme::L;
        auto w = getWidth();
        title_.setBounds (0, L::brTitleY, w, 24);
        auto searchRow = juce::Rectangle<int> (12, L::brSearchY, w - 24, L::brSearchH);
        filterBtn_->setBounds (searchRow.removeFromRight (34).reduced (3));
        searchRow.removeFromRight (6);
        // R3(A): the icon now lives INSIDE the recessed well (drawn over the
        // field in paintOverChildren) instead of sitting beside a bare JUCE box.
        search_.setBounds (searchRow);
        searchIconArea_ = searchRow.withWidth (28).reduced (7);
        tabStrip_.setBounds (12, L::brTabsY, w - 24, L::brTabsH);
        {
            auto tr = tabStrip_.getBounds().reduced (3, 3);
            const int tw = tr.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
                tabs_[(size_t) i].setBounds (tr.removeFromLeft (i == 2 ? tr.getWidth() : tw));
        }
        // AUTHORING MODE: the save block is now save row + destination/boundary
        // text + the alternative save. In the compact (overlay) browser the
        // same block is pinned to the bottom, because the destination and the
        // modulation boundary matter there exactly as much.
        // The full column keeps the pinned Theme geometry. The compact (model
        // page overlay) browser pins the same block to its bottom and gives the
        // rest to the list — and collapses the lines that are not showing, so a
        // page with no modulation live does not pay for a warning it is not
        // getting.
        const int listBottom = compact_ ? juce::jmax (90, getHeight() - saveBlockHeight())
                                        : L::brListY + L::brListH;
        list_.setBounds (14, L::brListY, w - 28, juce::jmax (60, listBottom - L::brListY));

        const int saveY = compact_ ? listBottom + 6 : L::brSaveY;
        auto save = juce::Rectangle<int> (12, saveY, w - 24, L::brSaveH);
        saveBtn_.setBounds (save.removeFromRight (74));
        save.removeFromRight (6);
        saveName_.setBounds (save);

        // OVERWRITE sits on its own full-width row directly under SAVE AS.
        const int overY = compact_ ? saveY + L::brSaveH + 2 : L::brOverY;
        overBtn_.setBounds (12, overY, w - 24, L::brOverH);

        int y = compact_ ? (overBtn_.isVisible() ? overY + L::brOverH + 2 : overY) : L::brInfoY;
        infoArea_ = juce::Rectangle<int> (14, y, w - 28, L::brInfoH);
        y = compact_ ? y + L::brInfoH + 2 : L::brWarnY;
        warnArea_ = juce::Rectangle<int> (14, y, w - 28, L::brWarnH);
        y = compact_ ? y + L::brWarnH + 4 : L::brAltY;
        altSaveBtn_.setBounds (14, y, w - 28, L::brAltH);
        artArea_ = compact_ ? juce::Rectangle<int>()
                            : juce::Rectangle<int> (12, L::brArtY, w - 24, L::brArtH);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        // recessed chassis column
        juce::ColourGradient body (juce::Colour (0xff100E0C), r.getX(), r.getY(),
                                   juce::Colour (0xff070706), r.getX(), r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 8.0f);
        ux::drawMicroWeave (g, r.reduced (2.0f), 0.10f, 8.0f);
        g.setColour (Theme::goldDark.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        // (the search well itself is drawn by the LookAndFeel — R3 item A)
        // list bed
        g.setColour (juce::Colour (0xff0A0908));
        g.fillRoundedRectangle (list_.getBounds().toFloat().expanded (2.0f), 6.0f);

        // ROUND 8: an empty list used to be an unexplained black rectangle. With
        // the client's library the FM and STRINGS pages have nothing to offer,
        // and "nothing here, and here is why" is the least a browser owes.
        if (rows_.empty())
        {
            g.setColour (Theme::muted.withAlpha (0.8f));
            g.setFont (Theme::ui (12.5f));
            g.drawFittedText (emptyMessage(), list_.getBounds().reduced (14, 18),
                              juce::Justification::centredTop, 6);
        }

        //======================================================= AUTHORING MODE
        // Where this save goes, and what it cannot take with it. Drawn, not
        // hidden behind a dialog, and present on every repaint — so "where did
        // my save go?" is answered before it is asked.
        // While OVERWRITE is armed the destination line is REPLACED by what the
        // next click will actually do, in the warning colour — the one moment a
        // save-over is a click away is the one moment to spell it out.
        const auto infoText = infoLineText();
        if (infoText.isNotEmpty() && ! infoArea_.isEmpty())
        {
            g.setColour (overArmed_ ? Theme::warnRed
                                    : resultIsGood_ ? Theme::okGreen
                                       : destLine_.startsWith ("NOT SAVED") ? Theme::warnRed
                                                                            : Theme::muted.brighter (0.25f));
            g.setFont (Theme::ui (10.5f));
            g.drawFittedText (infoText, infoArea_, juce::Justification::topLeft, 3, 0.9f);
        }
        if (boundaryLine_.isNotEmpty() && ! warnArea_.isEmpty())
        {
            auto plate = warnArea_.toFloat().expanded (3.0f, 2.0f);
            g.setColour (Theme::warnRed.withAlpha (0.10f));
            g.fillRoundedRectangle (plate, 4.0f);
            g.setColour (Theme::warnRed.withAlpha (0.55f));
            g.drawRoundedRectangle (plate.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (Theme::warnRed);
            g.setFont (Theme::ui (10.5f));
            g.drawFittedText (boundaryLine_, warnArea_.reduced (4, 1),
                              juce::Justification::centredLeft, 3, 0.9f);
        }

        if (! compact_ && ! artArea_.isEmpty()) drawBrandArt (g, artArea_.toFloat());
    }

    //=========================================================== ROUND 8 (test)
    // The row activation used to live entirely inside the private ListBoxModel
    // callback, which made "clicking a patch is audible" unreachable from the
    // self-check without synthesising mouse events into a ListBox row. It is
    // the same code path the mouse takes — listBoxItemClicked calls it.
    int rowCount() const { return (int) rows_.size(); }
    juce::String rowName (int i) const
    {
        return i >= 0 && i < (int) rows_.size() ? rows_[(size_t) i].name : juce::String();
    }
    bool rowIsBundle (int i) const
    {
        return i >= 0 && i < (int) rows_.size() && rows_[(size_t) i].kind == Row::Bundle;
    }
    int rowSlot (int i) const
    {
        return i >= 0 && i < (int) rows_.size() ? rows_[(size_t) i].slot : -1;
    }
    void activateRow (int index)
    {
        if (index < 0 || index >= (int) rows_.size()) return;
        auto& r = rows_[(size_t) index];
        switch (r.kind)
        {
            // ROUND 8: every row here belongs to a MODEL PAGE, i.e. the player
            // is auditioning a sound. A patch he deliberately picked has to be
            // one he can hear — see JMSignatureProcessor::makeSlotAudible.
            case Row::Bundle:
                for (auto& b : proc_.state.library.bundles())
                    if (b.dir == r.file) { proc_.loadBundleIntoSlot (r.slot, b); break; }
                if (proc_.engines[(size_t) r.slot].bundle() != nullptr) proc_.makeSlotAudible (r.slot);
                break;
            case Row::FactoryPreset:
                if (auto lb = proc_.engines[(size_t) r.slot].bundle())
                    if (r.presetIndex >= 0 && r.presetIndex < (int) lb->inst.presets.size())
                    {
                        proc_.applyJmiPresetToSlot (r.slot, lb->inst.presets[(size_t) r.presetIndex]);
                        proc_.makeSlotAudible (r.slot);
                    }
                break;
            case Row::UserPreset:
                if (proc_.applyUserPresetToSlot (r.slot, r.file)
                    && proc_.engines[(size_t) r.slot].bundle() != nullptr)
                    proc_.makeSlotAudible (r.slot);
                break;
            case Row::Performance:
                proc_.loadPerformancePreset (r.file);
                break;
        }
    }

    /// Why this list is empty, in the player's terms.
    juce::String emptyMessage()
    {
        const int page = proc_.state.currentPage;
        const int mode = tabs_[0].getToggleState() ? 0 : tabs_[2].getToggleState() ? 2 : 1;
        if (page < 0 || page > 3)
            return "No performance presets.\n\nArrange the slots on PERFORM,\nthen SAVE one below.";
        if (mode == 2) return "No favourites yet.\n\nRight-click a row and\n\"Toggle Favorite\" to star it.";
        if (search_.getText().trim().isNotEmpty())
            return "Nothing matches\n\"" + search_.getText().trim() + "\"";
        if (mode == 0) return "No user presets for " + juce::String (worldName (page)).toUpperCase()
                            + ".\n\nDial in a sound and SAVE it below.";
        return "No " + juce::String (worldName (page)).toUpperCase() + " instruments\nin your library.\n\n"
               "Sample one with JM Capture, or copy\na .jmi bundle into your library folder\n"
               "(gear icon " + juce::String::fromUTF8 ("\xE2\x86\x92") + " Settings).";
    }

    /// The search glyph sits inside the well, over the TextEditor.
    void paintOverChildren (juce::Graphics& g) override
    {
        if (searchIcon_ != nullptr && ! searchIconArea_.isEmpty())
            searchIcon_->drawWithin (g, searchIconArea_.toFloat(), juce::RectanglePlacement::centred, 0.85f);
    }

    /// JM box / USB brand art panel at the foot of the column (approved ref).
    void drawBrandArt (juce::Graphics& g, juce::Rectangle<float> r)
    {
        kit::drawNineSlice (g, kit::image (kit::Img::buttonMaster), r, 34, 34, 1.0f);
        auto inner = r.reduced (18.0f, 22.0f);
        if (shield_ != nullptr)
            shield_->drawWithin (g, inner.removeFromTop (inner.getHeight() * 0.48f),
                                 juce::RectanglePlacement::centred, 1.0f);
        g.setColour (Theme::goldHi);
        g.setFont (Theme::display (18.0f));
        g.drawText ("MIREKU", inner.removeFromTop (24.0f), juce::Justification::centred, false);
        g.setColour (Theme::muted);
        g.setFont (Theme::ui (10.0f));
        g.drawText ("SOUND LIBRARY", inner.removeFromTop (16.0f), juce::Justification::centred, false);
        // single gold flourish accent (kit) — used once, never as wallpaper
        kit::drawFlourish (g, inner.withTrimmedTop (6.0f).reduced (24.0f, 2.0f), 0.75f);
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
        {
            auto cell = juce::Rectangle<float> (1.0f, 1.0f, (float) w - 2.0f, (float) h - 2.0f);
            juce::ColourGradient grad (juce::Colour (0xff4A300B), cell.getX(), cell.getY(),
                                       juce::Colour (0xff1A1208), cell.getRight(), cell.getY(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (cell, 5.0f);
            g.setColour (Theme::gold.withAlpha (0.75f));
            g.drawRoundedRectangle (cell.reduced (0.5f), 5.0f, 1.0f);
        }
        else
        {
            g.setColour (juce::Colour (0xff1D1B18).withAlpha (0.55f));
            g.fillRect (6, h - 1, w - 12, 1);
        }
        g.setColour (selected ? Theme::goldHi : r.kind == Row::Bundle ? Theme::cream : Theme::muted.brighter (0.3f));
        g.setFont (Theme::ui (14.0f));
        g.drawText (r.name, 12, 0, w - 40, h, juce::Justification::centredLeft);
        if (proc_.state.library.isFavorite (r.favKey()) && starIcon_ != nullptr)
            starIcon_->drawWithin (g, juce::Rectangle<float> ((float) w - 26.0f, (float) h * 0.5f - 8.0f, 16.0f, 16.0f),
                                   juce::RectanglePlacement::centred, 1.0f);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) rows_.size()) return;
        auto& r = rows_[(size_t) row];
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addSectionHeader (r.name.removeCharacters (juce::String::fromUTF8 ("\xE2\x96\xA0")).trim());
            m.addItem (1, "Toggle Favorite");
            // NAME AND RENAME: a preset OR an instrument is renamed from the
            // row that carries it, which is the only place its old name is on
            // screen. Renaming an instrument used to be refused here and the
            // artist sent to Explorer to rename a folder; it is `meta.name`,
            // one guarded property, and nothing on disk moves — see
            // authoring::renameInstrument.
            m.addItem (3, r.kind == Row::Bundle ? "Rename sound..." : "Rename...");
            if (r.kind == Row::UserPreset || r.kind == Row::Performance) m.addItem (2, "Delete preset");

            // MOVE TO ANOTHER CATEGORY. Only for an instrument, and only in
            // AUTHORING mode: it edits the bundle's own instrument.json, which
            // is factory content being authored, not something a player of the
            // shipped product should be able to do to his library by accident.
            if (r.kind == Row::Bundle && proc_.state.library.isAuthoring())
            {
                const int currentWorld = proc_.state.library.find (r.file) != nullptr
                                           ? proc_.state.library.find (r.file)->world : -1;
                juce::PopupMenu cats;
                for (int w = 0; w < 4; ++w)
                    cats.addItem (10 + w, juce::String (worldName (w)).toUpperCase()
                                          + (juce::String (worldCatchAllHint (w)).isEmpty()
                                                 ? juce::String()
                                                 : "   " + juce::String (worldCatchAllHint (w))),
                                  w != currentWorld, w == currentWorld);
                m.addSeparator();
                m.addSubMenu ("Move to category", cats);
            }

            // R3(A): targeted so it inherits the JM chrome LookAndFeel (an
            // untargeted menu falls back to the host's default JUCE look)
            m.showMenuAsync (styledMenu (m, list_), [this, r, row] (int res)
            {
                if (res == 1) proc_.state.library.toggleFavorite (r.favKey());
                else if (res == 2) { r.file.moveToTrash(); }
                else if (res == 3) { promptRename (row); return; }
                else if (res >= 10 && res <= 13) { applyRecategorise (row, res - 10); return; }
                rebuild();
            });
            return;
        }
        activateRow (row);
    }

    /// The OVERWRITE row's label and state, from the processor's own answer to
    /// "what would a save-over replace?".
    void syncOverButton()
    {
        const bool offer = overTarget_.isNotEmpty();
        overBtn_.setVisible (offer);
        overBtn_.setArmed (overArmed_);
        overBtn_.setButtonText (! offer ? juce::String()
                                : overArmed_ ? "CLICK AGAIN TO REPLACE \"" + overTarget_ + "\""
                                             : "OVERWRITE \"" + overTarget_ + "\"");
        // The drawn line says which file changes; the full explanation — including
        // what SAVE AS does instead — lives here, so shortening the line did not
        // lose it.
        overBtn_.setTooltip (overLine_.isEmpty()
                                 ? juce::String()
                                 : overLine_ + ". SAVE AS makes a NEW preset under the typed name and "
                                               "never replaces anything.");
    }

    /// SAVE AS — a NEW preset under the typed name. It can fail, and when it
    /// fails because the name is taken it says so instead of replacing anything.
    void doSaveAs()
    {
        auto name = saveName_.getText().trim();
        if (name.isEmpty())
        {
            destLine_ = "NOT SAVED. Enter a name, or use OVERWRITE to replace the loaded preset.";
            resultIsGood_ = false;
            repaint();
            return;
        }
        const bool patch = proc_.state.currentPage <= 3;
        auto out = patch
            ? proc_.saveInstrumentPreset (slotForWorld (proc_.state, proc_.state.currentPage), name,
                                          JMSignatureProcessor::SaveIntent::saveAs)
            : proc_.savePerformanceSnapshot (name, JMSignatureProcessor::SaveIntent::saveAs);
        reportSave (out, out.ok);
    }

    /// SAVE OVER — replaces the preset that is loaded, keeping a .bak. Two
    /// clicks: the first arms and explains, the second commits. The arm goes
    /// stale after a few seconds so it can never sit waiting for a stray click.
    void doSaveOver()
    {
        if (overTarget_.isEmpty()) return;
        const auto now = juce::Time::getMillisecondCounter();
        if (! overArmed_ || now - overArmedAtMs_ > kArmWindowMs)
        {
            overArmed_ = true;
            overArmedAtMs_ = now;
            syncOverButton();
            repaint();
            return;
        }
        overArmed_ = false;
        const auto target = overTarget_;
        auto out = proc_.state.currentPage <= 3
            ? proc_.saveInstrumentPreset (slotForWorld (proc_.state, proc_.state.currentPage), target,
                                          JMSignatureProcessor::SaveIntent::saveOver)
            : proc_.savePerformanceSnapshot (target, JMSignatureProcessor::SaveIntent::saveOver);
        reportSave (out, out.ok);
    }

    /// The one-click escape from the format boundary: on a model page it saves
    /// the PERFORMANCE snapshot (which does carry modulation, the macros and the
    /// arp) rather than the patch. Named from the typed field, falling back to
    /// the performance that is loaded, so the button always has something to do.
    void doSavePerformanceInstead()
    {
        auto name = saveName_.getText().trim();
        if (name.isEmpty()) name = proc_.state.performPresetName.trim();
        if (name.isEmpty())
        {
            destLine_ = "NOT SAVED. Enter a name for the performance.";
            resultIsGood_ = false;
            repaint();
            return;
        }
        // A performance save from here is deliberately save-OVER-if-it-exists:
        // the artist pressed the button that rescues his modulation work, and
        // refusing it over a name clash would be the wrong moment to be strict.
        reportSave (proc_.savePerformanceSnapshot (name,
                        JMSignatureProcessor::SaveIntent::saveOver), true);
    }

    void reportSave (const JMSignatureProcessor::SaveOutcome& out, bool clearName)
    {
        if (out.ok && clearName) saveName_.clear();
        // A factory save belongs on the FACTORY tab and a user save on USER —
        // landing on the wrong tab is exactly how "where did my save go?" starts.
        if (out.ok)
        {
            const bool toFactory = out.dest == authoring::SaveDest::bundleFactory
                                || out.dest == authoring::SaveDest::performanceFactory;
            tabs_[toFactory ? 1 : 0].setToggleState (true, juce::dontSendNotification);
        }
        rebuild();

        // ...and say what actually happened, in place of what was going to.
        resultIsGood_ = out.ok && ! out.fellBack;
        destLine_ = out.ok ? out.message : juce::String ("NOT SAVED. ") + out.message;
        if (out.note.isNotEmpty()) destLine_ += "  (" + out.note + ")";
        repaint();
    }

    //======================================================= NAME AND RENAME
    /// The rename itself, separated from the dialog so the self-check can
    /// exercise the whole path (which references it has to keep coherent is the
    /// interesting part, and that lives in the processor).
    bool applyRename (int index, const juce::String& newName)
    {
        if (index < 0 || index >= (int) rows_.size()) return false;
        auto r = rows_[(size_t) index];
        JMSignatureProcessor::RenameOutcome out;
        switch (r.kind)
        {
            case Row::FactoryPreset:
            case Row::UserPreset:
                out = proc_.renameInstrumentPreset (r.slot, r.name, newName);
                break;
            case Row::Performance:
                out = proc_.renamePerformancePreset (r.file, newName);
                break;
            case Row::Bundle:
                out = proc_.renameBundle (r.file, newName);
                break;
        }
        rebuild();
        resultIsGood_ = out.ok;
        destLine_ = out.ok ? out.message : juce::String ("NOT RENAMED. ") + out.message;
        if (out.note.isNotEmpty()) destLine_ += "  (" + out.note + ")";
        repaint();
        return out.ok;
    }

    void promptRename (int index)
    {
        if (index < 0 || index >= (int) rows_.size()) return;
        const auto& row = rows_[(size_t) index];
        const auto oldName = row.name;
        const bool isBundle = row.kind == Row::Bundle;
        auto* aw = new juce::AlertWindow (isBundle ? "Rename sound" : "Rename preset",
                                          "New name for \"" + oldName + "\":",
                                          juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", oldName, {});
        aw->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, aw, index] (int res)
            {
                if (res == 1) applyRename (index, aw->getTextEditorContents ("name"));
                delete aw;
            }), false);
    }

    //================================================== MOVE TO ANOTHER CATEGORY
    bool applyRecategorise (int index, int world)
    {
        if (index < 0 || index >= (int) rows_.size()) return false;
        auto r = rows_[(size_t) index];
        if (r.kind != Row::Bundle) return false;
        auto out = proc_.recategoriseBundle (r.file, world);
        // The instrument has left this page (the processor moved currentPage to
        // its new one), so the list is rebuilt against where it now lives.
        rebuild();
        resultIsGood_ = out.ok;
        destLine_ = out.ok ? out.message : juce::String ("NOT MOVED. ") + out.message;
        if (out.note.isNotEmpty()) destLine_ += "  (" + out.note + ")";
        repaint();
        return out.ok;
    }

    JMSignatureProcessor& proc_;
    bool compact_;
    juce::Label title_;
    juce::TextEditor search_, saveName_;
    ux::SegmentStrip tabStrip_;
    std::array<ux::TabCellButton, 3> tabs_;
    ux::PlateButton saveBtn_;
    ux::PlateButton altSaveBtn_;                    // AUTHORING MODE: the save that keeps modulation
    ux::PlateButton overBtn_;                       // SAVE OVER, in the destructive register
    juce::String destLine_, boundaryLine_, overTarget_, overLine_;
    bool resultIsGood_ = false;
    bool overArmed_ = false;
    juce::uint32 overArmedAtMs_ = 0;
    static constexpr juce::uint32 kArmWindowMs = 6000;
    std::unique_ptr<juce::DrawableButton> filterBtn_;
    std::unique_ptr<juce::Drawable> searchIcon_ { assets::makeIcon (assets::Icon::search, Theme::muted) };
    std::unique_ptr<juce::Drawable> starIcon_ { ux::makePackIcon (ux::PackIcon::star, Theme::gold) };
    std::unique_ptr<juce::Drawable> shield_ { ux::makeShield() };
    juce::Rectangle<int> searchIconArea_, artArea_, infoArea_, warnArea_;
    juce::ListBox list_;
    std::vector<Row> rows_;
};

//==============================================================================
// One FX rack unit on a photoreal kit faceplate (docs/hardware-kit
// panel-{colour}.png). The plate already carries screws, bevel and depth, so it
// replaces the V2 drawn plate; the cells are laid out at the kit's native
// 0.764:1 ratio so the art is never stretched non-uniformly.
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
        title_.setMinimumHorizontalScale (0.7f);
        title_.setJustificationType (juce::Justification::centred);
        title_.setColour (juce::Label::textColourId, juce::Colour (0xffF7EEDB));
        addAndMakeVisible (title_);

        power_.ringColour = Theme::goldHi;
        power_.setState (! st.bypassed);
        power_.onToggle = [this] (bool on)
        {
            proc_.setParamNormalized (prefix_ + ".fx" + juce::String (idx_) + ".bypass", on ? 0.0f : 1.0f);
            bypass_.setToggleState (! on, juce::dontSendNotification);
        };
        power_.setTitle ("Enable " + juce::String (spec.name));
        addAndMakeVisible (power_);

        menuBtn_ = ux::makePackIconButton ("more", ux::PackIcon::kebab, Theme::muted);
        menuBtn_->onClick = [this] { showMenu(); };
        addAndMakeVisible (*menuBtn_);

        // CLIENT REVIEW ROUND 7 (item 2): "there's no way to delete effects from
        // rack". There was — buried in the kebab menu — so it might as well not
        // have existed. Removal is now a control ON the unit, beside BYPASS,
        // where a hardware rack would put it. The menu route still works.
        removeBtn_.setTooltip ("Remove this unit from the rack");
        removeBtn_.onClick = [this] { removeSelf(); };
        addAndMakeVisible (removeBtn_);

        // The grid in resized() packs 5+ parameters into 3 columns, which makes
        // each cell 52 px rather than 74 — so a dense unit's captions take the
        // smaller of the two type sizes. (See the grid note in resized().)
        const float capHeight = spec.params.size() <= 4 ? 10.0f : 9.0f;
        for (auto& ps : spec.params)
        {
            auto path = prefix_ + ".fx" + juce::String (idx_) + "." + ps.id;
            // `order_` is the layout order and it is the REGISTRY's order. The
            // grid used to take every knob first and every switch afterwards,
            // which moved each switch away from the parameter it qualifies: the
            // tape delay's SYNC sat two cells from TIME, and DIVISION — which
            // only means anything while SYNC is on — landed on a different row
            // from both. The registry already lists them in the order an
            // engineer reads them, so the panel follows it. See resized().
            juce::Component* added = nullptr;
            if (ps.kind == ParamKind::Float)
            {
                auto* k = new ParamKnob (proc_, path, ps.label, &ps);
                k->setShowValue (false);
                k->setLabelBelow (true, capHeight);
                knobs_.add (k);
                added = k;
            }
            else
            {
                auto* c = new ParamChoiceButton (proc_, path, ps.label, ps);
                c->setCaptionHeight (capHeight);
                c->setCaptionBelow (true);       // the knobs here use labelBelow
                choices_.add (c);
                added = c;
            }
            addAndMakeVisible (added);
            order_.add (added);
        }
        bypass_.setButtonText ("BYPASS");
        bypass_.setFontHeight (11.0f);
        bypass_.setClickingTogglesState (true);
        bypass_.setToggleState (st.bypassed, juce::dontSendNotification);
        bypass_.onClick = [this]
        {
            proc_.setParamNormalized (prefix_ + ".fx" + juce::String (idx_) + ".bypass",
                                      bypass_.getToggleState() ? 1.0f : 0.0f);
            power_.setState (! bypass_.getToggleState());
        };
        addAndMakeVisible (bypass_);
        if (type_ == FxType::widener) startTimerHz (10);
    }

    // FX personality mapping (unchanged from V1/V2, logged in DECISIONS) — the
    // FX family is its own hardware colour set, never the world colours.
    // 0 teal · 1 orange · 2 purple · 3 blue · 4 red · 5 green
    static int plateFamily (FxType t)
    {
        switch (t)
        {
            case FxType::jmChorus:       return 0;
            case FxType::vintageEq:      return 0;
            case FxType::tremoloAutoPan: return 1;
            case FxType::tapeDelay:      return 1;
            case FxType::epPreamp:       return 1;
            case FxType::phaser:         return 2;
            case FxType::flanger:        return 2;
            case FxType::hallShimmer:    return 3;
            case FxType::digitalDelay:   return 3;
            case FxType::compressor:     return 4;
            case FxType::plateReverb:    return 5;
            default:                     return 5;   // widener
        }
    }
    static juce::Colour accent (FxType t)
    {
        switch (plateFamily (t))
        {
            case 0: return Theme::fxTeal;   case 1: return Theme::fxOrange;
            case 2: return Theme::fxPurple; case 3: return Theme::fxBlue;
            case 4: return Theme::fxRed;    default: return Theme::fxGreen;
        }
    }

    void refresh()
    {
        for (auto* k : knobs_) k->refresh();
        for (auto* c : choices_) c->refresh();
        if (rackState_ != nullptr && idx_ < (int) rackState_->size())
        {
            const bool byp = (*rackState_)[(size_t) idx_].bypassed;
            bypass_.setToggleState (byp, juce::dontSendNotification);
            power_.setState (! byp);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const float u = (float) r.getWidth() / 164.0f;
        auto S = [u] (int v) { return juce::roundToInt ((float) v * u); };
        auto head = r.withY (S (8)).withHeight (S (20));
        power_.setBounds (r.getRight() - S (28), S (9), S (20), S (20));
        // R7: the top-left corner is now the GRAB HANDLE (drawn, not a
        // component), so the kebab moves down beside BYPASS and the new remove
        // chip takes the matching position on the right of it.
        gripArea_ = juce::Rectangle<int> (S (8), S (11), S (14), S (12));
        headerArea_ = juce::Rectangle<int> (0, 0, r.getWidth(), S (34));
        menuBtn_->setBounds (S (10), S (178), S (18), S (18));
        removeBtn_.setBounds (r.getRight() - S (28), S (178), S (18), S (18));
        title_.setBounds (head.reduced (S (26), 0));
        bypass_.setBounds (juce::Rectangle<int> (S (84), S (26)).withCentre ({ r.getCentreX(), S (189) }));

        // Registry order, set up in the constructor — knobs and switches
        // interleaved as the registry lists them, not grouped by kind.
        const juce::Array<juce::Component*>& items = order_;

        // CLIENT REQUEST — "put mix knob on all the time based effects".
        //
        // The grid was a FIXED 2x2 with `setVisible (i < 4)`, so a unit's fifth
        // and later parameters were created, routed, macro-droppable and
        // automatable — and invisible. That silently swallowed the MIX knob on
        // the chorus, the plate, the hall and both delays (they all carry mix
        // last), and it would have swallowed the new phaser/flanger MIX too.
        //
        // The grid is now driven by the parameter COUNT, so every registry
        // parameter has a cell. The cell geometry is chosen so the knob art
        // does not shrink until it has to: ParamKnob with a label below takes
        // `min(w, h - 13)` as its diameter, so the 2-column cell (74x58) and
        // the 3-column cell (52x58) both yield a 45 px knob — only the 7-to-9
        // parameter units (the two delays) drop to three rows and a 33 px knob.
        const int n = items.size();
        const int cols = n <= 4 ? 2 : 3;
        const int rows = n <= (cols * 2) ? 2 : 3;
        const int cellW = cols == 2 ? S (74) : S (52);
        const int cellH = rows == 2 ? S (58) : S (46);
        const int cx2[2] = { S (44), S (120) };
        const int cx3[3] = { S (30), S (82), S (134) };
        const int cy2[2] = { S (72), S (134) };
        const int cy3[3] = { S (57), S (103), S (149) };
        for (int i = 0; i < n; ++i)
        {
            const int col = i % cols, row = juce::jmin (rows - 1, i / cols);
            const int x = cols == 2 ? cx2[col] : cx3[col];
            const int y = rows == 2 ? cy2[row] : cy3[row];
            items[i]->setBounds (juce::Rectangle<int> (cellW, cellH).withCentre ({ x, y }));
            items[i]->setVisible (i < cols * rows);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        kit::drawFxPlate (g, r, plateFamily (type_));
        // opaque local plaque behind the title — never trade contrast for pattern
        const float u = r.getWidth() / 164.0f;
        auto plaque = juce::Rectangle<float> (r.getX() + 26 * u, r.getY() + 6 * u,
                                              r.getWidth() - 52 * u, 24 * u);
        g.setColour (juce::Colour (0xff0B0A08).withAlpha (0.80f));
        g.fillRoundedRectangle (plaque, 4.0f);
        g.setColour (Theme::gold.withAlpha (0.28f));
        g.drawRoundedRectangle (plaque, 4.0f, 1.0f);

        // R7 (item 3): the grab handle. Anywhere on the header strip starts a
        // reorder drag; the rails say so.
        ux::drawGrip (g, gripArea_.toFloat(), headerHot_);

        if (type_ == FxType::widener && rackDsp_ != nullptr)   // correlation meter
        {
            const float corr = rackDsp_->widenerCorrelation();
            auto bar = juce::Rectangle<float> (r.getX() + 30 * u, r.getY() + 168 * u,
                                               r.getWidth() - 60 * u, 4 * u);
            g.setColour (Theme::black);
            g.fillRect (bar);
            const float t = juce::jlimit (0.0f, 1.0f, (corr + 1.0f) * 0.5f);
            g.setColour (corr < 0.0f ? Theme::warnRed : Theme::okGreen);
            g.fillRect (bar.getX() + (bar.getWidth() - 4.0f) * t, bar.getY(), 4.0f, bar.getHeight());
        }
    }

    std::function<void()> onRackEdit;   // set by FxRackStrip: rebuild after structural ops

    int rackIndex() const { return idx_; }
    const juce::String& rackPrefix() const { return prefix_; }

    //=================================================== CLIENT REVIEW R7 (3)
    // Drag to reorder, spec §4. The gesture is armed ONLY from the header
    // strip: a knob is a child component and consumes its own mouse events, so
    // a drag that starts on a knob never reaches this handler and stays a knob
    // drag. Nothing about the existing controls changes.
    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool hot = headerArea_.contains (e.getPosition());
        setMouseCursor (hot ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        if (hot != headerHot_) { headerHot_ = hot; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        if (headerHot_) { headerHot_ = false; repaint(); }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragArmed_ = headerArea_.contains (e.getPosition());
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragArmed_ || dragging_ || e.getDistanceFromDragStart() < 6) return;
        if (rackState_ == nullptr || (int) rackState_->size() < 2) return;    // nothing to reorder
        auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this);
        if (c == nullptr) return;
        dragging_ = true;
        c->startDragging (fxdrag::makePayload (prefix_, idx_), this,
                          juce::ScaledImage (fxdrag::makeGhost (*this), fxdrag::kGhostScale), true);
    }
    void mouseUp (const juce::MouseEvent&) override { dragArmed_ = false; dragging_ = false; }

private:
    void timerCallback() override { repaint(); }

    /// R7 (item 2). Shared by the remove chip and the menu's "Remove" item so
    /// the two routes cannot drift.
    void removeSelf()
    {
        if (rackState_ == nullptr || idx_ >= (int) rackState_->size()) return;
        auto next = *rackState_;
        next.erase (next.begin() + idx_);
        commitStructural (std::move (next));
    }

    /// The one place a structural rack edit is applied: the model changes now
    /// (so the strip redraws immediately) and the DSP follows through the
    /// clickless two-step in FxRackDsp::applyStructuralChange.
    void commitStructural (FxRackState next)
    {
        *rackState_ = next;
        rackDsp_->applyStructuralChange (next);
        // onRackEdit rebuilds the strip, which DELETES this panel. Everything
        // that touches `this` has to happen before it, and the callback is
        // copied out first because the member itself dies with the panel.
        auto rebuildStrip = onRackEdit;
        proc_.stateChanged.sendChangeMessage();
        if (rebuildStrip) rebuildStrip();
    }

    void showMenu()
    {
        juce::PopupMenu m;
        juce::PopupMenu repl;
        for (auto& s : fxRegistry()) repl.addItem (100 + (int) s.type, s.name);
        m.addSubMenu ("Replace with", repl);
        m.addItem (1, "Move left", idx_ > 0);
        m.addItem (2, "Move right", rackState_ != nullptr && idx_ + 1 < (int) rackState_->size());
        m.addItem (3, "Remove");
        m.showMenuAsync (styledMenu (m, *menuBtn_), [this] (int r)
        {
            if (rackState_ == nullptr || idx_ >= (int) rackState_->size() || r == 0) return;
            auto next = *rackState_;
            if (r == 1 && idx_ > 0) std::swap (next[(size_t) idx_], next[(size_t) idx_ - 1]);
            else if (r == 2 && idx_ + 1 < (int) next.size()) std::swap (next[(size_t) idx_], next[(size_t) idx_ + 1]);
            else if (r == 3) next.erase (next.begin() + idx_);
            else if (r >= 100)
            {
                // an explicitly REPLACED unit is a deliberate act, so it comes
                // in active — unlike a default rack, which is built bypassed
                next[(size_t) idx_] = defaultUnitState ((FxType) (r - 100), false);
            }
            else return;
            commitStructural (std::move (next));
        });
    }

    JMSignatureProcessor& proc_;
    juce::String prefix_;
    int idx_;
    FxType type_;
    FxRackState* rackState_;
    FxRackDsp* rackDsp_;
    juce::Label title_;
    PowerButton power_;
    std::unique_ptr<juce::DrawableButton> menuBtn_;
    ux::PlateButton bypass_;
    ux::RemoveChipButton removeBtn_;
    juce::OwnedArray<ParamKnob> knobs_;
    juce::OwnedArray<ParamChoiceButton> choices_;
    /// Layout order = registry order. Owns nothing; knobs_ and choices_ do.
    juce::Array<juce::Component*> order_;
    juce::Rectangle<int> gripArea_, headerArea_;
    bool headerHot_ = false, dragArmed_ = false, dragging_ = false;
};

//==============================================================================
// MASTER FX column at the right of the rack strip (approved reference). Hosts
// the post-mix master rack's leading unit compactly plus the master output LED
// ladder; the kebab opens the same add/replace/remove menus as any rack unit,
// so no function is added or lost.
class MasterFxColumn : public juce::Component
{
public:
    explicit MasterFxColumn (JMSignatureProcessor& p) : proc_ (p) { rebuild(); }

    void rebuild()
    {
        knobs_.clear();
        auto& rack = proc_.state.masterRack;
        unitName_.clear();
        if (! rack.empty())
        {
            auto& spec = fxSpec (rack.front().type);
            unitName_ = juce::String (spec.name).toUpperCase();
            int n = 0;
            for (auto& ps : spec.params)
            {
                if (ps.kind != ParamKind::Float) continue;
                if (n >= 3) break;
                auto* k = new ParamKnob (proc_, "master.fx0." + juce::String (ps.id), ps.label, &ps);
                k->setShowValue (false);
                k->setLabelBelow (true, 10.0f);
                knobs_.add (k);
                addAndMakeVisible (k);
                ++n;
            }
        }
        if (menuBtn_ == nullptr)
        {
            menuBtn_ = ux::makePackIconButton ("mfx", ux::PackIcon::kebab, Theme::muted);
            menuBtn_->onClick = [this] { showMenu(); };
            addAndMakeVisible (*menuBtn_);
        }
        resized();
        repaint();
    }

    void refresh()
    {
        for (auto* k : knobs_) k->refresh();
        const float pk = juce::jmax (proc_.masterPeakL.load(), proc_.masterPeakR.load());
        if (std::abs (pk - pk_) > 1.0e-3f) { pk_ = pk; repaint(); }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const float u = (float) r.getWidth() / 196.0f;
        auto S = [u] (int v) { return juce::roundToInt ((float) v * u); };
        menuBtn_->setBounds (r.getRight() - S (26), S (8), S (18), S (18));
        const int cx[3] = { S (46), S (104), S (46) };
        const int cy[3] = { S (78), S (78), S (156) };
        for (int i = 0; i < knobs_.size(); ++i)
            knobs_[i]->setBounds (juce::Rectangle<int> (S (58), S (58))
                                      .withCentre ({ cx[juce::jmin (i, 2)], cy[juce::jmin (i, 2)] }));
        meterArea_ = juce::Rectangle<int> (S (148), S (48), S (22), S (146));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        kit::drawNineSlice (g, kit::image (kit::Img::panelMaster), r, 48, 48, 1.0f);
        g.setColour (Theme::goldHi);
        g.setFont (Theme::display (15.0f));
        g.drawText ("MASTER FX", r.withTrimmedTop (8.0f).withHeight (20.0f), juce::Justification::centred, false);
        if (unitName_.isNotEmpty())
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::ui (9.0f));
            g.drawText (unitName_, r.withTrimmedTop (28.0f).withHeight (12.0f), juce::Justification::centred, false);
        }
        else
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::ui (11.0f));
            g.drawFittedText ("RACK EMPTY\nADD VIA MENU",
                              r.reduced (44.0f, 70.0f).toNearestInt(), juce::Justification::centred, 2);
        }
        // dB ladder + kit LED meter
        auto m = meterArea_.toFloat();
        kit::drawLedMeterV (g, m, LevelMeter::norm (pk_));
        g.setColour (Theme::muted.withAlpha (0.8f));
        g.setFont (Theme::value (8.0f));
        const char* ticks[] = { "0", "-12", "-24", "-36", "-60" };
        for (int i = 0; i < 5; ++i)
            g.drawText (ticks[i], m.getRight() + 2.0f, m.getY() + m.getHeight() * (float) i / 4.6f - 5.0f,
                        24.0f, 10.0f, juce::Justification::centredLeft, false);
    }

private:
    void showMenu()
    {
        juce::PopupMenu m, add, repl;
        for (auto& s : fxRegistry()) add.addItem (100 + (int) s.type, s.name);
        m.addSubMenu ("Add unit", add, (int) proc_.state.masterRack.size() < FxRackDsp::kMaxSlots);
        if (! proc_.state.masterRack.empty())
        {
            for (auto& s : fxRegistry()) repl.addItem (300 + (int) s.type, s.name);
            m.addSubMenu ("Replace first unit", repl);
            m.addItem (2, "Remove first unit");
        }
        m.showMenuAsync (styledMenu (m, *menuBtn_), [this] (int r)
        {
            // R7 (item 1): the master rack is EMPTY by default and every unit it
            // gains from here is an explicit act, so those arrive active; what
            // it must never do is build itself a default rack that is on. It
            // goes through the same clickless two-step as the slot racks.
            auto next = proc_.state.masterRack;
            if (r >= 300) { if (next.empty()) return;
                            next.front() = defaultUnitState ((FxType) (r - 300), false); }
            else if (r >= 100) { if ((int) next.size() >= FxRackDsp::kMaxSlots) return;
                                 next.push_back (defaultUnitState ((FxType) (r - 100), false)); }
            else if (r == 2 && ! next.empty()) next.erase (next.begin());
            else return;
            proc_.state.masterRack = next;
            proc_.masterRack.applyStructuralChange (next);
            rebuild();
            proc_.stateChanged.sendChangeMessage();
        });
    }

    JMSignatureProcessor& proc_;
    juce::String unitName_;
    juce::OwnedArray<ParamKnob> knobs_;
    std::unique_ptr<juce::DrawableButton> menuBtn_;
    juce::Rectangle<int> meterArea_;
    float pk_ = 0;
};

//==============================================================================
// Bottom FX rack strip, restored to the approved arrangement: "FX RACK" title,
// a left vertical rail of rack icons, six colour-coded units, page dots and the
// MASTER FX column at the right.
class FxRackStrip : public juce::Component,
                    public juce::DragAndDropTarget
{
public:
    FxRackStrip (JMSignatureProcessor& p) : proc_ (p)
    {
        master_ = std::make_unique<MasterFxColumn> (proc_);
        addAndMakeVisible (*master_);
        addBtn_.setButtonText ("+ ADD UNIT");
        addBtn_.setFontHeight (11.0f);
        addBtn_.onClick = [this] { showAddMenu(); };
        addAndMakeVisible (addBtn_);
        rebuild();
    }

    void rebuild()
    {
        panels_.clear();
        const int page = proc_.state.currentPage;
        const int slot = page <= 3 ? slotForWorld (proc_.state, page) : 0;
        prefix_ = "slot" + juce::String (slot);
        rackState_ = &proc_.state.slots[(size_t) slot].rack;
        rackDsp_ = &proc_.slotRacks[(size_t) slot];
        slotLabel_ = juce::String::charToString ((juce::juce_wchar) ('A' + slot))
                     + "  (" + worldName (proc_.state.slots[(size_t) slot].world) + ")";
        const int shown = juce::jmin (6, (int) rackState_->size());
        for (int i = 0; i < shown; ++i)
        {
            auto* pn = new FxUnitPanel (proc_, prefix_, i, (*rackState_)[(size_t) i], rackState_, rackDsp_);
            pn->onRackEdit = [this] { rebuild(); };
            panels_.add (pn);
            addAndMakeVisible (pn);
        }
        addBtn_.setVisible (shown < 6 && (int) rackState_->size() < FxRackDsp::kMaxSlots);
        master_->rebuild();
        resized();
        repaint();
    }

    void refresh()
    {
        for (auto* pn : panels_) pn->refresh();
        master_->refresh();
    }

    //=================================================== CLIENT REVIEW R7 (3)
    // The strip is the drop target for its own units. The FX panels are not
    // targets themselves, so every drag-move over a unit lands here and the
    // insertion point is computed from the pointer's x against the unit
    // centres — the standard "which gap am I nearest" rule.
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return fxdrag::isPayload (d.description) && fxdrag::rackOf (d.description) == prefix_;
    }
    void itemDragEnter (const SourceDetails& d) override { itemDragMove (d); }
    void itemDragMove (const SourceDetails& d) override
    {
        const int gap = gapForX (d.localPosition.x);
        if (gap != dropGap_) { dropGap_ = gap; repaint(); }
    }
    void itemDragExit (const SourceDetails&) override
    {
        if (dropGap_ >= 0) { dropGap_ = -1; repaint(); }
    }
    void itemDropped (const SourceDetails& d) override
    {
        const int from = fxdrag::indexOf (d.description);
        const int gap = gapForX (d.localPosition.x);
        dropGap_ = -1;
        repaint();
        if (rackState_ == nullptr || from < 0 || from >= (int) rackState_->size()) return;
        // `gap` counts gaps, so once the unit is lifted out everything after it
        // shifts down by one; dropping into either gap adjacent to itself is a
        // no-op rather than a needless rack rebuild.
        const int to = gap > from ? gap - 1 : gap;
        if (to == from) return;
        auto next = *rackState_;
        auto moved = next[(size_t) from];
        next.erase (next.begin() + from);
        next.insert (next.begin() + juce::jlimit (0, (int) next.size(), to), std::move (moved));
        *rackState_ = next;
        rackDsp_->applyStructuralChange (next);
        rebuild();
        proc_.stateChanged.sendChangeMessage();
    }

    void resized() override
    {
        using L = Theme::L;
        for (int i = 0; i < panels_.size(); ++i)
            panels_[i]->setBounds (L::fxUnitX + i * L::fxUnitPitch, L::fxUnitY, L::fxUnitW, L::fxUnitH);
        if (addBtn_.isVisible())
            addBtn_.setBounds (juce::Rectangle<int> (110, 30)
                                   .withCentre ({ L::fxUnitX + panels_.size() * L::fxUnitPitch + L::fxUnitW / 2,
                                                  L::fxUnitY + L::fxUnitH / 2 }));
        master_->setBounds (L::fxMasterX, L::fxMasterY, L::fxMasterW, L::fxMasterH);
    }

    void paint (juce::Graphics& g) override
    {
        using L = Theme::L;
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient body (juce::Colour (0xff0E0D0B), r.getX(), r.getY(),
                                   juce::Colour (0xff060605), r.getX(), r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Theme::goldDark.withAlpha (0.5f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        // kit rack corners (single accent per corner)
        const float cs = 34.0f;
        kit::drawRackCorner (g, { r.getX() + 2, r.getY() + 2, cs, cs }, 0, 0.85f);
        kit::drawRackCorner (g, { r.getRight() - cs - 2, r.getY() + 2, cs, cs }, 1, 0.85f);

        g.setColour (Theme::gold);
        g.setFont (Theme::display (14.0f));
        g.drawText ("F X   R A C K      SLOT " + slotLabel_, r.withHeight (24.0f).withTrimmedTop (4.0f),
                    juce::Justification::centred, false);

        // left rack rail — five recessed page/rack glyphs (decorative fittings)
        auto rail = juce::Rectangle<float> ((float) L::fxRailX, (float) L::fxRailY,
                                            (float) L::fxRailW, (float) L::fxRailH);
        g.setColour (juce::Colour (0xff0A0908));
        g.fillRoundedRectangle (rail, 5.0f);
        g.setColour (Theme::goldDark.withAlpha (0.5f));
        g.drawRoundedRectangle (rail, 5.0f, 1.0f);
        for (int i = 0; i < 5; ++i)
        {
            auto cell = juce::Rectangle<float> (rail.getX() + 4.0f, rail.getY() + 8.0f + (float) i * 40.0f,
                                                rail.getWidth() - 8.0f, 32.0f);
            const bool on = i == 2;
            g.setColour (on ? Theme::gold.withAlpha (0.18f) : juce::Colours::transparentBlack);
            g.fillRoundedRectangle (cell, 4.0f);
            g.setColour (on ? Theme::goldHi : Theme::muted.withAlpha (0.55f));
            auto q = cell.reduced (9.0f);
            for (int k = 0; k < 4; ++k)
                g.fillRect (q.getX() + (k % 2) * (q.getWidth() * 0.6f),
                            q.getY() + (k / 2) * (q.getHeight() * 0.6f),
                            q.getWidth() * 0.4f, q.getHeight() * 0.4f);
        }

        // page dots under the units
        const int pages = juce::jmax (1, ((int) (rackState_ != nullptr ? rackState_->size() : 0) + 5) / 6);
        const float dcx = (float) (L::fxUnitX + 3 * L::fxUnitPitch);
        for (int i = 0; i < juce::jmax (pages, 5); ++i)
        {
            const bool active = i == 0;
            const bool real = i < pages;
            g.setColour (active ? Theme::gold : Theme::muted.withAlpha (real ? 0.5f : 0.22f));
            g.fillEllipse (dcx - 40.0f + (float) i * 20.0f, (float) L::fxDotsY, active ? 8.0f : 6.0f,
                           active ? 8.0f : 6.0f);
        }

        // R7 (item 3): the insertion rule — where the dragged unit will land.
        // Drawn in the gap between units so it never covers a control.
        if (dropGap_ >= 0)
        {
            const float x = (float) (L::fxUnitX + dropGap_ * L::fxUnitPitch) - 4.0f;
            auto bar = juce::Rectangle<float> (x - 1.5f, (float) L::fxUnitY - 4.0f,
                                               3.0f, (float) L::fxUnitH + 8.0f);
            g.setColour (Theme::gold.withAlpha (0.35f));
            g.fillRoundedRectangle (bar.expanded (3.0f, 0.0f), 3.0f);
            g.setColour (Theme::goldHi);
            g.fillRoundedRectangle (bar, 1.5f);
            for (int end = 0; end < 2; ++end)          // the rule's end caps
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f)
                                   .withCentre ({ x, end == 0 ? bar.getY() : bar.getBottom() }));
        }
    }

private:
    /// Insertion GAP (0..units) nearest to a pointer x in strip coordinates.
    int gapForX (int x) const
    {
        using L = Theme::L;
        const int n = panels_.size();
        int gap = 0;
        for (int i = 0; i < n; ++i)
            if (x > L::fxUnitX + i * L::fxUnitPitch + L::fxUnitW / 2) gap = i + 1;
        return juce::jlimit (0, n, gap);
    }

    void showAddMenu()
    {
        juce::PopupMenu m;
        for (auto& s : fxRegistry()) m.addItem (100 + (int) s.type, s.name);
        m.showMenuAsync (styledMenu (m, addBtn_), [this] (int r)
        {
            if (r < 100 || rackState_ == nullptr) return;
            if ((int) rackState_->size() >= FxRackDsp::kMaxSlots) return;
            // A unit the player adds by hand arrives ACTIVE: that is an explicit
            // act, unlike the world's default rack which now loads bypassed.
            auto next = *rackState_;
            next.push_back (defaultUnitState ((FxType) (r - 100), false));
            *rackState_ = next;
            rackDsp_->applyStructuralChange (next);
            rebuild();
            proc_.stateChanged.sendChangeMessage();
        });
    }

    JMSignatureProcessor& proc_;
    juce::String prefix_, slotLabel_;
    FxRackState* rackState_ = nullptr;
    FxRackDsp* rackDsp_ = nullptr;
    ux::PlateButton addBtn_;
    juce::OwnedArray<FxUnitPanel> panels_;
    std::unique_ptr<MasterFxColumn> master_;
    /// insertion point while a reorder drag is over the strip; -1 = no drag
    int dropGap_ = -1;
};

//==============================================================================
// Model-page OUTPUT meter along the panel's right edge (approved reference):
// framed column, "OUTPUT" caption, dB ladder and the kit LED ladder.
class OutputMeterV2 : public juce::Component
{
public:
    void setLevel (float l) { if (std::abs (l - lvl_) > 1.0e-3f) { lvl_ = l; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0A0908).withAlpha (0.92f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Theme::goldDark.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colour (0xffCDBB93));
        g.setFont (Theme::display (9.0f));
        g.drawText ("OUTPUT", r.withTrimmedTop (8.0f).withHeight (14.0f), juce::Justification::centred, false);
        auto ladder = r.reduced (6.0f).withTrimmedTop (26.0f).withTrimmedBottom (6.0f);
        auto bar = ladder.removeFromRight (ladder.getWidth() * 0.5f);
        kit::drawLedMeterV (g, bar, LevelMeter::norm (lvl_));
        g.setColour (Theme::muted.withAlpha (0.75f));
        g.setFont (Theme::value (8.0f));
        const char* ticks[] = { "0", "-6", "-12", "-18", "-24", "-36", "-60" };
        for (int i = 0; i < 7; ++i)
            g.drawText (ticks[i], ladder.getX() - 2.0f,
                        ladder.getY() + ladder.getHeight() * (float) i / 6.6f - 5.0f,
                        ladder.getWidth() + 4.0f, 10.0f, juce::Justification::centredRight, false);
    }
private:
    float lvl_ = 0;
};

//==============================================================================
// Centre model panel — kente surround, title + patch name with prev/next and a
// favourite heart, the hero knobs on a recessed control surface, the OUTPUT
// meter along the right edge and the mature sub-tab strip along the bottom.
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

        addAndMakeVisible (tabStrip_);
        const char* tabNames[] = { "MAIN", "AMP ENV", "EQ", "SETTINGS", "INFO" };
        for (int i = 0; i < 5; ++i)
        {
            auto& b = tabs_[(size_t) i];
            b.setButtonText (tabNames[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (888);
            b.setFirstCell (i == 0);
            b.setFontHeight (14.0f);
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
        repaint (0, Theme::L::mpPatchY - 4, getWidth(), 26);      // live patch subtitle
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
        proc_.makeSlotAudible (slot_);      // ROUND 8 — stepping patches is auditioning
    }

    void rebuildContent()
    {
        knobs_.clear();
        choices_.clear();
        order_.clear();                 // holds borrowed pointers into both
        settingsWidgets_.clear();
        browser_.reset();
        infoText_ = nullptr;
        slot_ = slotForWorld (proc_.state, world_);
        auto slotStr = juce::String (slot_);

        if (tab_ == 0)          // MAIN — hero knobs at the approved positions
        {
            using L = Theme::L;
            auto& ctrls = worldEngineControls (world_);
            const bool six = (int) ctrls.size() == 6;
            const int d = L::mpKnobD;
            int i = 0;
            for (auto& spec : ctrls)
            {
                const int* ctr = six ? L::mpSix[juce::jmin (i, 5)] : L::mpFive[juce::jmin (i, 4)];
                auto* k = new ParamKnob (proc_, "slot" + slotStr + ".engine." + spec.id, spec.label, &spec);
                k->setMotif (i % 5);
                k->setAccent (Theme::worldAccent (world_));
                k->setV2 (d, L::mpLabelOff, L::mpValueOff, 16.0f, 16.0f);
                k->setBounds (juce::Rectangle<int> (d + 70, d + 100).withCentre ({ ctr[0], ctr[1] }));
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
                        juce::Component* added = nullptr;
                        if (spec.kind == ParamKind::Float)
                        {
                            auto* k = new ParamKnob (proc_, path, spec.label, &spec);
                            knobs_.add (k); added = k;
                        }
                        else
                        {
                            auto* c = new ParamChoiceButton (proc_, path, spec.label, spec);
                            choices_.add (c); added = c;
                        }
                        addAndMakeVisible (added);
                        order_.add (added);
                    }
            }
            else if (tab_ == 3) // SETTINGS — library folder + rescan
            {
                auto* lbl = new juce::Label();
                lbl->setText ("LIBRARY FOLDER", juce::dontSendNotification);
                lbl->setFont (Theme::display (13.0f));
                lbl->setColour (juce::Label::textColourId, Theme::muted);
                addAndMakeVisible (lbl);
                settingsWidgets_.add (lbl);

                auto* path = new juce::Label();
                path->setText (proc_.state.library.libraryDir().getFullPathName()
                               + (proc_.state.library.usedFallback() ? "   (empty - using testdata fallback)" : ""),
                               juce::dontSendNotification);
                path->setFont (Theme::value (13.0f));
                path->setColour (juce::Label::textColourId, Theme::cream);
                addAndMakeVisible (path);
                settingsWidgets_.add (path);

                auto* browse = new ux::PlateButton ("BROWSE...");
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

                auto* rescan = new ux::PlateButton ("RESCAN LIBRARY");
                rescan->onClick = [this]
                {
                    proc_.state.library.rescan();
                    proc_.stateChanged.sendChangeMessage();
                };
                addAndMakeVisible (rescan);
                settingsWidgets_.add (rescan);

                // IMPORT A YAMAHA LIBRARY. The artist's own .X7L / .X8L files
                // become .jmi bundles in his library folder without a round trip
                // through the studio. The whole chain is in
                // engine/import/X7lImportJob.h; this is only the gesture.
                auto* import = new ux::PlateButton ("IMPORT LIBRARY...");
                import->setTooltip ("Convert Yamaha .X7L / .X8L libraries into this library folder. "
                                    "Existing instruments are never overwritten.");
                import->onClick = [this] { chooseX7lFiles(); };
                addAndMakeVisible (import);
                settingsWidgets_.add (import);
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

    //================================================ IMPORT A YAMAHA LIBRARY
    // Runs on a ThreadWithProgressWindow, not the message thread: a library is
    // hundreds of megabytes and the conversion writes every sample to disk, so
    // doing it inline would freeze the editor for minutes with no way to stop.
    // The destination is the LIBRARY FOLDER shown directly above the button, so
    // where it lands is on screen before the gesture starts.
    // launchThread() rather than runThread(): the latter needs modal loops,
    // which a plugin build does not have. The task owns itself, calls
    // threadComplete on the message thread when it finishes or is cancelled, and
    // deletes itself afterwards - so the completion handler is a callback rather
    // than code after a blocking call.
    struct ImportTask : public juce::ThreadWithProgressWindow
    {
        ImportTask (juce::Array<juce::File> f, juce::File dest, juce::String who,
                    std::function<void (bool, x7l::ImportReport)> done)
            : juce::ThreadWithProgressWindow ("Importing libraries", true, true),
              files (std::move (f)), destDir (std::move (dest)), author (std::move (who)),
              onDone (std::move (done))
        {
            setStatusMessage ("Reading...");
        }

        void run() override
        {
            report = x7l::importLibraries (
                files, destDir, author,
                [this] { return threadShouldExit(); },
                [this] (const juce::String& what, double p)
                {
                    setProgress (p);
                    setStatusMessage (what);
                });
        }

        void threadComplete (bool userCancelled) override
        {
            if (onDone) onDone (! userCancelled, report);
            delete this;
        }

        juce::Array<juce::File> files;
        juce::File destDir;
        juce::String author;
        std::function<void (bool, x7l::ImportReport)> onDone;
        x7l::ImportReport report;
    };

    void chooseX7lFiles()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Choose Yamaha libraries to import",
            juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"),
            "*.X7L;*.X8L;*.x7l;*.x8l");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectMultipleItems,
            [this] (const juce::FileChooser& fc)
            {
                juce::Array<juce::File> files;
                for (auto& f : fc.getResults()) if (f.existsAsFile()) files.add (f);
                if (files.isEmpty()) return;
                runX7lImport (files);
            });
    }

    void runX7lImport (const juce::Array<juce::File>& files)
    {
        const auto dest = proc_.state.library.libraryDir();
        if (! dest.isDirectory())
        {
            styledAlert ("Import libraries",
                         "Set the LIBRARY FOLDER first. Imported instruments are written into it.");
            return;
        }
        // The panel can be rebuilt or destroyed while the import runs, so the
        // completion handler holds a SafePointer and checks it.
        juce::Component::SafePointer<ModelPagePanel> safe (this);
        auto* task = new ImportTask (files, dest, "James Mireku",
            [safe] (bool finished, x7l::ImportReport report) mutable
            {
                auto lines = report.summaryLines();
                if (! finished) lines.insert (0, "Stopped. What had already been written was kept.");
                if (auto* self = safe.getComponent())
                {
                    self->proc_.state.library.rescan();
                    self->proc_.stateChanged.sendChangeMessage();
                    self->rebuildContent();
                    self->styledAlert ("Import libraries", lines.joinIntoString ("\n"));
                }
            });
        task->launchThread();
    }

    void styledAlert (const juce::String& title, const juce::String& body)
    {
        auto opts = juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::NoIcon)
                        .withTitle (title)
                        .withMessage (body)
                        .withButton ("OK");
        juce::AlertWindow::showAsync (opts, nullptr);
    }

    void toggleBrowser()
    {
        if (browser_ != nullptr) { browser_.reset(); setKnobsVisible (true); repaint(); return; }
        browser_ = std::make_unique<PresetBrowserPanel> (proc_, true);
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
        using L = Theme::L;
        const int cx = getWidth() / 2;
        patchBtn_.setBounds (cx - 150, L::mpTitleY - 4, 300, 60);
        prevBtn_->setBounds (cx - 242, L::mpTitleY + 4, 26, 26);
        nextBtn_->setBounds (cx + 216, L::mpTitleY + 4, 26, 26);
        heartBtn_->setBounds (cx + 254, L::mpTitleY + 4, 26, 26);
        meter_.setBounds (L::mpMeterX, L::mpMeterY, L::mpMeterW, L::mpMeterH);

        tabStrip_.setBounds (L::mpTabsX, L::mpTabsY, L::mpTabsW, L::mpTabsH);
        {
            auto tr = tabStrip_.getBounds().reduced (4, 4);
            const int tw = tr.getWidth() / 5;
            for (int i = 0; i < 5; ++i)
                tabs_[(size_t) i].setBounds (tr.removeFromLeft (i == 4 ? tr.getWidth() : tw));
        }

        const juce::Rectangle<int> surface (L::mpSurfX, L::mpSurfY, L::mpSurfW, L::mpSurfH);
        if (browser_ != nullptr) browser_->setBounds (surface.reduced (60, 16));

        auto area = surface.reduced (40);
        if (tab_ == 3 && settingsWidgets_.size() >= 4)
        {
            auto r = area.reduced (20);
            settingsWidgets_[0]->setBounds (r.removeFromTop (22));
            settingsWidgets_[1]->setBounds (r.removeFromTop (26));
            r.removeFromTop (12);
            auto row = r.removeFromTop (32);
            settingsWidgets_[2]->setBounds (row.removeFromLeft (130));
            row.removeFromLeft (12);
            settingsWidgets_[3]->setBounds (row.removeFromLeft (170));
            if (settingsWidgets_.size() > 4)
            {
                row.removeFromLeft (12);
                settingsWidgets_[4]->setBounds (row.removeFromLeft (180));
            }
            return;
        }
        if (infoText_ != nullptr) { infoText_->setBounds (area.reduced (16)); return; }
        if (tab_ == 1 || tab_ == 2)
        {
            // Registry order here too. This panel grouped the other way round
            // from the FX units — switches first, then knobs — so the same
            // parameter list read in two different orders depending on which
            // panel it was drawn in.
            const juce::Array<juce::Component*>& items = order_;
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
                        comp->setBounds (cell.withSizeKeepingCentre (juce::jmin (130, cell.getWidth() - 8), 56));
                    else
                    {
                        const int k = juce::jmin (150, juce::jmin (cell.getWidth() - 4, cell.getHeight() - 4));
                        comp->setBounds (cell.withSizeKeepingCentre (k, k));
                    }
                }
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        using L = Theme::L;
        auto r = getLocalBounds().toFloat();
        drawWorldSurround (g, r, world_);

        const float cx = r.getWidth() * 0.5f;
        // dark title plaque (design law: title text never on undimmed pattern);
        // it doubles as the click affordance for the preset browser
        auto plaque = juce::Rectangle<float> (cx - 214.0f, (float) L::mpTitleY - 6.0f, 428.0f, 62.0f);
        g.setColour (juce::Colour (0xff0A0805).withAlpha (0.82f));
        g.fillRoundedRectangle (plaque, 9.0f);
        g.setColour (Theme::goldDark.withAlpha (0.75f));
        g.drawRoundedRectangle (plaque, 9.0f, 1.0f);
        g.setColour (Theme::cream);
        g.setFont (Theme::display (30.0f));
        g.drawText (juce::String (worldName (world_)).toUpperCase(), 0, L::mpTitleY, getWidth(), 32,
                    juce::Justification::centred, false);
        auto& s = proc_.state.slots[(size_t) slot_];
        g.setColour (Theme::goldHi);
        g.setFont (Theme::display (14.0f));
        g.drawText (s.bundleName.isEmpty() ? "NO BUNDLE - CLICK TO BROWSE"
                                           : (s.bundleName + "  -  " + s.presetName).toUpperCase(),
                    0, L::mpPatchY, getWidth(), 18, juce::Justification::centred, false);

        if (tab_ != 0 || browser_ != nullptr)   // overlay plaque over the surface
        {
            auto surf = juce::Rectangle<float> ((float) L::mpSurfX, (float) L::mpSurfY,
                                                (float) L::mpSurfW, (float) L::mpSurfH);
            g.setColour (juce::Colour (0xff0A0805).withAlpha (0.96f));
            g.fillRoundedRectangle (surf, 12.0f);
            g.setColour (Theme::goldDark);
            g.drawRoundedRectangle (surf, 12.0f, 1.0f);
        }
    }

    /// Kente surround + recessed control surface (V2 materials retained: the
    /// graded raster, the vignette and the recess treatment).
    static void drawWorldSurround (juce::Graphics& g, juce::Rectangle<float> r, int world)
    {
        using L = Theme::L;
        juce::Path clip;
        clip.addRoundedRectangle (r, 10.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (clip);
            g.setColour (Theme::chassis);
            g.fillRect (r);
            auto& img = ux::kenteGraded (world);
            if (img.isValid()) ux::drawCover (g, img, r, Theme::V2::backdropOpacity);
            else KenteTexture::fill (g, r, KenteTexture::Style::Weave, Theme::worldAccent (world), 0.8f, 10.0f, world);
            // CLIENT REVIEW ROUND 3 (G): round 2's surround read dark and thin
            // at the left/right edges — the flat 0.34 scrim plus a vignette that
            // reached 0.80 black by the frame killed exactly the band that is
            // still on show. The reference's frame is a RICH RUST. The scrim is
            // halved, the vignette is softened and pushed outwards, and a warm
            // rust wash lifts the cloth back into the reference's register.
            g.setColour (juce::Colours::black.withAlpha (0.12f));
            g.fillRect (r);
            g.setColour (Theme::masterCloth.withAlpha (0.24f));       // warm rust wash
            g.fillRect (r);
            juce::ColourGradient vig (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                      juce::Colours::black.withAlpha (0.30f), r.getX(), r.getY(), true);
            vig.addColour (0.70, juce::Colours::transparentBlack);
            g.setGradientFill (vig);
            g.fillRect (r);
        }
        g.setColour (Theme::gold.withAlpha (0.62f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.8f);

        auto surf = juce::Rectangle<float> (r.getX() + L::mpSurfX, r.getY() + L::mpSurfY,
                                            (float) L::mpSurfW, (float) L::mpSurfH);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (surf.translated (0.0f, 4.0f), 14.0f);
        g.setColour (juce::Colour (0xff080604).withAlpha (0.93f));
        g.fillRoundedRectangle (surf, 14.0f);
        g.setColour (juce::Colour (0xff8A5B17));
        g.drawRoundedRectangle (surf, 14.0f, 2.0f);
        g.setColour (juce::Colour (0xffE3A92B).withAlpha (0.22f));
        g.drawRoundedRectangle (surf.reduced (10.0f), 10.0f, 1.0f);
        for (auto p : { surf.getTopLeft(), surf.getTopRight(), surf.getBottomLeft(), surf.getBottomRight() })
            kit::drawScrew (g, { p.x + (p.x < surf.getCentreX() ? 16.0f : -16.0f),
                                 p.y + (p.y < surf.getCentreY() ? 16.0f : -16.0f) }, 16.0f, 0.9f);
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
    ux::SegmentStrip tabStrip_;
    std::array<ux::TabCellButton, 5> tabs_;
    juce::OwnedArray<ParamKnob> knobs_;
    juce::OwnedArray<ParamChoiceButton> choices_;
    /// Layout order = registry order. Owns nothing; knobs_ and choices_ do.
    juce::Array<juce::Component*> order_;
    juce::OwnedArray<juce::Component> settingsWidgets_;
    juce::Label* infoText_ = nullptr;
    OutputMeterV2 meter_;
    std::unique_ptr<PresetBrowserPanel> browser_;
    std::unique_ptr<juce::DrawableButton> prevBtn_, nextBtn_, heartBtn_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace jm
