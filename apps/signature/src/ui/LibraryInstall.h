// LibraryInstall.h — INSTALL LIBRARY (client, 2026-11-10: "install places the
// library in its right place, no need to do any moving").
//
// The player downloads one file, "MIREKU Library.mirekupack", and picks it
// here. MIREKU checks it, writes the instruments and performances into its own
// library folder with a progress bar, points itself at that folder and shows
// the sounds. The same panel installs an UPDATE pack: it merges into the
// library already there. Opened from the gear menu, and by itself on a launch
// that finds no instruments.
//
// THE DOWNLOAD (2026-11-11, for the iPad app: "library downloads on first
// launch"). A build configured with -DMIREKU_LIBRARY_URL=<https address> offers
// DOWNLOAD THE LIBRARY: MIREKU fetches the file itself, then installs it exactly
// as a chosen file is installed, and removes the downloaded file afterwards.
//   * It CONTINUES. The bytes go to a ".part" file, and a download that stops
//     (no signal, the app closed, the iPad asleep) asks the server for the rest
//     with a Range header the next time. A server that ignores Range starts the
//     file again from the beginning, correctly.
//   * It is CHECKED. The pack's index carries a CRC-32 for every file and the
//     install verifies each one, so a damaged download cannot be installed.
//   * It asks for ROOM first: the file and the installed library exist side by
//     side for a moment, so twice the size must be free.
// A build with no address has no download button and makes no request.
#pragma once
#include "Macro.h"
#include "../engine/LibraryPack.h"

#ifndef MIREKU_LIBRARY_URL
 #define MIREKU_LIBRARY_URL ""
#endif

namespace jm {

class LibraryInstallPanel : public ChromePanel, private juce::Thread, private juce::Timer
{
public:
    static constexpr int kWidth = 660, kHeight = 340;

    /// Where the library is downloaded from. Empty: no download is offered.
    /// Compiled in; a test may point it at a local server.
    static juce::String& libraryUrl() { static juce::String u (MIREKU_LIBRARY_URL); return u; }

    /// The unfinished download. Beside the settings, so it survives a restart.
    static juce::File partFile()
    {
        return Library::appDataDir().getChildFile ("Downloads")
                   .getChildFile (juce::String ("MIREKU Library") + librarypack::kExtension + ".part");
    }

    explicit LibraryInstallPanel (JMSignatureProcessor& p)
        : ChromePanel ("Install library"), juce::Thread ("MIREKU Library Install"), proc_ (p)
    {
        // an update goes where the library already is; a first install goes to
        // MIREKU's own folder, which is where it looks without being told
        dest_ = proc_.state.library.bundles().empty() ? Library::defaultLibraryDir()
                                                      : proc_.state.library.libraryDir();
        const bool canDownload = libraryUrl().isNotEmpty();

        download_.setButtonText (partFile().existsAsFile() ? "CONTINUE THE DOWNLOAD" : "DOWNLOAD THE LIBRARY");
        download_.setFontHeight (13.0f);
        download_.onClick = [this] { beginDownload(); };
        addChildComponent (download_);
        download_.setVisible (canDownload);

        choose_.setButtonText (canDownload ? "USE A FILE..." : "CHOOSE THE LIBRARY FILE...");
        choose_.setFontHeight (canDownload ? 11.0f : 13.0f);
        choose_.onClick = [this] { chooseFile(); };
        addAndMakeVisible (choose_);

        change_.setButtonText ("CHANGE...");
        change_.setFontHeight (11.0f);
        change_.setTooltip ("Install onto another disk or folder");
        change_.onClick = [this] { chooseFolder(); };
        addChildComponent (change_);
        change_.setVisible (showsDestination());

        stop_.setButtonText ("STOP");
        stop_.setFontHeight (11.0f);
        stop_.onClick = [this] { progress_.cancel.store (true); };
        addChildComponent (stop_);

        if (canDownload)
            status_ = partFile().existsAsFile()
                        ? "A download was started earlier. Select CONTINUE THE DOWNLOAD and it carries on from where it stopped."
                        : "Nothing is installed yet. Select DOWNLOAD THE LIBRARY.";
        else
            status_ = "Nothing is installed yet. Choose the file named MIREKU Library" + juce::String (librarypack::kExtension) + ".";
        if (! proc_.state.library.bundles().empty())
            status_ = juce::String ((int) proc_.state.library.bundles().size())
                    + " instruments are installed. "
                    + (canDownload ? "Select DOWNLOAD THE LIBRARY to fetch the current library, or use an update file."
                                   : "Choose a library or update file to add to them.");
    }

