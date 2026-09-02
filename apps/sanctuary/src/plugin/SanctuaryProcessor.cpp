// SanctuaryProcessor.cpp — see header.
#include "SanctuaryProcessor.h"
#include "SanctuaryEditor.h"
#include "../engine/Factory.h"

namespace sanctuary {

static constexpr int kControlBlock = 64;
static constexpr float kRetireDb = -72.0f;
static constexpr double kRetireQuietSeconds = 0.5;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SanctuaryProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;
    ps.push_back (std::make_unique<P> (juce::ParameterID { "masterVol", 1 }, "Master Volume",
                                       juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f));
    ps.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "limiter", 1 }, "Limiter", true));
    for (int i = 1; i <= 4; ++i)
        ps.push_back (std::make_unique<P> (juce::ParameterID { "macro" + juce::String (i), 1 }, "Macro " + juce::String (i),
                                           juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));
    return { ps.begin(), ps.end() };
}

SanctuaryProcessor::SanctuaryProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Main", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Drone", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Click", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "SANCTUARY", createLayout())
{
    masterVolParam_ = apvts.getRawParameterValue ("masterVol");
    limiterParam_ = apvts.getRawParameterValue ("limiter");
    for (int i = 0; i < 4; ++i) macroParams_[(size_t) i] = apvts.getRawParameterValue ("macro" + juce::String (i + 1));

    for (auto d : { songsDir(), setsDir(), surfacesDir(), stageDir() }) d.createDirectory();
    library.rescan();
    ensureFactoryContent();
    reloadSongsFromDisk();

    // surfaces
    for (auto& f : surfacesDir().findChildFiles (juce::File::findFiles, false, "*.surface.json"))
        surfaces.push_back (Surface::fromVar (readJsonFile (f)));
    if (surfaces.empty()) { surfaces = factory::surfaces(); saveSurfaces(); }
    refreshPadMask();

    // §9 crash recovery: a lock file left behind means the last run did not exit cleanly
    sessionVar_ = readJsonFile (sessionFile());
    sessionAvailable_ = runningLock().existsAsFile() && sessionVar_.isObject();
    if (sessionAvailable_) sessionName = sessionVar_.getProperty ("setName", "").toString();
    runningLock().replaceWithText (juce::Time::getCurrentTime().toISO8601 (true));

    // default: the first set on disk, so the app is playable on launch
    auto sets = setsOnDisk();
    if (! sets.empty()) loadSet (sets.front());

    startTimerHz (30);
}

SanctuaryProcessor::~SanctuaryProcessor()
{
    stopTimer();
    preloadPool_.removeAllJobs (true, 4000);
    writeSession();
    runningLock().deleteFile();     // clean exit
}

bool SanctuaryProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i)
        if (! (l.outputBuses[i].isDisabled() || l.outputBuses[i] == juce::AudioChannelSet::stereo())) return false;
    return true;
}

//==============================================================================
void SanctuaryProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr_ = sampleRate; maxBlock_ = samplesPerBlock;
    load_.reset (sampleRate, samplesPerBlock);
    slotBuf_.setSize (2, samplesPerBlock);
    chainBuf_.setSize (2, samplesPerBlock);
    uiMidi_.ensureSize (4096);
    masterGain_.reset (sampleRate, 0.02);
    masterGain_.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterVolParam_->load(), -60.0f));
    expressionGain_.reset (sampleRate, 0.05);
    masterLimiter.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (masterLimiter.latencySamples());
    for (int i = 0; i < 4; ++i)
    {
        for (auto& ch : chains_[(size_t) i]) { ch.eng.prepare (sampleRate, samplesPerBlock); ch.rack.prepare (sampleRate, samplesPerBlock); }
        volSm_[(size_t) i].reset (sampleRate, 0.02);
        panSm_[(size_t) i].reset (sampleRate, 0.02);
        volSm_[(size_t) i].setCurrentAndTargetValue (routes_[(size_t) i].volDb.load());
        panSm_[(size_t) i].setCurrentAndTargetValue (routes_[(size_t) i].pan.load());
    }
    masterRack_.prepare (sampleRate, samplesPerBlock);
    drone.prepare (sampleRate, samplesPerBlock);
    chords.prepare (sampleRate);
    syncAllToDsp();
}

//==============================================================================
// MIDI
//==============================================================================
void SanctuaryProcessor::handleMidi (const juce::MidiMessage& m, int /*offset*/)
{
    const int ch = m.getChannel();
    const int shift = transpose_.load() + 12 * octave_.load();
    const int wheel = wheelShift_.load();

    auto slotTakes = [&] (int i, int note, int vel) -> bool
    {
        auto& r = routes_[(size_t) i];
        if (! r.enabled.load()) return false;
        const int c = r.channel.load();
        if (c != 0 && ch != 0 && c != ch) return false;
        if (note < r.keyLo.load() || note > r.keyHi.load()) return false;
        if (vel > 0 && (vel < r.velLo.load() || vel > r.velHi.load())) return false;
        return true;
    };
    auto slotNote = [&] (int i, int note) -> int
    {
        auto& r = routes_[(size_t) i];
        return juce::jlimit (0, 127, note + shift + 12 * r.octave.load() + (r.ignoreWheel.load() ? 0 : wheel));
    };

    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber(), vel = m.getVelocity();
        lastIncomingNote.store (note);
        // surface pads / buttons on notes never reach the keyboard (mask kept by the message thread)
        const bool consumed = (padNoteMask_[(size_t) (note >> 6)].load (std::memory_order_relaxed) >> (note & 63)) & 1u;
        if (consumed)
        {
            int s1, n1, s2, n2; surfFifo_.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) surfEvents_[(size_t) s1] = { -1, note, vel, ch };
            surfFifo_.finishedWrite (n1);
            return;
        }
        chords.noteOn (note + wheel);
        for (int i = 0; i < 4; ++i)
            if (slotTakes (i, note, vel))
                for (auto& c : chains_[(size_t) i])
                    if (c.live.load()) c.eng.noteOn (slotNote (i, note), vel);
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        chords.noteOff (note + wheel);
        for (int i = 0; i < 4; ++i)
            if (slotTakes (i, note, 0))
                for (auto& c : chains_[(size_t) i])
                    if (c.running.load()) c.eng.noteOff (slotNote (i, note));   // retiring chains too
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& s : chains_) for (auto& c : s) c.eng.allNotesOff (m.isAllNotesOff());
        chords.allOff();
    }
    else if (m.isPitchWheel())
    {
        const float semis = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f * 2.0f;
        for (auto& s : chains_) for (auto& c : s) if (c.running.load()) c.eng.setPitchBend (semis);
    }
    else if (m.isProgramChange())
    {
        pcRequest_.store (m.getProgramChangeNumber());
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        if (cc == 64)
        {
            for (auto& s : chains_) for (auto& c : s) if (c.running.load()) c.eng.sustainPedal (val >= 64);
        }
        else if (cc == 11)
        {
            const float n = (float) val / 127.0f;
            expressionGain_.setTargetValue (n * n);
        }
        else if (cc == 1)
        {
            const float n = (float) val / 127.0f;
            for (int i = 0; i < 4; ++i)
                for (auto& c : chains_[(size_t) i])
                {
                    if (! c.live.load()) continue;
                    switch (c.eng.world())
                    {
                        case 0: case 1:
                        {
                            const int u = modUnit_[(size_t) i].load (std::memory_order_relaxed);
                            if (u >= 0) c.rack.setParamRt (u, modParam_[(size_t) i].load (std::memory_order_relaxed), n * 100.0f);
                            break;
                        }
                        case 2: c.eng.setEngineParam (2 == c.eng.world() ? 0 : 0, n * 100.0f); break;   // FM brightness = index 0
                        case 3: c.eng.setEngineParam (0, n * 100.0f); break;                              // Strings swell = index 0
                    }
                }
            lastIncomingCc.store (cc);
        }
        else
        {
            lastIncomingCc.store (cc);
            int s1, n1, s2, n2; surfFifo_.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) surfEvents_[(size_t) s1] = { cc, -1, val, ch };
            surfFifo_.finishedWrite (n1);
        }
    }
}

