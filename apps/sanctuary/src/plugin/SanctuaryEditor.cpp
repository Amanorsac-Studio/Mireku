// SanctuaryEditor.cpp — WebView host + native bridge. See header.
#include "SanctuaryEditor.h"
#include "SanctuaryUi.h"
#include "../engine/Factory.h"

namespace sanctuary {

namespace {
    juce::String hex (juce::uint32 argb) { return "#" + juce::String::toHexString ((int) (argb & 0xffffff)).paddedLeft ('0', 6); }
    const juce::uint32 kSlotColours[6] = { 0xff6C8CF5, 0xff38BDE0, 0xff8B6BF5, 0xffC766E8, 0xffF2648F, 0xff4FD1C5 };
    juce::String slotColour (int i) { return hex (kSlotColours[i % 6]); }
    juce::String dbText (float db) { return db <= -59.9f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e")) : (db >= 0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String (std::abs (db), 1) + " dB"; }
    juce::String runText (int sec) { return sec <= 0 ? juce::String() : juce::String (sec / 60) + ":" + juce::String (sec % 60).paddedLeft ('0', 2); }
    juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 3); }
    juce::var obj() { return juce::var (new juce::DynamicObject()); }
    void set (juce::var& o, const char* k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
    juce::String keyText (int playIn, int soundIn, bool minor)
    {
        return playIn == soundIn ? keyDisplay (soundIn, minor) : keyDisplay (playIn, minor) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + keyDisplay (soundIn, minor);
    }
    juce::String paramText (const PathInfo& pi, float v)
    {
        if (pi.kind == ParamKind::Bool) return v >= 0.5f ? "on" : "off";
        if (pi.kind == ParamKind::Enum) return juce::String ((int) v);
        if (pi.max == 100.0f && pi.min == 0.0f) return juce::String ((int) std::round (v)) + " %";
        return juce::String (v, v < 10 ? 2 : 1);
    }
}

//==============================================================================
SanctuaryEditor::SanctuaryEditor (SanctuaryProcessor& p) : AudioProcessorEditor (p), proc_ (p)
{
    auto opts = juce::WebBrowserComponent::Options {}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
            .withUserDataFolder (SanctuaryProcessor::appDataDir().getChildFile ("WebView2"))
            .withBackgroundColour (juce::Colour (0xff04060D)))
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const auto& url) { return provideResource (url); })
        .withNativeFunction ("cmd", [this] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            const auto name = args.size() > 0 ? args[0].toString() : juce::String();
            const auto a = args.size() > 1 ? args[1] : juce::var();
            done (runCommand (name, a));
        });
    web_ = std::make_unique<juce::WebBrowserComponent> (opts);
    web_->setWantsKeyboardFocus (true);
    addAndMakeVisible (*web_);
    web_->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    proc_.stageChanged.addChangeListener (this);
    proc_.rigChanged.addChangeListener (this);
    proc_.learnChanged.addChangeListener (this);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (1100, 680, 4096, 2400);
    setSize (1280, 820);
    startTimerHz (30);
    // The WebView only receives native OS keyboard focus (and therefore keystrokes) once
    // something has explicitly asked JUCE to focus it — a plain click on the native child
    // window never routes back through JUCE's own focus system. Grab it once we're on
    // screen, and keep re-grabbing on the events below.
    juce::Component::SafePointer<SanctuaryEditor> self (this);
    juce::MessageManager::callAsync ([self] { if (self != nullptr) self->focusWebView(); });
}

void SanctuaryEditor::focusWebView()
{
    if (web_ == nullptr || ! isShowing())
        return;
    // Component::grabKeyboardFocus() early-outs if JUCE already believes `web_` is the
    // focused component — which happens after the FIRST call, long before WebView2's native
    // controller exists (it's created asynchronously). Every later retry would then silently
    // no-op forever, never re-forwarding focus into the browser once the controller is ready.
    // Force a real focus transition each time by dropping focus first.
    web_->giveAwayKeyboardFocus();
    web_->grabKeyboardFocus();
}

void SanctuaryEditor::visibilityChanged() { if (isShowing()) focusWebView(); }
void SanctuaryEditor::parentHierarchyChanged() { if (isShowing()) focusWebView(); }
void SanctuaryEditor::focusOfChildComponentChanged (FocusChangeType) {}

SanctuaryEditor::~SanctuaryEditor()
{
    proc_.stageChanged.removeChangeListener (this);
    proc_.rigChanged.removeChangeListener (this);
    proc_.learnChanged.removeChangeListener (this);
}

void SanctuaryEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff04060D)); }
void SanctuaryEditor::resized() { web_->setBounds (getLocalBounds()); }

std::optional<juce::WebBrowserComponent::Resource> SanctuaryEditor::provideResource (const juce::String& url)
{
    if (url == "/" || url == "/index.html")
    {
        juce::WebBrowserComponent::Resource r;
        r.data.assign ((const std::byte*) SanctuaryUi::index_html, (const std::byte*) SanctuaryUi::index_html + SanctuaryUi::index_htmlSize);
        r.mimeType = "text/html";
        return r;
    }
    return std::nullopt;
}

void SanctuaryEditor::changeListenerCallback (juce::ChangeBroadcaster*) { stateDirty_ = true; }

