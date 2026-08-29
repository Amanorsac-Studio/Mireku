// Arp.h — JM Signature modulation PHASE 2: the arpeggiator.
//
// Implements docs/JM-MODULATION-SPEC.html section E against the decisions in
// docs/MODULATION-RESEARCH.md section 3. ONE GLOBAL ARP with per-slot enable
// (spec decision 2): a slot with the arp off receives the same held chord as
// played, so the same keys can be arpeggiated into slot C while A and B sustain.
//
// The four rules that shape everything here, all from that research:
//
//   1. STEP POSITIONS ARE DERIVED FROM ppq, not counted in samples
//      (research section 3.7 / trap 2). `ppqStep = k*stepQuarters + swing` makes the
//      pattern land on the beat when the transport rolls, survives a locate and a
//      loop wrap with no repair, and absorbs a mid-block tempo change to within
//      one block. The clock is Phase 1's TransportClock — there is exactly one
//      transport reader in this plugin.
//
//   2. THE ARP CONSUMES THE HELD NOTES; the sampler never sees them
//      (research section 3.8). The routing fork happens AFTER the channel/device slot
//      mask is resolved, so one note can be arpeggiated into one slot and played
//      straight into another.
//
//   3. THE PEDAL HOLDS THE KEYS, NOT THE SOUND (Yamaha, research section 3.6). CC64 is
//      consumed by the arp for arp-driven slots and never reaches
//      SamplerEngine::sustainPedal() for them; slots with the arp off keep the
//      unchanged JM-BUILD-SPEC section 5.4 behaviour.
//
//   4. NO STUCK NOTES (research section 3.8 / trap 12). Every generated note-on is
//      tracked as an exact (note, slot mask) pair and flushed explicitly on every
//      discontinuity — arp off, latch released, transport stop, transport jump,
//      preset load, panic. CC 123 is never used as a substitute.
//
// Real-time discipline matches Phase 1: fixed-size arrays, no allocation, no
// locks and no juce::String on the audio thread. Settings cross from the message
// thread through the same double-buffered POD + atomic index idiom the mod table
// uses (research section 4.1).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "FxParams.h"
#include "Modulation.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace jm {

//==============================================================================
// The spec's seven modes. The Up/Down TURNAROUND is not two more modes but an
// explicit Inclusive/Exclusive toggle (research section 3.1, Arturia's names),
// defaulting to Exclusive.
enum class ArpMode : int { up = 0, down, upDown, downUp, asPlayed, random, chord, count };
constexpr int kNumArpModes = (int) ArpMode::count;

inline const char* arpModeName (int i)
{
    static const char* n[] = { "UP", "DOWN", "UP/DOWN", "DOWN/UP", "AS PLAYED", "RANDOM", "CHORD" };
    return (i >= 0 && i < kNumArpModes) ? n[i] : n[0];
}

/// Octave expansion direction. The expansion itself is Bitwig's "Thin" —
/// flatten notes x octaves into ONE linear shape, then order it (research section 3.3).
enum class ArpOctDir : int { up = 0, down, both, count };
inline const char* arpOctDirName (int i)
{
    static const char* n[] = { "UP", "DOWN", "BOTH" };
    return (i >= 0 && i < 3) ? n[i] : n[0];
}

enum class ArpVelMode : int { asPlayed = 0, fixed, accent, count };
inline const char* arpVelModeName (int i)
{
    static const char* n[] = { "AS PLAYED", "FIXED", "ACCENT" };
    return (i >= 0 && i < 3) ? n[i] : n[0];
}

constexpr int kMaxArpHeld     = 32;    // physical keys / latched notes
constexpr int kMaxArpExpanded = 128;   // held x octaves, after flattening
constexpr int kMaxArpPattern  = 256;   // the full cycle (Up/Down inclusive = 2n)
constexpr int kMaxArpSounding = 128;   // generated note-ons awaiting their note-off
constexpr float kArpSwingMax  = 75.0f;
constexpr float kArpRateMin   = 0.1f, kArpRateMax = 40.0f;   // free-run Hz (standalone)

