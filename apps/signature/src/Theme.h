// Theme.h — the ONE place for every colour, font and layout metric in MIREKU.
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
    // Hardware kit typography: Bahnschrift Condensed SemiBold, open-source
    // fallback Oswald SemiBold / Barlow Condensed SemiBold (docs/hardware-kit
    // layout-spec.json). Resolved once against the installed typefaces; live
    // text only (the kit's font PNG is a style reference).
    static const juce::String& faceDisplay()
    {
        static juce::String f = pick ({ "Bahnschrift SemiBold Condensed", "Bahnschrift SemiCondensed",
                                        "Bahnschrift Condensed", "Bahnschrift", "Oswald SemiBold", "Oswald",
                                        "Barlow Condensed SemiBold", "Barlow Condensed",
                                        "Franklin Gothic Medium Cond", "Arial Narrow", "Verdana" });
        return f;
    }
    static const juce::String& faceValue()
    {
        static juce::String f = pick ({ "Bahnschrift SemiBold Condensed", "Bahnschrift SemiCondensed",
                                        "Bahnschrift", "Oswald", "Consolas" });
        return f;
    }
    static juce::Font display (float h)
    {
        auto f = juce::Font (juce::FontOptions (faceDisplay(), h, juce::Font::bold));
        f.setExtraKerningFactor (0.04f);          // kit tracking 0.04em
        return f;
    }
    static juce::Font ui (float h)
    {
        auto f = juce::Font (juce::FontOptions (faceDisplay(), h, juce::Font::plain));
        f.setExtraKerningFactor (0.04f);
        return f;
    }
    static juce::Font value (float h) { return juce::Font (juce::FontOptions (faceValue(), h, juce::Font::plain)); }

private:
    static juce::String pick (const std::initializer_list<const char*>& names)
    {
        auto have = juce::Font::findAllTypefaceNames();
        for (auto* n : names)
            if (have.contains (juce::String (n))) return juce::String (n);
        return "Verdana";
    }
