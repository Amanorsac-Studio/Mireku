// ChordDetect.h — real-time held-note chord analysis (SANCTUARY-BUILD-SPEC.md §4.3).
//
// Audio thread: noteOn/noteOff/advance. Publishes { root, quality, bass, stable }
// through atomics. A chord must hold for kStableMs before it is reported so a
// passing tone does not move the Drone.
#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <array>
#include <cstdint>

namespace sanctuary {

enum class ChordQuality : int { none = 0, major, minor, sus2, sus4, dom7, maj7, min7, dim, aug, power };

inline const char* chordQualitySuffix (ChordQuality q)
{
    switch (q)
    {
        case ChordQuality::major: return "";
        case ChordQuality::minor: return "m";
        case ChordQuality::sus2: return "sus2";
        case ChordQuality::sus4: return "sus4";
        case ChordQuality::dom7: return "7";
        case ChordQuality::maj7: return "maj7";
        case ChordQuality::min7: return "m7";
        case ChordQuality::dim: return "dim";
        case ChordQuality::aug: return "aug";
        case ChordQuality::power: return "5";
        default: return "";
    }
}
/// The colour tone the Drone should voice for this quality (semitones above root), −1 = none.
inline int chordColourTone (ChordQuality q)
{
    switch (q)
    {
        case ChordQuality::major: return 4;
        case ChordQuality::minor: return 3;
        case ChordQuality::dom7: return 10;
        case ChordQuality::maj7: return 11;
        case ChordQuality::min7: return 3;
        case ChordQuality::sus2: return 2;
        case ChordQuality::sus4: return 5;
        case ChordQuality::dim: return 3;
        case ChordQuality::aug: return 4;
        default: return -1;
    }
}
inline bool chordIsMinorish (ChordQuality q) { return q == ChordQuality::minor || q == ChordQuality::min7 || q == ChordQuality::dim; }

struct ChordResult
{
    int root = -1;                    // pitch class, −1 = none
    ChordQuality quality = ChordQuality::none;
    int bass = -1;                    // pitch class of the lowest held note
    bool stable = false;
};

class ChordDetector
{
public:
    static constexpr int kStableMs = 120;

    void prepare (double sr) { sr_ = sr; held_.fill (0); heldCount_ = 0; stableSamples_ = 0; current_ = {}; publish ({}); }

    // ---------------------------------------------------------- audio thread
    void noteOn (int note) { if (note >= 0 && note < 128) { if (held_[(size_t) note]++ == 0) ++heldCount_; dirty_ = true; } }
    void noteOff (int note) { if (note >= 0 && note < 128 && held_[(size_t) note] > 0) { if (--held_[(size_t) note] == 0) --heldCount_; dirty_ = true; } }
    void allOff() { held_.fill (0); heldCount_ = 0; dirty_ = true; }

    /// Call once per block with the block length.
    void advance (int numSamples)
    {
        if (dirty_)
        {
            const ChordResult r = analyse();
            if (r.root != current_.root || r.quality != current_.quality)
            {
                current_ = r; current_.stable = false; stableSamples_ = 0;
            }
            else current_.bass = r.bass;
            dirty_ = false;
        }
        if (! current_.stable && current_.root >= 0)
        {
            stableSamples_ += numSamples;
            if (stableSamples_ >= (int) (sr_ * kStableMs / 1000.0)) { current_.stable = true; publish (current_); }
        }
        else if (current_.root < 0 && published_.load() != 0) publish (current_);
    }

    // -------------------------------------------------------- message thread
    ChordResult result() const
    {
        const auto v = published_.load();
        ChordResult r;
        r.root = (int) ((v >> 16) & 0xFF); if (r.root == 0xFF) r.root = -1;
        r.quality = (ChordQuality) ((v >> 8) & 0xFF);
        r.bass = (int) (v & 0xFF); if (r.bass == 0xFF) r.bass = -1;
        r.stable = r.root >= 0;
        return r;
    }

    /// Pure analysis of a pitch-class set — exposed for the unit tests.
    static ChordResult analysePitchClasses (uint32_t pcs, int bassPc)
    {
        ChordResult r; r.bass = bassPc;
        if (pcs == 0) return r;
        int count = 0; for (int i = 0; i < 12; ++i) if (pcs & (1u << i)) ++count;
        struct Tpl { ChordQuality q; uint32_t mask; int score; };
        static const Tpl tpls[] = {
            { ChordQuality::maj7, 0b100010010001, 9 }, { ChordQuality::dom7, 0b010010010001, 9 }, { ChordQuality::min7, 0b010010001001, 9 },
            { ChordQuality::major, 0b000010010001, 6 }, { ChordQuality::minor, 0b000010001001, 6 },
            { ChordQuality::sus4, 0b000010100001, 5 }, { ChordQuality::sus2, 0b000010000101, 5 },
            { ChordQuality::dim, 0b000001001001, 5 }, { ChordQuality::aug, 0b000100010001, 5 },
            { ChordQuality::power, 0b000010000001, 3 } };
        int bestScore = -1; int bestRoot = -1; ChordQuality bestQ = ChordQuality::none;
        for (int root = 0; root < 12; ++root)
        {
            const uint32_t rotated = ((pcs >> root) | (pcs << (12 - root))) & 0xFFF;
            for (auto& t : tpls)
            {
                if ((rotated & t.mask) != t.mask) continue;
                int extra = 0; for (int i = 0; i < 12; ++i) if ((rotated & (1u << i)) && ! (t.mask & (1u << i))) ++extra;
                int score = t.score * 2 - extra * 2 + (root == bassPc ? 3 : 0);
                if (score > bestScore) { bestScore = score; bestRoot = root; bestQ = t.q; }
            }
        }
        if (bestRoot < 0)
        {
            if (count == 1) { r.root = bassPc; r.quality = ChordQuality::none; return r; }
            r.root = bassPc; r.quality = ChordQuality::none; return r;
        }
        r.root = bestRoot; r.quality = bestQ;
        return r;
    }

private:
    ChordResult analyse() const
    {
        uint32_t pcs = 0; int bass = -1;
        for (int n = 0; n < 128; ++n)
            if (held_[(size_t) n] > 0) { pcs |= 1u << (n % 12); if (bass < 0) bass = n % 12; }
        if (heldCount_ == 0) return {};
        return analysePitchClasses (pcs, bass);
    }
    void publish (const ChordResult& r)
    {
        const uint32_t root = r.root < 0 ? 0xFFu : (uint32_t) r.root;
        const uint32_t bass = r.bass < 0 ? 0xFFu : (uint32_t) r.bass;
        published_.store ((root << 16) | ((uint32_t) r.quality << 8) | bass);
    }

    double sr_ = 48000.0;
    std::array<uint8_t, 128> held_ {};
    int heldCount_ = 0;
    bool dirty_ = false;
    ChordResult current_;
    int stableSamples_ = 0;
    std::atomic<uint32_t> published_ { 0xFF00FFu };
};

} // namespace sanctuary