void SanctuaryProcessor::sendUiMidi (const juce::MidiMessage& m)
{
    if (m.getRawDataSize() > 3) return;
    int s1, n1, s2, n2; uiFifo_.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
    {
        auto* d = m.getRawData();
        uiEvents_[(size_t) s1] = { d[0], (juce::uint8) (m.getRawDataSize() > 1 ? d[1] : 0), (juce::uint8) (m.getRawDataSize() > 2 ? d[2] : 0) };
    }
    uiFifo_.finishedWrite (n1);
}

//==============================================================================
// Audio
//==============================================================================
void SanctuaryProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (load_, buffer.getNumSamples());
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0) return;
    if (slotBuf_.getNumSamples() < n) { slotBuf_.setSize (2, n, false, false, true); chainBuf_.setSize (2, n, false, false, true); }

    // host tempo
    if (followHost_)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm()) { bpm_.store (*b); hostTempo.store (true); }

    // on-screen keyboard + UI wheels
    uiMidi_.clear();
    keyboardState.processNextMidiBuffer (uiMidi_, 0, n, true);
    for (const auto meta : uiMidi_) handleMidi (meta.getMessage(), 0);
    {
        int s1, n1, s2, n2; uiFifo_.prepareToRead (uiFifo_.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) { auto& e = uiEvents_[(size_t) (s1 + i)]; handleMidi (juce::MidiMessage (e.b0, e.b1, e.b2), 0); }
        for (int i = 0; i < n2; ++i) { auto& e = uiEvents_[(size_t) (s2 + i)]; handleMidi (juce::MidiMessage (e.b0, e.b1, e.b2), 0); }
        uiFifo_.finishedRead (n1 + n2);
    }

    // macros from host automation
    for (int i = 0; i < 4; ++i)
    {
        const float v = macroParams_[(size_t) i]->load();
        if (std::abs (v - lastMacro_[(size_t) i]) > 1e-4f) { lastMacro_[(size_t) i] = v; macroDirty_[(size_t) i] = true; }
    }

    // sample-accurate render: split at every MIDI event and at the control block
    auto it = midi.begin(); const auto end = midi.end();
    int pos = 0;
    while (pos < n)
    {
        while (it != end && (*it).samplePosition <= pos) { handleMidi ((*it).getMessage(), pos); ++it; }
        int stop = juce::jmin (n, pos + kControlBlock);
        if (it != end) stop = juce::jmin (stop, juce::jmax (pos + 1, (*it).samplePosition));
        renderChunk (buffer, pos, stop - pos);
        pos = stop;
    }
    while (it != end) { handleMidi ((*it).getMessage(), n - 1); ++it; }
    chords.advance (n);

    // master gain + expression, limiter (main bus only)
    auto main = getBusBuffer (buffer, false, 0);
    masterGain_.setTargetValue (juce::Decibels::decibelsToGain (masterVolParam_->load(), -60.0f));
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain_.getNextValue() * expressionGain_.getNextValue();
        main.getWritePointer (0)[i] *= g; main.getWritePointer (1)[i] *= g;
    }
    masterLimiter.setEnabled (limiterParam_->load() >= 0.5f);
    masterLimiter.process (main);
    masterPeakL.store (main.getMagnitude (0, 0, n));
    masterPeakR.store (main.getMagnitude (1, 0, n));

    // click: to its own bus, or main only if explicitly allowed (§8)
    juce::AudioBuffer<float>* clickBus = nullptr;
    juce::AudioBuffer<float> clickView;
    if (getBusCount (false) > 2 && getBus (false, 2)->isEnabled()) { clickView = getBusBuffer (buffer, false, 2); clickBus = &clickView; }
    renderClick (clickBus, main, n);

    if (load_.getXRunCount() > 0) overruns.fetch_add (1);
}

