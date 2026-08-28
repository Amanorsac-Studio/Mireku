// Modulation.h — JM Signature modulation system, phase 1 (LFOs + matrix).
//
// Implements docs/JM-MODULATION-SPEC.html section C/D against the decisions in
// docs/MODULATION-RESEARCH.md. The two structural rules that shape everything
// here, both from that research:
//
//   1. BASE + OFFSET, never overwrite (research section 0 / trap 1). The player's knob
//      value is the base and lives in AppState / APVTS exactly as before. The
//      matrix is evaluated on the AUDIO thread every 64 samples and produces a
//      normalised OFFSET that is added at the point of use. Nothing in this file
//      ever writes a stored parameter value, and nothing here ever calls
//      setValueNotifyingHost() — LFO motion must never reach host automation
//      (the Bitwig VST3 feedback-loop shape the research names).
//
//   2. SYNCED PHASE IS DERIVED FROM ppqPosition, not integrated from Hz
//      (research section 2.4 / trap 2). `phase = frac(ppq / beatsPerCycle)` is
//      stateless, so it is drift-free across a tempo ramp, needs no repair after
//      a locate or a loop wrap, and renders identically on every bounce. The
//      host's ppq is latched ONLY while the transport plays; otherwise the clock
//      free-runs at the last known tempo (Surge XT's rule), which is also what
//      makes the standalone build work through the same code path.
//
// Concurrency (research section 4.1): the routing table is a fixed-size, trivially
// copyable POD array in a double-buffered pair with an atomic index. The message
// thread fills the inactive buffer and publishes it with one release store; the
// audio thread does one acquire load per block. No lock, no allocation, no
// dropped block, and the audio thread never touches a juce::String — every
// destination path is resolved to integer indices on the message thread.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "FxParams.h"
#include <array>
#include <atomic>
#include <cmath>

