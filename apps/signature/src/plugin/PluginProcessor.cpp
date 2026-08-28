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
    masterVolParam = apvts.getRawParameterValue ("masterVol");
    limiterParam = apvts.getRawParameterValue ("limiter");
    transposeParam = apvts.getRawParameterValue ("transpose");
    octaveParam = apvts.getRawParameterValue ("octave");
    soloParam = apvts.getRawParameterValue ("solo");

    state.library.rescan();
    ensureFactoryPerformancePresets();
    // Auto-load the first available bundle of each world into its default slot.
    for (int i = 0; i < 4; ++i)
    {
        auto list = state.library.bundlesForWorld (i);
        if (! list.empty())
            loadBundleIntoSlot (i, list.front());
    }
    startTimerHz (30);
}

JMSignatureProcessor::~JMSignatureProcessor() { stopTimer(); }

//==============================================================================
void JMSignatureProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    loadMeasurer.reset (sampleRate, samplesPerBlock);
    slotBuf.setSize (2, samplesPerBlock);
    expressionGain.reset (sampleRate, 0.05);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterVolParam->load(), -60.0f));
    masterLimiter.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (masterLimiter.latencySamples());
    for (int i = 0; i < 4; ++i)
    {
        engines[(size_t) i].prepare (sampleRate, samplesPerBlock);
        slotRacks[(size_t) i].prepare (sampleRate, samplesPerBlock);
    }
    masterRack.prepare (sampleRate, samplesPerBlock);
    syncAllToDsp();
}

void JMSignatureProcessor::handleMidiAtBlockStart (const juce::MidiBuffer& midi)
{
    const int shift = (int) transposeParam->load() + 12 * (int) octaveParam->load();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int note = juce::jlimit (0, 127, m.getNoteNumber() + shift);
            for (int i = 0; i < 4; ++i)
                if (slotEnable[(size_t) i]->load() >= 0.5f)
                    engines[(size_t) i].noteOn (note, m.getVelocity());
        }
        else if (m.isNoteOff())
        {
            const int note = juce::jlimit (0, 127, m.getNoteNumber() + shift);
            for (auto& e : engines) e.noteOff (note);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& e : engines) e.allNotesOff (m.isAllNotesOff());
        }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber(), val = m.getControllerValue();
            if (cc == 64)                                     // §5.4 sustain
            {
                for (auto& e : engines) e.sustainPedal (val >= 64);
            }
            else if (cc == 11)                                // expression → volume
            {
                const float n = (float) val / 127.0f;
                expressionGain.setTargetValue (n * n);
            }
            else if (cc == 1)                                 // mod wheel per world (§5.3)
            {
                const float n = (float) val / 127.0f;
                for (int i = 0; i < 4; ++i)
                {
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
            else                                              // learned CCs → message thread
            {
                state.learn.lastIncomingCc.store (cc);
                int s1, n1, s2, n2;
                ccFifo.prepareToWrite (1, s1, n1, s2, n2);
                if (n1 > 0) ccEvents[(size_t) s1] = { cc, val };
                ccFifo.finishedWrite (n1);
            }
        }
        else if (m.isPitchWheel())
        {
            // Pitch bend intentionally not implemented for sampled worlds (v1).
        }
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

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm_ = *b;

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    handleMidiAtBlockStart (midi);

    int voices = 0;
    for (int i = 0; i < 4; ++i)
    {
        auto& eng = engines[(size_t) i];
        voices += eng.activeVoices();
        const bool enabled = slotEnable[(size_t) i]->load() >= 0.5f;
        if (! enabled && eng.activeVoices() == 0) { slotPeak[(size_t) i].store (0); continue; }

        slotBuf.clear (0, 0, n);
        slotBuf.clear (1, 0, n);
        eng.render (slotBuf, n);
        slotRacks[(size_t) i].process (slotBuf, bpm_);

        const float vol = juce::Decibels::decibelsToGain (slotVol[(size_t) i]->load(), -60.0f);
        const float pan = slotPan[(size_t) i]->load();
        const float gl = vol * std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.414f * 0.7071f;
        const float gr = vol * std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f) * 1.414f * 0.7071f;
        buffer.addFrom (0, 0, slotBuf, 0, 0, n, gl);
        buffer.addFrom (1, 0, slotBuf, 1, 0, n, gr);
        slotPeak[(size_t) i].store (slotBuf.getMagnitude (0, n));
    }
    totalVoices.store (voices);

    masterRack.process (buffer, bpm_);

    // master volume — smoothed (R3/D: it was a per-block constant, so every
    // knob move stepped the gain at the block boundary and zipped)
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterVolParam->load(), -60.0f));
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

    // macros (host-automatable): apply on change
    for (int i = 0; i < 4; ++i)
    {
        const float v = macroParams[(size_t) i]->load();
        if (std::abs (v - lastMacroValue[(size_t) i]) > 1.0e-5f)
        {
            lastMacroValue[(size_t) i] = v;
            applyMacro (i);
        }
    }

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

void JMSignatureProcessor::applyMacro (int mi)
{
    const float v = macroParams[(size_t) mi]->load();
    for (auto& t : state.macros[(size_t) mi].targets)
        if (! t.path.startsWith ("host.macro"))
            setParamNormalized (t.path, t.apply (v), true);
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
    if (v.hasProperty ("midiMap")) state.midiMap.fromVar (v.getProperty ("midiMap", juce::var()));
    if (auto* hp = v.getProperty ("hostParams", juce::var()).getDynamicObject())
        for (auto& prop : hp->getProperties())
            if (auto* p = apvts.getParameter (prop.name.toString()))
                p->setValueNotifyingHost ((float) (double) prop.value);
    state.performPresetName = v.getProperty ("name", f.getFileNameWithoutExtension()).toString();
    syncAllToDsp();
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
    o->setProperty ("version", 1);
    juce::Array<juce::var> slots;
    for (int i = 0; i < 4; ++i) slots.add (state.slotToVar (i));
    o->setProperty ("slots", slots);
    o->setProperty ("masterRack", rackToVar (state.masterRack));
    o->setProperty ("macros", state.macrosToVar());
    o->setProperty ("midiMap", state.midiMap.toVar());
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
    state.midiMap.fromVar (v.getProperty ("midiMap", juce::var()));
    state.currentPage = (int) v.getProperty ("currentPage", 0);
    state.performPresetName = v.getProperty ("performPresetName", "").toString();
    if (auto* hp = v.getProperty ("hostParams", juce::var()).getDynamicObject())
        for (auto& prop : hp->getProperties())
            if (auto* p = apvts.getParameter (prop.name.toString()))
                p->setValueNotifyingHost ((float) (double) prop.value);
    syncAllToDsp();
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