/// The arp's synced rate list: the spec's 1/1 ... 1/32 including dotted and
/// triplet, expressed as indices into the canonical append-only division table
/// in FxParams.h. Slowest first, so the menu reads like a rate control.
inline const std::vector<int>& arpDivisionOrder()
{
    static const std::vector<int> o {
        18, 19, 20,   // 1/1  1/1d  1/1t
        7, 16, 17,    // 1/2  1/2d  1/2t
        5, 6, 15,     // 1/4  1/4d  1/4t
        3, 4, 14,     // 1/8  1/8d  1/8t
        1, 2, 13,     // 1/16 1/16d 1/16t
        0, 11, 12 };  // 1/32 1/32d 1/32t
    return o;
}

//==============================================================================
/// SWING, defined once so it can never mean three things (research section 3.4 /
/// trap 9 flagged the spec's bare "0-75%" as ambiguous across the MPC 50-75
/// scale, a 0-100 amount and a raw fraction of the step).
///
/// THE DEFINITION: the control displays 0-75%, and the offbeat step's position
/// within its pair is
///
///     p = 0.5 + swing/300
///
/// so 0% = straight (p = 0.5), 50% = triplet feel (p = 2/3), 75% = the MPC's
/// documented ceiling (p = 0.75). The extra delay applied to the offbeat step is
/// therefore (2p-1) = swing/150 of one step, and is always < one step.
inline double arpSwingPairPosition (double swingPercent)
{
    return 0.5 + juce::jlimit (0.0, (double) kArpSwingMax, swingPercent) / 300.0;
}
inline double arpSwingDelayFraction (double swingPercent)
{
    return 2.0 * arpSwingPairPosition (swingPercent) - 1.0;      // == swing/150
}

//==============================================================================
/// Serialisable arp settings. Trivially copyable so it can ride in the published
/// audio-thread buffer.
struct ArpSettings
{
    int   enabled   = 0;
    int   slotMask  = 0x0F;                       // per-slot enable, bit i = Perform slot A..D
    int   mode      = (int) ArpMode::up;
    int   sync      = 1;
    int   division  = 3;                          // "1/8" in the canonical table
    float rateHz    = 6.0f;                       // free-run rate (standalone / sync off)
    float gate      = 50.0f;                      // 1..100 % of the step
    int   octaves   = 1;                          // 1..4
    int   octDir    = (int) ArpOctDir::up;
    float swing     = 0.0f;                       // displayed 0..75, see arpSwingPairPosition()
    int   hold      = 0;                          // manual latch; ORs with the pedal
    int   velMode   = (int) ArpVelMode::asPlayed;
    int   fixedVel  = 100;                        // 1..127
    int   accentMask = 0x1;                       // 4-step accent pattern, bit 0 = step 1
    int   inclusive = 0;                          // Up/Down turnaround; 0 = EXCLUSIVE (default)

    bool runsOnAnySlot() const { return enabled != 0 && (slotMask & 0x0F) != 0; }
    /// Fields that change the note ORDER, so the pattern must be re-derived.
    bool orderingDiffers (const ArpSettings& o) const
    {
        return mode != o.mode || octaves != o.octaves || octDir != o.octDir || inclusive != o.inclusive;
    }
};

//==============================================================================
/// One generated event. `mask` is the slot mask the source key was routed to,
/// intersected with the arp's per-slot enable — so an arpeggiated note reaches
/// exactly the slots the played note would have reached.
struct ArpEvent
{
    int note = 0;
    int velocity = 0;
    juce::uint8 mask = 0;
    bool noteOn = false;
};

//==============================================================================
/// The arpeggiator. All state below is audio-thread private; the only thing
/// crossing from the message thread is an ArpSettings POD.
class ArpEngine
{
public:
    struct Note { int note = 0; int vel = 0; juce::uint8 mask = 0; int order = 0; };