namespace jm {

//==============================================================================
// Sources. Twelve of the spec's list; the mod envelope is CUT from v1 (spec
// decision 5), so it deliberately has no enum value.
enum class ModSource : int
{
    none = 0,
    macro1, macro2, macro3, macro4,
    lfo1, lfo2, lfo3,
    velocity, aftertouch, modWheel, expression, keyTrack, random,
    count
};
constexpr int kNumModSources = (int) ModSource::count;

/// Research section 1.1: polarity is a property of the SOURCE, and the depth formula
/// re-centres a bipolar source so that peak-to-peak swing is |depth|*(max-min)
/// for every source type ("School A", Vital / Serum). Only the LFOs are bipolar.
inline bool modSourceIsBipolar (ModSource s)
{
    return s == ModSource::lfo1 || s == ModSource::lfo2 || s == ModSource::lfo3;
}

inline const char* modSourceName (ModSource s)
{
    switch (s)
    {
        case ModSource::macro1: return "MACRO 1";
        case ModSource::macro2: return "MACRO 2";
        case ModSource::macro3: return "MACRO 3";
        case ModSource::macro4: return "MACRO 4";
        case ModSource::lfo1:   return "LFO 1";
        case ModSource::lfo2:   return "LFO 2";
        case ModSource::lfo3:   return "LFO 3";
        case ModSource::velocity:   return "VELOCITY";
        case ModSource::aftertouch: return "AFTERTOUCH";
        case ModSource::modWheel:   return "MOD WHEEL";
        case ModSource::expression: return "EXPRESSION";
        case ModSource::keyTrack:   return "KEY TRACK";
        case ModSource::random:     return "RANDOM";
        default: return "-";
    }
}

inline const char* modSourceId (ModSource s)
{
    switch (s)
    {
        case ModSource::macro1: return "macro1";
        case ModSource::macro2: return "macro2";
        case ModSource::macro3: return "macro3";
        case ModSource::macro4: return "macro4";
        case ModSource::lfo1:   return "lfo1";
        case ModSource::lfo2:   return "lfo2";
        case ModSource::lfo3:   return "lfo3";
        case ModSource::velocity:   return "velocity";
        case ModSource::aftertouch: return "aftertouch";
        case ModSource::modWheel:   return "modWheel";
        case ModSource::expression: return "expression";
        case ModSource::keyTrack:   return "keyTrack";
        case ModSource::random:     return "random";
        default: return "none";
    }
}

inline ModSource modSourceFromId (const juce::String& id)
{
    for (int i = 0; i < kNumModSources; ++i)
        if (id == modSourceId ((ModSource) i)) return (ModSource) i;
    return ModSource::none;
}

/// Chip colour for a source (research section 1.3: the modulation span arc is drawn in the
/// source's own colour). Macros keep the accent they already use on the PERFORM
/// page; the LFOs and note sources stay inside the gold family, because a second
/// hue would fight the chrome.
inline juce::uint32 modSourceColourArgb (ModSource s)
{
    switch (s)
    {
        case ModSource::macro1: return 0xffE66F17;
        case ModSource::macro2: return 0xffA9D62E;
        case ModSource::macro3: return 0xff39B8FF;
        case ModSource::macro4: return 0xffD85CE8;
        case ModSource::lfo1:   return 0xffFFD77A;
        case ModSource::lfo2:   return 0xffFFB11B;
        case ModSource::lfo3:   return 0xffD9A648;
        default:                return 0xffC9B27A;   // note sources: warm neutral, legible on the ring
    }
}

//==============================================================================
// LFO. Three of them, GLOBAL (spec decision 1): one phase shared by every
// sounding voice.
enum class LfoShape : int { sine = 0, triangle, sawUp, sawDown, square, sampleHold, smoothRandom, count };
constexpr int kNumLfoShapes = (int) LfoShape::count;

inline const char* lfoShapeName (int i)
{
    static const char* n[] = { "SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "S & H", "SMOOTH RAND" };
    return (i >= 0 && i < kNumLfoShapes) ? n[i] : n[0];
}

enum class LfoRetrigger : int { free = 0, firstNote, everyNote, count };
inline const char* lfoRetriggerName (int i)
{
    static const char* n[] = { "FREE", "FIRST NOTE", "EVERY NOTE" };
    return (i >= 0 && i < 3) ? n[i] : n[0];
}

constexpr float kLfoRateMin = 0.01f, kLfoRateMax = 40.0f;   // spec section C, log taper
constexpr int   kNumLfos = 3;

/// Serialisable LFO settings. Trivially copyable so it can ride in the published
/// audio-thread table.
struct LfoSettings
{
    int   shape      = (int) LfoShape::sine;
    float rateHz     = 1.0f;                    // free rate (spec default 1 Hz)
    int   sync       = 1;                       // spec default ON
    int   division   = kDivQuarter;             // index into divisionBeats()
    float phaseDeg   = 0.0f;                    // 0..360, added at READ time
    float fadeSec    = 0.0f;                    // 0..5 s
    int   retrigger  = (int) LfoRetrigger::free;
};

//==============================================================================
/// Transport clock, updated once per processBlock (research section 2.4).
/// The host's ppq is hard-locked while playing and free-run otherwise, so an LFO
/// keeps moving when the player stops the transport to practise, and snaps back
/// into phase lock the instant playback starts. Standalone (no playhead at all)
/// falls out of the same branch.
struct TransportClock
{
    double ppq = 0.0;
    double bpm = 120.0;                 // last GOOD bpm; never falls back to 0
    bool   playing = false;
    bool   jumped  = false;             // locate / loop wrap, detected by prediction

    void update (juce::AudioPlayHead* ph, int numSamples, double sr)
    {
        const double blockBeats = (double) numSamples * (bpm / 60.0) / juce::jmax (1.0, sr);
        const double predicted  = ppq + blockBeats;
        jumped = false;

        if (ph != nullptr)
        {
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm())
                    if (*b > 0.0) bpm = *b;                 // keep the last valid tempo forever
                playing = pos->getIsPlaying();
                if (playing)
                {
                    if (auto q = pos->getPpqPosition())
                    {
                        // No API in JUCE or VST3 flags a transport jump, so detect
                        // it by prediction. The anchored phase branch is stateless
                        // and needs no repair; only the sample-and-hold knots do.
                        jumped = std::abs (*q - predicted) > juce::jmax (0.5 * blockBeats, 1.0e-3);
                        ppq = *q;
                        return;
                    }
                }
            }
        }
        ppq = predicted;                 // stopped / standalone / host gave nothing
    }

