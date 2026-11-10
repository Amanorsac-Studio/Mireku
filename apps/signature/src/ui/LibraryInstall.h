// LibraryInstall.h — INSTALL LIBRARY (client, 2026-11-10: "install places the
// library in its right place, no need to do any moving").
//
// The player downloads one file, "MIREKU Library.mirekupack", and picks it
// here. MIREKU checks it, writes the instruments and performances into its own
// library folder with a progress bar, points itself at that folder and shows
// the sounds. The same panel installs an UPDATE pack: it merges into the
// library already there. Opened from the gear menu, and by itself on a launch
// that finds no instruments.
#pragma once
#include "Macro.h"
#include "../engine/LibraryPack.h"

namespace jm {

class LibraryInstallPanel : public ChromePanel, private juce::Thread, private juce::Timer
{
public:
    static constexpr int kWidth = 660, kHeight = 340;

    explicit LibraryInstallPanel (JMSignatureProcessor& p)
        : ChromePanel ("Install library"), juce::Thread ("MIREKU Library Install"), proc_ (p)
    {
        // an update goes where the library already is; a first install goes to
        // MIREKU's own folder, which is where it looks without being told
        dest_ = proc_.state.library.bundles().empty() ? Library::defaultLibraryDir()
                                                      : proc_.state.library.libraryDir();
        choose_.setButtonText ("CHOOSE THE LIBRARY FILE...");
        choose_.setFontHeight (13.0f);
        choose_.onClick = [this] { chooseFile(); };
        addAndMakeVisible (choose_);

        change_.setButtonText ("CHANGE...");
        change_.setFontHeight (11.0f);
        change_.setTooltip ("Install onto another disk or folder");
        change_.onClick = [this] { chooseFolder(); };
        addAndMakeVisible (change_);

        stop_.setButtonText ("STOP");
        stop_.setFontHeight (11.0f);
        stop_.onClick = [this] { progress_.cancel.store (true); };
        addChildComponent (stop_);

        status_ = "Nothing is installed yet. Choose the file named MIREKU Library" + juce::String (librarypack::kExtension) + ".";
        if (! proc_.state.library.bundles().empty())
            status_ = juce::String ((int) proc_.state.library.bundles().size())
                    + " instruments are installed. Choose a library or update file to add to them.";
    }

    ~LibraryInstallPanel() override
    {
        progress_.cancel.store (true);
        stopThread (30000);
    }

    // --- self-check hooks: the install itself, without the file dialogs
    void setDestination (const juce::File& d) { dest_ = d; repaint(); }
    juce::File destination() const { return dest_; }
    void installFrom (const juce::File& pack) { begin (pack); }
    bool running() const { return isThreadRunning(); }
    bool finishedOk() const { return done_ && result_.ok; }
    juce::String statusText() const { return status_; }

    void contentResized (juce::Rectangle<int> area) override
    {
        auto r = area.reduced (12, 0);
        r.removeFromTop (62);                                    // the two lines of text
        auto destRow = r.removeFromTop (30);
        change_.setBounds (destRow.removeFromRight (110).reduced (0, 2));
        destArea_ = destRow;
        r.removeFromTop (16);
        choose_.setBounds (r.removeFromTop (40).withSizeKeepingCentre (300, 38));
        r.removeFromTop (18);
        auto barRow = r.removeFromTop (22);
        stop_.setBounds (barRow.removeFromRight (80));
        barRow.removeFromRight (10);
        barArea_ = barRow;
        r.removeFromTop (8);
        statusArea_ = r.removeFromTop (40);
    }