void SanctuaryEditor::timerCallback()
{
    // WebView2's native controller (needed for MoveFocus) is created asynchronously, so the
    // very first focusWebView() call after construction usually no-ops. Keep retrying for a
    // few seconds at startup; harmless once it has actually taken (grabbing focus we already
    // have is a no-op), and stops well before a user would be typing into a text field.
    if (focusGrabAttempts_ < 90) { ++focusGrabAttempts_; focusWebView(); }

    if (splitSlot_ >= 0)
    {
        const int note = proc_.lastIncomingNote.load();
        if (note >= 0)
        {
            auto& s = proc_.rig.slots[(size_t) splitSlot_];
            // the split key becomes the top of a left-hand slot or the bottom of a right-hand one
            if (s.keyLo == 0) proc_.setSlotRange (splitSlot_, 0, juce::jmax (0, note - 1), s.velLo, s.velHi, s.octaveShift, s.channel, s.ignoreWheel);
            else proc_.setSlotRange (splitSlot_, note, 127, s.velLo, s.velHi, s.octaveShift, s.channel, s.ignoreWheel);
            proc_.lastAction = "Split set at " + noteName (note);
            splitSlot_ = -1; stateDirty_ = true;
        }
    }
    if (stateDirty_) { stateDirty_ = false; pushState(); }
    web_->emitEventIfBrowserIsVisible ("tick", buildTick());
    if (++tickCount_ % 30 == 0) web_->emitEventIfBrowserIsVisible ("state", buildState());   // once a second, catch-all
}

void SanctuaryEditor::pushState() { web_->emitEventIfBrowserIsVisible ("state", buildState()); }

