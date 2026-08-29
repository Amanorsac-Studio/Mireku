#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace jm {

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout JMSignatureProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout lay;
    auto db = juce::NormalisableRange<float> (-60.0f, 6.0f, 0.0f);
    db.setSkewForCentre (-9.0f);

    for (int i = 0; i < 4; ++i)
        lay.add (std::make_unique<P> (juce::ParameterID { "macro" + juce::String (i + 1), 1 },
                                      "Macro " + juce::String (i + 1), 0.0f, 1.0f, 0.0f));
    lay.add (std::make_unique<P> (juce::ParameterID { "masterVol", 1 }, "Master Volume", db, 0.0f));
    lay.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "transpose", 1 }, "Transpose", -12, 12, 0));
    lay.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "octave", 1 }, "Octave", -2, 2, 0));
    lay.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "solo", 1 }, "Solo Mode", false));
    // CLIENT REVIEW ROUND 3 (D): the header LIMITER plate bound to "limiter",
    // which no layout ever declared — the button was permanently disabled and
    // there was no DSP behind it. Both now exist (see engine/Limiter.h).
    lay.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "limiter", 1 }, "Master Limiter", true));
    // CLIENT REVIEW ROUND 4 (item 2): pitch bend is implemented now, and the
    // brief asked for a configurable range "if that's cheap". It is — one int
    // parameter, edited in Settings, persisted with everything else.
    lay.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "bendRange", 1 },
                                                        "Pitch Bend Range", 0, 12, 2));
    for (int i = 0; i < 4; ++i)
    {
        auto n = juce::String::charToString ((juce::juce_wchar) ('A' + i));
        lay.add (std::make_unique<P> (juce::ParameterID { "slotVol" + juce::String (i), 1 }, "Slot " + n + " Volume", db, 0.0f));
        lay.add (std::make_unique<P> (juce::ParameterID { "slotPan" + juce::String (i), 1 }, "Slot " + n + " Pan", -1.0f, 1.0f, 0.0f));
        lay.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "slotEnable" + juce::String (i), 1 },
                                                             "Slot " + n + " Enable", i == 0));
    }
    return lay;
}

JMSignatureProcessor::JMSignatureProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "JMSignature", createLayout())
{
    for (int i = 0; i < 4; ++i)
    {
        macroParams[(size_t) i] = apvts.getRawParameterValue ("macro" + juce::String (i + 1));
        slotVol[(size_t) i] = apvts.getRawParameterValue ("slotVol" + juce::String (i));
        slotPan[(size_t) i] = apvts.getRawParameterValue ("slotPan" + juce::String (i));
        slotEnable[(size_t) i] = apvts.getRawParameterValue ("slotEnable" + juce::String (i));
        engines[(size_t) i].setWorld (i);
    }
    // MODULATION PHASE 1: the continuous host parameters modulation may offset.
    // We keep the RangedAudioParameter (not the raw value) because an offset is
    // normalised and has to be converted through the parameter's own range.
    {
        const char* ids[hmCount] = { "masterVol",
                                     "slotVol0", "slotVol1", "slotVol2", "slotVol3",
                                     "slotPan0", "slotPan1", "slotPan2", "slotPan3",
                                     "macro1", "macro2", "macro3", "macro4" };
        for (int i = 0; i < hmCount; ++i)
            hostModParam[(size_t) i] = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (ids[i]));
    }
    masterVolParam = apvts.getRawParameterValue ("masterVol");
    limiterParam = apvts.getRawParameterValue ("limiter");
    transposeParam = apvts.getRawParameterValue ("transpose");
    octaveParam = apvts.getRawParameterValue ("octave");
    soloParam = apvts.getRawParameterValue ("solo");
    bendRangeParam = apvts.getRawParameterValue ("bendRange");

    // Round 4 (1): in standalone builds we own the MIDI inputs so each device
    // can drive a different slot. Plugin builds leave the hardware alone.
    if (MidiDeviceRouter::active().load())
        deviceRouter = std::make_unique<MidiDeviceRouter> (routedQueue);
    midiRouting.refreshDeviceList();

    state.library.rescan();
    ensureFactoryPerformancePresets();
    // Auto-load the first available bundle of each world into its default slot.
    for (int i = 0; i < 4; ++i)
    {
        auto list = state.library.bundlesForWorld (i);
        if (! list.empty())
            loadBundleIntoSlot (i, list.front());
    }
    publishModulation();
    stateChanged.addChangeListener (this);
    startTimerHz (30);
}

JMSignatureProcessor::~JMSignatureProcessor()
{
    stateChanged.removeChangeListener (this);
    stopTimer();
}

void JMSignatureProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    publishModulation();
}

//==============================================================================
void JMSignatureProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    loadMeasurer.reset (sampleRate, samplesPerBlock);
    slotBuf.setSize (2, samplesPerBlock);
    uiMidiBuf_.ensureSize (4096);          // never allocates on the audio thread
    expressionGain.reset (sampleRate, 0.05);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterVolParam->load(), -60.0f));
    masterLimiter.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (masterLimiter.latencySamples());
    mod.prepare (sampleRate);
    arp.prepare (sampleRate);       // PHASE 2: also clears held notes and the sounding set
    for (int i = 0; i < 4; ++i)
    {
        engines[(size_t) i].prepare (sampleRate, samplesPerBlock);
        slotRacks[(size_t) i].prepare (sampleRate, samplesPerBlock);
        // R3(D)'s 20 ms is the house number for a gain ramp; pan is smoothed as a
        // POSITION and the equal-power law recomputed from it per sample.
        slotVolSm[(size_t) i].reset (sampleRate, 0.02);
        slotPanSm[(size_t) i].reset (sampleRate, 0.02);
        slotVolSm[(size_t) i].setCurrentAndTargetValue (slotVol[(size_t) i]->load());
        slotPanSm[(size_t) i].setCurrentAndTargetValue (slotPan[(size_t) i]->load());
    }
    masterRack.prepare (sampleRate, samplesPerBlock);
    syncAllToDsp();
}

int JMSignatureProcessor::pitchBendRange() const
{
    return bendRangeParam != nullptr ? (int) bendRangeParam->load() : 2;
}

void JMSignatureProcessor::refreshMidiDevices()
{
    // Opening/closing MidiInputs and mutating the device table is message-thread
    // work; setStateInformation() can arrive on a host loader thread, so bounce.
    if (! juce::MessageManager::existsAndIsCurrentThread())
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<JMSignatureProcessor> (this)]
        {
            if (safe != nullptr) safe->refreshMidiDevices();
        });
        return;
    }
    midiRouting.refreshDeviceList();
    if (deviceRouter != nullptr) deviceRouter->sync (midiRouting);
}