void SanctuaryProcessor::renderChunk (juce::AudioBuffer<float>& buffer, int start, int len)
{
    auto mainBus = getBusBuffer (buffer, false, 0);
    float* outPtr[2] = { mainBus.getWritePointer (0) + start, mainBus.getWritePointer (1) + start };
    juce::AudioBuffer<float> outView (outPtr, 2, len);
    float* slotPtr[2] = { slotBuf_.getWritePointer (0), slotBuf_.getWritePointer (1) };
    juce::AudioBuffer<float> slotView (slotPtr, 2, len);
    float* chPtr[2] = { chainBuf_.getWritePointer (0), chainBuf_.getWritePointer (1) };
    juce::AudioBuffer<float> chainView (chPtr, 2, len);

    const int quietSamples = (int) (sr_ * kRetireQuietSeconds);
    const float retireGain = juce::Decibels::decibelsToGain (kRetireDb);
    int voices = 0;

    for (int i = 0; i < 4; ++i)
    {
        slotView.clear();
        bool anything = false;
        for (auto& c : chains_[(size_t) i])
        {
            if (! c.running.load()) continue;
            const bool isLive = c.live.load();
            voices += c.eng.activeVoices();
            if (isLive && ! routes_[(size_t) i].enabled.load() && c.eng.activeVoices() == 0) continue;
            chainView.clear();
            c.eng.render (chainView, len);
            c.rack.process (chainView, bpm_.load());
            const float pk = chainView.getMagnitude (0, len);
            c.peak = pk;
            if (! isLive)
            {
                // §4.2: a retiring chain is released once its voices are gone and
                // its tail has fallen below −72 dBFS for 500 ms.
                if (c.eng.activeVoices() == 0 && pk < retireGain) c.silentSamples += len; else c.silentSamples = 0;
                if (c.silentSamples >= quietSamples) { c.running.store (false); continue; }
            }
            slotView.addFrom (0, 0, chainView, 0, 0, len);
            slotView.addFrom (1, 0, chainView, 1, 0, len);
            anything = true;
        }
        if (! anything) { if (start == 0) slotPeak[(size_t) i].store (0); continue; }

        auto& vs = volSm_[(size_t) i]; auto& ps = panSm_[(size_t) i];
        vs.setTargetValue (routes_[(size_t) i].volDb.load());
        ps.setTargetValue (routes_[(size_t) i].pan.load());
        constexpr float kPanK = 1.414f * 0.7071f;
        if (vs.isSmoothing() || ps.isSmoothing())
        {
            auto* l = outView.getWritePointer (0); auto* r = outView.getWritePointer (1);
            const auto* sl = slotView.getReadPointer (0); const auto* sr = slotView.getReadPointer (1);
            for (int s = 0; s < len; ++s)
            {
                const float vol = juce::Decibels::decibelsToGain (vs.getNextValue(), -60.0f);
                const float a = (ps.getNextValue() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                l[s] += sl[s] * vol * std::cos (a) * kPanK;
                r[s] += sr[s] * vol * std::sin (a) * kPanK;
            }
        }
        else
        {
            const float vol = juce::Decibels::decibelsToGain (vs.getCurrentValue(), -60.0f);
            const float a = (ps.getCurrentValue() + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            outView.addFrom (0, 0, slotView, 0, 0, len, vol * std::cos (a) * kPanK);
            outView.addFrom (1, 0, slotView, 1, 0, len, vol * std::sin (a) * kPanK);
        }
        if (start == 0) slotPeak[(size_t) i].store (slotView.getMagnitude (0, len));
    }
    if (start == 0) totalVoices.store (voices);

    masterRack_.process (outView, bpm_.load());

    // drone: own bus when the host exposes it, else the main bus (§8)
    const bool droneBus = getBusCount (false) > 1 && getBus (false, 1)->isEnabled();
    if (droneBus)
    {
        auto db = getBusBuffer (buffer, false, 1);
        float* dp[2] = { db.getWritePointer (0) + start, db.getWritePointer (1) + start };
        juce::AudioBuffer<float> dv (dp, 2, len);
        drone.render (dv, len);
    }
    else drone.render (outView, len);
}

void SanctuaryProcessor::renderClick (juce::AudioBuffer<float>* clickBus, juce::AudioBuffer<float>& main, int n)
{
    const double bpm = bpm_.load();
    const double beatLen = sr_ * 60.0 / juce::jmax (20.0, bpm) * (eighthMeter_ ? 0.5 : 1.0);
    const bool on = clickOn_.load();
    auto* dst = clickBus != nullptr ? clickBus : (clickToMain_ ? &main : nullptr);
    if (clickBus != nullptr) clickBus->clear();
    for (int i = 0; i < n; ++i)
    {
        beatSamplePos_ += 1.0;
        if (beatSamplePos_ >= beatLen)
        {
            beatSamplePos_ -= beatLen;
            beatIndex_ = (beatIndex_ + 1) % juce::jmax (1, beatsPerBar_);
            if (on) { clickSamplesLeft_ = (int) (sr_ * 0.012); clickFreq_ = beatIndex_ == 0 ? 1600.0f : 1000.0f; clickPhase_ = 0.0; }
        }
        if (clickSamplesLeft_ > 0 && dst != nullptr)
        {
            const float env = (float) clickSamplesLeft_ / (float) (sr_ * 0.012);
            const float s = (float) std::sin (clickPhase_) * env * 0.5f;
            clickPhase_ += juce::MathConstants<double>::twoPi * clickFreq_ / sr_;
            for (int ch = 0; ch < juce::jmin (2, dst->getNumChannels()); ++ch) dst->getWritePointer (ch)[i] += s;
            --clickSamplesLeft_;
        }
    }
    beatPhase.store ((double) beatIndex_ + beatSamplePos_ / beatLen);
}

//==============================================================================
// Timer (message thread, 30 Hz): surface events, glides, drone follow, meters.
//==============================================================================
void SanctuaryProcessor::timerCallback()
{
    cpuLoad.store ((float) load_.getLoadAsProportion());
    drainSurfaceFifo();
    const int pc = pcRequest_.exchange (-1);
    if (pc >= 0 && pc < (int) set.songs.size()) gotoSong (pc);
    for (int i = 0; i < 4; ++i)
        if (macroDirty_[(size_t) i]) { macroDirty_[(size_t) i] = false; rig.macros[(size_t) i].value = lastMacro_[(size_t) i]; applyMacro (i); }
    stepGlides();
    updateDroneTarget();
}

void SanctuaryProcessor::drainSurfaceFifo()
{
    int s1, n1, s2, n2; surfFifo_.prepareToRead (surfFifo_.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) { auto& e = surfEvents_[(size_t) (s1 + i)]; handleSurfaceEvent (e.cc, e.note, e.value, e.channel); }
    for (int i = 0; i < n2; ++i) { auto& e = surfEvents_[(size_t) (s2 + i)]; handleSurfaceEvent (e.cc, e.note, e.value, e.channel); }
    surfFifo_.finishedRead (n1 + n2);
}

void SanctuaryProcessor::handleSurfaceEvent (int cc, int note, int value, int channel)
{
    if (surfaces.empty()) return;
    auto& surf = currentSurface();

    // learn: bind the incoming control (creating a control entry if the surface has none)
    if (learnTarget_.isNotEmpty())
    {
        const SurfaceControl* sc = cc >= 0 ? surf.controlForCc (cc, channel) : surf.controlForNote (note, channel);
        juce::String id;
        if (sc != nullptr) id = sc->id;
        else
        {
            id = cc >= 0 ? "cc" + juce::String (cc) : "note" + juce::String (note);
            surf.controls.push_back ({ id, cc >= 0 ? ControlKind::knob : ControlKind::pad, cc, note, 0, id, cc >= 0 ? ControlMode::absolute : ControlMode::momentary });
        }
        surf.bind (id, learnTarget_);
        learnTarget_.clear();
        saveSurfaces();
        refreshPadMask();
        learnChanged.sendChangeMessage();
        return;
    }

    const SurfaceControl* sc = cc >= 0 ? surf.controlForCc (cc, channel) : surf.controlForNote (note, channel);
    if (sc == nullptr) return;
    auto target = surf.targetFor (sc->id);
    if (target.isEmpty()) return;

    // "trimN" targets resolve through the current Song's Live Trim list
    if (target.startsWith ("trim"))
    {
        const int idx = target.substring (4).getIntValue() - 1;
        auto* s = currentSong();
        if (s == nullptr) return;
        auto paths = s->liveTrimPaths();
        if (idx < 0 || idx >= paths.size()) return;
        target = paths[idx];
    }

    if (isActionTarget (target))
    {
        const bool press = value >= 64;
        if (press) triggerAction (target);
        return;
    }
    const auto pi = pathInfo (rig, target);
    if (! pi.valid) return;
    if (pi.kind == ParamKind::Bool && (sc->mode == ControlMode::toggle || sc->kind == ControlKind::button || sc->kind == ControlKind::pad || sc->kind == ControlKind::pedal))
    {
        if (value >= 64) { auto cur = rigGet (rig, target).value_or (0.0f); setPath (target, cur >= 0.5f ? 0.0f : 1.0f); }
        return;
    }
    if (sc->mode == ControlMode::relative)
    {
        const float delta = (value < 64 ? (float) value : (float) (value - 128)) / 127.0f;
        setPathNorm (target, getPathNorm (target) + delta);
        return;
    }
    setPathNorm (target, (float) value / 127.0f);
}

//==============================================================================
// Parameter paths
//==============================================================================
float SanctuaryProcessor::getPathNorm (const juce::String& path) const
{
    const auto pi = pathInfo (rig, path);
    if (! pi.valid) return 0.0f;
    return normFromValue (pi, rigGet (rig, path).value_or (pi.min));
}

bool SanctuaryProcessor::setPathNorm (const juce::String& path, float norm, bool writeToMoment)
{
    const auto pi = pathInfo (rig, path);
    if (! pi.valid) return false;
    return setPath (path, valueFromNorm (pi, norm), writeToMoment);
}

void SanctuaryProcessor::applyFxParamLive (int slot, int unit, const juce::String& id, float v)
{
    if (slot < 0)
    {
        if (id == "bypass") masterRack_.setBypassed (unit, v >= 0.5f); else masterRack_.setParam (unit, id.toStdString(), v);
        return;
    }
    for (auto& c : chains_[(size_t) slot])
        if (c.live.load())
        {
            if (id == "bypass") c.rack.setBypassed (unit, v >= 0.5f); else c.rack.setParam (unit, id.toStdString(), v);
        }
}

bool SanctuaryProcessor::setPath (const juce::String& path, float raw, bool writeToMoment)
{
    if (! rigSet (rig, path, raw)) return false;
    auto parts = juce::StringArray::fromTokens (path, ".", "");
    const auto head = parts[0];
    if (head.startsWith ("slot"))
    {
        const int i = head.substring (4).getIntValue();
        auto& s = rig.slots[(size_t) i];
        auto& r = routes_[(size_t) i];
        if (parts[1] == "vol") r.volDb.store (s.volumeDb);
        else if (parts[1] == "pan") r.pan.store (s.pan);
        else if (parts[1] == "enabled") r.enabled.store (s.enabled);
        else if (parts[1] == "engine")
        {
            for (auto& c : chains_[(size_t) i]) if (c.live.load()) c.eng.setEngineParam (parts[2].toStdString(), s.engineParams[parts[2].toStdString()]);
        }
        else if (parts[1] == "ampenv")
        {
            for (auto& c : chains_[(size_t) i])
                if (c.live.load())
                {
                    c.eng.ampEnv().attackMs = s.ampEnv.attackMs; c.eng.ampEnv().decayMs = s.ampEnv.decayMs;
                    c.eng.ampEnv().sustain = s.ampEnv.sustain; c.eng.ampEnv().releaseMs = s.ampEnv.releaseMs;
                }
        }
        else if (parts[1].startsWith ("fx"))
        {
            const int u = parts[1].substring (2).getIntValue();
            applyFxParamLive (i, u, parts[2], rigGet (rig, path).value_or (raw));
        }
    }
    else if (head == "master")
    {
        const int u = parts[1].substring (2).getIntValue();
        applyFxParamLive (-1, u, parts[2], rigGet (rig, path).value_or (raw));
    }
    else if (head.startsWith ("macro"))
    {
        const int n = head.substring (5).getIntValue() - 1;
        if (auto* p = apvts.getParameter ("macro" + juce::String (n + 1)))
            if (std::abs (p->getValue() - rig.macros[(size_t) n].value) > 1e-4f) p->setValueNotifyingHost (rig.macros[(size_t) n].value);
        lastMacro_[(size_t) n] = rig.macros[(size_t) n].value;
        applyMacro (n);
    }
    else if (head == "drone")
    {
        pushDroneSettings();
        if (parts[1] == "follow" || parts[1] == "on") updateDroneTarget();
    }
    if (writeToMoment && ! glideActive()) writeToCurrentMoment (path, getPathNorm (path));
    rigChanged.sendChangeMessage();
    return true;
}

void SanctuaryProcessor::applyMacro (int index)
{
    auto& m = rig.macros[(size_t) index];
    for (auto& l : m.links)
    {
        const float norm = l.min + (l.max - l.min) * m.value;
        setPathNorm (l.path, norm, false);
    }
}

void SanctuaryProcessor::writeToCurrentMoment (const juce::String& path, float norm)
{
    auto* s = currentSongMutable();
    if (s == nullptr || momentIndex < 0 || momentIndex >= (int) s->moments.size()) return;
    // A value equal to the base rig's value is not a diff.
    const auto pi = pathInfo (s->baseRig, path);
    if (! pi.valid) return;
    const float baseNorm = normFromValue (pi, rigGet (s->baseRig, path).value_or (pi.min));
    auto& diff = s->moments[(size_t) momentIndex].diff;
    if (momentIndex == 0 || std::abs (baseNorm - norm) < 1e-4f)
    {
        // Moment 0 IS the base state: edits there change the base rig for every Moment
        if (momentIndex == 0) { rigSet (s->baseRig, path, valueFromNorm (pi, norm)); diff.erase (path); }
        else diff.erase (path);
    }
    else diff[path] = norm;
}

bool SanctuaryProcessor::triggerAction (const juce::String& a)
{
    if (a == "stage.nextMoment") { nextMoment(); return true; }
    if (a == "stage.prevMoment") { prevMoment(); return true; }
    if (a == "stage.nextSong") { nextSong(); return true; }
    if (a == "stage.prevSong") { prevSong(); return true; }
    if (a == "stage.panic") { panic(); return true; }
    if (a == "stage.droneToggle") { toggleDrone(); return true; }
    if (a == "stage.deck") { deck = ! deck; stageChanged.sendChangeMessage(); return true; }
    if (a == "wheel.soundIn.up") { nudgeSoundIn (1); return true; }
    if (a == "wheel.soundIn.down") { nudgeSoundIn (-1); return true; }
    if (a == "tempo.tap") { tapTempo(); return true; }
    if (a.startsWith ("stage.moment")) { gotoMoment (a.substring (12).getIntValue() - 1); return true; }
    return false;
}

//==============================================================================
// Stage control
//==============================================================================
const Song* SanctuaryProcessor::currentSong() const
{
    if (songIndex < 0 || songIndex >= (int) set.songs.size()) return nullptr;
    auto it = songs.find (set.songs[(size_t) songIndex].songId);
    return it == songs.end() ? nullptr : &it->second;
}
Song* SanctuaryProcessor::currentSongMutable() { return const_cast<Song*> (std::as_const (*this).currentSong()); }
const Song* SanctuaryProcessor::nextSongInSet() const
{
    if (songIndex + 1 >= (int) set.songs.size()) return nullptr;
    auto it = songs.find (set.songs[(size_t) songIndex + 1].songId);
    return it == songs.end() ? nullptr : &it->second;
}

void SanctuaryProcessor::loadSet (const Set& s)
{
    set = s;
    songIndex = -1; momentIndex = 0;
    if (! set.songs.empty()) gotoSong (0);
    else stageChanged.sendChangeMessage();
    writeSession();
}

jm::BundlePtr SanctuaryProcessor::bundleFor (const juce::String& dir)
{
    if (dir.isEmpty()) return nullptr;
    std::lock_guard<std::mutex> lg (loadMutex_);
    auto it = bundleCache_.find (dir);
    if (it != bundleCache_.end()) return it->second;
    juce::File f (dir);
    if (! f.isDirectory()) return nullptr;
    juce::String err;
    auto b = library.loadBundle (f, err);
    if (b == nullptr) juce::Logger::writeToLog ("Sanctuary: bundle load failed: " + err);
    bundleCache_[dir] = b;
    return b;
}

void SanctuaryProcessor::preloadSong (const Song& s)
{
    // §4.2: warm the next Song's bundles in the background so switching is instant
    std::vector<juce::String> dirs;
    for (auto& sl : s.baseRig.slots) if (sl.hasSound()) dirs.push_back (sl.bundleDir);
    juce::WeakReference<SanctuaryProcessor> weak (this);
    preloadPool_.addJob ([weak, dirs]
    {
        for (auto& d : dirs)
        {
            if (weak == nullptr) return;
            weak->bundleFor (d);
        }
    });
}

void SanctuaryProcessor::gotoSong (int index)
{
    if (index < 0 || index >= (int) set.songs.size()) return;
    auto it = songs.find (set.songs[(size_t) index].songId);
    if (it == songs.end()) { juce::Logger::writeToLog ("Sanctuary: set references a missing song"); return; }
    const auto& entry = set.songs[(size_t) index];
    auto& song = it->second;
    songIndex = index; momentIndex = 0;
    glides_.clear();

    // wheel (§6.1): song lock > set entry override > set global > song's own key
    playIn_ = song.playIn;
    if (song.wheelLock) soundIn_ = song.soundIn;
    else if (entry.keyOverride) soundIn_ = *entry.keyOverride;
    else if (set.globalWheelActive) { playIn_ = set.globalPlayIn; soundIn_ = set.globalSoundIn; }
    else soundIn_ = song.soundIn;
    wheelShift_.store (wheelShift());

    // tempo
    if (! followHost_) bpm_.store (entry.tempoOverride.value_or (song.tempo));
    beatsPerBar_ = song.beatsPerBar(); eighthMeter_ = song.meter.endsWith ("/8");

    Rig target = song.rigForMoment (0);
    // drone between songs (§5.4): keep the bed running and crossfade its key
    if (set.padBetweenSongs == PadBetweenSongs::keep && rig.drone.on) target.drone.on = true;
    else if (set.padBetweenSongs == PadBetweenSongs::stop) target.drone.on = false;
    applyRigToDsp (target, true);

    if (auto* nx = nextSongInSet()) preloadSong (*nx);
    stageChanged.sendChangeMessage();
    rigChanged.sendChangeMessage();
    writeSession();
}

static bool rackStructureDiffers (const FxRackState& a, const FxRackState& b)
{
    if (a.size() != b.size()) return true;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].type != b[i].type || a[i].bypassed != b[i].bypassed) return true;
    return false;
}

