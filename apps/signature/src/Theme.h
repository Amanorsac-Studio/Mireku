// Theme.h — the ONE place for every colour, font and layout metric in JM Signature.
// The UX passes (kente materials, JM engraved motif family) restyle by editing this
// file + the LookAndFeel only. Spec §5 / design-reference README "Core tokens".
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace jm {

struct Theme
{
    // -------------- core tokens (UX pack V2 tokens/design-tokens.json colors)
    static inline const juce::Colour bg           { 0xff050505 };
    static inline const juce::Colour chassis      { 0xff090908 };
    static inline const juce::Colour deepPanel    { 0xff100E0B };  // "panel"
    static inline const juce::Colour panelDeep    { 0xff080706 };
    static inline const juce::Colour panelRaised  { 0xff15130F };
    static inline const juce::Colour line         { 0xff3B2A13 };
    static inline const juce::Colour black        { 0xff050505 };  // recesses (= bg)
    static inline const juce::Colour chassisBase  { 0xff080807 };
    static inline const juce::Colour gold         { 0xffD9A648 };
    static inline const juce::Colour goldHi       { 0xffFFD77A };  // "goldBright"
    static inline const juce::Colour goldHot      { 0xffFFB11B };  // hot edge
    static inline const juce::Colour goldDark     { 0xff6B4612 };
    static inline const juce::Colour cream        { 0xffF4EAD5 };
    static inline const juce::Colour muted        { 0xffA59A87 };

    // world ACCENTS (V2)
    static inline const juce::Colour rhodesAccent { 0xffE66F17 };
    static inline const juce::Colour mksAccent    { 0xffA9D62E };
    static inline const juce::Colour fmAccent     { 0xff39B8FF };
    static inline const juce::Colour stringsAccent{ 0xffD85CE8 };
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
        // UX pack V2 reference viewport: 1000x720 (25:18), uniform-contain
        static constexpr int designW        = 1000;
        static constexpr int designH        = 720;
        static constexpr int minW = 750, minH = 540, maxW = 2000, maxH = 1440;
        static constexpr int pad            = 8;
        static constexpr int corner         = 6;
        static constexpr float arcThickness = 3.0f;
        // legacy aliases still referenced by generic panels
        static constexpr int knobLarge      = 112;
        static constexpr int knobMedium     = 72;
        static constexpr int knobSmall      = 44;
        static constexpr int topBarH = 74, statusBarH = 0, leftColW = 0, rightColW = 0, fxStripH = 0;
    };

    // ------------------------- V2 exact geometry (tokens/design-tokens.json)
    struct V2
    {
        // header
        static constexpr int headerX = 16, headerY = 12, headerW = 968, headerH = 52;
        static constexpr int brandW = 230;
        static constexpr int navX = 270, navY = 18, navW = 462, navH = 40, navItemW = 92, navCellW = 88;
        static constexpr int masterX = 760, masterY = 18, masterW = 208, masterH = 40;
        // content
        static constexpr int contentX = 16, contentY = 74, contentW = 968, contentH = 630, contentR = 8;
        // model page
        static constexpr int titleY = 96;
        static constexpr int surfX = 190, surfY = 150, surfW = 620, surfH = 430, surfR = 12;
        static constexpr int knobD = 112, knob6D = 96;
        static constexpr int labelOffsetY = -74, valueOffsetY = 68;
        static constexpr int outX = 830, outY = 185, outW = 54, outH = 330;
        // perform page
        static constexpr int slotW = 390, slotH = 132;
        static constexpr int modeX = 236, modeY = 412, modeW = 184, modeH = 46;
        static constexpr int transX = 480, transY = 395, transW = 284, transH = 78;
        static constexpr int macroD = 116, macroY = 594;
        static constexpr int macroX[4] = { 230, 410, 590, 770 };
        static constexpr int slotX[4] = { 94, 512, 94, 512 };
        static constexpr int slotY[4] = { 116, 116, 264, 264 };
        // model knob centers (5-control worlds / MKS 6-control)
        static constexpr int five[5][2] = { { 340, 284 }, { 500, 284 }, { 660, 284 }, { 420, 456 }, { 580, 456 } };
        static constexpr int six[6][2]  = { { 320, 270 }, { 500, 270 }, { 680, 270 }, { 320, 445 }, { 500, 445 }, { 680, 445 } };
        // materials
        static constexpr float backdropOpacity = 0.92f, backdropSat = 1.18f, backdropCon = 1.16f, backdropBri = 0.74f;
        static constexpr float vignetteOpacity = 0.46f;
        static constexpr float fxMicroOpacity = 0.18f, fxMicroScale = 0.32f;
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