// CLIENT REVIEW ROUND 4 (item 1) — every MIDI source funnels through here with
// a 4-bit slot mask, so per-slot filtering is applied to note-ons, note-offs,
// sustain, CCs and pitch bend by exactly the same rule. mask == 0 means "this
// message is routed to no slot"; only the panic messages ignore it.
void JMSignatureProcessor::handleRoutedMessage (const juce::MidiMessage& m, juce::uint8 mask, int offset)
{
    const int shift = (int) transposeParam->load() + 12 * (int) octaveParam->load();

    // ------------------------------------------------------ MODULATION PHASE 2
    // THE ROUTING FORK. It happens HERE, after the device/channel slot mask has
    // been resolved, which is what makes per-slot enable work the way the spec
    // describes: one held chord can be arpeggiated into slot C while slots A and
    // B receive it as played (research §3.8).
    //
    //   arpMask    = the slots this key reaches that have the arp ON  -> the arp
    //   directMask = the slots this key reaches that have the arp OFF -> the sampler
    //
    // Only NOTES and CC64 are forked. Pitch bend, mod wheel, expression and
    // aftertouch are forwarded to every routed slot exactly as before — the arp
    // consumes the pedal, nothing else (research §3.6 / §3.8).
    const auto& as = arp.activeSettings();
    const juce::uint8 arpMask = as.runsOnAnySlot()
                                    ? (juce::uint8) (mask & (juce::uint8) (as.slotMask & 0x0F)) : 0;
    const juce::uint8 directMask = (juce::uint8) (mask & ~arpMask);
    auto routed = [mask] (int slot) { return (mask & (1u << slot)) != 0; };
    auto direct = [directMask] (int slot) { return (directMask & (1u << slot)) != 0; };
    auto arpSink = [this] (const ArpEvent& e) { applyArpEvent (e); };

    {
        if (m.isNoteOn())
        {
            const int note = juce::jlimit (0, 127, m.getNoteNumber() + shift);
            for (int i = 0; i < 4; ++i)
                if (direct (i) && slotEnable[(size_t) i]->load() >= 0.5f)
                    engines[(size_t) i].noteOn (note, m.getVelocity());
            if (arpMask != 0) arp.noteOn (note, m.getVelocity(), arpMask, offset, arpSink);
            // MODULATION PHASE 1: velocity / key track / random are per-note
            // sources, and the 0-held-notes -> 1 edge is what drives First-note
            // retrigger and the LFO fade-in for these GLOBAL LFOs. The PLAYED
            // note drives them, arpeggiated or not.
            if (mask != 0) mod.noteOn (note, m.getVelocity());
        }
        else if (m.isNoteOff())
        {
            const int note = juce::jlimit (0, 127, m.getNoteNumber() + shift);
            for (int i = 0; i < 4; ++i)
                if (direct (i)) engines[(size_t) i].noteOff (note);
            if (arpMask != 0) arp.noteOff (note, arpSink);
            if (mask != 0) mod.noteOff();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            // panic is never filtered — a stuck note must always be clearable
            arp.panic (arpSink);
            for (auto& e : engines) e.allNotesOff (m.isAllNotesOff());
            mod.allNotesOff();
        }
        else if (m.isChannelPressure())
        {
            if (mask != 0) mod.setAftertouch ((float) m.getChannelPressureValue() / 127.0f);
        }
        else if (m.isAftertouch())
        {
            if (mask != 0) mod.setAftertouch ((float) m.getAfterTouchValue() / 127.0f);
        }
        else if (m.isPitchWheel())
        {
            // Round 4 (item 2): pitch bend IS implemented now — ±bendRange
            // semitones as a smoothed playback-rate multiplier (Sampler.h).
            const float semis = ((float) m.getPitchWheelValue() - 8192.0f) / 8192.0f
                                * (float) pitchBendRange();
            for (int i = 0; i < 4; ++i)
                if (routed (i)) engines[(size_t) i].setPitchBend (semis);
        }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber(), val = m.getControllerValue();
            if (cc == 64)                                     // §5.4 sustain
            {
                // PHASE 2 rule 1 (research §3.6, Yamaha: "the sustain pedal does
                // not sustain the sound, it holds the keys for you"). The arp
                // CONSUMES CC64 for the slots it drives — forwarding it there
                // would sustain every step's release tail into mush. Slots with
                // the arp off keep the §5.4 behaviour byte for byte.
                for (int i = 0; i < 4; ++i)
                    if (direct (i)) engines[(size_t) i].sustainPedal (val >= 64);
                if (arpMask != 0) arp.setPedal (val >= 64, arpSink);
            }
            else if (cc == 11)                                // expression → volume
            {
                // expression rides the MASTER bus, so it is applied whenever the
                // message reaches any slot at all (there is one master gain).
                if (mask != 0)
                {
                    const float n = (float) val / 127.0f;
                    expressionGain.setTargetValue (n * n);
                    mod.setExpression (n);            // also a matrix source now
                }
            }
            else if (cc == 1)                                 // mod wheel per world (§5.3)
            {
                const float n = (float) val / 127.0f;
                if (mask != 0) mod.setModWheel (n);           // also a matrix source now
                for (int i = 0; i < 4; ++i)
                {
                    if (! routed (i)) continue;
                    if (slotEnable[(size_t) i]->load() < 0.5f) continue;
                    switch (engines[(size_t) i].world())
                    {
                        case 0:                                // Rhodes → tremolo depth
                            for (size_t u = 0; u < state.slots[(size_t) i].rack.size(); ++u)
                                if (state.slots[(size_t) i].rack[u].type == FxType::tremoloAutoPan)
                                    slotRacks[(size_t) i].setParam ((int) u, "depth", n * 100.0f);
                            break;
                        case 1:                                // MKS → chorus depth
                            for (size_t u = 0; u < state.slots[(size_t) i].rack.size(); ++u)
                                if (state.slots[(size_t) i].rack[u].type == FxType::jmChorus)
                                    slotRacks[(size_t) i].setParam ((int) u, "depth", n * 100.0f);
                            break;
                        case 2: engines[(size_t) i].setEngineParam ("brightness", n * 100.0f); break;
                        case 3: engines[(size_t) i].setEngineParam ("swell", n * 100.0f); break;
                    }
                }
                state.learn.lastIncomingCc.store (cc);
            }
            else if (mask != 0)                               // learned CCs → message thread
            {
                state.learn.lastIncomingCc.store (cc);
                int s1, n1, s2, n2;
                ccFifo.prepareToWrite (1, s1, n1, s2, n2);
                if (n1 > 0) ccEvents[(size_t) s1] = { cc, val };
                ccFifo.finishedWrite (n1);
            }
        }
    }
}

void JMSignatureProcessor::drainRoutedQueueAtBlockStart()
{
    // Devices we opened ourselves (standalone) and the on-screen keyboard /
    // pitch-bend / mod wheels: already carrying their resolved slot mask. These
    // cross from device / message threads through a lock-free FIFO of 5-byte
    // PODs and carry NO timestamp, so offset 0 is the honest answer for them
    // (research §4.2). The host buffer is the half that gained real timestamps.
    routedQueue.drain ([this] (const RoutedMidiEvent& e)
    {
        const auto m = e.toMessage();
        const int ch = m.getChannel();
        const juce::uint8 chMask = (e.resolved != 0 || ch <= 0) ? kSlotMaskAll
                                                                : midiRouting.channelMask (ch);
        handleRoutedMessage (m, (juce::uint8) (e.mask & chMask), 0);
    });
}

// MODULATION PHASE 2 — one arp-generated note into the slots its source key was
// routed to. Audio thread; `e.mask` is already the intersection of that key's
// routing with the arp's per-slot enable.
void JMSignatureProcessor::applyArpEvent (const ArpEvent& e)
{
    for (int i = 0; i < 4; ++i)
    {
        if ((e.mask & (1u << i)) == 0) continue;
        if (e.noteOn)
        {
            if (slotEnable[(size_t) i]->load() >= 0.5f)
                engines[(size_t) i].noteOn (e.note, e.velocity);
        }
        else
            engines[(size_t) i].noteOff (e.note);
    }
}

void JMSignatureProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());
    buffer.clear();
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if (slotBuf.getNumSamples() < n) slotBuf.setSize (2, n, false, false, true);

    // MODULATION PHASE 1 — the transport clock. getPosition() may ONLY be called
    // from processBlock, so this is its one call site. The clock latches the
    // host's ppq while playing and free-runs otherwise, which is what keeps a
    // synced LFO moving when the player stops the transport (and is the whole of
    // the standalone code path).
    transport.update (getPlayHead(), n, getSampleRate());
    bpm_ = transport.bpm;

    // The on-screen keyboard is part of the instrument, not an external source:
    // its notes go into the routed queue pre-resolved (omni) rather than into
    // the host buffer, so a "channel 1 -> slot A" routing cannot silently mute
    // the UI keyboard (round 4, item 1).
    uiMidiBuf_.clear();
    keyboardState.processNextMidiBuffer (uiMidiBuf_, 0, n, true);
    for (const auto meta : uiMidiBuf_)
        routedQueue.push (meta.getMessage(), kSlotMaskAll, true);

    for (int i = 0; i < 4; ++i) mod.setMacroBase (i, macroParams[(size_t) i]->load());

    // MODULATION PHASE 2 — the arp latches its settings and computes every step
    // onset in this block BEFORE the render loop, from ppq (research §3.7), and
    // handles the transport edges (stop / locate / loop wrap) here so the flush
    // happens in the same block the edge is detected.
    auto arpSink = [this] (const ArpEvent& e) { applyArpEvent (e); };
    arp.beginBlock (transport, n, mod.arpOffset (amRate), mod.arpOffset (amGate), arpSink);

    // The untimestamped sources are delivered AFTER beginBlock so that a key
    // played on a MIDI device or the on-screen keyboard starts its phrase inside
    // THIS block's schedule rather than the previous one's.
    drainRoutedQueueAtBlockStart();

    // ------------------------------------------------------------------------
    // MODULATION PHASE 1 + 2 — the render loop, now SUB-BLOCK SPLIT.
    //
    // Phase 1 split the block into 64-sample control chunks for the matrix.
    // Phase 2 additionally splits at every host MIDI timestamp and every arp
    // event, with JUCE's own 32-sample minimum subdivision (research §3.7:
    // juce::Synthesiser's setMinimumRenderingSubdivisionSize default) so a burst
    // of near-simultaneous events does not shred the block into 1-sample
    // fragments. Events landing inside the first 32 samples of a sub-block are
    // applied at its start, which is exactly JUCE's non-strict batching.
    //
    // NOTE THIS ALSO FIXES ORDINARY PLAY: handleMidiAtBlockStart() used to throw
    // every timestamp away and apply the whole buffer at sample 0 (research trap
    // 13). Notes now start where the host said they do.
    constexpr int kMinSubBlock = 32;
    const int chunk = mod.active() ? juce::jmin (n, kModControlBlock) : n;
    auto mIt = midi.begin();
    const auto mEnd = midi.end();
    int pos = 0;
    while (pos < n)
    {
        const int cut = juce::jmin (n, pos + kMinSubBlock);

        // 1. host MIDI due at or within the minimum subdivision of this point
        while (mIt != mEnd && (*mIt).samplePosition < cut)
        {
            const auto meta = *mIt;
            const auto m = meta.getMessage();
            const int ch = m.getChannel();
            handleRoutedMessage (m, ch > 0 ? midiRouting.channelMask (ch) : kSlotMaskAll, pos);
            ++mIt;
        }
        // 2. arp note-offs then note-ons due here (off strictly before on at a
        //    100% gate — research §3.5)
        arp.emitDue (cut, arpSink);

        // 3. how far can we render before the next event or control boundary?
        int end = juce::jmin (n, pos + chunk);
        if (mIt != mEnd) end = juce::jmin (end, juce::jmax (cut, (*mIt).samplePosition));
        const int arpNext = arp.nextEventOffset();
        if (arpNext > pos) end = juce::jmin (end, juce::jmax (cut, arpNext));
        end = juce::jlimit (juce::jmin (cut, n), n, end);

        mod.process (transport, end - pos, pos, *this);
        renderChunk (buffer, pos, end - pos);
        pos = end;
    }
    arp.endBlock();

    // master volume — smoothed (R3/D: it was a per-block constant, so every
    // knob move stepped the gain at the block boundary and zipped). Modulation
    // reaches it as a NORMALISED OFFSET on top of the automated base, so the
    // host's automation lane is never written (research trap 1).
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (modulatedHostValue (hmMasterVol), -60.0f));
    for (int i = 0; i < n; ++i)
    {
        const float g = masterGain.getNextValue() * expressionGain.getNextValue();
        buffer.getWritePointer (0)[i] *= g;
        buffer.getWritePointer (1)[i] *= g;
    }

    // master-bus safety limiter, post master volume (R3/D)
    masterLimiter.setEnabled (limiterParam != nullptr && limiterParam->load() >= 0.5f);
    masterLimiter.process (buffer);

    masterPeakL.store (buffer.getMagnitude (0, 0, n));
    masterPeakR.store (buffer.getMagnitude (1, 0, n));
}