void SanctuaryProcessor::syncSlotToChain (int slot, SlotChain& ch, const SlotSpec& s, bool clickless)
{
    ch.eng.setWorld (s.world);
    if (ch.bundleDir != s.bundleDir)
    {
        ch.eng.setBundle (bundleFor (s.bundleDir));
        ch.bundleDir = s.bundleDir;
    }
    for (auto& [id, v] : s.engineParams) ch.eng.setEngineParam (id, v);
    ch.eng.ampEnv().attackMs = s.ampEnv.attackMs; ch.eng.ampEnv().decayMs = s.ampEnv.decayMs;
    ch.eng.ampEnv().sustain = s.ampEnv.sustain; ch.eng.ampEnv().releaseMs = s.ampEnv.releaseMs;
    // a LIVE chain whose rack changes shape hands over through the dry path (25 ms) so
    // a unit appearing, vanishing or un-bypassing never steps the signal (§4.2)
    if (clickless && rackStructureDiffers (rig.slots[(size_t) slot].rack, s.rack)) ch.rack.applyStructuralChange (s.rack);
    else ch.rack.setState (s.rack);
    // mod wheel destination (§5.3 of the core spec): Rhodes → tremolo depth, MKS → chorus depth
    int unit = -1, param = -1;
    for (size_t u = 0; u < s.rack.size(); ++u)
    {
        if (s.world == 0 && s.rack[u].type == FxType::tremoloAutoPan) { unit = (int) u; param = 1; break; }
        if (s.world == 1 && s.rack[u].type == FxType::jmChorus) { unit = (int) u; param = 2; break; }
    }
    modUnit_[(size_t) slot].store (unit); modParam_[(size_t) slot].store (param);
}