//==============================================================================
// State
//==============================================================================
juce::var SanctuaryEditor::buildState() const
{
    auto& p = proc_;
    auto st = obj();
    auto* song = p.currentSong();

    // set + songs
    set (st, "setId", p.set.id); set (st, "setName", p.set.name); set (st, "serviceType", p.set.serviceType); set (st, "setDate", p.set.date);
    set (st, "padBetween", (int) p.set.padBetweenSongs);
    juce::Array<juce::var> songs; int totalRun = 0;
    for (int i = 0; i < (int) p.set.songs.size(); ++i)
    {
        auto& e = p.set.songs[(size_t) i];
        auto it = p.songs.find (e.songId);
        auto o = obj();
        set (o, "id", e.songId);
        if (it == p.songs.end()) { set (o, "title", "Missing song"); set (o, "warn", "missing"); set (o, "colour", "#FF6B8A"); set (o, "moments", juce::Array<juce::var>()); songs.add (o); continue; }
        auto& s = it->second;
        const int soundIn = i == p.songIndex ? p.soundIn() : e.keyOverride.value_or (s.soundIn);
        const int playIn = i == p.songIndex ? p.playIn() : s.playIn;
        set (o, "title", s.title); set (o, "key", keyText (playIn, soundIn, s.minor));
        set (o, "bpm", (int) std::round (i == p.songIndex ? p.tempo() : e.tempoOverride.value_or (s.tempo)));
        set (o, "meter", s.meter); set (o, "tags", s.tags.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))));
        set (o, "run", runText (s.runtimeSec)); totalRun += s.runtimeSec;
        set (o, "colour", hex (s.colour));
        int muted = 0, missing = 0;
        for (auto& sl : s.baseRig.slots) { if (sl.hasSound() && ! sl.enabled) ++muted; if (sl.hasSound() && ! juce::File (sl.bundleDir).isDirectory()) ++missing; }
        set (o, "warn", missing > 0 ? juce::String (missing) + " bundle missing" : muted > 0 ? juce::String (muted) + " slot muted" : juce::String());
        juce::Array<juce::var> ms;
        for (auto& m : s.moments) { auto mo = obj(); set (mo, "name", m.name); set (mo, "bars", m.transitionBars); set (mo, "colour", hex (m.colour)); ms.add (mo); }
        set (o, "moments", ms);
        songs.add (o);
    }
    set (st, "songs", songs); set (st, "songIndex", p.songIndex); set (st, "momentIndex", p.momentIndex);
    set (st, "setRun", totalRun > 0 ? juce::String (totalRun / 60) + " min" : juce::String());

    // now
    auto now = obj();
    set (now, "title", song ? song->title : juce::String ("No song"));
    set (now, "key", song ? keyText (p.playIn(), p.soundIn(), song->minor) : juce::String());
    set (now, "playIn", p.playIn()); set (now, "soundIn", p.soundIn()); set (now, "semis", p.wheelShift());
    set (now, "playInName", keyDisplay (p.playIn(), song && song->minor)); set (now, "soundInName", keyDisplay (p.soundIn(), song && song->minor));
    set (now, "bpm", (int) std::round (p.tempo())); set (now, "meter", song ? song->meter : "4/4");
    set (now, "tags", song ? song->tags.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))) : juce::String());
    set (now, "lock", song ? song->wheelLock : false); set (now, "minor", song ? song->minor : false);
    set (now, "notes", song ? song->notes : juce::String());
    set (now, "artist", song ? song->artist : juce::String()); set (now, "songKey", song ? song->songKey : 0);
    set (now, "runtimeSec", song ? song->runtimeSec : 0);
    set (now, "colour", song ? hex (song->colour) : juce::String ("#6C8CF5"));
    juce::Array<juce::var> tagArr; if (song) for (auto& t : song->tags) tagArr.add (t); set (now, "tagList", tagArr);
    juce::Array<juce::var> cues; if (song) for (int i = 0; i < (int) song->cues.size(); ++i) { auto& c = song->cues[(size_t) i]; auto co = obj(); set (co, "moment", c.moment); set (co, "type", c.type); set (co, "value", c.value); cues.add (co); }
    set (now, "cues", cues);
    set (st, "now", now);

    // slots (used ones)
    juce::Array<juce::var> slots;
    const int used = p.usedSlots();
    for (int i = 0; i < used; ++i)
    {
        auto& s = p.rig.slots[(size_t) i];
        auto o = obj();
        set (o, "i", i); set (o, "name", s.label.isNotEmpty() ? s.label : (s.bundleName.isNotEmpty() ? s.bundleName : juce::String ("Slot ") + slotLetter (i)));
        set (o, "world", s.hasSound() ? juce::String (jm::worldName (s.world)) : juce::String("empty")); set (o, "bundle", s.bundleName);
        set (o, "colour", slotColour (i)); set (o, "on", s.enabled); set (o, "solo", p.isSolo (i));
        set (o, "vol", (s.volumeDb + 60.0f) / 66.0f); set (o, "db", dbText (s.volumeDb));
        set (o, "keyLo", s.keyLo); set (o, "keyHi", s.keyHi); set (o, "velLo", s.velLo); set (o, "velHi", s.velHi);
        set (o, "oct", s.octaveShift); set (o, "ch", s.channel); set (o, "ignoreWheel", s.ignoreWheel); set (o, "pan", s.pan);
        set (o, "missing", s.hasSound() && ! juce::File (s.bundleDir).isDirectory()); set (o, "hasSound", s.hasSound());
        juce::Array<juce::var> fx;
        for (int u = 0; u < (int) s.rack.size(); ++u) { auto fo = obj(); set (fo, "unit", u); set (fo, "name", jm::fxSpec (s.rack[(size_t) u].type).name); set (fo, "on", ! s.rack[(size_t) u].bypassed); fx.add (fo); }
        set (o, "fx", fx);
        slots.add (o);
    }
    set (st, "slots", slots); set (st, "freeSlots", kSlots - used);
    juce::Array<juce::var> bundles;
    for (auto& b : p.library.bundles()) { auto bo = obj(); set (bo, "name", b.name); set (bo, "dir", b.dir.getFullPathName()); set (bo, "world", jm::worldName (b.world)); bundles.add (bo); }
    set (st, "bundles", bundles);

    // racks (per used slot + master)
    auto rackVar = [&] (const FxRackState& rack, const juce::String& prefix)
    {
        juce::Array<juce::var> units;
        for (int u = 0; u < (int) rack.size(); ++u)
        {
            auto& unit = rack[(size_t) u]; auto& spec = jm::fxSpec (unit.type);
            auto uo = obj(); set (uo, "index", u); set (uo, "type", juce::String (spec.typeId)); set (uo, "name", spec.name); set (uo, "on", ! unit.bypassed);
            juce::Array<juce::var> params; juce::String summary;
            for (auto& ps : spec.params)
            {
                const juce::String path = prefix + ".fx" + juce::String (u) + "." + ps.id;
                const auto pi = pathInfo (p.rig, path); const float v = p.getPath (path).value_or (ps.def);
                auto po = obj(); set (po, "id", ps.id); set (po, "label", ps.label); set (po, "path", path); set (po, "p", normFromValue (pi, v) * 100.0f); set (po, "val", paramText (pi, v));
                set (po, "kind", (int) ps.kind);
                juce::Array<juce::var> ch; for (auto* c : ps.choices) ch.add (juce::String (c)); set (po, "choices", ch);
                params.add (po);
                if (summary.isEmpty()) summary = juce::String (ps.label).toLowerCase() + " " + paramText (pi, v);
            }
            set (uo, "params", params); set (uo, "summary", summary);
            units.add (uo);
        }
        return units;
    };
    juce::Array<juce::var> racks;
    for (int i = 0; i < used; ++i) racks.add (rackVar (p.rig.slots[(size_t) i].rack, "slot" + juce::String (i)));
    set (st, "racks", racks); set (st, "masterRack", rackVar (p.rig.masterRack, "master"));
    juce::Array<juce::var> fxTypes; for (auto& spec : jm::fxRegistry()) { auto fo = obj(); set (fo, "id", juce::String (spec.typeId)); set (fo, "name", spec.name); fxTypes.add (fo); }
    set (st, "fxTypes", fxTypes); set (st, "rackMax", jm::FxRackDsp::kMaxSlots);

    // drone
    auto d = obj(); auto& dr = p.rig.drone;
    set (d, "on", dr.on); set (d, "character", dr.character);
    juce::Array<juce::var> chars; for (auto& c : droneCharacters()) chars.add (juce::String (c.name)); set (d, "characters", chars);
    set (d, "level", dr.level / 100.0f); set (d, "db", dbText (-40.0f + 40.0f * dr.level / 100.0f));
    set (d, "follow", (int) dr.follow); set (d, "key", p.drone.currentRoot() >= 0 && dr.follow != DroneFollow::off ? p.drone.currentRoot() : dr.key);
    set (d, "keyName", keyDisplay (p.drone.currentRoot() >= 0 && dr.follow != DroneFollow::off ? p.drone.currentRoot() : dr.key, dr.minor));
    set (d, "minor", dr.minor); set (d, "bright", dr.brightness); set (d, "air", dr.air); set (d, "motion", dr.motion); set (d, "shimmer", dr.shimmer); set (d, "depth", dr.depth);
    set (st, "drone", d);

    // tempo
    auto t = obj(); set (t, "bpm", (int) std::round (p.tempo())); set (t, "beats", p.beatsPerBar()); set (t, "source", p.followHostTempo() ? "Host" : "Song"); set (st, "tempo", t);

    // live trim
    juce::Array<juce::var> trim;
    if (song)
        for (auto& path : song->liveTrimPaths())
        {
            const auto pi = pathInfo (p.rig, path); if (! pi.valid) continue;
            const float v = p.getPath (path).value_or (pi.min);
            auto o = obj(); set (o, "path", path);
            juce::String label = pi.label;
            if (path.startsWith ("slot")) { const int si = path.substring (4, 5).getIntValue(); auto& sl = p.rig.slots[(size_t) si]; label = (sl.label.isNotEmpty() ? sl.label : juce::String (slotLetter (si))) + " " + pi.label; }
            set (o, "label", label.toUpperCase()); set (o, "p", normFromValue (pi, v) * 100.0f); set (o, "val", paramText (pi, v));
            set (o, "colour", path.startsWith ("slot") ? slotColour (path.substring (4, 5).getIntValue()) : path.startsWith ("drone") ? juce::String ("#8B6BF5") : juce::String ("#4C8DFF"));
            trim.add (o);
        }
    set (st, "trim", trim);
    set (st, "trimMoment", song && p.momentIndex < (int) song->moments.size() ? song->moments[(size_t) p.momentIndex].name : juce::String());

    // pads
    juce::Array<juce::var> padArr;
    for (int i = 0; i < PadEngine::kPads; ++i) { auto& ps = padSpecs()[(size_t) i]; auto o = obj(); set (o, "name", ps.name); set (o, "mode", padModeText (ps)); set (o, "colour", hex (ps.colour)); set (o, "hold", ps.mode == PadMode::hold); padArr.add (o); }
    set (st, "pads", padArr);

    // header / status
    set (st, "rehearsal", p.rehearsal); set (st, "learnPick", p.learnPick); set (st, "learnTarget", p.learnTarget()); set (st, "learnControl", p.learnControl());
    set (st, "lastAction", p.lastAction);
    const float mv = p.apvts.getRawParameterValue ("masterVol")->load();
    auto m = obj(); set (m, "norm", (mv + 60.0f) / 66.0f); set (m, "db", dbText (mv)); set (st, "master", m);
    set (st, "sessionAvailable", p.sessionAvailable()); set (st, "sessionName", p.sessionName);
    set (st, "deck", p.deck);

    // moments editor (current song)
    juce::Array<juce::var> moments;
    if (song)
        for (int i = 0; i < (int) song->moments.size(); ++i)
        {
            auto& mm = song->moments[(size_t) i];
            auto o = obj(); set (o, "name", mm.name); set (o, "bars", mm.transitionBars); set (o, "colour", hex (mm.colour));
            juce::Array<juce::var> diff;
            for (auto& [path, norm] : mm.diff)
            {
                const auto pi = pathInfo (song->baseRig, path); if (! pi.valid) continue;
                const float before = song->baseRig.slots.size() ? rigGet (song->baseRig, path).value_or (pi.min) : 0.0f;
                auto dO = obj(); set (dO, "path", path); set (dO, "before", paramText (pi, before)); set (dO, "after", paramText (pi, valueFromNorm (pi, norm))); set (dO, "val", (int) std::round (norm * 100)); diff.add (dO);
            }
            set (o, "diff", diff);
            moments.add (o);
        }
    set (st, "moments", moments);

    // sets + all songs (for the rail menus)
    juce::Array<juce::var> sets;
    for (auto& s : p.setsOnDisk()) { auto o = obj(); set (o, "id", s.id); set (o, "name", s.name); set (o, "type", s.serviceType); set (o, "count", (int) s.songs.size()); sets.add (o); }
    set (st, "sets", sets);
    juce::Array<juce::var> all;
    for (auto& [id, s] : p.songs) { auto o = obj(); set (o, "id", id); set (o, "title", s.title); set (o, "key", keyDisplay (s.songKey, s.minor)); all.add (o); }
    set (st, "allSongs", all);

    // surfaces
    juce::Array<juce::var> surfs; for (auto& s : p.surfaces) surfs.add (s.name); set (st, "surfaces", surfs); set (st, "surfaceIndex", p.surfaceIndex);
    juce::Array<juce::var> controls;
    if (! p.surfaces.empty())
    {
        auto& s = p.surfaces[(size_t) juce::jlimit (0, (int) p.surfaces.size() - 1, p.surfaceIndex)];
        for (auto& c : s.controls)
        {
            auto o = obj(); set (o, "id", c.id); set (o, "kind", (int) c.kind); set (o, "cc", c.cc); set (o, "note", c.note); set (o, "label", c.label); set (o, "mode", (int) c.mode); set (o, "channel", c.channel);
            set (o, "target", s.targetFor (c.id)); controls.add (o);
        }
    }
    set (st, "controls", controls);
    juce::Array<juce::var> paths; for (auto& a : actionTargets()) paths.add (a); for (int i = 1; i <= 8; ++i) paths.add ("trim" + juce::String (i)); for (auto& pth : allPaths (p.rig)) paths.add (pth);
    set (st, "paths", paths);

    // settings + library
    set (st, "settings", p.settings.toVar());
    auto lib = obj(); set (lib, "dir", p.library.libraryDir().getFullPathName()); set (lib, "bundles", (int) p.library.bundles().size()); set (lib, "songs", (int) p.songs.size()); set (lib, "fallback", p.library.usedFallback());
    set (st, "library", lib);
    set (st, "standalone", juce::JUCEApplicationBase::isStandaloneApp());
    set (st, "midiInputs", [] { juce::Array<juce::var> a; for (auto& d : juce::MidiInput::getAvailableDevices()) a.add (d.name); return a; }());
    return st;
}