    /// ppq at a sample offset inside the current block (sub-block evaluation).
    double ppqAt (int offsetSamples, double sr) const
    {
        return ppq + (double) offsetSamples * (bpm / 60.0) / juce::jmax (1.0, sr);
    }
};

//==============================================================================
namespace modmath {

inline double frac01 (double x) { return x - std::floor (x); }

/// Shape functions. Pirkle's start-phase offsets are folded in so triangle and
/// both ramps BEGIN AT ZERO OUTPUT rather than at -1 (research section 2.1).
inline float shapeValue (int shape, double p)
{
    p = frac01 (p);
    switch ((LfoShape) shape)
    {
        case LfoShape::sine:     return (float) std::sin (p * juce::MathConstants<double>::twoPi);
        case LfoShape::triangle: return p < 0.25 ? (float) (4.0 * p)
                                      : p < 0.75 ? (float) (2.0 - 4.0 * p)
                                                 : (float) (4.0 * p - 4.0);
        case LfoShape::sawUp:    return (float) (2.0 * frac01 (p + 0.5) - 1.0);
        case LfoShape::sawDown:  return (float) (1.0 - 2.0 * frac01 (p + 0.5));
        case LfoShape::square:   return p < 0.5 ? 1.0f : -1.0f;
        default: return 0.0f;    // S&H / smooth random are stateful, handled in Lfo
    }
}

/// Vital's "sine interpolate" raised cosine, the interpolation the research
/// recommends for Smooth Random: C1-continuous, monotone between knots and
/// incapable of overshooting +-1 (so it can never break the clamp).
inline float raisedCosine (float from, float to, double t)
{
    const float s = (float) (0.5 - 0.5 * std::cos (juce::jlimit (0.0, 1.0, t) * juce::MathConstants<double>::pi));
    return from + (to - from) * s;
}

/// THE depth formula (research section 1.1, "School A" with polarity on the source).
///   shaped = clamp01(bipolar ? s*0.5+0.5 : s) ^ curve      (curve/invert as today)
///   f      = bipolar ? shaped-0.5 : shaped
///   offset = depth * (max-min) * f
/// Peak-to-peak swing is |depth|*(max-min) for EVERY source type, so swapping
/// velocity for an LFO in a row does not silently double the effect.
inline float modOffset (float sourceValue, bool bipolarSource,
                        float depth, float lo, float hi, float curve, bool invert)
{
    const float s = bipolarSource ? sourceValue * 0.5f + 0.5f : sourceValue;
    float shaped = std::pow (juce::jlimit (0.0f, 1.0f, s), juce::jmax (0.01f, curve));
    if (invert) shaped = 1.0f - shaped;
    const float f = bipolarSource ? shaped - 0.5f : shaped;
    return juce::jlimit (-1.0f, 1.0f, depth) * (hi - lo) * f;
}

} // namespace modmath

//==============================================================================
/// One global LFO. All state is audio-thread private.
class GlobalLfo
{
public:
    void reset()
    {
        freePhase_ = 0.0;
        anchorPpq_ = 0.0;
        lastPhase_ = 0.0;
        rndFrom_ = rndTo_ = 0.0f;
        rndSeeded_ = false;
        fadePos_ = 0.0;
        fadeStart_ = 0.0f;
        fadeArmed_ = false;
        value_ = 0.0f;
    }