void SanctuaryProcessor::refreshPadMask()
{
    juce::uint64 m[2] = { 0, 0 };
    if (auto* s = currentSurfaceConst())
        for (auto& c : s->controls)
            if (c.note >= 0 && c.note < 128 && s->targetFor (c.id).isNotEmpty())
                m[c.note >> 6] |= 1ull << (c.note & 63);
    padNoteMask_[0].store (m[0]); padNoteMask_[1].store (m[1]);
}

void SanctuaryProcessor::syncRoutes()
{
    for (int i = 0; i < 4; ++i)
    {
        auto& s = rig.slots[(size_t) i]; auto& r = routes_[(size_t) i];
        r.keyLo.store (s.keyLo); r.keyHi.store (s.keyHi); r.velLo.store (s.velLo); r.velHi.store (s.velHi);
        r.octave.store (s.octaveShift); r.channel.store (s.channel); r.ignoreWheel.store (s.ignoreWheel);
        r.enabled.store (s.enabled); r.volDb.store (s.volumeDb); r.pan.store (s.pan);
    }
    transpose_.store (rig.transpose); octave_.store (rig.octave);
}

void SanctuaryProcessor::applyRigToDsp (const Rig& newRig, bool songChange)
{
    for (int i = 0; i < 4; ++i)
    {
        auto& pair = chains_[(size_t) i];
        int liveIdx = -1;
        for (int c = 0; c < kChains; ++c) if (pair[(size_t) c].live.load()) liveIdx = c;
        const auto& s = newRig.slots[(size_t) i];
        const bool bundleChanged = liveIdx < 0 || pair[(size_t) liveIdx].bundleDir != s.bundleDir;
        if (songChange && bundleChanged)
        {
            // §4.2: hand new notes to a FREE chain; the old one keeps ringing until quiet.
            // Three chains per slot: live + two tails. If all are busy, the quietest tail is taken.
            int nextIdx = -1; int quietest = -1; int quietestSamples = -1;
            for (int c = 0; c < kChains; ++c)
            {
                if (c == liveIdx) continue;
                auto& cand = pair[(size_t) c];
                if (! cand.running.load()) { nextIdx = c; break; }
                if (cand.silentSamples > quietestSamples) { quietestSamples = cand.silentSamples; quietest = c; }
            }
            if (nextIdx < 0) nextIdx = quietest >= 0 ? quietest : (liveIdx + 1) % kChains;
            auto& nc = pair[(size_t) nextIdx];
            if (nc.running.load() && nc.eng.activeVoices() > 0) nc.eng.allNotesOff (true);
            nc.live.store (false);
            syncSlotToChain (i, nc, s, false);
            nc.silentSamples = 0;
            if (liveIdx >= 0)
            {
                auto& oc = pair[(size_t) liveIdx];
                oc.live.store (false);
                oc.silentSamples = 0;
                if (oc.eng.activeVoices() == 0) oc.running.store (false);
            }
            nc.running.store (true);
            nc.live.store (true);
        }
        else
        {
            if (liveIdx < 0) { liveIdx = 0; pair[0].running.store (true); pair[0].live.store (true); }
            syncSlotToChain (i, pair[(size_t) liveIdx], s, true);
        }
    }
    if (rackStructureDiffers (rig.masterRack, newRig.masterRack)) masterRack_.applyStructuralChange (newRig.masterRack);
    else masterRack_.setState (newRig.masterRack);
    rig = newRig;
    syncRoutes();
    pushDroneSettings();
    for (int i = 0; i < 4; ++i)
    {
        if (auto* p = apvts.getParameter ("macro" + juce::String (i + 1))) p->setValueNotifyingHost (rig.macros[(size_t) i].value);
        lastMacro_[(size_t) i] = rig.macros[(size_t) i].value;
    }
    updateDroneTarget();
}

void SanctuaryProcessor::syncAllToDsp() { applyRigToDsp (rig, true); }

void SanctuaryProcessor::gotoMoment (int index, bool glide)
{
    auto* s = currentSongMutable();
    if (s == nullptr || index < 0 || index >= (int) s->moments.size()) return;
    const Rig from = rig;
    Rig to = s->rigForMoment (index);
    // structure (bundles, ranges, racks) always comes from the live rig — a Moment
    // only changes VALUES; keep any live structural edits
    for (int i = 0; i < 4; ++i)
    {
        auto& ts = to.slots[(size_t) i]; auto& fs = from.slots[(size_t) i];
        ts.bundleDir = fs.bundleDir; ts.bundleName = fs.bundleName; ts.world = fs.world; ts.label = fs.label;
        ts.keyLo = fs.keyLo; ts.keyHi = fs.keyHi; ts.velLo = fs.velLo; ts.velHi = fs.velHi;
        ts.octaveShift = fs.octaveShift; ts.channel = fs.channel; ts.ignoreWheel = fs.ignoreWheel;
    }
    momentIndex = index;
    const double bars = glide ? s->moments[(size_t) index].transitionBars : 0.0;
    if (bars <= 0.0)
    {
        glides_.clear();
        applyRigToDsp (to, false);
    }
    else startGlides (from, to, bars);
    stageChanged.sendChangeMessage();
    rigChanged.sendChangeMessage();
    writeSession();
}
void SanctuaryProcessor::nextMoment()
{
    auto* s = currentSong();
    if (s == nullptr) return;
    if (momentIndex + 1 < (int) s->moments.size()) gotoMoment (momentIndex + 1);
    else if (songIndex + 1 < (int) set.songs.size()) nextSong();
}
void SanctuaryProcessor::prevMoment()
{
    if (momentIndex > 0) gotoMoment (momentIndex - 1);
    else if (songIndex > 0) prevSong();
}

void SanctuaryProcessor::startGlides (const Rig& from, const Rig& to, double bars)
{
    glides_.clear();
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double durMs = bars * beatsPerBar() * beatSeconds() * 1000.0;
    glideStartMs_ = now; glideDurMs_ = durMs;
    for (auto& path : allPaths (from))
    {
        const auto pi = pathInfo (from, path);
        if (! pi.valid) continue;
        const auto a = rigGet (from, path), b = rigGet (to, path);
        if (! a || ! b || std::abs (*a - *b) < 1e-5f) continue;
        Glide g; g.path = path; g.fromNorm = normFromValue (pi, *a); g.toNorm = normFromValue (pi, *b);
        g.startMs = now; g.durMs = durMs;
        if (! pi.glidable)
        {
            // discrete: at the start, except slot DISABLE (end) — §4.2
            const bool isDisable = path.endsWith (".enabled") && *b < 0.5f;
            if (isDisable) { g.discreteAtEnd = true; glides_.push_back (g); }
            else setPath (path, *b, false);
            continue;
        }
        glides_.push_back (g);
    }
    // drone diffs that are not paths (key/minor/character) apply now
    Rig tmp = rig;
    tmp.drone.key = to.drone.key; tmp.drone.minor = to.drone.minor;
    rig.drone.key = to.drone.key; rig.drone.minor = to.drone.minor;
    pushDroneSettings();
    updateDroneTarget();
}