//==============================================================================
// MODULATION PHASE 1 — one control block of audio.
void JMSignatureProcessor::renderChunk (juce::AudioBuffer<float>& buffer, int start, int len)
{
    // allocation-free views onto the destination block
    float* outPtr[2] = { buffer.getWritePointer (0) + start, buffer.getWritePointer (1) + start };
    juce::AudioBuffer<float> outView (outPtr, 2, len);
    float* slotPtr[2] = { slotBuf.getWritePointer (0), slotBuf.getWritePointer (1) };
    juce::AudioBuffer<float> slotView (slotPtr, 2, len);

    int voices = 0;
    for (int i = 0; i < 4; ++i)
    {
        auto& eng = engines[(size_t) i];
        voices += eng.activeVoices();
        const bool enabled = slotEnable[(size_t) i]->load() >= 0.5f;
        if (! enabled && eng.activeVoices() == 0)
        {
            if (start == 0) slotPeak[(size_t) i].store (0);
            continue;
        }

        slotView.clear();
        eng.render (slotView, len);
        slotRacks[(size_t) i].process (slotView, bpm_);

        // Slot volume and pan are modulation destinations. Research section 1.5: smooth
        // the PAN POSITION and recompute the equal-power law from it, never the
        // two channel gains independently (that breaks the power constraint).
        auto& vs = slotVolSm[(size_t) i];
        auto& ps = slotPanSm[(size_t) i];
        vs.setTargetValue (modulatedHostValue (hmSlotVol0 + i));
        ps.setTargetValue (modulatedHostValue (hmSlotPan0 + i));
        constexpr float kPanK = 1.414f * 0.7071f;
        if (vs.isSmoothing() || ps.isSmoothing())
        {
            auto* l = outView.getWritePointer (0);
            auto* r = outView.getWritePointer (1);
            const auto* sl = slotView.getReadPointer (0);
            const auto* sr = slotView.getReadPointer (1);
            for (int s = 0; s < len; ++s)
            {
                const float vol = juce::Decibels::decibelsToGain (vs.getNextValue(), -60.0f);
                const float pan = ps.getNextValue();
                const float a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
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

    masterRack.process (outView, bpm_);
}

/// Base (automated / stored) value of a continuous host parameter with the
/// matrix's normalised offset added on top. The parameter itself is NEVER
/// written — this is the whole point of the base+offset model.
float JMSignatureProcessor::modulatedHostValue (int target) const
{
    auto* p = hostModParam[(size_t) target];
    if (p == nullptr) return 0.0f;
    const float off = mod.hostOffset (target);
    const float base = p->getValue();                        // normalised
    return p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, base + off));
}

//==============================================================================
void JMSignatureProcessor::timerCallback()
{
    cpuLoad.store ((float) loadMeasurer.getLoadAsProportion());

    // learned CC events
    int s1, n1, s2, n2;
    ccFifo.prepareToRead (ccFifo.getNumReady(), s1, n1, s2, n2);
    auto handle = [this] (const CcEvent& e)
    {
        if (state.learn.midiLearnArmed.load() && state.learn.pendingMidiPath.isNotEmpty())
        {
            state.midiMap.set (e.cc, state.learn.pendingMidiPath);
            state.learn.pendingMidiPath.clear();
            state.learn.midiLearnArmed.store (false);
            stateChanged.sendChangeMessage();
            return;
        }
        auto path = state.midiMap.pathForCc (e.cc);
        if (path.isNotEmpty())
            setParamNormalized (path, (float) e.value / 127.0f);
    };
    for (int i = 0; i < n1; ++i) handle (ccEvents[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) handle (ccEvents[(size_t) (s2 + i)]);
    ccFifo.finishedRead (n1 + n2);

    // MODULATION PHASE 1: macro values are read by the AUDIO thread every control
    // block now, so this timer no longer applies them — the 30 Hz stepping that
    // used to be audible on a macro sweep is gone. What is left here is base
    // tracking: re-read every modulated destination's base value so that turning
    // a knob (or a host automating one) moves the whole modulated range.
    refreshModulationBases();

    // SOLO mode: newly-enabled slot switches the others off (radio, spec §5.3)
    for (int i = 0; i < 4; ++i)
    {
        const float e = slotEnable[(size_t) i]->load();
        if (e != lastEnable[(size_t) i])
        {
            const bool turnedOn = e >= 0.5f && lastEnable[(size_t) i] >= 0.0f && lastEnable[(size_t) i] < 0.5f;
            lastEnable[(size_t) i] = e;
            if (turnedOn && soloParam->load() >= 0.5f)
                enforceSolo (i);
        }
    }
}

void JMSignatureProcessor::enforceSolo (int enabledSlot)
{
    for (int i = 0; i < 4; ++i)
        if (i != enabledSlot)
            if (auto* p = apvts.getParameter ("slotEnable" + juce::String (i)))
                if (p->getValue() >= 0.5f)
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (0.0f);
                    p->endChangeGesture();
                }
}

// MODULATION PHASE 1 — applyMacro() is no longer a parameter writer.
//
// It used to run on the 30 Hz message-thread timer and call setParamNormalized(),
// which OVERWRITES the stored value (engineParams[id] / unit.params[id]) and, on
// a host.* path, called setValueNotifyingHost(). docs/MODULATION-RESEARCH.md section 0
// shows why that cannot carry an LFO: it is below the Nyquist rate of a 40 Hz
// LFO, it destroys the player's knob position, and it would write modulation
// into host automation (the known Bitwig VST3 feedback-loop shape).
//
// Macros are now source #1 of the ONE matrix. Every macro link is republished
// into the resolved routing table and evaluated on the audio thread every 64
// samples as an OFFSET on top of the knob's base value. Callers (the macro
// editor's live() and the assignment routes) are unchanged — this call still
// means "the macro's links changed, make it so".
void JMSignatureProcessor::applyMacro (int)
{
    publishModulation();
}

//==============================================================================
// MODULATION PHASE 1 — path resolution (MESSAGE THREAD ONLY).
// Every juce::String comparison in the whole modulation system happens here.
bool JMSignatureProcessor::resolveModDest (const juce::String& path, ModDest& out) const
{
    out = ModDest {};
    if (path.isEmpty()) return false;

    // ---- cross-modulation destinations (spec §D: "LFO rate and depth are
    //      themselves valid destinations, so a slow LFO can sweep a fast one")
    if (path.startsWith ("mod.lfo"))
    {
        const int i = path.substring (7, 8).getIntValue() - 1;
        if (i < 0 || i >= kNumLfos) return false;
        if (! path.endsWith (".rate")) return false;
        out.kind = (int) ModDestKind::lfoRate;
        out.unit = i;
        return true;
    }
    // ---- PHASE 2: the two arp parameters that are musically worth sweeping
    //      (research/task: rate and gate). Mode, octaves and the per-slot enables
    //      are structural and are deliberately not offered.
    if (path.startsWith ("arp."))
    {
        const int t = path == "arp.rate" ? amRate : path == "arp.gate" ? amGate : -1;
        if (t < 0) return false;
        out.kind = (int) ModDestKind::arpParam;
        out.unit = t;
        return true;
    }
    if (path.startsWith ("mod.row"))
    {
        const int i = path.fromFirstOccurrenceOf ("mod.row", false, false)
                          .upToFirstOccurrenceOf (".", false, false).getIntValue() - 1;
        if (i < 0 || i >= kMaxMatrixRows) return false;
        if (! path.endsWith (".depth")) return false;
        out.kind = (int) ModDestKind::rowDepth;
        out.unit = i;
        return true;
    }

    // ---- continuous host parameters (applied as an offset at point of use)
    if (path.startsWith ("host."))
    {
        const auto id = path.fromFirstOccurrenceOf ("host.", false, false);
        static const char* ids[hmCount] = { "masterVol",
                                            "slotVol0", "slotVol1", "slotVol2", "slotVol3",
                                            "slotPan0", "slotPan1", "slotPan2", "slotPan3",
                                            "macro1", "macro2", "macro3", "macro4" };
        for (int i = 0; i < hmCount; ++i)
            if (id == ids[i])
            {
                out.kind = (int) ModDestKind::hostParam;
                out.unit = i;
                return true;
            }
        return false;             // bool/int host params are not modulatable
    }

    int slot = -1;
    juce::String rest;
    const FxRackState* rack = nullptr;
    if (path.startsWith ("slot"))
    {
        slot = path.substring (4, 5).getIntValue();
        if (slot < 0 || slot > 3) return false;
        rest = path.fromFirstOccurrenceOf (".", false, false);
        rack = &state.slots[(size_t) slot].rack;
    }
    else if (path.startsWith ("master."))
    {
        rest = path.fromFirstOccurrenceOf (".", false, false);
        rack = &state.masterRack;
    }
    else return false;

    if (rest.startsWith ("engine.") && slot >= 0)
    {
        const auto id = rest.fromFirstOccurrenceOf ("engine.", false, false).toStdString();
        auto& ctrls = worldEngineControls (state.slots[(size_t) slot].world);
        for (size_t i = 0; i < ctrls.size(); ++i)
            if (id == ctrls[i].id)
            {
                out.kind = (int) ModDestKind::engine;
                out.slot = slot;
                out.param = (int) i;
                out.lo = ctrls[i].min;
                out.hi = ctrls[i].max;
                out.quantise = ctrls[i].kind == ParamKind::Float ? 0 : 1;
                return true;
            }
        return false;
    }
    if (rest.startsWith ("ampenv.") && slot >= 0)
    {
        const auto id = rest.fromFirstOccurrenceOf ("ampenv.", false, false);
        const int f = id == "attackMs" ? 0 : id == "decayMs" ? 1
                    : id == "sustain"  ? 2 : id == "releaseMs" ? 3 : -1;
        if (f < 0) return false;
        out.kind = (int) ModDestKind::ampEnv;
        out.slot = slot;
        out.param = f;
        return true;                        // conversion happens in the applier
    }
    if (rest.startsWith ("fx") && rack != nullptr)
    {
        const int u = rest.substring (2).getIntValue();
        if (u < 0 || u >= (int) rack->size()) return false;
        const auto id = rest.fromFirstOccurrenceOf (".", false, false).toStdString();
        auto& spec = fxSpec ((*rack)[(size_t) u].type);
        if (id == "bypass")
        {
            out.kind = (int) ModDestKind::fxBypass;
            out.slot = slot;                 // -1 == master rack
            out.unit = u;
            return true;
        }
        for (size_t i = 0; i < spec.params.size(); ++i)
            if (id == spec.params[i].id)
            {
                out.kind = (int) ModDestKind::fxParam;
                out.slot = slot;
                out.unit = u;
                out.param = (int) i;
                out.lo = spec.params[i].min;
                out.hi = spec.params[i].max;
                out.logTaper = spec.params[i].logTaper ? 1 : 0;
                out.quantise = spec.params[i].kind == ParamKind::Float ? 0 : 1;
                return true;
            }
    }
    return false;
}

bool JMSignatureProcessor::isModulatableDest (const juce::String& path) const
{
    ModDest d;
    return resolveModDest (path, d);
}

//==============================================================================
// AUDIO THREAD. One modulated normalised value -> the DSP. Nothing here writes
// `state`, APVTS or host automation.
void JMSignatureProcessor::applyModulatedValue (const ModDest& dest, float nv)
{
    switch ((ModDestKind) dest.kind)
    {
        case ModDestKind::engine:
            engines[(size_t) dest.slot].setEngineParam (dest.param, dest.denorm (nv));
            break;

        case ModDestKind::ampEnv:
        {
            auto& env = engines[(size_t) dest.slot].ampEnv();     // plain atomics
            switch (dest.param)
            {
                case 0: env.attackMs  = 1.0f + nv * nv * 1999.0f; break;
                case 1: env.decayMs   = nv * nv * 4000.0f;        break;
                case 2: env.sustain   = nv * 100.0f;              break;
                default: env.releaseMs = 5.0f + nv * nv * 3995.0f; break;
            }
            break;
        }

        case ModDestKind::fxParam:
            (dest.slot < 0 ? masterRack : slotRacks[(size_t) dest.slot])
                .setParamRt (dest.unit, dest.param, dest.denorm (nv));
            break;

        case ModDestKind::fxBypass:
            (dest.slot < 0 ? masterRack : slotRacks[(size_t) dest.slot])
                .setBypassedRt (dest.unit, nv >= 0.5f);
            break;

        default: break;      // host / lfoRate / rowDepth are consumed in ModEngine
    }
}

//==============================================================================
// MESSAGE THREAD — build the resolved routing table and publish it.
//
// Macro links and matrix rows are published TOGETHER into one table, which is
// what makes "one modulation system, not two" true rather than rhetorical: a
// macro link is simply a row whose source is macro N.
void JMSignatureProcessor::publishModulation()
{
    auto& t = mod.editBuffer();
    t = ModTable {};
    for (int i = 0; i < kNumLfos; ++i) t.lfo[(size_t) i] = state.mod.lfos[(size_t) i];

    std::vector<juce::String> paths, groupPaths;
    std::vector<int> sources, groups;
    std::vector<ModDest> groupDests;

    auto addRow = [&] (ModSource src, const juce::String& path,
                       float depth, float lo, float hi, float curve, bool invert, int matrixIndex)
    {
        if (t.rowCount >= kMaxModLinks) return;
        if (src == ModSource::none || path.isEmpty()) return;
        ModDest d;
        if (! resolveModDest (path, d)) return;
        // Vital's self-reference block: a row may not drive its own source's rate
        // or its own depth (spec §D "guard against a row modulating its own source").
        if ((ModDestKind) d.kind == ModDestKind::lfoRate
            && (int) src == (int) ModSource::lfo1 + d.unit) return;
        if ((ModDestKind) d.kind == ModDestKind::rowDepth && d.unit == matrixIndex) return;
        if ((ModDestKind) d.kind == ModDestKind::hostParam
            && d.unit >= hmMacro1 && (int) src == (int) ModSource::macro1 + (d.unit - hmMacro1)) return;

        int g = -1;
        for (int i = 0; i < t.groupCount; ++i) if (t.groups[(size_t) i] == d) { g = i; break; }
        if (g < 0)
        {
            if (t.groupCount >= kMaxModLinks) return;
            g = t.groupCount++;
            t.groups[(size_t) g] = d;
            groupDests.push_back (d);
            groupPaths.push_back (path);
            mod.setGroupBase (g, getParamNormalized (path));
        }

        auto& r = t.rows[(size_t) t.rowCount++];
        r.dest = d;
        r.source = (int) src;
        r.group = g;
        r.matrixIndex = matrixIndex;
        r.depth = juce::jlimit (-1.0f, 1.0f, depth);
        r.lo = juce::jmin (lo, hi);
        r.hi = juce::jmax (lo, hi);
        r.curve = juce::jmax (0.01f, curve);
        r.invert = invert ? 1 : 0;
        paths.push_back (path);
        sources.push_back ((int) src);
        groups.push_back (g);
        if (src >= ModSource::lfo1 && src <= ModSource::lfo3)
            t.lfoUsed |= 1 << ((int) src - (int) ModSource::lfo1);
    };

    // macros first — the fixed source evaluation order the research prescribes
    for (int m = 0; m < 4; ++m)
        for (auto& lnk : state.macros[(size_t) m].targets)
            addRow ((ModSource) ((int) ModSource::macro1 + m), lnk.path,
                    lnk.depth, lnk.min, lnk.max, lnk.curve, lnk.invert, -1);

    for (int i = 0; i < juce::jlimit (0, kMaxMatrixRows, state.mod.rowCount); ++i)
    {
        auto& r = state.mod.rows[(size_t) i];
        if (! r.active()) continue;
        addRow (r.source, r.destPath, r.depth, r.min, r.max, r.curve, r.invert, i);
    }

    mod.publish();

    // Destinations that dropped out of the table (a row was disabled, deleted or
    // re-pointed) keep whatever modulated value they last received, so push
    // their base back through the normal router once.
    for (size_t i = 0; i < pubGroupDest_.size(); ++i)
    {
        bool stillThere = false;
        for (auto& d : groupDests) if (d == pubGroupDest_[i]) { stillThere = true; break; }
        if (! stillThere && (ModDestKind) pubGroupDest_[i].kind != ModDestKind::hostParam)
            setParamNormalized (pubGroupPath_[i], getParamNormalized (pubGroupPath_[i]));
    }

    pubPath_ = std::move (paths);
    pubSource_ = std::move (sources);
    pubGroup_ = std::move (groups);
    pubGroupDest_ = std::move (groupDests);
    pubGroupPath_ = std::move (groupPaths);

    publishArp();     // one call site republishes the whole modulation system
}

// MODULATION PHASE 2 — the arp settings cross to the audio thread through the
// same double-buffered POD + atomic index idiom the routing table uses
// (research §4.1): the message thread fills the inactive buffer and publishes it
// with one release store, the audio thread does one acquire load per block. The
// audio thread's own state (held notes, latch, sounding set) is NEVER touched
// from here — a settings change that invalidates the schedule is resolved on the
// audio thread, in ArpEngine::beginBlock, where the note-off flush can happen.
void JMSignatureProcessor::publishArp()
{
    arp.editBuffer() = state.mod.arp;
    arp.publish();
}

void JMSignatureProcessor::refreshModulationBases()
{
    for (size_t g = 0; g < pubGroupPath_.size(); ++g)
        mod.setGroupBase ((int) g, getParamNormalized (pubGroupPath_[g]));
}

//==============================================================================
// UI readback for the modulated-knob display (research section 1.3): the base value is
// the pointer, the modulation SPAN is a second arc, and a live marker shows the
// value being swept right now.
JMSignatureProcessor::ModSpan JMSignatureProcessor::modSpanForPath (const juce::String& path) const
{
    ModSpan out;
    float dominant = 0.0f;
    for (size_t i = 0; i < pubPath_.size(); ++i)
    {
        if (pubPath_[i] != path) continue;
        const auto src = (ModSource) pubSource_[i];
        const auto& row = mod.liveTable().rows[i];
        // extremes of the shaped source: unipolar spans [0,1], bipolar [-0.5,+0.5]
        const float span = row.depth * (row.hi - row.lo);
        const float a = modSourceIsBipolar (src) ? -0.5f * span : 0.0f;
        const float b = modSourceIsBipolar (src) ?  0.5f * span : span;
        out.lo += juce::jmin (a, b);
        out.hi += juce::jmax (a, b);
        out.current += mod.rowOffset ((int) i);
        out.active = true;
        if (std::abs (span) >= dominant)
        {
            dominant = std::abs (span);
            out.colour = juce::Colour (modSourceColourArgb (src));
        }
    }
    return out;
}

juce::StringArray JMSignatureProcessor::modDestinationPaths() const
{
    juce::StringArray out;
    for (int s = 0; s < 4; ++s)
    {
        for (auto& c : worldEngineControls (state.slots[(size_t) s].world))
            out.add ("slot" + juce::String (s) + ".engine." + juce::String (c.id));
        for (auto* f : { "attackMs", "decayMs", "sustain", "releaseMs" })
            out.add ("slot" + juce::String (s) + ".ampenv." + juce::String (f));
        auto& rack = state.slots[(size_t) s].rack;
        for (size_t u = 0; u < rack.size(); ++u)
            for (auto& p : fxSpec (rack[u].type).params)
                out.add ("slot" + juce::String (s) + ".fx" + juce::String ((int) u) + "." + juce::String (p.id));
        out.add ("host.slotVol" + juce::String (s));
        out.add ("host.slotPan" + juce::String (s));
    }
    for (size_t u = 0; u < state.masterRack.size(); ++u)
        for (auto& p : fxSpec (state.masterRack[u].type).params)
            out.add ("master.fx" + juce::String ((int) u) + "." + juce::String (p.id));
    out.add ("host.masterVol");
    for (int m = 0; m < 4; ++m) out.add ("host.macro" + juce::String (m + 1));
    for (int i = 0; i < kNumLfos; ++i) out.add ("mod.lfo" + juce::String (i + 1) + ".rate");
    out.add ("arp.rate");
    out.add ("arp.gate");
    for (int i = 0; i < juce::jlimit (0, kMaxMatrixRows, state.mod.rowCount); ++i)
        out.add ("mod.row" + juce::String (i + 1) + ".depth");
    return out;
}

bool JMSignatureProcessor::modRowWouldCycle (int rowIndex, ModSource source,
                                             const juce::String& destPath) const
{
    ModDest d;
    if (! resolveModDest (destPath, d)) return false;
    if ((ModDestKind) d.kind == ModDestKind::lfoRate
        && (int) source == (int) ModSource::lfo1 + d.unit) return true;
    if ((ModDestKind) d.kind == ModDestKind::rowDepth && d.unit == rowIndex) return true;
    if ((ModDestKind) d.kind == ModDestKind::hostParam && d.unit >= hmMacro1
        && (int) source == (int) ModSource::macro1 + (d.unit - hmMacro1)) return true;
    return false;
}

//==============================================================================
// parameter router
static float denorm (const ParamSpec& s, float norm)
{
    norm = juce::jlimit (0.0f, 1.0f, norm);
    if (s.logTaper && s.min > 0.0f)
        return s.min * std::pow (s.max / s.min, norm);
    return s.min + (s.max - s.min) * norm;
}
static float norm (const ParamSpec& s, float value)
{
    if (s.logTaper && s.min > 0.0f && value > 0.0f)
        return juce::jlimit (0.0f, 1.0f, std::log (value / s.min) / std::log (s.max / s.min));
    return s.max > s.min ? juce::jlimit (0.0f, 1.0f, (value - s.min) / (s.max - s.min)) : 0.0f;
}

bool JMSignatureProcessor::setParamNormalized (const juce::String& path, float nv, bool)
{
    nv = juce::jlimit (0.0f, 1.0f, nv);
    // MODULATION PHASE 1: this IS the base value. Publishing it immediately is
    // what makes "turn the knob while an LFO runs and the whole modulated range
    // moves" instant rather than waiting for the 30 Hz base refresh.
    for (size_t g = 0; g < pubGroupPath_.size(); ++g)
        if (pubGroupPath_[g] == path) { mod.setGroupBase ((int) g, nv); break; }
    if (path.startsWith ("host."))
    {
        if (auto* p = apvts.getParameter (path.fromFirstOccurrenceOf ("host.", false, false)))
        {
            p->setValueNotifyingHost (nv);
            return true;
        }
        return false;
    }

    FxRackState* rack = nullptr;
    FxRackDsp* rackDsp = nullptr;
    juce::String rest;
    int slot = -1;
    if (path.startsWith ("slot"))
    {
        slot = path.substring (4, 5).getIntValue();
        if (slot < 0 || slot > 3) return false;
        rest = path.fromFirstOccurrenceOf (".", false, false);
        rack = &state.slots[(size_t) slot].rack;
        rackDsp = &slotRacks[(size_t) slot];
    }
    else if (path.startsWith ("master."))
    {
        rest = path.fromFirstOccurrenceOf (".", false, false);
        rack = &state.masterRack;
        rackDsp = &masterRack;
    }
    else return false;

    if (rest.startsWith ("engine.") && slot >= 0)
    {
        auto id = rest.fromFirstOccurrenceOf ("engine.", false, false).toStdString();
        for (auto& s : worldEngineControls (state.slots[(size_t) slot].world))
            if (id == s.id)
            {
                const float v = denorm (s, nv);
                state.slots[(size_t) slot].engineParams[id] = v;
                engines[(size_t) slot].setEngineParam (id, v);
                return true;
            }
        return false;
    }
    if (rest.startsWith ("ampenv.") && slot >= 0)
    {
        auto id = rest.fromFirstOccurrenceOf ("ampenv.", false, false);
        auto& env = state.slots[(size_t) slot].ampEnv;
        auto& dst = engines[(size_t) slot].ampEnv();
        if (id == "attackMs")  { env.attackMs = 1.0f + nv * nv * 1999.0f;  dst.attackMs = env.attackMs; }
        else if (id == "decayMs")   { env.decayMs = nv * nv * 4000.0f;    dst.decayMs = env.decayMs; }
        else if (id == "sustain")   { env.sustain = nv * 100.0f;          dst.sustain = env.sustain; }
        else if (id == "releaseMs") { env.releaseMs = 5.0f + nv * nv * 3995.0f; dst.releaseMs = env.releaseMs; }
        else return false;
        return true;
    }
    if (rest.startsWith ("fx") && rack != nullptr)
    {
        const int u = rest.substring (2).getIntValue();
        if (u < 0 || u >= (int) rack->size()) return false;
        auto id = rest.fromFirstOccurrenceOf (".", false, false).toStdString();
        auto& unit = (*rack)[(size_t) u];
        if (id == "bypass")
        {
            unit.bypassed = nv >= 0.5f;
            rackDsp->setBypassed (u, unit.bypassed);
            return true;
        }
        if (auto* spec = findParam (fxSpec (unit.type), id))
        {
            const float v = spec->kind == ParamKind::Float ? denorm (*spec, nv)
                          : juce::jlimit (spec->min, spec->max, spec->min + nv * (spec->max - spec->min));
            unit.params[id] = spec->kind == ParamKind::Float ? v : std::round (v);
            rackDsp->setParam (u, id, unit.params[id]);
            return true;
        }
    }
    return false;
}

float JMSignatureProcessor::getParamNormalized (const juce::String& path) const
{
    if (path.startsWith ("host."))
        if (auto* p = apvts.getParameter (path.fromFirstOccurrenceOf ("host.", false, false)))
            return p->getValue();

    int slot = -1;
    const FxRackState* rack = nullptr;
    juce::String rest;
    if (path.startsWith ("slot"))
    {
        slot = path.substring (4, 5).getIntValue();
        if (slot < 0 || slot > 3) return 0;
        rest = path.fromFirstOccurrenceOf (".", false, false);
        rack = &state.slots[(size_t) slot].rack;
    }
    else if (path.startsWith ("master."))
    {
        rest = path.fromFirstOccurrenceOf (".", false, false);
        rack = &state.masterRack;
    }
    else return 0;

    if (rest.startsWith ("engine.") && slot >= 0)
    {
        auto id = rest.fromFirstOccurrenceOf ("engine.", false, false).toStdString();
        for (auto& s : worldEngineControls (state.slots[(size_t) slot].world))
            if (id == s.id)
            {
                auto it = state.slots[(size_t) slot].engineParams.find (id);
                return norm (s, it != state.slots[(size_t) slot].engineParams.end() ? it->second : s.def);
            }
    }
    if (rest.startsWith ("ampenv.") && slot >= 0)
    {
        auto id = rest.fromFirstOccurrenceOf ("ampenv.", false, false);
        auto& env = state.slots[(size_t) slot].ampEnv;
        if (id == "attackMs") return std::sqrt (juce::jlimit (0.0f, 1.0f, (env.attackMs - 1.0f) / 1999.0f));
        if (id == "decayMs") return std::sqrt (juce::jlimit (0.0f, 1.0f, env.decayMs / 4000.0f));
        if (id == "sustain") return env.sustain / 100.0f;
        if (id == "releaseMs") return std::sqrt (juce::jlimit (0.0f, 1.0f, (env.releaseMs - 5.0f) / 3995.0f));
    }
    if (rest.startsWith ("fx") && rack != nullptr)
    {
        const int u = rest.substring (2).getIntValue();
        if (u < 0 || u >= (int) rack->size()) return 0;
        auto id = rest.fromFirstOccurrenceOf (".", false, false).toStdString();
        auto& unit = (*rack)[(size_t) u];
        if (id == "bypass") return unit.bypassed ? 1.0f : 0.0f;
        if (auto* spec = findParam (fxSpec (unit.type), id))
        {
            auto it = unit.params.find (id);
            const float v = it != unit.params.end() ? it->second : spec->def;
            return spec->kind == ParamKind::Float ? norm (*spec, v)
                 : (spec->max > spec->min ? (v - spec->min) / (spec->max - spec->min) : 0.0f);
        }
    }
    return 0;
}

// R3(F): the macro editor lists targets by name, so router paths are turned into
// something a musician recognises: "A · RHODES/EP · Bark", "MASTER FX 1 · Mix".
juce::String JMSignatureProcessor::paramDisplayName (const juce::String& path) const
{
    auto pretty = [] (const juce::String& id)
    {
        juce::String out;
        for (int i = 0; i < id.length(); ++i)
        {
            const auto c = id[i];
            if (i > 0 && juce::CharacterFunctions::isUpperCase (c)) out << ' ';
            out << (i == 0 ? juce::CharacterFunctions::toUpperCase (c) : c);
        }
        return out;
    };

    if (path.startsWith ("host."))
    {
        if (auto* p = apvts.getParameter (path.fromFirstOccurrenceOf ("host.", false, false)))
            return p->getName (64);
        return path;
    }
    if (path == "arp.rate") return "ARP  -  RATE (FREE)";
    if (path == "arp.gate") return "ARP  -  GATE";

    juce::String prefix, rest;
    const FxRackState* rack = nullptr;
    int slot = -1;
    if (path.startsWith ("slot"))
    {
        slot = path.substring (4, 5).getIntValue();
        if (slot < 0 || slot > 3) return path;
        rest = path.fromFirstOccurrenceOf (".", false, false);
        prefix = juce::String::charToString ((juce::juce_wchar) ('A' + slot)) + "  "
                 + juce::String (worldName (state.slots[(size_t) slot].world)).toUpperCase();
        rack = &state.slots[(size_t) slot].rack;
    }
    else if (path.startsWith ("master."))
    {
        rest = path.fromFirstOccurrenceOf (".", false, false);
        prefix = "MASTER";
        rack = &state.masterRack;
    }
    else return path;

    if (rest.startsWith ("engine.") && slot >= 0)
    {
        auto id = rest.fromFirstOccurrenceOf ("engine.", false, false).toStdString();
        for (auto& s : worldEngineControls (state.slots[(size_t) slot].world))
            if (id == s.id) return prefix + "  -  " + juce::String (s.label);
        return prefix + "  -  " + pretty (juce::String (id));
    }
    if (rest.startsWith ("ampenv."))
        return prefix + "  -  AMP ENV  -  " + pretty (rest.fromFirstOccurrenceOf ("ampenv.", false, false));
    if (rest.startsWith ("fx") && rack != nullptr)
    {
        const int u = rest.substring (2).getIntValue();
        auto id = rest.fromFirstOccurrenceOf (".", false, false);
        juce::String unit = "FX " + juce::String (u + 1);
        if (u >= 0 && u < (int) rack->size()) unit = juce::String (fxSpec ((*rack)[(size_t) u].type).name);
        if (id == "bypass") return prefix + "  -  " + unit.toUpperCase() + "  -  Bypass";
        if (u >= 0 && u < (int) rack->size())
            if (auto* spec = findParam (fxSpec ((*rack)[(size_t) u].type), id.toStdString()))
                return prefix + "  -  " + unit.toUpperCase() + "  -  " + juce::String (spec->label);
        return prefix + "  -  " + unit.toUpperCase() + "  -  " + pretty (id);
    }
    return path;
}

bool JMSignatureProcessor::handleLearnClick (const juce::String& path)
{
    const int mi = state.learn.macroLearnArmed.load();
    if (mi >= 0)
    {
        MacroTarget t;
        t.path = path;
        t.min = 0.0f;
        t.max = 1.0f;
        state.macros[(size_t) mi].targets.push_back (t);
        state.learn.macroLearnArmed.store (-1);
        stateChanged.sendChangeMessage();
        return true;
    }
    if (state.learn.midiLearnArmed.load())
    {
        state.learn.pendingMidiPath = path;   // bound when the next CC arrives
        return true;
    }
    return false;
}

//==============================================================================
// bundle / preset ops (message thread)
void JMSignatureProcessor::loadBundleIntoSlot (int slot, const BundleInfo& info)
{
    juce::String err;
    auto b = state.library.loadBundle (info.dir, err);
    if (b == nullptr)
    {
        juce::Logger::writeToLog ("JM Signature: bundle load failed: " + err);
        return;
    }
    auto& s = state.slots[(size_t) slot];
    const bool worldChanged = s.world != b->world || s.bundleDir.isEmpty();
    s.world = b->world;
    s.bundleDir = info.dir.getFullPathName();
    s.bundleName = info.name;
    engines[(size_t) slot].setWorld (b->world);
    engines[(size_t) slot].setBundle (b);
    if (worldChanged)
    {
        s.engineParams = defaultEngineParams (b->world);
        s.rack = buildDefaultRack (b->world);
    }
    s.ampEnv = AmpEnvState {};
    const bool sus = b->inst.soundType == jmi::SoundType::Sustaining;
    s.ampEnv.attackMs = sus ? 10.0f : 2.0f;
    s.ampEnv.releaseMs = sus ? 400.0f : 250.0f;
    if (! b->inst.presets.empty())
        applyJmiPresetToSlot (slot, b->inst.presets.front());
    else
    {
        s.presetName = "Init";
        syncSlotToDsp (slot);
    }
    stateChanged.sendChangeMessage();
}

void JMSignatureProcessor::applyJmiPresetToSlot (int slot, const jmi::Preset& preset)
{
    auto& s = state.slots[(size_t) slot];
    s.presetName = juce::String (preset.name);
    s.engineParams = engineParamsFromJmiPreset (preset, s.world);
    auto rack = rackFromJmiPreset (preset);
    if (! rack.empty()) s.rack = std::move (rack);
    s.ampEnv.attackMs = (float) preset.attackMs;
    s.ampEnv.decayMs = (float) preset.decayMs;
    s.ampEnv.sustain = (float) preset.sustain;
    s.ampEnv.releaseMs = (float) preset.releaseMs;
    syncSlotToDsp (slot);
    stateChanged.sendChangeMessage();
}

bool JMSignatureProcessor::applyUserPresetToSlot (int slot, const juce::File& f)
{
    auto v = juce::JSON::parse (f.loadFileAsString());
    if (! v.isObject()) return false;
    auto& s = state.slots[(size_t) slot];
    s.presetName = v.getProperty ("name", f.getFileNameWithoutExtension()).toString();
    s.engineParams = paramValuesFromVar (v.getProperty ("engine", juce::var()), worldEngineControls (s.world));
    auto rack = rackFromVar (v.getProperty ("fxRack", juce::var()));
    if (! rack.empty()) s.rack = std::move (rack);
    auto env = v.getProperty ("ampEnvelope", juce::var());
    s.ampEnv.attackMs = (float) (double) env.getProperty ("attackMs", s.ampEnv.attackMs);
    s.ampEnv.decayMs = (float) (double) env.getProperty ("decayMs", s.ampEnv.decayMs);
    s.ampEnv.sustain = (float) (double) env.getProperty ("sustain", s.ampEnv.sustain);
    s.ampEnv.releaseMs = (float) (double) env.getProperty ("releaseMs", s.ampEnv.releaseMs);
    syncSlotToDsp (slot);
    stateChanged.sendChangeMessage();
    return true;
}

juce::var JMSignatureProcessor::slotPresetToVar (int slot, const juce::String& name) const
{
    auto& s = state.slots[(size_t) slot];
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("tags", juce::Array<juce::var>());
    o->setProperty ("engine", paramValuesToVar (s.engineParams, worldEngineControls (s.world)));
    o->setProperty ("fxRack", rackToVar (s.rack));
    auto* env = new juce::DynamicObject();
    env->setProperty ("attackMs", s.ampEnv.attackMs);
    env->setProperty ("decayMs", s.ampEnv.decayMs);
    env->setProperty ("sustain", s.ampEnv.sustain);
    env->setProperty ("releaseMs", s.ampEnv.releaseMs);
    o->setProperty ("ampEnvelope", juce::var (env));
    return juce::var (o);
}

void JMSignatureProcessor::saveUserPreset (int slot, const juce::String& name)
{
    auto dir = worldPresetDir (state.slots[(size_t) slot].world);
    dir.createDirectory();
    dir.getChildFile (juce::File::createLegalFileName (name) + ".json")
       .replaceWithText (juce::JSON::toString (slotPresetToVar (slot, name)));
    state.slots[(size_t) slot].presetName = name;
    stateChanged.sendChangeMessage();
}

void JMSignatureProcessor::savePerformancePreset (const juce::String& name)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    juce::Array<juce::var> slots;
    for (int i = 0; i < 4; ++i) slots.add (state.slotToVar (i));
    o->setProperty ("slots", slots);
    o->setProperty ("masterRack", rackToVar (state.masterRack));
    o->setProperty ("macros", state.macrosToVar());
    // Spec §F / decision 4: modulation belongs to the PERFORMANCE layer — plugin
    // state and performance presets carry it; .jmi bundle presets never do (that
    // schema is shared with JM Capture, which has no LFOs to model).
    o->setProperty ("mod", state.mod.toVar());
    o->setProperty ("midiMap", state.midiMap.toVar());
    auto* hp = new juce::DynamicObject();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            hp->setProperty (rp->paramID, rp->getValue());
    o->setProperty ("hostParams", juce::var (hp));
    auto dir = performancePresetDir();
    dir.createDirectory();
    dir.getChildFile (juce::File::createLegalFileName (name) + ".json")
       .replaceWithText (juce::JSON::toString (juce::var (o)));
    state.performPresetName = name;
    stateChanged.sendChangeMessage();
}