    // ------------------------------------------------ message-thread publishing
    ArpSettings& editBuffer() { return buf_[1 - live_.load (std::memory_order_relaxed)]; }
    void publish() { live_.store (1 - live_.load (std::memory_order_relaxed), std::memory_order_release); }
    const ArpSettings& settings() const { return buf_[(size_t) live_.load (std::memory_order_acquire)]; }

    void prepare (double sr) { sr_ = juce::jmax (1.0, sr); hardReset(); }

    /// Everything except the settings buffer. Used by prepareToPlay and by a
    /// preset load, which is a discontinuity (research section 3.8).
    void hardReset()
    {
        heldCount_ = latchCount_ = 0;
        patternLen_ = 0;
        stepIndex_ = 0; bagPos_ = 0; bagLen_ = 0;
        for (auto& s : sounding_) s.active = false;
        pedalDown_ = false; latchActive_ = false;
        anchorPpq_ = 0.0; anchored_ = false; freePos_ = 0.0; freeK_ = 0;
        stepCount_ = 0; emitCursor_ = 0; wasPlaying_ = false;
    }

    // ------------------------------------------------------------- introspection
    int heldNoteCount() const { return heldCount_; }
    int latchedNoteCount() const { return latchCount_; }
    bool latchIsActive() const { return latchActive_; }
    int patternLength() const { return patternLen_; }
    int soundingCount() const { int n = 0; for (auto& s : sounding_) if (s.active) ++n; return n; }
    int patternNoteAt (int i) const { return (i >= 0 && i < patternLen_) ? pattern_[(size_t) i].note : -1; }
    int currentStepIndex() const { return stepIndex_; }
    float gateNow() const { return gateNow_; }
    float rateNow() const { return rateNow_; }

    //==========================================================================
    // MIDI in. All of these take a sink so that a discontinuity can flush its
    // note-offs at the exact sample offset it happened at.

    /// A key went down. `mask` is ALREADY intersected with the arp's per-slot
    /// enable by the caller, and is never 0 here. `offset` is the sample position
    /// inside the current block.
    template <typename Sink>
    void noteOn (int note, int vel, juce::uint8 mask, int offset, Sink&& sink)
    {
        const bool wasSilent = effectiveCount() == 0;

        // RULE 3 (the new-phrase rule): with latch engaged and every physical key
        // up, the next note-on CLEARS the latched set and starts a new chord; if
        // any key is still down the note is ADDED. This is Ableton's Hold rule,
        // Sequential's "relatch" and Logic's Reset/Add boundary in one rule.
        if (latchActive_)
        {
            if (heldCount_ == 0) { latchCount_ = 0; flushAll (sink); }
            addTo (latched_, latchCount_, note, vel, mask);
        }
        addTo (held_, heldCount_, note, vel, mask);

        rebuildPattern();
        if (wasSilent && patternLen_ > 0) startPhrase (offset);
    }

    /// A key came up. With latch engaged the note STAYS in the pattern — the
    /// pedal holds the keys for you (research section 3.6).
    template <typename Sink>
    void noteOff (int note, Sink&& sink)
    {
        removeFrom (held_, heldCount_, note);
        if (! latchActive_)
        {
            rebuildPattern();
            if (patternLen_ == 0) { flushAll (sink); anchored_ = false; }   // RULE 4
        }
    }

    /// CC 64. RULES 2 and 4.
    template <typename Sink>
    void setPedal (bool down, Sink&& sink)
    {
        if (pedalDown_ == down) return;
        pedalDown_ = down;
        updateLatch (pedalDown_ || s_.hold != 0, sink);
    }

    template <typename Sink>
    void panic (Sink&& sink)
    {
        flushAll (sink);
        heldCount_ = latchCount_ = 0;
        pedalDown_ = false;
        latchActive_ = false;
        anchored_ = false;
        rebuildPattern();
    }

    //==========================================================================
    // The block.