void SanctuaryProcessor::stepGlides()
{
    if (glides_.empty()) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    bool anyLeft = false;
    for (auto& g : glides_)
    {
        const double t = g.durMs <= 0.0 ? 1.0 : juce::jlimit (0.0, 1.0, (now - g.startMs) / g.durMs);
        if (g.discreteAtEnd) { if (t >= 1.0) setPath (g.path, 0.0f, false); else anyLeft = true; continue; }
        const auto pi = pathInfo (rig, g.path);
        float norm;
        if (g.path.endsWith (".vol"))
        {
            // volume glides in dB space (linear in dB) — the norm already IS dB-linear
            norm = g.fromNorm + (g.toNorm - g.fromNorm) * (float) t;
        }
        else norm = g.fromNorm + (g.toNorm - g.fromNorm) * (float) t;
        setPath (g.path, valueFromNorm (pi, norm), false);
        if (t < 1.0) anyLeft = true;
    }
    if (! anyLeft) glides_.clear();
}
float SanctuaryProcessor::glideProgress() const
{
    if (glides_.empty() || glideDurMs_ <= 0.0) return 1.0f;
    return (float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - glideStartMs_) / glideDurMs_);
}

void SanctuaryProcessor::panic()
{
    for (auto& s : chains_) for (auto& c : s) { c.eng.sustainPedal (false); c.eng.allNotesOff (true); }
    chords.allOff();
    if (rig.drone.on)
    {
        DroneSettings d = rig.drone; d.on = false; d.fallSeconds = 1.0f;
        drone.setSettings (d);
        rig.drone.on = false;
        rigChanged.sendChangeMessage();
    }
}
void SanctuaryProcessor::toggleDrone() { setPath ("drone.on", rig.drone.on ? 0.0f : 1.0f); }

//==============================================================================
// Wheel + tempo
//==============================================================================
void SanctuaryProcessor::setPlayIn (int pc)
{
    playIn_ = ((pc % 12) + 12) % 12; wheelShift_.store (wheelShift());
    if (auto* s = currentSongMutable()) s->playIn = playIn_;
    updateDroneTarget(); stageChanged.sendChangeMessage(); writeSession();
}
void SanctuaryProcessor::setSoundIn (int pc)
{
    soundIn_ = ((pc % 12) + 12) % 12; wheelShift_.store (wheelShift());
    if (auto* s = currentSongMutable()) s->soundIn = soundIn_;
    updateDroneTarget(); stageChanged.sendChangeMessage(); writeSession();
}
void SanctuaryProcessor::setTempo (double bpm)
{
    bpm_.store (juce::jlimit (40.0, 240.0, bpm));
    if (auto* s = currentSongMutable()) s->tempo = bpm_.load();
    stageChanged.sendChangeMessage();
}
void SanctuaryProcessor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps_.empty() && now - taps_.back() > 2500.0) taps_.clear();
    taps_.push_back (now);
    if (taps_.size() > 4) taps_.erase (taps_.begin());
    if (taps_.size() >= 2)
    {
        double sum = 0; for (size_t i = 1; i < taps_.size(); ++i) sum += taps_[i] - taps_[i - 1];
        const double avg = sum / (double) (taps_.size() - 1);
        setTempo (60000.0 / avg);
    }
}
double SanctuaryProcessor::beatSeconds() const { return 60.0 / juce::jmax (20.0, bpm_.load()) * (eighthMeter_ ? 0.5 : 1.0); }
int SanctuaryProcessor::beatsPerBar() const { return beatsPerBar_; }

//==============================================================================
// Drone follow (§5.3)
//==============================================================================
void SanctuaryProcessor::pushDroneSettings() { drone.setSettings (rig.drone); }

void SanctuaryProcessor::updateDroneTarget()
{
    auto& d = rig.drone;
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (d.follow == DroneFollow::off) { drone.setTarget (d.key, d.minor, -1); return; }
    const int songRoot = soundIn_;   // the room's key
    const bool songMinor = currentSong() ? currentSong()->minor : d.minor;
    if (d.follow == DroneFollow::songKey) { drone.setTarget (songRoot, songMinor, -1); return; }

    // chords: re-voice after the chord has been stable for followLagMs; fall back
    // to the song key once nothing has been held for holdSeconds
    const auto c = chords.result();
    if (c.root >= 0)
    {
        chordSeenMs_ = now;
        if (c.root != pendingRoot_ || c.quality != pendingQ_) { pendingRoot_ = c.root; pendingQ_ = c.quality; lastChordChangeMs_ = now; }
        else if (now - lastChordChangeMs_ >= d.followLagMs && (c.root != lastChordRoot_ || c.quality != lastChordQ_))
        {
            lastChordRoot_ = c.root; lastChordQ_ = c.quality;
            drone.setTarget (c.root, chordIsMinorish (c.quality), chordColourTone (c.quality));
        }
    }
    else if (lastChordRoot_ >= 0 && now - chordSeenMs_ > d.holdSeconds * 1000.0)
    {
        lastChordRoot_ = -1; lastChordQ_ = ChordQuality::none; pendingRoot_ = -1;
        drone.setTarget (songRoot, songMinor, -1);
    }
    else if (lastChordRoot_ < 0) drone.setTarget (songRoot, songMinor, -1);
}

//==============================================================================
// Slot editing
//==============================================================================
void SanctuaryProcessor::loadBundleIntoSlot (int slot, const jm::BundleInfo& info)
{
    auto& s = rig.slots[(size_t) slot];
    const bool worldChanged = s.world != info.world || s.bundleDir.isEmpty();
    s.world = info.world; s.bundleDir = info.dir.getFullPathName(); s.bundleName = info.name;
    if (s.label.isEmpty()) s.label = info.name;
    if (worldChanged) { s.engineParams = jm::defaultEngineParams (info.world); s.rack = jm::buildDefaultRack (info.world); }
    s.presetName = "Init"; s.enabled = true;
    s.ampEnv = AmpEnv {}; s.ampEnv.attackMs = info.sustaining ? 10.0f : 2.0f; s.ampEnv.releaseMs = info.sustaining ? 400.0f : 250.0f;
    // structural change on the live rig → same handover as a song change for this slot
    Rig r = rig; applyRigToDsp (r, true);
    if (auto* song = currentSongMutable()) song->baseRig.slots[(size_t) slot] = s;
    rigChanged.sendChangeMessage();
}
void SanctuaryProcessor::clearSlot (int slot)
{
    auto& s = rig.slots[(size_t) slot];
    s.bundleDir.clear(); s.bundleName.clear(); s.enabled = false;
    Rig r = rig; applyRigToDsp (r, true);
    if (auto* song = currentSongMutable()) song->baseRig.slots[(size_t) slot] = s;
    rigChanged.sendChangeMessage();
}
void SanctuaryProcessor::setSlotRange (int slot, int keyLo, int keyHi, int velLo, int velHi, int octave, int channel, bool ignoreWheel)
{
    auto& s = rig.slots[(size_t) slot];
    s.keyLo = juce::jlimit (0, 127, juce::jmin (keyLo, keyHi)); s.keyHi = juce::jlimit (0, 127, juce::jmax (keyLo, keyHi));
    s.velLo = juce::jlimit (1, 127, juce::jmin (velLo, velHi)); s.velHi = juce::jlimit (1, 127, juce::jmax (velLo, velHi));
    s.octaveShift = juce::jlimit (-3, 3, octave); s.channel = juce::jlimit (0, 16, channel); s.ignoreWheel = ignoreWheel;
    syncRoutes();
    if (auto* song = currentSongMutable())
    {
        auto& b = song->baseRig.slots[(size_t) slot];
        b.keyLo = s.keyLo; b.keyHi = s.keyHi; b.velLo = s.velLo; b.velHi = s.velHi; b.octaveShift = s.octaveShift; b.channel = s.channel; b.ignoreWheel = s.ignoreWheel;
    }
    rigChanged.sendChangeMessage();
}
void SanctuaryProcessor::setSlotRack (int slot, const FxRackState& rack)
{
    rig.slots[(size_t) slot].rack = rack;
    for (auto& c : chains_[(size_t) slot]) if (c.live.load()) c.rack.applyStructuralChange (rack);
    if (auto* song = currentSongMutable()) song->baseRig.slots[(size_t) slot].rack = rack;
    rigChanged.sendChangeMessage();
}
void SanctuaryProcessor::setMasterRack (const FxRackState& rack)
{
    rig.masterRack = rack;
    masterRack_.applyStructuralChange (rack);
    if (auto* song = currentSongMutable()) song->baseRig.masterRack = rack;
    rigChanged.sendChangeMessage();
}