    /// Retrigger: restart the shared phase and re-arm the fade (research section 2.3 —
    /// the fade is retriggered on the FIRST note only, Omnisphere's legato rule).
    void retrigger (const TransportClock& clock, bool restartFade)
    {
        anchorPpq_ = clock.ppq;
        freePhase_ = 0.0;
        lastPhase_ = 0.0;
        rndSeeded_ = false;
        if (restartFade) armFade();
    }

    /// Re-arm the fade without touching the phase. A global LFO in Free mode has
    /// no restart, but its fade still belongs to the 0-held-notes -> 1 edge.
    void armFade() { fadePos_ = 0.0; fadeArmed_ = true; }

    float value() const { return value_; }
    double phase() const { return lastPhase_; }

    /// One control block. `rateOffsetNorm` is the cross-modulation offset applied
    /// to the rate's NORMALISED position (previous block's value — the one-block
    /// latch that stands in for Vital's cr::Feedback, research section 1.4).
    float process (const LfoSettings& s, const TransportClock& clock,
                   double sr, int numSamples, double ppqOffset, float rateOffsetNorm)
    {
        const double beats = divisionBeats (s.division);
        const bool freeze = s.sync != 0 && beats <= 0.0;   // Vital's "Freeze" entry

        double p;
        if (freeze)
        {
            p = lastRawPhase_;
        }
        else if (s.sync != 0)
        {
            // ANCHORED: a pure function of ppq. Stateless, so a tempo ramp, a
            // locate and a loop wrap all resolve correctly with no repair.
            p = modmath::frac01 ((clock.ppq + ppqOffset - anchorPpq_) / beats);
        }
        else
        {
            const float rateNorm = juce::jlimit (0.0f, 1.0f, rateNormFor (s.rateHz) + rateOffsetNorm);
            const double hz = (double) rateFromNorm (rateNorm);
            freePhase_ = modmath::frac01 (freePhase_ + hz * (double) numSamples / juce::jmax (1.0, sr));
            p = freePhase_;
        }
        lastRawPhase_ = p;

        // Phase offset is kept SEPARATE from the running phase and added at read
        // time (research section 2.2, Vital's separation), so it is meaningful in every
        // retrigger mode including Free, and turning the knob is a pure rotation.
        const double pr = modmath::frac01 (p + (double) s.phaseDeg / 360.0);

        float v;
        const auto shape = (LfoShape) s.shape;
        if (shape == LfoShape::sampleHold || shape == LfoShape::smoothRandom)
        {
            if (clock.jumped) rndSeeded_ = false;             // don't let a frozen step survive a locate
            if (! rndSeeded_)
            {
                rndFrom_ = nextRandom();
                rndTo_   = nextRandom();
                rndSeeded_ = true;
            }
            else if (pr < lastPhase_ - 1.0e-9)                // cycle wrapped: new knot
            {
                rndFrom_ = rndTo_;
                rndTo_ = nextRandom();
            }
            v = shape == LfoShape::sampleHold ? rndFrom_
                                              : modmath::raisedCosine (rndFrom_, rndTo_, pr);
        }
        else
            v = modmath::shapeValue (s.shape, pr);
        lastPhase_ = pr;

        // Fade-in: the CROSSFADE form (Serum's Rise / Vital), not a depth
        // envelope, so a unipolar destination does not jump at note-on
        // (research section 2.3).
        if (s.fadeSec > 1.0e-3f)
        {
            if (fadeArmed_) { fadeStart_ = v; fadeArmed_ = false; }
            fadePos_ += (double) numSamples / juce::jmax (1.0, sr);
            const double t = juce::jlimit (0.0, 1.0, fadePos_ / (double) s.fadeSec);
            v = fadeStart_ + (v - fadeStart_) * (float) t;
        }
        else { fadeArmed_ = false; fadePos_ = 1.0e9; }

        value_ = juce::jlimit (-1.0f, 1.0f, v);
        return value_;
    }