    /// Called once per processBlock, BEFORE any sub-block. Latches the settings
    /// for the block, handles the transport edges, and computes every step onset
    /// that falls inside this block.
    ///
    /// `rateOffsetNorm` / `gateOffsetNorm` are the modulation matrix's normalised
    /// offsets for arp.rate and arp.gate (Phase 1's base+offset model: the stored
    /// settings are never written).
    template <typename Sink>
    void beginBlock (const TransportClock& clock, int numSamples,
                     float rateOffsetNorm, float gateOffsetNorm, Sink&& sink)
    {
        const ArpSettings next = settings();
        const bool reorder = next.orderingDiffers (s_);
        s_ = next;
        blockLen_ = numSamples;
        emitCursor_ = 0;
        stepCount_ = 0;
        inBlock_ = true;
        playing_ = clock.playing;
        blockPpq_ = clock.ppq;
        spq_ = sr_ * 60.0 / juce::jmax (1.0, clock.bpm);

        // Hold control and the pedal OR together (RULE 5).
        updateLatch (pedalDown_ || s_.hold != 0, sink);
        if (reorder) rebuildPattern();

        if (! s_.runsOnAnySlot())
        {
            flushAll (sink);                    // arp switched off -> never a stuck note
            heldCount_ = latchCount_ = 0;
            patternLen_ = 0;
            anchored_ = false;
            wasPlaying_ = clock.playing;
            return;
        }

        // RULE 6 / research section 3.7: flush IN THE SAME BLOCK the stop is detected,
        // because some hosts stop calling processBlock straight afterwards. A
        // transport stop also drops the latch (Omnisphere's precedent).
        if (wasPlaying_ && ! clock.playing)
        {
            flushAll (sink);
            latchCount_ = 0;
            latchActive_ = pedalDown_ || s_.hold != 0;
            if (latchActive_) snapshotLatch();
            rebuildPattern();
            anchored_ = false;
        }
        // A locate or a loop wrap: kill what is sounding and recompute k from the
        // new ppq — but DO NOT restart the pattern index (research section 3.2 / trap 8).
        if (clock.jumped) flushAll (sink);
        wasPlaying_ = clock.playing;

        // Gate and rate are modulation destinations (base + offset; the stored
        // setting is never written). Rate modulation applies to the FREE rate —
        // see the panel caption and DECISIONS-signature.md.
        gateNow_ = juce::jlimit (1.0f, 100.0f, s_.gate + gateOffsetNorm * 99.0f);
        rateNow_ = rateFromNorm (juce::jlimit (0.0f, 1.0f, rateNormFor (s_.rateHz) + rateOffsetNorm));

        swingFrac_ = arpSwingDelayFraction (s_.swing);
        stepQ_ = juce::jmax (1.0e-4, divisionBeats (s_.division));
        stepSamples_ = juce::jmax (8.0, sr_ / juce::jmax (0.001, (double) rateNow_));

        scheduleSteps (0);
    }

    /// The sample offset of the next arp event in this block at or after the emit
    /// cursor, or `blockLen` if there is none. This is what drives the sub-block
    /// split in the render path.
    int nextEventOffset() const
    {
        int best = blockLen_;
        for (int i = 0; i < stepCount_; ++i)
        {
            const auto& st = steps_[(size_t) i];
            if (! st.fired && st.offset >= emitCursor_) { best = juce::jmin (best, st.offset); break; }
        }
        for (auto& s : sounding_)
            if (s.active && s.offAt >= emitCursor_ && s.offAt < best) best = s.offAt;
        return best;
    }