juce::var SanctuaryEditor::buildTick() const
{
    auto& p = proc_;
    auto t = obj();
    juce::Array<juce::var> meters;
    const int used = p.usedSlots();
    for (int i = 0; i < used; ++i) meters.add (juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (p.slotPeak[(size_t) i].load(), -60.0f) + 60.0f) / 60.0f));
    set (t, "meters", meters);
    set (t, "master", juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (juce::jmax (p.masterPeakL.load(), p.masterPeakR.load()), -60.0f) + 60.0f) / 60.0f));
    set (t, "dronePeak", juce::jlimit (0.0f, 1.0f, p.drone.outputPeak() * 3.0f)); set (t, "droneOn", p.rig.drone.on); set (t, "droneSounding", p.drone.isSounding());
    set (t, "droneKey", p.drone.currentRoot() >= 0 ? p.drone.currentRoot() : p.rig.drone.key);
    set (t, "droneKeyName", keyDisplay (p.drone.currentRoot() >= 0 ? p.drone.currentRoot() : p.rig.drone.key, p.rig.drone.minor));
    set (t, "beat", p.currentBeat()); set (t, "beats", p.beatsPerBar());
    set (t, "glideActive", p.glideActive()); set (t, "glide", p.glideProgress()); set (t, "glideBars", p.glideBarsRemaining());
    const auto c = p.currentChord();
    set (t, "chord", c.root >= 0 ? juce::String (keyName (c.root, keyPrefersFlats (p.soundIn()))) + chordQualitySuffix (c.quality) : juce::String());
    set (t, "chordBass", c.root >= 0 && c.bass >= 0 && c.bass != c.root ? juce::String (keyName (c.bass, keyPrefersFlats (p.soundIn()))) : juce::String());
    juce::Array<juce::var> held;
    for (int b = 0; b < 2; ++b) { const auto m = p.heldNotes[(size_t) b].load(); for (int k = 0; k < 64; ++k) if ((m >> k) & 1ull) held.add (b * 64 + k); }
    set (t, "held", held);
    set (t, "cpu", (int) (p.cpuLoad.load() * 100)); set (t, "voices", p.totalVoices.load()); set (t, "overruns", p.overruns.load());
    set (t, "elapsed", (int) p.serviceElapsedSeconds()); set (t, "autosaveAgo", (int) p.secondsSinceAutosave());
    juce::Array<juce::var> lit, prog; for (int i = 0; i < PadEngine::kPads; ++i) { lit.add (p.pads.isLit (i)); prog.add (p.pads.progress (i)); }
    set (t, "padsLit", lit); set (t, "padsProg", prog);
    set (t, "learnTarget", p.learnTarget()); set (t, "learnControl", p.learnControl());
    set (t, "lastCc", p.lastIncomingCc.load()); set (t, "lastNote", p.lastIncomingNote.load());
    return t;
}