    /// Log-tapered rate mapping (research section 2.5): normalised position is log2(Hz)
    /// between the endpoints, which is what every shipping synth does and makes
    /// automation perceptually even.
    static float rateNormFor (float hz)
    {
        hz = juce::jlimit (kLfoRateMin, kLfoRateMax, hz);
        return (float) (std::log (hz / kLfoRateMin) / std::log (kLfoRateMax / kLfoRateMin));
    }
    static float rateFromNorm (float n)
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        return kLfoRateMin * std::pow (kLfoRateMax / kLfoRateMin, n);
    }

private:
    float nextRandom() { return rng_.nextFloat() * 2.0f - 1.0f; }

    double freePhase_ = 0.0, anchorPpq_ = 0.0, lastPhase_ = 0.0, lastRawPhase_ = 0.0;
    float  rndFrom_ = 0.0f, rndTo_ = 0.0f;
    bool   rndSeeded_ = false;
    double fadePos_ = 1.0e9;
    float  fadeStart_ = 0.0f;
    bool   fadeArmed_ = false;
    float  value_ = 0.0f;
    juce::Random rng_ { 0x5eed1234 };
};

//==============================================================================
// Destinations, resolved to integers on the MESSAGE thread. The audio thread
// never sees a juce::String.
enum class ModDestKind : int
{
    none = 0,
    engine,      // slot engine control      (slot, param)
    ampEnv,      // slot amp envelope field  (slot, param 0..3)
    fxParam,     // FX unit param            (slot: -1 = master rack, unit, param)
    fxBypass,    // FX unit bypass           (slot, unit)
    hostParam,   // APVTS param, applied as an offset at point of use (unit = HostModTarget)
    lfoRate,     // cross-modulation         (unit = LFO index)
    rowDepth     // cross-modulation         (unit = matrix row index)
};

/// The continuous host parameters modulation may reach. Bool/int host params
/// (solo, limiter, transpose, octave, bend range) are deliberately NOT offerable
/// as destinations — modulating them has no musical meaning and would fight the
/// host's own automation.
enum HostModTarget
{
    hmMasterVol = 0,
    hmSlotVol0, hmSlotVol1, hmSlotVol2, hmSlotVol3,
    hmSlotPan0, hmSlotPan1, hmSlotPan2, hmSlotPan3,
    hmMacro1, hmMacro2, hmMacro3, hmMacro4,
    hmCount
};

struct ModDest
{
    int kind  = (int) ModDestKind::none;
    int slot  = -1;
    int unit  = -1;
    int param = -1;
    float lo = 0.0f, hi = 1.0f;      // denormalisation window, cached at resolve time
    int logTaper = 0;
    int quantise = 0;                // enum/bool destination: round after denorm

    bool valid() const { return kind != (int) ModDestKind::none; }
    bool operator== (const ModDest& o) const
    {
        return kind == o.kind && slot == o.slot && unit == o.unit && param == o.param;
    }
    float denorm (float n) const
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        float v = (logTaper != 0 && lo > 0.0f) ? lo * std::pow (hi / lo, n) : lo + (hi - lo) * n;
        return quantise != 0 ? std::round (v) : v;
    }
};

/// One published row: POD, trivially copyable, no strings.
struct ResolvedRow
{
    ModDest dest;
    int   source = 0;
    int   group  = 0;         // index into the table's destination groups
    int   matrixIndex = -1;   // matrix row this came from (-1 = a macro link)
    float depth = 0.0f, lo = 0.0f, hi = 1.0f, curve = 1.0f;
    int   invert = 0;
};

constexpr int kMaxMatrixRows = 16;   // the spec ships 8; sized so growing is a UI change only
constexpr int kDefaultMatrixRows = 8;
constexpr int kMaxModLinks = 96;     // matrix rows + every macro link, published together
constexpr int kModControlBlock = 64; // research section 1.5: ~689 Hz at 44.1 kHz