    /// Emit every event strictly before `limit`. Note-offs go first: at a 100%
    /// gate the off of step k lands exactly on the on of step k+1 and must be
    /// delivered before it (research section 3.5, NI's documented ordering).
    template <typename Sink>
    void emitDue (int limit, Sink&& sink)
    {
        limit = juce::jmin (limit, blockLen_);
        for (auto& s : sounding_)
            if (s.active && s.offAt < limit)
            {
                sink (ArpEvent { s.note, 0, s.mask, false });
                s.active = false;
            }
        for (int i = 0; i < stepCount_; ++i)
        {
            auto& st = steps_[(size_t) i];
            if (st.fired || st.offset >= limit) continue;
            st.fired = true;
            fireStep (st, sink);
        }
        emitCursor_ = juce::jmax (emitCursor_, limit);
    }

    /// End of the host block: carry the pending note-off deadlines over.
    void endBlock()
    {
        for (auto& s : sounding_) if (s.active) s.offAt -= blockLen_;
        if (s_.sync == 0 && anchored_) freePos_ += (double) blockLen_;
        emitCursor_ = 0;
        stepCount_ = 0;
        inBlock_ = false;
    }

    /// Log-tapered free rate, the same shape the LFO uses.
    static float rateNormFor (float hz)
    {
        hz = juce::jlimit (kArpRateMin, kArpRateMax, hz);
        return (float) (std::log (hz / kArpRateMin) / std::log (kArpRateMax / kArpRateMin));
    }
    static float rateFromNorm (float n)
    {
        n = juce::jlimit (0.0f, 1.0f, n);
        return kArpRateMin * std::pow (kArpRateMax / kArpRateMin, n);
    }

    //==========================================================================
    /// Pattern derivation. Static and pure, so the ordering tests drive exactly
    /// the function the audio thread runs — no processor, transport or sample
    /// rate involved.
    static int buildPattern (const Note* in, int n, const ArpSettings& s,
                             Note* out, int outCapacity)
    {
        if (in == nullptr || n <= 0 || outCapacity <= 0) return 0;

        // ---- 1. octave expansion, Bitwig "Thin": flatten notes x octaves FIRST,
        //         then order the result as one linear shape (research section 3.3).
        //         Every direction produces exactly `octaves` transpositions, so the
        //         cycle length does not change with the direction.
        const int oct = juce::jlimit (1, 4, s.octaves);
        int offs[4] {};
        for (int i = 0; i < oct; ++i)
        {
            switch ((ArpOctDir) juce::jlimit (0, 2, s.octDir))
            {
                case ArpOctDir::up:   offs[i] = 12 * i; break;
                case ArpOctDir::down: offs[i] = -12 * i; break;
                default:              offs[i] = 12 * (i - (oct - 1) / 2); break;   // centred
            }
        }

        Note ex[kMaxArpExpanded];
        int m = 0;
        for (int i = 0; i < n && m < kMaxArpExpanded; ++i)
            for (int o = 0; o < oct && m < kMaxArpExpanded; ++o)
            {
                const int nn = in[i].note + offs[o];
                if (nn < 0 || nn > 127) continue;
                ex[m] = in[i];
                ex[m].note = nn;
                ex[m].order = in[i].order * 8 + o;      // as-played: key order, then octave
                ++m;
            }
        if (m == 0) return 0;

        const auto mode = (ArpMode) juce::jlimit (0, kNumArpModes - 1, s.mode);

        // ---- 2. ordering
        auto byPitch = [] (const Note& a, const Note& b)
        { return a.note != b.note ? a.note < b.note : a.order < b.order; };
        auto byOrder = [] (const Note& a, const Note& b)
        { return a.order != b.order ? a.order < b.order : a.note < b.note; };

        if (mode == ArpMode::asPlayed) std::stable_sort (ex, ex + m, byOrder);
        else                           std::stable_sort (ex, ex + m, byPitch);

        int len = 0;
        auto push = [&] (const Note& x) { if (len < outCapacity) out[len++] = x; };

        switch (mode)
        {
            case ArpMode::up:
            case ArpMode::asPlayed:
            case ArpMode::random:                    // the bag is drawn from this set
            case ArpMode::chord:                     // one step; the caller plays all of it
                for (int i = 0; i < m; ++i) push (ex[i]);
                break;
            case ArpMode::down:
                for (int i = m - 1; i >= 0; --i) push (ex[i]);
                break;
            case ArpMode::upDown:
            case ArpMode::downUp:
            {
                const bool startLow = (mode == ArpMode::upDown);
                // The turnaround. Inclusive repeats the extremes (cycle 2n);
                // Exclusive does not (cycle 2n-2), which DEGENERATES at n = 1 and
                // n = 2 and is special-cased here (research section 3.1 / trap 7).
                if (m == 1) { push (ex[0]); break; }
                if (s.inclusive == 0 && m == 2)
                {
                    push (ex[startLow ? 0 : 1]);
                    push (ex[startLow ? 1 : 0]);
                    break;
                }
                for (int i = 0; i < m; ++i) push (startLow ? ex[i] : ex[m - 1 - i]);
                if (s.inclusive != 0)
                    for (int i = 0; i < m; ++i) push (startLow ? ex[m - 1 - i] : ex[i]);
                else
                    for (int i = m - 2; i >= 1; --i) push (startLow ? ex[i] : ex[m - 1 - i]);
                break;
            }
            default: break;
        }
        return len;
    }