//==============================================================================
// Commands
//==============================================================================
juce::var SanctuaryEditor::runCommand (const juce::String& n, const juce::var& a)
{
    auto& p = proc_;
    auto I = [&] (const char* k, int def = 0) { return a.hasProperty (k) ? (int) a[k] : def; };
    auto F = [&] (const char* k, double def = 0) { return a.hasProperty (k) ? (double) a[k] : def; };
    auto S = [&] (const char* k) { return a.getProperty (k, "").toString(); };
    auto B = [&] (const char* k) { return (bool) a.getProperty (k, false); };
    auto ok = [&] { stateDirty_ = true; return juce::var (true); };

    if (n == "ready") { pushState(); return true; }
    if (n == "gotoSong") { p.saveCurrentSong(); p.gotoSong (I ("i")); return ok(); }
    if (n == "gotoMoment") { p.gotoMoment (I ("i")); return ok(); }
    if (n == "next") { p.nextMoment(); return ok(); }
    if (n == "prev") { p.prevMoment(); return ok(); }
    if (n == "nextSong") { p.nextSong(); return ok(); }
    if (n == "prevSong") { p.prevSong(); return ok(); }
    if (n == "setPathNorm")
    {
        const auto path = S ("path");
        if (p.learnPick) { p.learnPick = false; p.armLearn (path); p.lastAction = "Learn armed for " + path + " \xe2\x80\x94 move a control"; return ok(); }
        p.setPathNorm (path, (float) F ("v")); return true;
    }
    if (n == "setPathRaw") { p.setPath (S ("path"), (float) F ("v")); return true; }
    if (n == "fxToggle")
    {
        const juce::String path = (I ("slot") < 0 ? juce::String ("master") : "slot" + juce::String (I ("slot"))) + ".fx" + juce::String (I ("unit")) + ".bypass";
        p.setPath (path, p.getPath (path).value_or (1.0f) >= 0.5f ? 0.0f : 1.0f); return ok();
    }
    if (n == "mute") { p.setPath ("slot" + juce::String (I ("slot")) + ".enabled", B ("on") ? 0.0f : 1.0f); return ok(); }
    if (n == "solo") { p.setSolo (I ("slot"), B ("on")); return ok(); }
    if (n == "range") { auto& s = p.rig.slots[(size_t) juce::jlimit (0, kSlots - 1, I ("slot"))]; p.setSlotRange (I ("slot"), I ("lo"), I ("hi"), s.velLo, s.velHi, s.octaveShift, s.channel, s.ignoreWheel); return ok(); }
    if (n == "slotField")
    {
        const int i = juce::jlimit (0, kSlots - 1, I ("slot")); auto& s = p.rig.slots[(size_t) i]; const auto f = S ("field");
        int keyLo = s.keyLo, keyHi = s.keyHi, velLo = s.velLo, velHi = s.velHi, oct = s.octaveShift, ch = s.channel; bool iw = s.ignoreWheel;
        if (f == "oct") oct = I ("value"); else if (f == "ch") ch = I ("value"); else if (f == "ignoreWheel") iw = B ("value");
        else if (f == "velLo") velLo = I ("value"); else if (f == "velHi") velHi = I ("value"); else if (f == "keyLo") keyLo = I ("value"); else if (f == "keyHi") keyHi = I ("value");
        else if (f == "on") { p.setPath ("slot" + juce::String (i) + ".enabled", B ("value") ? 1.0f : 0.0f); return ok(); }
        else if (f == "name") { s.label = S ("value"); if (auto* sg = p.currentSongMutable()) sg->baseRig.slots[(size_t) i].label = s.label; return ok(); }
        p.setSlotRange (i, keyLo, keyHi, velLo, velHi, oct, ch, iw); return ok();
    }
    if (n == "addSlot")
    {
        const int i = juce::jmin (kSlots - 1, p.usedSlots()); const auto dir = S ("dir");
        for (auto& b : p.library.bundles()) if (b.dir.getFullPathName() == dir) { p.loadBundleIntoSlot (i, b); break; }
        return ok();
    }
    if (n == "slotBundle")
    {
        const auto dir = S ("dir"); const int i = I ("slot");
        if (dir.isEmpty()) p.clearSlot (i); else for (auto& b : p.library.bundles()) if (b.dir.getFullPathName() == dir) { p.loadBundleIntoSlot (i, b); break; }
        return ok();
    }
    if (n == "removeSlot") { p.clearSlot (I ("slot")); return ok(); }
    if (n == "splitLearn") { p.lastIncomingNote.store (-1); splitSlot_ = I ("slot"); p.lastAction = "Press the split key on your keyboard\xe2\x80\xa6"; return ok(); }
    if (n == "droneToggle") { p.toggleDrone(); return ok(); }
    if (n == "droneKey") { p.rig.drone.key = I ("k"); p.setPath ("drone.follow", 0.0f); p.setPath ("drone.level", p.rig.drone.level); return ok(); }
    if (n == "droneMinor") { p.rig.drone.minor = B ("on"); p.setPath ("drone.level", p.rig.drone.level); return ok(); }
    if (n == "droneFollow") { p.setPath ("drone.follow", (float) I ("f")); return ok(); }
    if (n == "droneChar") { p.setPath ("drone.character", (float) I ("i")); return ok(); }
    if (n == "droneLevel") { p.setPath ("drone.level", (float) (F ("v") * 100.0)); return true; }
    if (n == "wheel") { if (S ("which") == "play") p.nudgePlayIn (I ("delta")); else p.nudgeSoundIn (I ("delta")); return ok(); }
    if (n == "wheelSet") { if (S ("which") == "play") p.setPlayIn (I ("pc")); else p.setSoundIn (I ("pc")); return ok(); }
    if (n == "wheelLock") { if (auto* s = p.currentSongMutable()) { s->wheelLock = B ("on"); s->playIn = p.playIn(); s->soundIn = p.soundIn(); } return ok(); }
    if (n == "tempo") { p.setTempo (p.tempo() + I ("delta")); return ok(); }
    if (n == "tempoSet") { p.setTempo (F ("bpm")); return ok(); }
    if (n == "tap") { p.tapTempo(); return ok(); }
    if (n == "pad") { p.firePad (I ("i"), B ("down")); return true; }
    if (n == "reset") { p.reset(); return ok(); }
    if (n == "rehearse") { p.rehearsal = ! p.rehearsal; p.stageChanged.sendChangeMessage(); return ok(); }
    if (n == "learnPick") { p.learnPick = ! p.learnPick; if (! p.learnPick) p.cancelLearn(); p.lastAction = p.learnPick ? "Learn armed \xe2\x80\x94 touch a control on screen, then move one on your surface" : juce::String(); return ok(); }
    if (n == "learn") { p.armLearn (S ("path")); p.lastAction = "Learning " + S ("path") + " \xe2\x80\x94 move a control on your surface"; return ok(); }
    if (n == "unbindPath") { p.unbindTarget (S ("path")); return ok(); }
    if (n == "setPathPin") { if (auto* s = p.currentSongMutable()) { s->trimPaths.removeString (S ("path")); s->trimPaths.insert (0, S ("path")); p.stageChanged.sendChangeMessage(); } return ok(); }
    if (n == "setPathUnpin") { if (auto* s = p.currentSongMutable()) { s->trimPaths.removeString (S ("path")); p.stageChanged.sendChangeMessage(); } return ok(); }
    if (n == "cancelLearn") { p.cancelLearn(); p.learnPick = false; return ok(); }
    if (n == "master") { if (auto* pr = p.apvts.getParameter ("masterVol")) pr->setValueNotifyingHost (pr->convertTo0to1 ((float) (-60.0 + 66.0 * F ("v")))); return true; }
    if (n == "limiter") { if (auto* pr = p.apvts.getParameter ("limiter")) pr->setValueNotifyingHost (B ("on") ? 1.0f : 0.0f); return ok(); }
    if (n == "addMoment") { p.addMoment (S ("name").isNotEmpty() ? S ("name") : "Moment " + juce::String ((int) (p.currentSong() ? p.currentSong()->moments.size() + 1 : 1))); return ok(); }
    if (n == "saveMoment") { p.captureRigIntoMoment (p.momentIndex); p.saveCurrentSong(); return ok(); }
    if (n == "capture") { p.captureRigIntoMoment (I ("i")); return ok(); }
    if (n == "momentField")
    {
        const int i = I ("i"); const auto f = S ("field");
        if (f == "name") p.renameMoment (i, S ("value"));
        else if (f == "bars") p.setMomentTransition (i, (float) F ("value"));
        else if (f == "colour") { if (auto* s = p.currentSongMutable()) if (i >= 0 && i < (int) s->moments.size()) { s->moments[(size_t) i].colour = (juce::uint32) (0xff000000u | (juce::uint32) S ("value").substring (1).getHexValue32()); p.stageChanged.sendChangeMessage(); } }
        return ok();
    }
    if (n == "deleteMoment") { p.deleteMoment (I ("i")); return ok(); }
    if (n == "moveMoment") { p.moveMoment (I ("from"), I ("to")); return ok(); }
    if (n == "revert") { p.revertMomentEntry (I ("i"), S ("path")); return ok(); }
    if (n == "songField")
    {
        auto* s = p.currentSongMutable(); if (s == nullptr) return false;
        const auto f = S ("field");
        if (f == "title") s->title = S ("value"); else if (f == "artist") s->artist = S ("value");
        else if (f == "key") { s->songKey = I ("value"); s->baseRig.drone.key = s->songKey; }
        else if (f == "minor") { s->minor = B ("value"); s->baseRig.drone.minor = s->minor; p.rig.drone.minor = s->minor; p.setPath ("drone.level", p.rig.drone.level, false); }
        else if (f == "tempo") p.setTempo (F ("value")); else if (f == "meter") s->meter = S ("value");
        else if (f == "notes") s->notes = S ("value"); else if (f == "runtimeSec") s->runtimeSec = I ("value");
        else if (f == "colour") s->colour = (juce::uint32) (0xff000000u | (juce::uint32) S ("value").substring (1).getHexValue32());
        else if (f == "tagToggle") { const auto tg = S ("value"); if (s->tags.contains (tg)) s->tags.removeString (tg); else s->tags.add (tg); }
        else if (f == "addCue") { s->cues.push_back ({ I ("moment"), S ("type"), I ("value") }); }
        else if (f == "removeCue") { const int i = I ("value"); if (i >= 0 && i < (int) s->cues.size()) s->cues.erase (s->cues.begin() + i); }
        p.stageChanged.sendChangeMessage(); return ok();
    }
    if (n == "saveAll") { p.saveCurrentSong(); p.saveSet (p.set); p.saveSurfaces(); p.lastAction = "Saved song, set and surfaces"; return ok(); }
    if (n == "loadSet") { p.saveCurrentSong(); for (auto& s : p.setsOnDisk()) if (s.id == S ("id")) { p.loadSet (s); break; } return ok(); }
    if (n == "newSet") { Set s; s.name = S ("name").isNotEmpty() ? S ("name") : "New set"; s.serviceType = S ("type").isNotEmpty() ? S ("type") : "Morning"; p.saveSet (s); p.loadSet (s); return ok(); }
    if (n == "addSongToSet") { auto s = p.set; s.songs.push_back ({ S ("id"), std::nullopt, std::nullopt }); p.saveSet (s); p.gotoSong ((int) s.songs.size() - 1); return ok(); }
    if (n == "newSong") { p.newSongFromRig (S ("title").isNotEmpty() ? S ("title") : "New Song"); return ok(); }
    if (n == "duplicateSong") { if (auto* s = p.currentSong()) { Song c = *s; c.id = juce::Uuid().toString(); c.title = s->title + " (copy)"; p.saveSong (c); auto st = p.set; st.songs.push_back ({ c.id, std::nullopt, std::nullopt }); p.saveSet (st); } return ok(); }
    if (n == "removeFromSet") { auto s = p.set; const int i = I ("i"); if (i >= 0 && i < (int) s.songs.size()) { s.songs.erase (s.songs.begin() + i); p.saveSet (s); if (! s.songs.empty()) p.gotoSong (juce::jmin (i, (int) s.songs.size() - 1)); else { p.songIndex = -1; p.stageChanged.sendChangeMessage(); } } return ok(); }
    if (n == "moveSong") { auto s = p.set; const int from = I ("from"), to = I ("to"); if (from >= 0 && from < (int) s.songs.size() && to >= 0 && to < (int) s.songs.size()) { auto e = s.songs[(size_t) from]; s.songs.erase (s.songs.begin() + from); s.songs.insert (s.songs.begin() + to, e); p.saveSet (s); p.gotoSong (to); } return ok(); }
    if (n == "songKeyOverride") { auto s = p.set; const int i = I ("i"); if (i >= 0 && i < (int) s.songs.size()) { if (I ("key", -1) < 0) s.songs[(size_t) i].keyOverride.reset(); else s.songs[(size_t) i].keyOverride = I ("key"); p.saveSet (s); if (i == p.songIndex) p.gotoSong (i); } return ok(); }
    if (n == "setPadBetween") { auto s = p.set; s.padBetweenSongs = (PadBetweenSongs) juce::jlimit (0, 2, I ("v")); p.saveSet (s); return ok(); }
    if (n == "surfaceSelect") { p.surfaceIndex = I ("i"); p.saveSurfaces(); p.refreshPadMask(); return ok(); }
    if (n == "bind") { auto& s = p.currentSurface(); if (S ("target").isEmpty()) s.unbind (S ("control")); else s.bind (S ("control"), S ("target")); p.saveSurfaces(); p.refreshPadMask(); return ok(); }
    if (n == "controlMode") { auto& s = p.currentSurface(); for (auto& c : s.controls) if (c.id == S ("control")) c.mode = (ControlMode) juce::jlimit (0, 3, I ("mode")); p.saveSurfaces(); return ok(); }
    if (n == "learnControl") { p.armLearnControl (S ("control")); return ok(); }
    if (n == "newSurface") { Surface s; s.name = S ("name").isNotEmpty() ? S ("name") : "Surface " + juce::String ((int) p.surfaces.size() + 1); s = factory::generic8 (s.name, "", { 20, 21, 22, 23, 24, 25, 26, 27 }, { 12, 13, 14, 15, 16, 17, 18, 19 }, { 80, 81, 82, 83, 84, 85, 86, 87 }, { 36, 37, 38, 39, 40, 41 }); p.surfaces.push_back (s); p.surfaceIndex = (int) p.surfaces.size() - 1; p.saveSurfaces(); return ok(); }
    if (n == "setting") { p.settings.set (S ("key"), a.getProperty ("value", juce::var())); p.saveSettings(); if (S ("key") == "padBetween") { auto s = p.set; s.padBetweenSongs = (PadBetweenSongs) juce::jlimit (0, 2, I ("value")); p.saveSet (s); } return ok(); }
    if (n == "resetSettings") { p.settings.values.clear(); p.saveSettings(); return ok(); }
    if (n == "rescan") { p.library.rescan(); return ok(); }
    if (n == "chooseLibrary")
    {
        chooser_ = std::make_unique<juce::FileChooser> ("Choose the sound library folder", p.library.libraryDir());
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        { auto f = fc.getResult(); if (f.isDirectory()) { proc_.library.setLibraryDir (f); stateDirty_ = true; } });
        return true;
    }
    if (n == "audioSettings") { if (auto& f = audioSettingsHook()) f(); return true; }
    if (n == "openFolder") { SanctuaryProcessor::appDataDir().revealToUser(); return true; }
    if (n == "exportPack")
    {
        chooser_ = std::make_unique<juce::FileChooser> ("Export set pack", juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (juce::File::createLegalFileName (p.set.name) + ".setpack.zip"), "*.zip");
        chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        { auto f = fc.getResult(); if (f == juce::File()) return; juce::String rep; proc_.exportSetPack (f, rep); proc_.lastAction = rep; stateDirty_ = true; });
        return true;
    }
    if (n == "importPack")
    {
        chooser_ = std::make_unique<juce::FileChooser> ("Import set pack", juce::File::getSpecialLocation (juce::File::userDesktopDirectory), "*.zip");
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        { auto f = fc.getResult(); if (f == juce::File()) return; juce::String rep; proc_.importSetPack (f, rep); proc_.lastAction = rep; stateDirty_ = true; });
        return true;
    }
    if (n == "exportSong") { if (auto* s = p.currentSong()) { auto f = juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (juce::File::createLegalFileName (s->title) + ".song.json"); writeJsonFile (f, s->toVar()); p.lastAction = "Exported " + f.getFileName(); } return ok(); }
    if (n == "resumeSession") { p.resumeSession(); return ok(); }
    if (n == "discardSession") { p.discardSession(); return ok(); }
    if (n == "deck") { p.deck = B ("on"); return ok(); }
    if (n == "rackAdd")
    {
        const int slot = I ("slot"); auto rack = slot < 0 ? p.rig.masterRack : p.rig.slots[(size_t) slot].rack;
        if (auto t = jm::fxTypeFromId (S ("type").toStdString()); t.has_value() && (int) rack.size() < jm::FxRackDsp::kMaxSlots)
        { rack.push_back (jm::defaultUnitState (*t, false)); if (slot < 0) p.setMasterRack (rack); else p.setSlotRack (slot, rack); }
        return ok();
    }
    if (n == "rackRemove")
    {
        const int slot = I ("slot"), u = I ("unit"); auto rack = slot < 0 ? p.rig.masterRack : p.rig.slots[(size_t) slot].rack;
        if (u >= 0 && u < (int) rack.size()) { rack.erase (rack.begin() + u); if (slot < 0) p.setMasterRack (rack); else p.setSlotRack (slot, rack); }
        return ok();
    }
    if (n == "rackMove")
    {
        const int slot = I ("slot"), u = I ("unit"), d = I ("dir"); auto rack = slot < 0 ? p.rig.masterRack : p.rig.slots[(size_t) slot].rack;
        const int v = u + d; if (u >= 0 && u < (int) rack.size() && v >= 0 && v < (int) rack.size()) { std::swap (rack[(size_t) u], rack[(size_t) v]); if (slot < 0) p.setMasterRack (rack); else p.setSlotRack (slot, rack); }
        return ok();
    }
    if (n == "macroLink") { const int m = juce::jlimit (0, 3, I ("macro")); auto& mc = p.rig.macros[(size_t) m]; if (S ("path").isEmpty()) mc.links.clear(); else mc.links.push_back ({ S ("path"), (float) F ("min", 0), (float) F ("max", 1) }); if (auto* s = p.currentSongMutable()) s->baseRig.macros[(size_t) m] = mc; return ok(); }
    juce::Logger::writeToLog ("Sanctuary UI: unknown command " + n);
    return false;
}

} // namespace sanctuary