//==============================================================================
// Moments editing
//==============================================================================
void SanctuaryProcessor::addMoment (const juce::String& name)
{
    auto* s = currentSongMutable(); if (s == nullptr) return;
    Moment m; m.id = juce::Uuid().toString(); m.name = name; m.transitionBars = 1.0f;
    static const juce::uint32 palette[] = { 0xff3D8BFF, 0xff35D3F2, 0xff8E7CFF, 0xff4FE0C2, 0xffFFC24B, 0xff8A9BB8 };
    m.colour = palette[s->moments.size() % 6];
    s->moments.push_back (m);
    stageChanged.sendChangeMessage();
}
void SanctuaryProcessor::deleteMoment (int index)
{
    auto* s = currentSongMutable(); if (s == nullptr || s->moments.size() <= 1 || index < 0 || index >= (int) s->moments.size()) return;
    s->moments.erase (s->moments.begin() + index);
    if (momentIndex >= (int) s->moments.size()) momentIndex = (int) s->moments.size() - 1;
    stageChanged.sendChangeMessage();
}
void SanctuaryProcessor::renameMoment (int index, const juce::String& name)
{
    auto* s = currentSongMutable(); if (s == nullptr || index < 0 || index >= (int) s->moments.size()) return;
    s->moments[(size_t) index].name = name; stageChanged.sendChangeMessage();
}
void SanctuaryProcessor::setMomentTransition (int index, float bars)
{
    auto* s = currentSongMutable(); if (s == nullptr || index < 0 || index >= (int) s->moments.size()) return;
    s->moments[(size_t) index].transitionBars = juce::jlimit (0.0f, 8.0f, bars); stageChanged.sendChangeMessage();
}
void SanctuaryProcessor::moveMoment (int from, int to)
{
    auto* s = currentSongMutable(); if (s == nullptr) return;
    const int n = (int) s->moments.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    auto m = s->moments[(size_t) from];
    s->moments.erase (s->moments.begin() + from);
    s->moments.insert (s->moments.begin() + to, m);
    if (momentIndex == from) momentIndex = to;
    stageChanged.sendChangeMessage();
}

//==============================================================================
// Persistence
//==============================================================================
void SanctuaryProcessor::ensureFactoryContent()
{
    const auto marker = appDataDir().getChildFile ("factory-v1.txt");
    if (marker.existsAsFile()) return;
    factory::BundlePick pick (library.bundles());
    auto songList = factory::songs (pick);
    for (auto& s : songList) writeJsonFile (songFile (s.id), s.toVar());
    for (auto& st : factory::sets (songList)) writeJsonFile (setFile (st.id), st.toVar());
    marker.replaceWithText ("Factory content written " + juce::Time::getCurrentTime().toISO8601 (true));
}
void SanctuaryProcessor::reloadSongsFromDisk()
{
    songs.clear();
    for (auto& f : songsDir().findChildFiles (juce::File::findFiles, false, "*.song.json"))
    {
        auto s = Song::fromVar (readJsonFile (f));
        songs[s.id] = s;
    }
}
std::vector<Set> SanctuaryProcessor::setsOnDisk() const
{
    std::vector<Set> out;
    for (auto& f : setsDir().findChildFiles (juce::File::findFiles, false, "*.set.json")) out.push_back (Set::fromVar (readJsonFile (f)));
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}
void SanctuaryProcessor::newSongFromRig (const juce::String& title)
{
    Song s; s.title = title; s.baseRig = rig; s.songKey = soundIn_; s.playIn = playIn_; s.soundIn = soundIn_; s.tempo = bpm_.load();
    songs[s.id] = s; saveSong (s);
    set.songs.push_back ({ s.id, std::nullopt, std::nullopt });
    saveSet (set);
    gotoSong ((int) set.songs.size() - 1);
}
void SanctuaryProcessor::saveCurrentSong() { if (auto* s = currentSong()) saveSong (*s); }
void SanctuaryProcessor::saveSong (const Song& s) { songs[s.id] = s; writeJsonFile (songFile (s.id), s.toVar()); stageChanged.sendChangeMessage(); }
void SanctuaryProcessor::deleteSong (const juce::String& id)
{
    songFile (id).deleteFile(); songs.erase (id);
    set.songs.erase (std::remove_if (set.songs.begin(), set.songs.end(), [&] (auto& e) { return e.songId == id; }), set.songs.end());
    if (songIndex >= (int) set.songs.size()) songIndex = (int) set.songs.size() - 1;
    stageChanged.sendChangeMessage();
}
void SanctuaryProcessor::saveSet (const Set& s) { writeJsonFile (setFile (s.id), s.toVar()); if (s.id == set.id) set = s; stageChanged.sendChangeMessage(); }
void SanctuaryProcessor::deleteSet (const juce::String& id) { setFile (id).deleteFile(); stageChanged.sendChangeMessage(); }

bool SanctuaryProcessor::exportSetPack (const juce::File& zipFile, juce::String& report) const
{
    juce::ZipFile::Builder b;
    juce::StringArray required;
    std::vector<std::unique_ptr<juce::TemporaryFile>> temps;
    auto add = [&] (const juce::String& name, const juce::var& v)
    {
        auto tmp = std::make_unique<juce::TemporaryFile> (".json");
        tmp->getFile().replaceWithText (juce::JSON::toString (v));
        b.addFile (tmp->getFile(), 6, name);
        temps.push_back (std::move (tmp));
    };
    add ("set.json", set.toVar());
    for (auto& e : set.songs)
    {
        auto it = songs.find (e.songId); if (it == songs.end()) continue;
        add ("songs/" + e.songId + ".song.json", it->second.toVar());
        for (auto& sl : it->second.baseRig.slots) if (sl.hasSound()) required.addIfNotAlreadyThere (sl.bundleName);
    }
    for (auto& s : surfaces) add ("surfaces/" + juce::File::createLegalFileName (s.name) + ".surface.json", s.toVar());
    auto* m = new juce::DynamicObject();
    juce::Array<juce::var> req; for (auto& r : required) req.add (r);
    m->setProperty ("requiredBundles", req); m->setProperty ("exportedBy", "Sanctuary 1.0");
    add ("manifest.json", juce::var (m));
    juce::FileOutputStream os (zipFile);
    if (! os.openedOk()) { report = "Could not write " + zipFile.getFullPathName(); return false; }
    b.writeToStream (os, nullptr);
    report = "Exported " + juce::String ((int) set.songs.size()) + " songs. Bundles needed: " + required.joinIntoString (", ");
    return true;
}
bool SanctuaryProcessor::importSetPack (const juce::File& zipFile, juce::String& report)
{
    juce::ZipFile z (zipFile);
    if (z.getNumEntries() == 0) { report = "Not a set pack"; return false; }
    Set imported; juce::StringArray missing; int songCount = 0;
    for (int i = 0; i < z.getNumEntries(); ++i)
    {
        auto* e = z.getEntry (i);
        std::unique_ptr<juce::InputStream> in (z.createStreamForEntry (i));
        if (in == nullptr) continue;
        auto v = juce::JSON::parse (in->readEntireStreamAsString());
        if (e->filename == "set.json") imported = Set::fromVar (v);
        else if (e->filename.startsWith ("songs/"))
        {
            auto s = Song::fromVar (v);
            for (auto& sl : s.baseRig.slots)
                if (sl.hasSound())
                {
                    bool found = false;
                    for (auto& b : library.bundles()) if (b.name == sl.bundleName) { sl.bundleDir = b.dir.getFullPathName(); found = true; break; }
                    if (! found) { missing.addIfNotAlreadyThere (sl.bundleName); sl.bundleDir.clear(); sl.enabled = false; }
                }
            saveSong (s); ++songCount;
        }
        else if (e->filename.startsWith ("surfaces/")) { auto s = Surface::fromVar (v); bool dup = false; for (auto& x : surfaces) if (x.name == s.name) dup = true; if (! dup) surfaces.push_back (s); }
    }
    saveSurfaces();
    saveSet (imported);
    loadSet (imported);
    report = "Imported " + juce::String (songCount) + " songs.";
    if (! missing.isEmpty()) report += " Missing bundles (slots muted): " + missing.joinIntoString (", ");
    return true;
}