    /// Test/UI hook: force the settings the audio thread uses without going
    /// through a block (the ordering tests and the panel preview use it).
    void setSettingsDirect (const ArpSettings& s) { s_ = s; rebuildPattern(); }
    const ArpSettings& activeSettings() const { return s_; }

private:
    struct Step { long long k = 0; int offset = 0; int interval = 0; bool fired = false; };
    struct Sound { int note = 0; juce::uint8 mask = 0; int offAt = 0; bool active = false; };

    // ---------------------------------------------------------- held-note sets
    void addTo (std::array<Note, kMaxArpHeld>& a, int& count, int note, int vel, juce::uint8 mask)
    {
        for (int i = 0; i < count; ++i)
            if (a[(size_t) i].note == note)          // re-struck key: refresh, keep its place
            { a[(size_t) i].vel = vel; a[(size_t) i].mask = (juce::uint8) (a[(size_t) i].mask | mask); return; }
        if (count >= kMaxArpHeld) return;
        a[(size_t) count] = Note { note, vel, mask, ++orderSeq_ };
        ++count;
    }
    static void removeFrom (std::array<Note, kMaxArpHeld>& a, int& count, int note)
    {
        for (int i = 0; i < count; ++i)
            if (a[(size_t) i].note == note)
            {
                for (int j = i; j < count - 1; ++j) a[(size_t) j] = a[(size_t) j + 1];
                --count;
                return;
            }
    }
    int effectiveCount() const { return latchActive_ ? latchCount_ : heldCount_; }

    void snapshotLatch()
    {
        latchCount_ = heldCount_;
        for (int i = 0; i < heldCount_; ++i) latched_[(size_t) i] = held_[(size_t) i];
    }

    /// RULES 2, 4 and 5: the pedal and the Hold control OR together; the rising
    /// edge captures the keys physically held right now, the falling edge hands
    /// the pattern back to the physical keys and stops if there are none.
    template <typename Sink>
    void updateLatch (bool wanted, Sink&& sink)
    {
        if (wanted == latchActive_) return;
        latchActive_ = wanted;
        if (wanted) snapshotLatch();
        else        latchCount_ = 0;
        rebuildPattern();
        if (patternLen_ == 0) { flushAll (sink); anchored_ = false; }
    }

    void rebuildPattern()
    {
        const auto& src = latchActive_ ? latched_ : held_;
        const int n = effectiveCount();
        const int before = patternLen_;
        patternLen_ = buildPattern (src.data(), n, s_, pattern_.data(), kMaxArpPattern);

        // Notes added or removed mid-pattern: re-derive and CLAMP the index.
        // Never restart implicitly (research section 3.2 / trap 8).
        stepIndex_ = patternLen_ > 0 ? (before > 0 ? stepIndex_ % patternLen_ : 0) : 0;
        bagLen_ = 0; bagPos_ = 0;
    }