    void paint (juce::Graphics& g) override
    {
        ChromePanel::paint (g);
        g.setColour (Theme::cream);
        g.setFont (Theme::ui (13.0f));
        g.drawFittedText ("Choose the library file you downloaded. MIREKU checks it, puts every instrument and "
                          "performance in place and shows them. There is nothing to unzip or move.",
                          28, 60, getWidth() - 56, 40, juce::Justification::topLeft, 2, 0.95f);

        g.setColour (Theme::muted);
        g.setFont (Theme::display (11.0f));
        g.drawText ("INSTALL TO", destArea_.withWidth (90), juce::Justification::centredLeft, false);
        g.setColour (Theme::goldHi);
        g.setFont (Theme::value (12.0f));
        g.drawText (dest_.getFullPathName(), destArea_.withTrimmedLeft (92).withTrimmedRight (8),
                    juce::Justification::centredLeft, true);

        // the bar
        auto bar = barArea_.toFloat();
        g.setColour (juce::Colour (0xff0A0908));
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (Theme::goldDark.withAlpha (0.7f));
        g.drawRoundedRectangle (bar.reduced (0.5f), 5.0f, 1.0f);
        if (total_ > 0)
        {
            const float f = juce::jlimit (0.0f, 1.0f, (float) ((double) progress_.bytesDone.load() / (double) total_));
            g.setColour (done_ && ! result_.ok ? Theme::warnRed : Theme::gold);
            g.fillRoundedRectangle (bar.reduced (2.0f).withWidth (juce::jmax (4.0f, (bar.getWidth() - 4.0f) * f)), 4.0f);
        }

        g.setColour (done_ ? (result_.ok ? Theme::okGreen : Theme::warnRed) : Theme::cream);
        g.setFont (Theme::ui (12.5f));
        g.drawFittedText (status_, statusArea_.reduced (2, 0), juce::Justification::topLeft, 2, 0.95f);
    }

private:
    void chooseFile()
    {
        if (isThreadRunning()) return;
        chooser_ = std::make_unique<juce::FileChooser> ("Choose the MIREKU Library file",
                                                        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                                            .getChildFile ("Downloads"),
                                                        juce::String ("*") + librarypack::kExtension);
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [this] (const juce::FileChooser& fc)
                               {
                                   if (fc.getResult().existsAsFile()) begin (fc.getResult());
                               });
    }

    void chooseFolder()
    {
        if (isThreadRunning()) return;
        chooser_ = std::make_unique<juce::FileChooser> ("Where should the library be installed?", dest_.getParentDirectory());
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& fc)
                               {
                                   auto d = fc.getResult();
                                   if (! d.isDirectory()) return;
                                   // a chosen DISK or parent gets a folder of its own, never loose files
                                   dest_ = d.getFileName().containsIgnoreCase ("library") ? d : d.getChildFile ("MIREKU Library");
                                   repaint();
                               });
    }

    void begin (const juce::File& pack)
    {
        if (isThreadRunning()) return;
        librarypack::Index idx;
        juce::String err;
        done_ = false;
        result_ = {};
        if (! librarypack::readIndex (pack, idx, err))
        {
            done_ = true;
            result_.message = err;
            status_ = err;
            total_ = 0;
            repaint();
            return;
        }
        pack_ = pack;
        total_ = idx.totalBytes;
        files_ = (int) idx.entries.size();
        progress_.bytesDone.store (0);
        progress_.filesDone.store (0);
        progress_.cancel.store (false);
        choose_.setEnabled (false);
        change_.setEnabled (false);
        stop_.setVisible (true);
        status_ = "Checking and installing...";
        startThread (Priority::normal);
        startTimerHz (10);
        repaint();
    }

    void run() override
    {
        result_ = librarypack::install (pack_, dest_, progress_);
    }

    void timerCallback() override
    {
        if (isThreadRunning())
        {
            status_ = "Installing " + juce::String (progress_.filesDone.load()) + " of " + juce::String (files_) + " files, "
                    + juce::String ((double) progress_.bytesDone.load() / 1.0e9, 2) + " of "
                    + juce::String ((double) total_ / 1.0e9, 2) + " GB";
            repaint();
            return;
        }
        stopTimer();
        done_ = true;
        choose_.setEnabled (true);
        change_.setEnabled (true);
        stop_.setVisible (false);
        status_ = result_.message;
        if (result_.ok || result_.written > 0)
        {
            // point MIREKU at what was installed, and bring the sounds in
            proc_.state.library.setLibraryDir (dest_);
            proc_.restoreSlotBundles();
            proc_.stateChanged.sendChangeMessage();
            if (result_.ok)
                status_ = "Installed. " + result_.message + " Select CLOSE and play.";
        }
        repaint();
    }

    JMSignatureProcessor& proc_;
    ux::PlateButton choose_, change_, stop_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::File dest_, pack_;
    juce::Rectangle<int> destArea_, barArea_, statusArea_;
    librarypack::Progress progress_;
    librarypack::Result result_;
    juce::int64 total_ = 0;
    int files_ = 0;
    bool done_ = false;
    juce::String status_;
};

} // namespace jm