/// The immutable table the audio thread reads. Trivially copyable POD.
struct ModTable
{
    int rowCount = 0;
    int groupCount = 0;
    std::array<ResolvedRow, kMaxModLinks> rows {};
    std::array<ModDest, kMaxModLinks> groups {};
    std::array<LfoSettings, kNumLfos> lfo {};
    int lfoUsed = 0;              // bitmask of LFOs that feed at least one enabled row
};

/// Implemented by the processor: pushes a modulated value into the DSP. Called
/// on the audio thread, once per modulated destination per control block.
struct ModApplier
{
    virtual ~ModApplier() = default;
    virtual void applyModulatedValue (const ModDest& dest, float normalised) = 0;
};

//==============================================================================
/// The engine: source evaluation + matrix summation, on the audio thread.
class ModEngine
{
public:
    ModEngine() { for (auto& l : lfos_) l.reset(); }

    // ------------------------------------------------ message-thread publishing
    /// Fill the inactive buffer, then publish it with one release store. Row
    /// edits therefore take effect on a control-block boundary, which is what you
    /// want anyway (a routing that changed mid-block would step).
    ModTable& editBuffer() { return tables_[1 - live_.load (std::memory_order_relaxed)]; }
    void publish()
    {
        live_.store (1 - live_.load (std::memory_order_relaxed), std::memory_order_release);
    }
    const ModTable& liveTable() const { return tables_[live_.load (std::memory_order_acquire)]; }

    /// Base values, one per destination group. Written by the message thread
    /// whenever a knob moves (so the whole modulated range follows the knob
    /// immediately) and refreshed wholesale on the 30 Hz timer.
    void setGroupBase (int group, float norm)
    {
        if (group >= 0 && group < kMaxModLinks)
            groupBase_[(size_t) group].store (juce::jlimit (0.0f, 1.0f, norm), std::memory_order_relaxed);
    }
    float groupBase (int group) const
    {
        return group >= 0 && group < kMaxModLinks
                   ? groupBase_[(size_t) group].load (std::memory_order_relaxed) : 0.0f;
    }