    /// The first note of a phrase while the transport is NOT rolling: free-run
    /// from here (Korg's documented Key Sync semantics, research section 3.7), and
    /// schedule the rest of this block from the same instant so a long buffer
    /// does not swallow the first few steps.
    void startPhrase (int offset)
    {
        stepIndex_ = 0;
        bagLen_ = 0; bagPos_ = 0;
        if (playing_) return;                  // rolling: the grid owns the timing
        anchored_ = true;
        anchorPpq_ = blockPpq_ + (double) offset / juce::jmax (1.0, spq_);
        freePos_ = -(double) offset;
        freeK_ = 0;
        scheduleSteps (offset);
    }

    /// Append every step onset in [fromOffset, blockLen). Synced steps come from
    /// ppq (research section 3.7); free steps from the sample clock.
    void scheduleSteps (int fromOffset)
    {
        // `inBlock_` guards the case where a note arrives outside a render block
        // (never in the plugin, but the tests drive the engine directly): the
        // schedule belongs to the CURRENT block or to none at all.
        if (! inBlock_ || ! s_.runsOnAnySlot() || blockLen_ <= 0) return;

        if (s_.sync != 0)
        {
            // While the transport plays the grid is anchored to the TIMELINE
            // ORIGIN, so a pattern lands on the beat the instant the transport
            // rolls. Stopped / standalone: the anchor is the first note.
            const double base = playing_ ? 0.0 : (anchored_ ? anchorPpq_ : 0.0);
            const double startQ = blockPpq_ - base + (double) fromOffset / spq_;
            const double endQ = blockPpq_ - base + (double) blockLen_ / spq_;
            if (! playing_ && ! anchored_) return;      // nothing held yet: no free-run grid

            long long k = (long long) std::floor (startQ / stepQ_) - 1;
            for (int guard = 0; guard < 2048; ++guard, ++k)
            {
                const double q = onsetQ (k);
                if (q >= endQ) break;
                if (q < startQ) continue;
                pushStep (k, (int) ((q - (blockPpq_ - base)) * spq_),
                          (int) std::llround ((onsetQ (k + 1) - q) * spq_));
            }
        }
        else
        {
            if (! anchored_) return;                    // free-run starts at the first note
            long long k = freeK_;
            for (int guard = 0; guard < 2048; ++guard, ++k)
            {
                const double s = onsetS (k) - freePos_;
                if (s >= (double) blockLen_) { freeK_ = k; break; }
                if (s < (double) fromOffset) { freeK_ = k + 1; continue; }
                pushStep (k, (int) s, (int) std::llround (onsetS (k + 1) - onsetS (k)));
                freeK_ = k + 1;
            }
        }
    }

    double onsetQ (long long k) const
    {
        return (double) k * stepQ_ + ((k & 1) ? stepQ_ * swingFrac_ : 0.0);
    }
    double onsetS (long long k) const
    {
        return (double) k * stepSamples_ + ((k & 1) ? stepSamples_ * swingFrac_ : 0.0);
    }

    void pushStep (long long k, int offset, int interval)
    {
        if (stepCount_ >= (int) steps_.size()) return;
        steps_[(size_t) stepCount_++] = Step { k, juce::jlimit (0, juce::jmax (0, blockLen_ - 1), offset),
                                               juce::jmax (2, interval), false };
    }

    template <typename Sink>
    void flushAll (Sink&& sink)
    {
        for (auto& s : sounding_)
            if (s.active) { sink (ArpEvent { s.note, 0, s.mask, false }); s.active = false; }
    }

    int velocityFor (const Note& n, long long k) const
    {
        switch ((ArpVelMode) juce::jlimit (0, 2, s_.velMode))
        {
            case ArpVelMode::fixed:  return juce::jlimit (1, 127, s_.fixedVel);
            case ArpVelMode::accent:
            {
                const int slot = (int) (((k % 4) + 4) % 4);
                const int v = juce::jlimit (1, 127, s_.fixedVel);
                return (s_.accentMask & (1 << slot)) != 0
                           ? v : juce::jlimit (1, 127, (int) std::lround (v * 0.55));
            }
            default: return juce::jlimit (1, 127, n.vel);
        }
    }

