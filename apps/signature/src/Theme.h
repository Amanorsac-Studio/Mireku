// Theme.h — the ONE place for every colour, font and layout metric in JM Signature.
// The later UX pass (kente textures, adinkra engravings) restyles by editing this
// file + the LookAndFeel only. Spec §5 / design-reference README "Core tokens".
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace jm {

struct Theme
{
    // ---------------------------------------------------------- core tokens
    static inline const juce::Colour chassis      { 0xff0C0A07 };
    static inline const juce::Colour deepPanel    { 0xff11100D };
    static inline const juce::Colour black        { 0xff080807 };
    static inline const juce::Colour gold         { 0xffD9A648 };
    static inline const juce::Colour goldHi       { 0xffF1C66A };
    static inline const juce::Colour goldDark     { 0xff725322 };
    static inline const juce::Colour cream        { 0xffEFE7D4 };
    static inline const juce::Colour muted        { 0xff9B9383 };

    // module accents (flat placeholders for the future kente cloth tints)
    static inline const juce::Colour rhodesAccent { 0xff7A3B22 };  // rust/amber
    static inline const juce::Colour mksAccent    { 0xff2E4A2B };  // forest/gold
    static inline const juce::Colour fmAccent     { 0xff23305C };  // indigo
    static inline const juce::Colour stringsAccent{ 0xff4A2B5C };  // violet
    static inline const juce::Colour masterAccent { 0xff5C3A16 };  // royal gold/burgundy

    // FX unit cloth tints (per design reference)
    static inline const juce::Colour fxChorus     { 0xff1E4A44 };  // teal
    static inline const juce::Colour fxTremolo    { 0xff4A3A1E };
    static inline const juce::Colour fxPhaser     { 0xff45265C };  // violet
    static inline const juce::Colour fxFlanger    { 0xff26455C };
    static inline const juce::Colour fxDrive      { 0xff5C3A26 };
    static inline const juce::Colour fxEq         { 0xff3A4A2E };
    static inline const juce::Colour fxComp       { 0xff5C2626 };  // crimson
    static inline const juce::Colour fxTapeDelay  { 0xff5C4526 };  // ember
    static inline const juce::Colour fxDigDelay   { 0xff26305C };
    static inline const juce::Colour fxPlate      { 0xff2E3E4A };
    static inline const juce::Colour fxHall       { 0xff2E4A38 };  // forest / indigo shimmer
    static inline const juce::Colour fxWidener    { 0xff44265C };

    // master section cloth (royal gold/burgundy weave)
    static inline const juce::Colour masterCloth  { 0xff5C2A20 };
    // near-black weave for chassis-level panels (browser, dock)
    static inline const juce::Colour chassisCloth { 0xff17130E };

    static inline const juce::Colour okGreen      { 0xff6FBF5A };
    static inline const juce::Colour warnRed      { 0xffC24A3A };
    static inline const juce::Colour meterGreen   { 0xff7FCC5E };

    static juce::Colour worldAccent (int worldIndex)
    {
        switch (worldIndex) { case 0: return rhodesAccent; case 1: return mksAccent;
                              case 2: return fmAccent;     case 3: return stringsAccent; }
        return masterAccent;
    }

    // ---------------------------------------------------------------- fonts
    static juce::Font display (float h)  { return juce::Font (juce::FontOptions ("Verdana", h, juce::Font::bold)); }
    static juce::Font ui (float h)       { return juce::Font (juce::FontOptions ("Verdana", h, juce::Font::plain)); }
    static juce::Font value (float h)    { return juce::Font (juce::FontOptions ("Consolas", h, juce::Font::plain)); }

    // -------------------------------------------------------------- metrics
    // All in "design units" at the 1380x900 reference size; the editor scales.
    struct M
    {
        static constexpr int designW        = 1380;
        static constexpr int designH        = 900;
        static constexpr int topBarH        = 56;
        static constexpr int statusBarH     = 26;
        static constexpr int leftColW       = 240;
        static constexpr int rightColW      = 330;
        static constexpr int fxStripH       = 240;
        static constexpr int knobLarge      = 108;
        static constexpr int knobMedium     = 72;
        static constexpr int knobSmall      = 44;
        static constexpr int pad            = 8;
        static constexpr int corner         = 6;
        static constexpr float arcThickness = 3.5f;
    };

    // ---------------------------------------------------- kente texture params
    // (design-reference README: desaturated/darkened cloth behind gold UI,
    //  pattern legible up close, <= ~35% perceived contrast against chassis)
    struct Kente
    {
        static constexpr int   weaveTile   = 288;   // tileable image size, woven style
        static constexpr int   weaveBlock  = 48;    // patchwork block size
        static constexpr int   flatTile    = 256;   // flat-graphic style (Perform strips)
        static constexpr int   flatBlock   = 64;
        static constexpr float weaveMaxBri = 0.21f; // brightness ceiling (keeps contrast law)
        static constexpr float flatMaxBri  = 0.24f;
        static constexpr float minBri      = 0.05f;
        static constexpr float warpAlpha   = 0.055f; // vertical thread micro-lines
        static constexpr float weftAlpha   = 0.13f;  // light seam lines between blocks
    };
};

} // namespace jm