public:

    // -------------------------------------------------------------- metrics
    // All in "design units" at the 1380x900 reference size; the editor scales.
    struct M
    {
        // CLIENT REVIEW ROUND 2: the approved reference image is 1536x1024 (3:2)
        // and shows every region at once. Root coordinate system == that image,
        // uniform-contain scaled (V2 root discipline retained).
        static constexpr int designW        = 1536;
        static constexpr int designH        = 1024;
        // R3(E): maximise must be able to fill a large display; the root is
        // uniform-contain scaled inside whatever size it gets, so the extra
        // headroom only ever adds chassis letterbox, never stretch.
        static constexpr int minW = 1152, minH = 768, maxW = 7680, maxH = 4320;
        static constexpr int pad            = 8;
        static constexpr int corner         = 6;
        static constexpr float arcThickness = 3.0f;
        static constexpr int knobLarge      = 112;
        static constexpr int knobMedium     = 72;
        static constexpr int knobSmall      = 44;
    };

    // ---------------- restored arrangement (measured off the approved image)
    struct L
    {
        // header ------------------------------------------------------------
        static constexpr int headerH   = 86;
        static constexpr int shieldX = 14, shieldY = 10, shieldD = 64;
        static constexpr int navX = 400, navY = 24, navW = 616, navH = 50;
        static constexpr int navCellW[5] = { 158, 104, 96, 134, 124 };
        static constexpr int cartX = 1072, iconY = 30, iconD = 30, gearX = 1116;
        static constexpr int masterLblX = 1176, masterKnobCx = 1262, masterKnobCy = 43, masterKnobD = 44;
        static constexpr int masterMeterX = 1300, masterMeterY = 30, masterMeterW = 126, masterMeterH = 26;
        static constexpr int limiterX = 1434, limiterY = 28, limiterW = 88, limiterH = 30;

        // main bands --------------------------------------------------------
        static constexpr int browserX = 8,   browserY = 88,  browserW = 254,  browserH = 876;
        static constexpr int centerX  = 270, centerY  = 88,  centerW  = 690,  centerH  = 584;
        static constexpr int dockX    = 966, dockY    = 88,  dockW    = 558,  dockH    = 584;
        static constexpr int rackX    = 270, rackY    = 672, rackW    = 1254, rackH    = 292;
        static constexpr int barY     = 968, barH     = 56;

        // preset browser (local coords) --------------------------------------
        static constexpr int brTitleY = 16, brSearchY = 52, brSearchH = 30;
        static constexpr int brTabsY = 94, brTabsH = 30;
        static constexpr int brListY = 134, brListH = 388;
        // AUTHORING MODE: the save control is no longer one bare button. It
        // states its DESTINATION, and when the patch cannot hold the artist's
        // modulation it offers the save that can, right beside the one that
        // would lose it. The brand art below gives up the height.
        static constexpr int brSaveY = 528, brSaveH = 28;   // name field + SAVE
        static constexpr int brInfoY = 558, brInfoH = 30;   // where it goes / where it went
        static constexpr int brWarnY = 590, brWarnH = 32;   // what a patch cannot hold
        static constexpr int brAltY  = 624, brAltH  = 26;   // SAVE PERFORMANCE INSTEAD
        static constexpr int brArtY = 656, brArtH = 212;

        // model page (local to centerX/centerY) -------------------------------
        static constexpr int mpTitleY = 26, mpPatchY = 60;
        static constexpr int mpSurfX = 18, mpSurfY = 86, mpSurfW = 600, mpSurfH = 440;
        static constexpr int mpKnobD = 112, mpLabelOff = -82, mpValueOff = 68;
        static constexpr int mpFive[5][2] = { { 126, 200 }, { 337, 200 }, { 542, 200 },
                                             { 232, 370 }, { 440, 370 } };
        static constexpr int mpSix[6][2]  = { { 126, 186 }, { 337, 186 }, { 542, 186 },
                                             { 126, 376 }, { 337, 376 }, { 542, 376 } };
        static constexpr int mpMeterX = 626, mpMeterY = 116, mpMeterW = 50, mpMeterH = 344;
        static constexpr int mpTabsX = 8, mpTabsY = 526, mpTabsW = 674, mpTabsH = 44;

        // perform dock (local) ------------------------------------------------
        static constexpr int dkTitleH = 26;
        static constexpr int dkSlotX = 4, dkSlotY = 30, dkSlotW = 550, dkSlotH = 78, dkSlotPitch = 87;
        // R4(2): OCTAVE is a visible stepper beside TRANSPOSE (was shift-click)
        static constexpr int dkModeX = 4, dkModeY = 380, dkModeW = 214, dkModeH = 48;
        static constexpr int dkTransX = 222, dkTransY = 380, dkTransW = 190, dkTransH = 48;
        static constexpr int dkOctX = 418, dkOctY = 380, dkOctW = 136, dkOctH = 48;
        // R4(4): the LEARN / EDIT chips are gone (right-click / double-click the
        // knob instead), so the macro cell loses its 20 px chip row and the knob
        // centre stays exactly where it was.
        static constexpr int dkMacroY = 436, dkMacroH = 122, dkMacroD = 72;
        static constexpr int dkMacroCx[4] = { 77, 206, 335, 464 };

        // perform CENTER page (client correction: the four slots move to the
        // middle when PERFORM is selected) -----------------------------------
        // (the centre stage widens to centerW + dockW when PERFORM is selected)
        static constexpr int pcStageW = centerW + dockW + 6;      // 1254
        static constexpr int pcSlotW = 590, pcSlotH = 132;
        static constexpr int pcSlotX[4] = { 26, 638, 26, 638 };
        static constexpr int pcSlotY[4] = { 46, 46, 186, 186 };
        static constexpr int pcModeX = 196, pcModeY = 332, pcModeW = 290, pcModeH = 52;
        static constexpr int pcTransX = 520, pcTransY = 332, pcTransW = 300, pcTransH = 52;
        static constexpr int pcOctX = 854, pcOctY = 332, pcOctW = 204, pcOctH = 52;
        static constexpr int pcMacroY = 396, pcMacroH = 160, pcMacroD = 104;
        static constexpr int pcMacroCx[4] = { 251, 502, 753, 1004 };

        // fx rack (local) -----------------------------------------------------
        static constexpr int fxTitleH = 26;
        static constexpr int fxRailX = 4, fxRailY = 32, fxRailW = 32, fxRailH = 215;
        static constexpr int fxUnitX = 42, fxUnitY = 32, fxUnitW = 164, fxUnitH = 215, fxUnitPitch = 168;
        static constexpr int fxDotsY = 258;
        static constexpr int fxMasterX = 1054, fxMasterY = 32, fxMasterW = 196, fxMasterH = 215;

        // bottom bar (local) ---------------------------------------------------
        static constexpr int bbBtnY = 10, bbBtnH = 40;
        static constexpr int bbBtnX[4] = { 268, 496, 672, 830 };
        static constexpr int bbBtnW[4] = { 220, 168, 150, 128 };
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