// CLIENT REVIEW ROUND 3 (G): the browser listed nothing on PERFORM because the
// spec's "factory folder" of performance presets was never created. Four honest
// factory snapshots are written on first run; they only carry the parameters
// they actually mean to set, so loading one never disturbs the rest.
void JMSignatureProcessor::ensureFactoryPerformancePresets()
{
    auto dir = performancePresetDir().getChildFile ("Factory");
    if (dir.isDirectory() && dir.findChildFiles (juce::File::findFiles, false, "*.json").size() > 0)
        return;
    dir.createDirectory();

    struct Def { const char* name; bool en[4]; bool solo; const char* macros[4]; };
    const Def defs[] = {
        { "Stage Stack",  { true,  true,  true,  true  }, false, { "Shimmer", "Movement", "Space", "Warmth" } },
        { "Solo Rhodes",  { true,  false, false, false }, true,  { "Bark",    "Tremolo",  "Space", "Tine"   } },
        { "EP And Strings", { true, false, false, true }, false, { "Blend",   "Swell",    "Space", "Air"    } },
        { "Wide Keys",    { true,  true,  false, false }, false, { "Width",   "Chorus",   "Space", "Warmth" } },
    };

    for (auto& d : defs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", d.name);
        juce::Array<juce::var> macros;
        for (int i = 0; i < 4; ++i)
        {
            auto* mo = new juce::DynamicObject();
            mo->setProperty ("name", d.macros[i]);
            mo->setProperty ("targets", juce::Array<juce::var>());
            macros.add (juce::var (mo));
        }
        o->setProperty ("macros", juce::var (macros));
        auto* hp = new juce::DynamicObject();
        for (int i = 0; i < 4; ++i)
            hp->setProperty ("slotEnable" + juce::String (i), d.en[i] ? 1.0 : 0.0);
        hp->setProperty ("solo", d.solo ? 1.0 : 0.0);
        o->setProperty ("hostParams", juce::var (hp));
        dir.getChildFile (juce::File::createLegalFileName (d.name) + ".json")
           .replaceWithText (juce::JSON::toString (juce::var (o)));
    }
}