    ~LibraryInstallPanel() override
    {
        progress_.cancel.store (true);
        stopThread (30000);
        juce::Desktop::setScreenSaverEnabled (true);
    }

    // --- self-check hooks: the install itself, without the file dialogs
    void setDestination (const juce::File& d) { dest_ = d; repaint(); }
    juce::File destination() const { return dest_; }
    void installFrom (const juce::File& pack) { begin (pack); }
    void downloadAndInstall() { beginDownload(); }
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
        auto buttons = r.removeFromTop (40);
        if (download_.isVisible())
        {
            auto pair = buttons.withSizeKeepingCentre (300 + 14 + 130, 38);
            download_.setBounds (pair.removeFromLeft (300));
            pair.removeFromLeft (14);
            choose_.setBounds (pair.reduced (0, 4));
        }
        else
            choose_.setBounds (buttons.withSizeKeepingCentre (300, 38));
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
        g.drawFittedText (download_.isVisible()
                              ? "MIREKU downloads its library (about 6 GB), checks it and puts every instrument and "
                                "performance in place. Use Wi-Fi and keep MIREKU open; a download that is interrupted "
                                "continues where it stopped."
                              : "Choose the library file you downloaded. MIREKU checks it, puts every instrument and "
                                "performance in place and shows them. There is nothing to unzip or move.",
                          28, 60, getWidth() - 56, 40, juce::Justification::topLeft, 2, 0.95f);

        if (showsDestination())
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::display (11.0f));
            g.drawText ("INSTALL TO", destArea_.withWidth (90), juce::Justification::centredLeft, false);
            g.setColour (Theme::goldHi);
            g.setFont (Theme::value (12.0f));
            g.drawText (dest_.getFullPathName(), destArea_.withTrimmedLeft (92).withTrimmedRight (8),
                        juce::Justification::centredLeft, true);
        }

        // the bar: the download while it downloads, then the install
        auto bar = barArea_.toFloat();
        g.setColour (juce::Colour (0xff0A0908));
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (Theme::goldDark.withAlpha (0.7f));
        g.drawRoundedRectangle (bar.reduced (0.5f), 5.0f, 1.0f);
        const bool dl = downloading_.load();
        const juce::int64 whole = dl ? dlTotal_.load() : total_.load();
        const juce::int64 sofar = dl ? dlDone_.load() : progress_.bytesDone.load();
        if (whole > 0)
        {
            const float f = juce::jlimit (0.0f, 1.0f, (float) ((double) sofar / (double) whole));
            g.setColour (done_ && ! result_.ok ? Theme::warnRed : Theme::gold);
            g.fillRoundedRectangle (bar.reduced (2.0f).withWidth (juce::jmax (4.0f, (bar.getWidth() - 4.0f) * f)), 4.0f);
        }

        g.setColour (done_ ? (result_.ok ? Theme::okGreen : Theme::warnRed) : Theme::cream);
        g.setFont (Theme::ui (12.5f));
        g.drawFittedText (status_, statusArea_.reduced (2, 0), juce::Justification::topLeft, 2, 0.95f);
    }