    template <typename Sink>
    void fireStep (const Step& st, Sink&& sink)
    {
        if (patternLen_ <= 0) return;
        const auto mode = (ArpMode) juce::jlimit (0, kNumArpModes - 1, s_.mode);

        // Gate: a percentage of the step (research section 3.5). A note-off may never
        // land on the same sample as its own note-on (trap 10), and at 100% it
        // lands on the next note-on and is emitted strictly before it.
        const int gateSamples = juce::jlimit (1, juce::jmax (1, st.interval),
                                              (int) std::lround (st.interval * (double) gateNow_ * 0.01));

        auto play = [&] (const Note& n)
        {
            const int vel = velocityFor (n, st.k);
            for (auto& s : sounding_)                 // retrigger of the same pitch: off first
                if (s.active && s.note == n.note && s.mask == n.mask)
                { sink (ArpEvent { s.note, 0, s.mask, false }); s.active = false; }
            sink (ArpEvent { n.note, vel, n.mask, true });
            for (auto& s : sounding_)
                if (! s.active) { s = Sound { n.note, n.mask, st.offset + gateSamples, true }; return; }
        };

        if (mode == ArpMode::chord)
        {
            for (int i = 0; i < patternLen_; ++i) play (pattern_[(size_t) i]);
            return;
        }
        if (mode == ArpMode::random)
        {
            play (pattern_[(size_t) drawFromBag()]);
            return;
        }
        stepIndex_ %= patternLen_;
        play (pattern_[(size_t) stepIndex_]);
        stepIndex_ = (stepIndex_ + 1) % patternLen_;
    }

    /// Ableton's "Random Other": a shuffled bag, refilled when exhausted. Pure
    /// random repeats notes and sounds broken on 2- and 3-note chords
    /// (research section 3.1).
    int drawFromBag()
    {
        if (bagPos_ >= bagLen_)
        {
            bagLen_ = juce::jmin (patternLen_, kMaxArpPattern);
            for (int i = 0; i < bagLen_; ++i) bag_[(size_t) i] = i;
            for (int i = bagLen_ - 1; i > 0; --i)
                std::swap (bag_[(size_t) i], bag_[(size_t) rng_.nextInt (i + 1)]);
            bagPos_ = 0;
        }
        return bag_[(size_t) bagPos_++];
    }

    std::array<ArpSettings, 2> buf_ {};
    std::atomic<int> live_ { 0 };
    ArpSettings s_ {};

    double sr_ = 48000.0, spq_ = 24000.0, blockPpq_ = 0.0;
    double stepQ_ = 0.5, stepSamples_ = 8000.0, swingFrac_ = 0.0;
    int blockLen_ = 0, emitCursor_ = 0, stepCount_ = 0;
    bool playing_ = false, inBlock_ = false;
    std::array<Step, 64> steps_ {};

    std::array<Note, kMaxArpHeld> held_ {}, latched_ {};
    int heldCount_ = 0, latchCount_ = 0, orderSeq_ = 0;
    std::array<Note, kMaxArpPattern> pattern_ {};
    int patternLen_ = 0, stepIndex_ = 0;
    std::array<int, kMaxArpPattern> bag_ {};
    int bagLen_ = 0, bagPos_ = 0;

    std::array<Sound, kMaxArpSounding> sounding_ {};

    bool pedalDown_ = false, latchActive_ = false, wasPlaying_ = false, anchored_ = false;
    double anchorPpq_ = 0.0, freePos_ = 0.0;
    long long freeK_ = 0;
    float gateNow_ = 50.0f, rateNow_ = 6.0f;
    juce::Random rng_ { 0x27ab99f1 };
};

} // namespace jm