bool JMSignatureProcessor::loadPerformancePreset (const juce::File& f)
{
    auto v = juce::JSON::parse (f.loadFileAsString());
    if (! v.isObject()) return false;
    if (auto* slots = v.getProperty ("slots", juce::var()).getArray())
        for (int i = 0; i < juce::jmin (4, slots->size()); ++i)
            state.slotFromVar (i, slots->getReference (i));
    if (v.hasProperty ("masterRack")) state.masterRack = rackFromVar (v.getProperty ("masterRack", juce::var()));
    state.macrosFromVar (v.getProperty ("macros", juce::var()));
    if (v.hasProperty ("mod")) state.mod.fromVar (v.getProperty ("mod", juce::var()));
    if (v.hasProperty ("midiMap")) state.midiMap.fromVar (v.getProperty ("midiMap", juce::var()));
    if (auto* hp = v.getProperty ("hostParams", juce::var()).getDynamicObject())
        for (auto& prop : hp->getProperties())
            if (auto* p = apvts.getParameter (prop.name.toString()))
                p->setValueNotifyingHost ((float) (double) prop.value);
    state.performPresetName = v.getProperty ("name", f.getFileNameWithoutExtension()).toString();
    syncAllToDsp();
    publishModulation();
    stateChanged.sendChangeMessage();
    return true;
}