    // ------------------------------------------------------- audio-thread input
    void noteOn (int note, int velocity)
    {
        srcVelocity_.store ((float) velocity / 127.0f, std::memory_order_relaxed);
        srcKeyTrack_.store ((float) juce::jlimit (0, 127, note) / 127.0f, std::memory_order_relaxed);
        srcRandom_.store (rng_.nextFloat(), std::memory_order_relaxed);
        const int held = heldNotes_.fetch_add (1, std::memory_order_relaxed) + 1;
        noteEvent_.store (held == 1 ? 2 : 1, std::memory_order_relaxed);   // 2 = first note
    }
    void noteOff()
    {
        int h = heldNotes_.load (std::memory_order_relaxed);
        while (h > 0 && ! heldNotes_.compare_exchange_weak (h, h - 1, std::memory_order_relaxed)) {}
    }
    void allNotesOff() { heldNotes_.store (0, std::memory_order_relaxed); }
    void setAftertouch (float n)  { srcAftertouch_.store (n, std::memory_order_relaxed); }
    void setModWheel  (float n)   { srcModWheel_.store (n, std::memory_order_relaxed); }
    void setExpression (float n)  { srcExpression_.store (n, std::memory_order_relaxed); }
    void setMacroBase (int i, float v)
    {
        if (i >= 0 && i < 4) macroBase_[(size_t) i].store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed);
    }

    void prepare (double sr) { sr_ = sr; for (auto& l : lfos_) l.reset(); }

    /// True when at least one row is live — lets processBlock keep its whole-block
    /// fast path when nothing is modulated.
    bool active() const { return liveTable().rowCount > 0; }

    // --------------------------------------------------------------- the block
    /// One control block. Sources are evaluated in a FIXED ORDER — macros, MIDI /
    /// note sources, then LFO 1, 2, 3 — before any DSP consumes them, using the
    /// PREVIOUS block's value for any backward reference. That is Vital's
    /// cr::Feedback behaviour (research section 1.4) with none of the machinery, and it
    /// makes a cross-modulation cycle cost one control block (~1.5 ms) of latency
    /// instead of hanging.
    void process (const TransportClock& clock, int numSamples, int blockOffset, ModApplier& applier)
    {
        const ModTable& t = liveTable();

        // ---- 1. macros and MIDI/note sources
        for (int i = 0; i < 4; ++i)
            src_[(size_t) ModSource::macro1 + (size_t) i] =
                juce::jlimit (0.0f, 1.0f, macroBase_[(size_t) i].load (std::memory_order_relaxed)
                                              + hostOffset_[(size_t) hmMacro1 + (size_t) i]);
        src_[(size_t) ModSource::velocity]   = srcVelocity_.load (std::memory_order_relaxed);
        src_[(size_t) ModSource::aftertouch] = srcAftertouch_.load (std::memory_order_relaxed);
        src_[(size_t) ModSource::modWheel]   = srcModWheel_.load (std::memory_order_relaxed);
        src_[(size_t) ModSource::expression] = srcExpression_.load (std::memory_order_relaxed);
        src_[(size_t) ModSource::keyTrack]   = srcKeyTrack_.load (std::memory_order_relaxed);
        src_[(size_t) ModSource::random]     = srcRandom_.load (std::memory_order_relaxed);
        src_[(size_t) ModSource::none]       = 0.0f;

        // ---- 2. retrigger edges (global LFOs: the 0 -> 1 held-note transition)
        const int ev = noteEvent_.exchange (0, std::memory_order_relaxed);
        if (ev != 0)
        {
            const bool firstNote = (ev == 2);
            for (int i = 0; i < kNumLfos; ++i)
            {
                const auto rt = (LfoRetrigger) t.lfo[(size_t) i].retrigger;
                const bool restartPhase = rt == LfoRetrigger::everyNote
                                       || (rt == LfoRetrigger::firstNote && firstNote);
                if (restartPhase)      lfos_[(size_t) i].retrigger (clock, firstNote);
                else if (firstNote)    lfos_[(size_t) i].armFade();   // Free: fade only
            }
        }

        // ---- 3. LFOs (after macros / MIDI, per the fixed evaluation order)
        const double ppqOffset = (double) blockOffset * (clock.bpm / 60.0) / juce::jmax (1.0, sr_);
        for (int i = 0; i < kNumLfos; ++i)
            src_[(size_t) ModSource::lfo1 + (size_t) i] =
                lfos_[(size_t) i].process (t.lfo[(size_t) i], clock, sr_, numSamples,
                                           ppqOffset, lfoRateOffset_[(size_t) i]);

        // ---- 4. rows -> per-destination sums (research section 1.2: additive, then clamp
        //         at the destination's own range; summed in NORMALISED space so the
        //         parameter's taper is applied exactly once, afterwards)
        for (int g = 0; g < t.groupCount; ++g) sum_[(size_t) g] = 0.0f;

        for (int r = 0; r < t.rowCount; ++r)
        {
            const auto& row = t.rows[(size_t) r];
            const auto srcEnum = (ModSource) row.source;
            float depth = row.depth;
            if (row.matrixIndex >= 0 && row.matrixIndex < kMaxMatrixRows)
                depth = juce::jlimit (-1.0f, 1.0f, depth + rowDepthOffset_[(size_t) row.matrixIndex]);

            const float off = modmath::modOffset (src_[(size_t) row.source],
                                                  modSourceIsBipolar (srcEnum),
                                                  depth, row.lo, row.hi, row.curve, row.invert != 0);
            rowOffset_[(size_t) r].store (off, std::memory_order_relaxed);   // UI readback
            if (row.group >= 0 && row.group < t.groupCount) sum_[(size_t) row.group] += off;
        }

        // ---- 5. apply
        for (int i = 0; i < kNumLfos; ++i) nextLfoRateOffset_[(size_t) i] = 0.0f;
        for (int i = 0; i < kMaxMatrixRows; ++i) nextRowDepthOffset_[(size_t) i] = 0.0f;
        for (int i = 0; i < hmCount; ++i) hostOffset_[(size_t) i] = 0.0f;

        for (int g = 0; g < t.groupCount; ++g)
        {
            const auto& d = t.groups[(size_t) g];
            const float s = sum_[(size_t) g];
            switch ((ModDestKind) d.kind)
            {
                case ModDestKind::hostParam:
                    if (d.unit >= 0 && d.unit < hmCount) hostOffset_[(size_t) d.unit] = s;
                    break;
                case ModDestKind::lfoRate:
                    if (d.unit >= 0 && d.unit < kNumLfos) nextLfoRateOffset_[(size_t) d.unit] = s;
                    break;
                case ModDestKind::rowDepth:
                    if (d.unit >= 0 && d.unit < kMaxMatrixRows) nextRowDepthOffset_[(size_t) d.unit] = s;
                    break;
                default:
                    applier.applyModulatedValue (d, juce::jlimit (0.0f, 1.0f, groupBase (g) + s));
                    break;
            }
            groupSum_[(size_t) g].store (s, std::memory_order_relaxed);
        }
        // one-block latch for the cross-modulation destinations
        for (int i = 0; i < kNumLfos; ++i) lfoRateOffset_[(size_t) i] = nextLfoRateOffset_[(size_t) i];
        for (int i = 0; i < kMaxMatrixRows; ++i) rowDepthOffset_[(size_t) i] = nextRowDepthOffset_[(size_t) i];
    }

    /// Normalised offset for a continuous host parameter (applied at point of use
    /// so the stored/automated base value is never written).
    float hostOffset (int target) const
    {
        return target >= 0 && target < hmCount ? hostOffset_[(size_t) target] : 0.0f;
    }

    /// UI readback: the offset a published row is contributing right now.
    float rowOffset (int publishedRow) const
    {
        return publishedRow >= 0 && publishedRow < kMaxModLinks
                   ? rowOffset_[(size_t) publishedRow].load (std::memory_order_relaxed) : 0.0f;
    }
    float lfoValue (int i) const { return i >= 0 && i < kNumLfos ? lfos_[(size_t) i].value() : 0.0f; }
    float sourceValue (int s) const
    {
        return s > 0 && s < kNumModSources ? src_[(size_t) s] : 0.0f;
    }