private:
    /// An iPad app lives in its own container: there is no other disk to choose
    /// and the path means nothing to the player, so the row is not shown there.
    static bool showsDestination()
    {
       #if JUCE_IOS
        return false;
       #else
        return true;
       #endif
    }

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

    void setBusy (bool busy)
    {
        download_.setEnabled (! busy);
        choose_.setEnabled (! busy);
        change_.setEnabled (! busy);
        stop_.setVisible (busy);
        // an iPad that goes to sleep stops the download
        juce::Desktop::setScreenSaverEnabled (! busy);
    }

    void begin (const juce::File& pack)
    {
        if (isThreadRunning()) return;
        librarypack::Index idx;
        juce::String err;
        done_ = false;
        result_ = {};
        viaDownload_ = false;
        downloading_.store (false);
        if (! librarypack::readIndex (pack, idx, err))
        {
            done_ = true;
            result_.message = err;
            status_ = err;
            total_.store (0);
            repaint();
            return;
        }
        pack_ = pack;
        total_.store (idx.totalBytes);
        files_.store ((int) idx.entries.size());
        progress_.bytesDone.store (0);
        progress_.filesDone.store (0);
        progress_.cancel.store (false);
        setBusy (true);
        status_ = "Checking and installing...";
        startThread (Priority::normal);
        startTimerHz (10);
        repaint();
    }

    void beginDownload()
    {
        if (isThreadRunning() || libraryUrl().isEmpty()) return;
        done_ = false;
        result_ = {};
        viaDownload_ = true;
        downloading_.store (true);
        dlDone_.store (partFile().getSize());
        dlTotal_.store (0);
        total_.store (0);
        progress_.bytesDone.store (0);
        progress_.filesDone.store (0);
        progress_.cancel.store (false);
        setBusy (true);
        status_ = "Connecting...";
        startThread (Priority::normal);
        startTimerHz (10);
        repaint();
    }

    //==========================================================================
    // the worker thread

    void run() override
    {
        if (viaDownload_)
        {
            juce::String err;
            if (! download (err))
            {
                result_ = {};
                result_.cancelled = progress_.cancel.load();
                result_.message = err;
                return;
            }
            librarypack::Index idx;
            if (! librarypack::readIndex (pack_, idx, err))
            {
                // not a library file at all: a full download of the wrong thing
                pack_.deleteFile();
                result_ = {};
                result_.message = "The downloaded file is not a MIREKU library and has been removed. Select DOWNLOAD to try again.";
                return;
            }
            total_.store (idx.totalBytes);
            files_.store ((int) idx.entries.size());
            downloading_.store (false);
        }

        result_ = librarypack::install (pack_, dest_, progress_);

        if (viaDownload_ && ! result_.cancelled)
            pack_.deleteFile();      // installed, or damaged: either way the 6 GB is not kept
    }

    /// Fetches the library into pack_. True when the whole file is on disk.
    bool download (juce::String& err)
    {
        const auto part = partFile();
        part.getParentDirectory().createDirectory();
        const auto whole = part.getSiblingFile (part.getFileNameWithoutExtension());   // drops ".part"
        pack_ = whole;

        int quietFailures = 0;
        for (;;)
        {
            if (progress_.cancel.load()) { err = "Stopped. Select CONTINUE THE DOWNLOAD to carry on from here."; return false; }

            const juce::int64 have = part.getSize();
            juce::StringPairArray response;
            int status = 0;
            // (InputStreamOptions cannot be assigned, so the header is decided first)
            const juce::String rangeHeader = have > 0 ? "Range: bytes=" + juce::String (have) + "-" : juce::String();
            const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                     .withConnectionTimeoutMs (20000)
                                     .withResponseHeaders (&response)
                                     .withStatusCode (&status)
                                     .withNumRedirectsToFollow (5)
                                     .withExtraHeaders (rangeHeader);

            std::unique_ptr<juce::InputStream> in (juce::URL (libraryUrl()).createInputStream (options));

            // 416: the server has nothing after `have`, so the file is complete
            if (in != nullptr && status == 416 && have > 0) break;

            if (in == nullptr || (status != 200 && status != 206))
            {
                if (status == 404 || status == 403 || status == 401)
                {
                    err = "The library could not be found on the server. Try again later, or contact hello@amanorsac.studio.";
                    return false;
                }
                if (++quietFailures > 5)
                {
                    err = "The download stopped: no connection to the server. Check the internet connection, "
                          "then select CONTINUE THE DOWNLOAD.";
                    return false;
                }
                waitUnlessCancelled (2000);
                continue;
            }

            // 200 to a Range request: the server ignored it and is sending the
            // whole file, so what is on disk is replaced, not appended to.
            const bool resumed = have > 0 && status == 206;
            if (! resumed) part.deleteFile();
            const juce::int64 start = resumed ? have : 0;

            juce::int64 length = in->getTotalLength();           // of THIS response
            juce::int64 full = length > 0 ? start + length : 0;
            const auto range = response.getValue ("Content-Range", {});   // "bytes a-b/total"
            if (range.contains ("/"))
            {
                const auto t = range.fromLastOccurrenceOf ("/", false, false).trim().getLargeIntValue();
                if (t > 0) full = t;
            }
            dlTotal_.store (full);
            dlDone_.store (start);

            // room for the rest of the file AND the installed copy of it
            if (full > 0)
            {
                const juce::int64 free = part.getParentDirectory().getBytesFreeOnVolume();
                const juce::int64 need = (full - start) + full + 200LL * 1024 * 1024;
                if (free > 0 && free < need)
                {
                    err = "Not enough free space. The library needs " + juce::String ((double) need / 1.0e9, 1)
                        + " GB free while it installs; " + juce::String ((double) free / 1.0e9, 1) + " GB is free.";
                    return false;
                }
            }

            juce::FileOutputStream out (part);
            if (! out.openedOk()) { err = "The download could not be written to this device."; return false; }
            out.setPosition (start);

            juce::HeapBlock<char> buf (1 << 18);
            bool progressed = false;
            for (;;)
            {
                if (progress_.cancel.load()) break;
                const int n = in->read (buf.get(), 1 << 18);
                if (n <= 0) break;
                if (! out.write (buf.get(), (size_t) n)) { err = "The device ran out of space during the download."; return false; }
                dlDone_.fetch_add (n);
                progressed = true;
            }
            out.flush();

            if (progress_.cancel.load()) continue;                // reported at the top of the loop
            if (full > 0 && part.getSize() >= full) break;        // all of it
            if (full <= 0 && in->isExhausted() && progressed) break;   // no length given: the stream ended

            // the connection dropped part-way: go round again and ask for the rest
            if (progressed) quietFailures = 0;
            else if (++quietFailures > 5)
            {
                err = "The download stopped. Check the internet connection, then select CONTINUE THE DOWNLOAD.";
                return false;
            }
            waitUnlessCancelled (1500);
        }

        whole.deleteFile();
        if (! part.moveFileTo (whole)) { err = "The downloaded file could not be prepared for installing."; return false; }
        return true;
    }

    void waitUnlessCancelled (int ms)
    {
        for (int i = 0; i < ms / 50 && ! progress_.cancel.load(); ++i) juce::Thread::sleep (50);
    }

    //==========================================================================
    void timerCallback() override
    {
        if (isThreadRunning())
        {
            if (downloading_.load())
            {
                const auto t = dlTotal_.load();
                status_ = t > 0 ? "Downloading " + juce::String ((double) dlDone_.load() / 1.0e9, 2) + " of "
                                    + juce::String ((double) t / 1.0e9, 2) + " GB"
                                : "Connecting...";
            }
            else
                status_ = "Installing " + juce::String (progress_.filesDone.load()) + " of " + juce::String (files_.load()) + " files, "
                        + juce::String ((double) progress_.bytesDone.load() / 1.0e9, 2) + " of "
                        + juce::String ((double) total_.load() / 1.0e9, 2) + " GB";
            repaint();
            return;
        }
        stopTimer();
        done_ = true;
        downloading_.store (false);
        setBusy (false);
        download_.setButtonText (partFile().existsAsFile() ? "CONTINUE THE DOWNLOAD" : "DOWNLOAD THE LIBRARY");
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
    ux::PlateButton download_, choose_, change_, stop_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::File dest_, pack_;
    juce::Rectangle<int> destArea_, barArea_, statusArea_;
    librarypack::Progress progress_;
    librarypack::Result result_;
    std::atomic<juce::int64> total_ { 0 }, dlDone_ { 0 }, dlTotal_ { 0 };
    std::atomic<int> files_ { 0 };
    std::atomic<bool> downloading_ { false };
    bool viaDownload_ = false;
    bool done_ = false;
    juce::String status_;
};

} // namespace jm