//==============================================================================
void JMSignatureProcessor::syncSlotToDsp (int i)
{
    auto& s = state.slots[(size_t) i];
    auto& eng = engines[(size_t) i];
    eng.setWorld (s.world);
    for (auto& [id, v] : s.engineParams) eng.setEngineParam (id, v);
    eng.ampEnv().attackMs = s.ampEnv.attackMs;
    eng.ampEnv().decayMs = s.ampEnv.decayMs;
    eng.ampEnv().sustain = s.ampEnv.sustain;
    eng.ampEnv().releaseMs = s.ampEnv.releaseMs;
    slotRacks[(size_t) i].setState (s.rack);

    // (re)load bundle if the state references one the engine doesn't have
    if (s.bundleDir.isNotEmpty())
    {
        auto current = eng.bundle();
        if (current == nullptr || current->dir.getFullPathName() != s.bundleDir)
        {
            juce::File dir (s.bundleDir);
            juce::String err;
            if (auto b = dir.isDirectory() ? state.library.loadBundle (dir, err) : nullptr)
            {
                eng.setBundle (b);
                // keep engine params from state (setBundle resets env defaults)
                eng.ampEnv().attackMs = s.ampEnv.attackMs;
                eng.ampEnv().decayMs = s.ampEnv.decayMs;
                eng.ampEnv().sustain = s.ampEnv.sustain;
                eng.ampEnv().releaseMs = s.ampEnv.releaseMs;
            }
        }
    }
}