private:
    std::array<ModTable, 2> tables_ {};
    std::atomic<int> live_ { 0 };
    std::array<std::atomic<float>, kMaxModLinks> groupBase_ {};
    std::array<std::atomic<float>, kMaxModLinks> groupSum_ {};
    std::array<std::atomic<float>, kMaxModLinks> rowOffset_ {};

    std::array<GlobalLfo, kNumLfos> lfos_;
    std::array<float, kNumModSources> src_ {};
    std::array<float, kMaxModLinks> sum_ {};
    std::array<float, hmCount> hostOffset_ {};
    std::array<float, kNumLfos> lfoRateOffset_ {}, nextLfoRateOffset_ {};
    std::array<float, kMaxMatrixRows> rowDepthOffset_ {}, nextRowDepthOffset_ {};

    std::array<std::atomic<float>, 4> macroBase_ {};
    std::atomic<float> srcVelocity_ { 0 }, srcAftertouch_ { 0 }, srcModWheel_ { 0 },
                       srcExpression_ { 0 }, srcKeyTrack_ { 0 }, srcRandom_ { 0 };
    std::atomic<int> heldNotes_ { 0 }, noteEvent_ { 0 };
    juce::Random rng_ { 0x1a2b3c4d };
    double sr_ = 48000.0;
};

} // namespace jm