void SanctuaryProcessor::saveSurfaces()
{
    for (auto& f : surfacesDir().findChildFiles (juce::File::findFiles, false, "*.surface.json")) f.deleteFile();
    for (auto& s : surfaces) writeJsonFile (surfacesDir().getChildFile (juce::File::createLegalFileName (s.name) + ".surface.json"), s.toVar());
}
Surface& SanctuaryProcessor::currentSurface()
{
    if (surfaces.empty()) surfaces.push_back (Surface {});
    surfaceIndex = juce::jlimit (0, (int) surfaces.size() - 1, surfaceIndex);
    return surfaces[(size_t) surfaceIndex];
}
juce::String SanctuaryProcessor::bindingFor (const juce::String& target) const
{
    if (surfaces.empty()) return {};
    auto& s = surfaces[(size_t) juce::jlimit (0, (int) surfaces.size() - 1, surfaceIndex)];
    for (auto& b : s.bindings) if (b.target == target)
        for (auto& c : s.controls) if (c.id == b.controlId) return c.label.isNotEmpty() ? c.label : c.id;
    return {};
}
void SanctuaryProcessor::unbindTarget (const juce::String& target)
{
    auto& s = currentSurface();
    s.bindings.erase (std::remove_if (s.bindings.begin(), s.bindings.end(), [&] (auto& b) { return b.target == target; }), s.bindings.end());
    saveSurfaces(); refreshPadMask(); learnChanged.sendChangeMessage();
}

// ------------------------------------------------------------------ session (§9)
void SanctuaryProcessor::writeSession()
{
    if (rehearsal) return;
    auto* o = new juce::DynamicObject();
    o->setProperty ("setId", set.id); o->setProperty ("setName", set.name);
    o->setProperty ("songIndex", songIndex); o->setProperty ("momentIndex", momentIndex);
    o->setProperty ("playIn", playIn_); o->setProperty ("soundIn", soundIn_);
    o->setProperty ("droneOn", rig.drone.on); o->setProperty ("tempo", bpm_.load());
    o->setProperty ("time", juce::Time::getCurrentTime().toISO8601 (true));
    writeJsonFile (sessionFile(), juce::var (o));
}
void SanctuaryProcessor::resumeSession()
{
    if (! sessionVar_.isObject()) return;
    const auto setId = sessionVar_.getProperty ("setId", "").toString();
    for (auto& s : setsOnDisk()) if (s.id == setId) { loadSet (s); break; }
    const int si = (int) sessionVar_.getProperty ("songIndex", -1);
    if (si >= 0 && si < (int) set.songs.size()) gotoSong (si);
    const int mi = (int) sessionVar_.getProperty ("momentIndex", 0);
    if (mi > 0) gotoMoment (mi, false);
    setPlayIn ((int) sessionVar_.getProperty ("playIn", playIn_));
    setSoundIn ((int) sessionVar_.getProperty ("soundIn", soundIn_));
    if (! followHost_) bpm_.store ((double) sessionVar_.getProperty ("tempo", bpm_.load()));
    setPath ("drone.on", (bool) sessionVar_.getProperty ("droneOn", false) ? 1.0f : 0.0f, false);
    sessionAvailable_ = false;
    stageChanged.sendChangeMessage();
}
void SanctuaryProcessor::discardSession() { sessionAvailable_ = false; }

//==============================================================================
// Plugin state
//==============================================================================
void SanctuaryProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("version", 1);
    o->setProperty ("setId", set.id);
    o->setProperty ("set", set.toVar());
    o->setProperty ("songIndex", songIndex); o->setProperty ("momentIndex", momentIndex);
    o->setProperty ("playIn", playIn_); o->setProperty ("soundIn", soundIn_);
    o->setProperty ("tempo", bpm_.load());
    o->setProperty ("rig", rigToVar (rig));
    if (auto* s = currentSong()) o->setProperty ("song", s->toVar());
    o->setProperty ("surfaceIndex", surfaceIndex);
    o->setProperty ("followHost", followHost_); o->setProperty ("clickToMain", clickToMain_);
    auto* hp = new juce::DynamicObject();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) hp->setProperty (rp->paramID, rp->getValue());
    o->setProperty ("hostParams", juce::var (hp));
    const auto text = juce::JSON::toString (juce::var (o));
    dest.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
}
void SanctuaryProcessor::setStateInformation (const void* data, int size)
{
    auto v = juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, size));
    if (! v.isObject()) return;
    if (v.hasProperty ("song")) { auto s = Song::fromVar (v["song"]); songs[s.id] = s; }
    if (v.hasProperty ("set")) { set = Set::fromVar (v["set"]); }
    followHost_ = (bool) v.getProperty ("followHost", false); clickToMain_ = (bool) v.getProperty ("clickToMain", false);
    surfaceIndex = (int) v.getProperty ("surfaceIndex", 0);
    refreshPadMask();
    const int si = (int) v.getProperty ("songIndex", -1);
    if (si >= 0 && si < (int) set.songs.size()) { songIndex = si; momentIndex = juce::jlimit (0, 64, (int) v.getProperty ("momentIndex", 0)); }
    playIn_ = (int) v.getProperty ("playIn", 0); soundIn_ = (int) v.getProperty ("soundIn", 0); wheelShift_.store (wheelShift());
    if (! followHost_) bpm_.store ((double) v.getProperty ("tempo", 72.0));
    if (auto* s = currentSong()) { beatsPerBar_ = s->beatsPerBar(); eighthMeter_ = s->meter.endsWith ("/8"); }
    Rig r = rigFromVar (v.getProperty ("rig", juce::var()));
    applyRigToDsp (r, true);
    if (auto* hp = v.getProperty ("hostParams", juce::var()).getDynamicObject())
        for (auto& prop : hp->getProperties())
            if (auto* p = apvts.getParameter (prop.name.toString())) p->setValueNotifyingHost ((float) (double) prop.value);
    stateLoaded_ = true;
    stageChanged.sendChangeMessage(); rigChanged.sendChangeMessage();
}

//==============================================================================
int SanctuaryProcessor::activeVoicesInSlot (int slot) const
{
    int n = 0; for (auto& c : chains_[(size_t) slot]) n += c.eng.activeVoices(); return n;
}
int SanctuaryProcessor::runningChains (int slot) const
{
    int n = 0; for (auto& c : chains_[(size_t) slot]) if (c.running.load()) ++n; return n;
}

juce::AudioProcessorEditor* SanctuaryProcessor::createEditor() { return new SanctuaryEditor (*this); }

} // namespace sanctuary

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new sanctuary::SanctuaryProcessor(); }