void JMSignatureProcessor::syncAllToDsp()
{
    for (int i = 0; i < 4; ++i) syncSlotToDsp (i);
    masterRack.setState (state.masterRack);
}

//==============================================================================
void JMSignatureProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto* o = new juce::DynamicObject();
    // version 2 adds "mod" (MODULATION PHASE 1). A version-1 state has no such
    // property, so ModState::fromVar leaves everything at defaults and every
    // macro link in it keeps working exactly as before.
    o->setProperty ("version", 2);
    juce::Array<juce::var> slots;
    for (int i = 0; i < 4; ++i) slots.add (state.slotToVar (i));
    o->setProperty ("slots", slots);
    o->setProperty ("masterRack", rackToVar (state.masterRack));
    o->setProperty ("macros", state.macrosToVar());
    o->setProperty ("mod", state.mod.toVar());
    o->setProperty ("midiMap", state.midiMap.toVar());
    o->setProperty ("midiRouting", midiRouting.toVar());     // round 4 (item 1)
    o->setProperty ("currentPage", state.currentPage);
    o->setProperty ("performPresetName", state.performPresetName);
    auto* hp = new juce::DynamicObject();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            hp->setProperty (rp->paramID, rp->getValue());
    o->setProperty ("hostParams", juce::var (hp));
    const auto text = juce::JSON::toString (juce::var (o));
    dest.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

void JMSignatureProcessor::setStateInformation (const void* data, int size)
{
    auto v = juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, size));
    if (! v.isObject()) return;
    if (auto* slots = v.getProperty ("slots", juce::var()).getArray())
        for (int i = 0; i < juce::jmin (4, slots->size()); ++i)
            state.slotFromVar (i, slots->getReference (i));
    state.masterRack = rackFromVar (v.getProperty ("masterRack", juce::var()));
    state.macrosFromVar (v.getProperty ("macros", juce::var()));
    state.mod.fromVar (v.getProperty ("mod", juce::var()));   // absent in version 1
    state.midiMap.fromVar (v.getProperty ("midiMap", juce::var()));
    midiRouting.fromVar (v.getProperty ("midiRouting", juce::var()));
    refreshMidiDevices();                                    // re-open what was saved
    state.currentPage = (int) v.getProperty ("currentPage", 0);
    state.performPresetName = v.getProperty ("performPresetName", "").toString();
    if (auto* hp = v.getProperty ("hostParams", juce::var()).getDynamicObject())
        for (auto& prop : hp->getProperties())
            if (auto* p = apvts.getParameter (prop.name.toString()))
                p->setValueNotifyingHost ((float) (double) prop.value);
    syncAllToDsp();
    publishModulation();
    stateChanged.sendChangeMessage();
}

juce::AudioProcessorEditor* JMSignatureProcessor::createEditor()
{
    return new JMSignatureEditor (*this);
}

} // namespace jm

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new jm::JMSignatureProcessor();
}
