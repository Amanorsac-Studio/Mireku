// Theme.h — the ONE place for every colour, font and layout metric in JM Signature.
// The later UX pass (kente textures, adinkra engravings) restyles by editing this
// file + the LookAndFeel only. Spec §5 / design-reference README "Core tokens".
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace jm {

struct Theme
{
    // ------------------------- core tokens (UX pack v1.0.0 manifest.json)
    static inline const juce::Colour bg           { 0xff050505 };
    static inline const juce::Colour chassis      { 0xff0A0A09 };
    static inline const juce::Colour deepPanel    { 0xff10100E };  // "panel"
    static inline const juce::Colour panelRaised  { 0xff15130F };
    static inline const juce::Colour line         { 0xff3B2A13 };
    static inline const juce::Colour black        { 0xff050505 };  // recesses (= bg)
    static inline const juce::Colour chassisBase  { 0xff080807 };  // model-panel layer 1
    static inline const juce::Colour gold         { 0xffD9A648 };
    static inline const juce::Colour goldHi       { 0xffF3C868 };  // "goldBright"
    static inline const juce::Colour goldDark     { 0xff7B551A };
    static inline const juce::Colour cream        { 0xffEFE7D4 };
    static inline const juce::Colour muted        { 0xff9C9485 };

    // world ACCENTS (edges/glows/labels/macro rings; cloth comes from the panel rasters)
    static inline const juce::Colour rhodesAccent { 0xffD36B18 };
    static inline const juce::Colour mksAccent    { 0xffA7D61A };
    static inline const juce::Colour fmAccent     { 0xff2FA8FF };
    static inline const juce::Colour stringsAccent{ 0xffC150D8 };
    static inline const juce::Colour masterAccent { 0xffD9A648 };

    // FX faceplate family (fx-module-*.svg) — its own JM hardware family,
    // never the world colors
    static inline const juce::Colour fxTeal       { 0xff0B655F };
    static inline const juce::Colour fxOrange     { 0xffB85B09 };
    static inline const juce::Colour fxPurple     { 0xff5A315C };
    static inline const juce::Colour fxBlue       { 0xff1B536F };
    static inline const juce::Colour fxRed        { 0xff8A2B17 };
    static inline const juce::Colour fxGreen      { 0xff25583C };

    // legacy interim tokens (procedural KenteTexture fallback only)
    static inline const juce::Colour masterCloth  { 0xff5C2A20 };
    static inline const juce::Colour chassisCloth { 0xff17130E };

    static inline const juce::Colour okGreen      { 0xff68F02A };
    static inline const juce::Colour warnRed      { 0xffF07229 };

    // LED meter (UX pack vertical-meter.svg)
    static inline const juce::Colour meterSafe    { 0xff68F02A };  // green segments
    static inline const juce::Colour meterWarn    { 0xffE0C024 };  // yellow segments
    static inline const juce::Colour meterPeak    { 0xffF07229 };  // orange segments
    static inline const juce::Colour meterBorder  { 0xff392A17 };
    static inline const juce::Colour meterTrack   { 0xff070707 };

    // knob shells (UX pack knob-primary.svg / knob-secondary.svg)
    static inline const juce::Colour knobOuter    { 0xff070707 };  // shell base
    static inline const juce::Colour knobOuterRim { 0xff2B2418 };  // shell stroke
    static inline const juce::Colour knobTrack    { 0xff4F3A16 };  // value track arc
    static inline const juce::Colour knobArc      { 0xffD9A648 };  // value arc (glow)
    static inline const juce::Colour knobGold0    { 0xffFFF0A6 };  // gold ring stops
    static inline const juce::Colour knobGold35   { 0xffF3C868 };
    static inline const juce::Colour knobGold70   { 0xffD9A648 };
    static inline const juce::Colour knobGold100  { 0xff7B551A };
    static inline const juce::Colour knobFace0    { 0xff2A2925 };  // face radial stops
    static inline const juce::Colour knobFace55   { 0xff11110F };
    static inline const juce::Colour knobFace100  { 0xff050505 };
    static inline const juce::Colour knobIndicator{ 0xffF3C868 };
    static inline const juce::Colour knobHub      { 0xff0A0A09 };  // center hub (engraving bed)
    static inline const juce::Colour knobHubRim   { 0xff5B431B };
    static inline const juce::Colour knobSecRim   { 0xff5A421A };  // secondary shell stroke

    // legacy pattern palette (procedural fallback only)
    static inline const juce::Colour patGold      { 0xffD9A648 };
    static inline const juce::Colour patGreen     { 0xff274E27 };
    static inline const juce::Colour patRed       { 0xff8B1A1A };
    static inline const juce::Colour patBurgundy  { 0xff6B1A1A };
    static inline const juce::Colour patDark      { 0xff0C0A07 };
    static inline const juce::Colour patCream     { 0xffEFE7D4 };
    static inline const juce::Colour patBlue      { 0xff2A5B84 };
    static inline const juce::Colour patBlack     { 0xff1A1510 };

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
        // UX pack v1.0.0 reference viewport 1536x1024
        static constexpr int designW        = 1536;
        static constexpr int designH        = 1024;
        static constexpr int topBarH        = 60;
        static constexpr int statusBarH     = 26;
        static constexpr int leftColW       = 260;   // preset-browser-frame width
        static constexpr int rightColW      = 356;
        static constexpr int fxStripH       = 268;   // fx-module frame 250 + strip chrome
        static constexpr int knobPrimary    = 96;    // manifest controlSizes
        static constexpr int knobSecondary  = 56;
        static constexpr int knobMacro      = 104;
        static constexpr int knobLarge      = 96;    // legacy aliases
        static constexpr int knobMedium     = 72;
        static constexpr int knobSmall      = 44;
        static constexpr int powerButton    = 48;
        static constexpr int slotW          = 520, slotH = 92;   // perform slot reference
        static constexpr int pad            = 8;
        static constexpr int corner         = 6;
        static constexpr float arcThickness = 3.0f;  // knob shell arc stroke
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
