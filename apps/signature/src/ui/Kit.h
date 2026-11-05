// Kit.h — photoreal hardware asset kit (docs/hardware-kit) runtime layer.
//
// Client review round 2 delivered a rendered hardware kit (RGBA transparent
// overlays) that replaces the drawn knob / FX faceplate / meter / button art.
// Everything here is decoded ONCE and cached; nothing is parsed or recomputed
// per repaint. Reference measurements come from docs/hardware-kit/layout-spec.json.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "JMAssets.h"
#include "../Theme.h"

namespace jm::kit {

//============================================================== decoded images
enum class Img
{
    knobLarge = 0, knobSmall,
    panelTeal, panelOrange, panelPurple, panelBlue, panelRed, panelGreen,
    panelMaster, buttonUp, buttonDown, buttonMaster,
    meterLeft, meterRight, screw, power, rackCorner, dividerGold, flourishGold,
    count
};

inline const juce::Image& image (Img which)
{
    static std::array<juce::Image, (size_t) Img::count> cache;
    static std::array<bool, (size_t) Img::count> tried {};
    const auto i = (size_t) which;
    if (! tried[i])
    {
        tried[i] = true;
        const char* d = nullptr; int n = 0;
        switch (which)
        {
            case Img::knobLarge:    d = JMAssets::knoblarge00_png;     n = JMAssets::knoblarge00_pngSize; break;
            case Img::knobSmall:    d = JMAssets::knobsmall00_png;     n = JMAssets::knobsmall00_pngSize; break;
            case Img::panelTeal:    d = JMAssets::panelteal_png;       n = JMAssets::panelteal_pngSize; break;
            case Img::panelOrange:  d = JMAssets::panelorange_png;     n = JMAssets::panelorange_pngSize; break;
            case Img::panelPurple:  d = JMAssets::panelpurple_png;     n = JMAssets::panelpurple_pngSize; break;
            case Img::panelBlue:    d = JMAssets::panelblue_png;       n = JMAssets::panelblue_pngSize; break;
            case Img::panelRed:     d = JMAssets::panelred_png;        n = JMAssets::panelred_pngSize; break;
            case Img::panelGreen:   d = JMAssets::panelgreen_png;      n = JMAssets::panelgreen_pngSize; break;
            case Img::panelMaster:  d = JMAssets::panelmasterempty_png; n = JMAssets::panelmasterempty_pngSize; break;
            case Img::buttonUp:     d = JMAssets::buttonemptyup_png;   n = JMAssets::buttonemptyup_pngSize; break;
            case Img::buttonDown:   d = JMAssets::buttonemptydown_png; n = JMAssets::buttonemptydown_pngSize; break;
            case Img::buttonMaster: d = JMAssets::buttonmasterempty_png; n = JMAssets::buttonmasterempty_pngSize; break;
            case Img::meterLeft:    d = JMAssets::meterledleft_png;    n = JMAssets::meterledleft_pngSize; break;
            case Img::meterRight:   d = JMAssets::meterledright_png;   n = JMAssets::meterledright_pngSize; break;
            case Img::screw:        d = JMAssets::screw00_png;         n = JMAssets::screw00_pngSize; break;
            case Img::power:        d = JMAssets::iconpower_png;       n = JMAssets::iconpower_pngSize; break;
            case Img::rackCorner:   d = JMAssets::rackcorner_png;      n = JMAssets::rackcorner_pngSize; break;
            case Img::dividerGold:  d = JMAssets::dividergold_png;     n = JMAssets::dividergold_pngSize; break;
            default:                d = JMAssets::flourishgold_png;    n = JMAssets::flourishgold_pngSize; break;
        }
        cache[i] = juce::ImageFileFormat::loadFrom (d, (size_t) n);
    }
    return cache[i];
}

//======================================================== optical centre / span
/// The kit crops are NOT square (knob-large 278x283, knob-small 215x185), so the
/// pixel centre is not the rotation centre. We derive the optical centre from the
/// binary alpha silhouette (alpha > 128 — this excludes the soft drop shadow,
/// which is low-alpha) and take its area centroid: for a disc with an evenly
/// distributed stud bezel that is the true axis. `span` is the silhouette's
/// larger dimension, i.e. the diameter the art is drawn at.
struct Optical { juce::Point<float> centre { 0, 0 }; float span = 1.0f; bool valid = false; };

inline Optical optical (Img which)
{
    static std::array<Optical, (size_t) Img::count> cache;
    const auto i = (size_t) which;
    if (cache[i].valid) return cache[i];
    auto& img = image (which);
    if (! img.isValid()) return cache[i];

    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    double sx = 0, sy = 0, n = 0;
    int minX = bd.width, minY = bd.height, maxX = -1, maxY = -1;
    for (int y = 0; y < bd.height; ++y)
        for (int x = 0; x < bd.width; ++x)
            if (bd.getPixelColour (x, y).getAlpha() > 128)
            {
                sx += x; sy += y; ++n;
                minX = juce::jmin (minX, x); maxX = juce::jmax (maxX, x);
                minY = juce::jmin (minY, y); maxY = juce::jmax (maxY, y);
            }
    if (n <= 0) return cache[i];
    Optical o;
    o.centre = { (float) (sx / n) + 0.5f, (float) (sy / n) + 0.5f };
    o.span = (float) juce::jmax (maxX - minX + 1, maxY - minY + 1);
    o.valid = true;
    cache[i] = o;
    return o;
}

//================================================================ knob drawing
/// Draws a kit knob centred on `centre` at `displayDiameter`, rotated by
/// `angle` radians about its OPTICAL centre (kit note: use one image per size
/// and rotate -135deg..+135deg). Nothing else about the art is redrawn.
inline void drawKnob (juce::Graphics& g, juce::Point<float> centre, float displayDiameter,
                      float angle, bool large, float alpha = 1.0f)
{
    const auto which = large ? Img::knobLarge : Img::knobSmall;
    auto& img = image (which);
    auto o = optical (which);
    if (! img.isValid() || ! o.valid) return;
    const float s = displayDiameter / o.span;
    auto t = juce::AffineTransform::translation (-o.centre.x, -o.centre.y)
                 .rotated (angle)
                 .scaled (s)
                 .translated (centre.x, centre.y);
    juce::Graphics::ScopedSaveState ss (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (alpha);
    g.drawImageTransformed (img, t);
}

//=========================================================== nine-slice plaque
/// Draws a bordered plate into an arbitrary rectangle without distorting its
/// corners: fixed corner caps, stretched edges and centre. Used for the button
/// plaques and the master panel, whose native aspect never matches the slot.
inline void drawNineSlice (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> dest,
                           int capX, int capY, float alpha = 1.0f)
{
    if (! img.isValid() || dest.getWidth() < 4.0f || dest.getHeight() < 4.0f) return;
    const int iw = img.getWidth(), ih = img.getHeight();
    capX = juce::jlimit (1, iw / 2 - 1, capX);
    capY = juce::jlimit (1, ih / 2 - 1, capY);
    const float dcx = juce::jmin ((float) capX, dest.getWidth() * 0.45f);
    const float dcy = juce::jmin ((float) capY, dest.getHeight() * 0.45f);

    const int sxs[3] = { 0, capX, iw - capX };
    const int sws[3] = { capX, iw - 2 * capX, capX };
    const int sys[3] = { 0, capY, ih - capY };
    const int shs[3] = { capY, ih - 2 * capY, capY };
    const float dxs[3] = { dest.getX(), dest.getX() + dcx, dest.getRight() - dcx };
    const float dws[3] = { dcx, dest.getWidth() - 2 * dcx, dcx };
    const float dys[3] = { dest.getY(), dest.getY() + dcy, dest.getBottom() - dcy };
    const float dhs[3] = { dcy, dest.getHeight() - 2 * dcy, dcy };

    juce::Graphics::ScopedSaveState ss (g);
    g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
    g.setOpacity (alpha);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
        {
            if (sws[c] <= 0 || shs[r] <= 0 || dws[c] <= 0.05f || dhs[r] <= 0.05f) continue;
            g.drawImage (img, dxs[c], dys[r], dws[c], dhs[r], sxs[c], sys[r], sws[c], shs[r]);
        }
}

//============================================================ pill (stadium) plate
/// CLIENT REVIEW ROUND 4 (item 3) — the kit's button plate (`button-empty-up`,
/// 377x123) is a STADIUM: its "corners" are two semicircular caps whose radius
/// is half the art's height. Nine-slicing it is geometrically wrong. At the
/// bottom bar's 220x40 cell the nine-slice drew a 53x30 corner source into
/// 53x18 while squashing the 63px-tall source waist into 4px, so each rounded
/// end collapsed into a wedge — the "triangle" the client is describing.
///
/// This routine 3-slices HORIZONTALLY instead: the full source height is mapped
/// to the full destination height at ONE uniform scale (so the cap profile is
/// never distorted) and only the flat middle column is stretched. The silhouette
/// is then an exact stadium at any width.
inline void drawPillPlate (juce::Graphics& g, const juce::Image& img,
                           juce::Rectangle<float> dest, float alpha = 1.0f)
{
    if (! img.isValid() || dest.getWidth() < 4.0f || dest.getHeight() < 2.0f) return;
    const int iw = img.getWidth(), ih = img.getHeight();
    const int capSrc = juce::jlimit (1, iw / 2 - 1, ih / 2 + 2);   // the whole rounded end
    const float s = dest.getHeight() / (float) ih;                 // ONE uniform scale
    const float capDst = juce::jmin ((float) capSrc * s, dest.getWidth() * 0.5f);

    juce::Graphics::ScopedSaveState ss (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (alpha);
    // left cap
    g.drawImage (img, dest.getX(), dest.getY(), capDst, dest.getHeight(),
                 0, 0, capSrc, ih);
    // right cap
    g.drawImage (img, dest.getRight() - capDst, dest.getY(), capDst, dest.getHeight(),
                 iw - capSrc, 0, capSrc, ih);
    // stretched waist
    const float midW = dest.getWidth() - 2.0f * capDst;
    if (midW > 0.05f && iw - 2 * capSrc > 0)
        g.drawImage (img, dest.getX() + capDst, dest.getY(), midW, dest.getHeight(),
                     capSrc, 0, iw - 2 * capSrc, ih);
}

/// The dark well inside the plate's gold bevel, measured off the art: the frame
/// is ~12.2% of the height top/bottom and ~16% at the caps. Anything drawn as
/// the button's ENGAGED state must use exactly this rectangle + a stadium
/// radius, so the fill follows the button's own outline.
inline juce::Rectangle<float> pillInner (juce::Rectangle<float> dest)
{
    const float h = dest.getHeight();
    return dest.reduced (juce::jmin (h * 0.16f, dest.getWidth() * 0.45f), h * 0.125f);
}
inline float pillRadius (juce::Rectangle<float> r)
{
    return juce::jmin (r.getHeight(), r.getWidth()) * 0.5f;
}

//=========================================================== proportional fit
/// Draws an image centred inside `dest`, preserving aspect (never stretched).
inline void drawFitted (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> dest,
                        float alpha = 1.0f, bool cover = false)
{
    if (! img.isValid()) return;
    const float sx = dest.getWidth() / (float) img.getWidth();
    const float sy = dest.getHeight() / (float) img.getHeight();
    const float s = cover ? juce::jmax (sx, sy) : juce::jmin (sx, sy);
    const float w = (float) img.getWidth() * s, h = (float) img.getHeight() * s;
    juce::Graphics::ScopedSaveState ss (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (alpha);
    g.drawImage (img, dest.getCentreX() - w * 0.5f, dest.getCentreY() - h * 0.5f, w, h,
                 0, 0, img.getWidth(), img.getHeight());
}

//=================================================================== FX plates
inline Img fxPlate (int family)      // 0 teal 1 orange 2 purple 3 blue 4 red 5 green
{
    switch (juce::jlimit (0, 5, family))
    {
        case 0: return Img::panelTeal;   case 1: return Img::panelOrange;
        case 2: return Img::panelPurple; case 3: return Img::panelBlue;
        case 4: return Img::panelRed;    default: return Img::panelGreen;
    }
}

/// Photoreal FX faceplate. The kit plate already carries screws, bevel and
/// depth, so it replaces the V2 drawn plate wholesale. Native aspect is 0.764.
///
/// A UNIT WIDER THAN THAT IS DRAWN AS FOUR PATCHES, NOT STRETCHED. A rack unit
/// is as wide as its parameters need (FxUnitPanel::naturalWidth), so the same
/// plate has to cover 164 px and 238 px without the screws turning into ovals
/// or the scrollwork smearing. The art is not a flat panel: it carries screws
/// in the corners, an engraved border of vine scrollwork down each side, and a
/// fleuron centred at the top and another at the bottom. Stretching it ruins
/// all three. So the plate is drawn as
///
///   LEFT edge   source [0, kSideCut)        -> left of dest, native scale
///   RIGHT edge  source (1 - kSideCut, 1]    -> right of dest, native scale
///   MIDDLE      one flat column at kFlatX   -> stretched across the gap
///   CENTREPIECE source band [kBandL, kBandR] -> stamped centred, native scale
///
/// The flat column is sampled from bare plate texture between the side
/// scrollwork and the centre fleurons, so stretching it horizontally is
/// invisible while still carrying the top and bottom frame lines. The band is
/// stamped last, centred, so both fleurons stay whole and centred whatever the
/// unit's width. Fractions, not pixels, so a re-rendered kit at another
/// resolution still slices in the same places.
inline void drawFxPlate (juce::Graphics& g, juce::Rectangle<float> dest, int family, float alpha = 1.0f)
{
    auto& img = image (fxPlate (family));
    if (! img.isValid())
    {
        g.setColour (juce::Colour (0xff120D08));
        g.fillRoundedRectangle (dest, 6.0f);
        return;
    }

    juce::Graphics::ScopedSaveState ss (g);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (alpha);

    const float iw = (float) img.getWidth();

    // THE REFERENCE IS THE UNIT, NOT THE FILE. The plates are not actually at
    // 0.764 and not even at the same aspect as each other (teal is 378x596,
    // orange 394x570), so they have ALWAYS been squeezed into the 164x215 cell.
    // The reference width is therefore what one standard unit shows today, so a
    // standard unit still renders exactly as the client approved it and only
    // the extra width of a wide unit is new.
    const float refW = dest.getHeight() * (float) Theme::L::fxUnitW / (float) Theme::L::fxUnitH;

    if (dest.getWidth() <= refW + 1.0f)
    {
        g.drawImage (img, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                     0, 0, img.getWidth(), img.getHeight());
        return;
    }

    constexpr float kSideCut = 0.30f;   // screws and scrollwork end well inside this
    constexpr float kBandL   = 0.30f;   // the centred fleurons live inside this band
    constexpr float kBandR   = 0.70f;

    const int sideSrc = juce::roundToInt (iw * kSideCut);
    const float sideDst = refW * kSideCut;

    g.drawImage (img, dest.getX(), dest.getY(), sideDst, dest.getHeight(),
                 0, 0, sideSrc, img.getHeight());
    g.drawImage (img, dest.getRight() - sideDst, dest.getY(), sideDst, dest.getHeight(),
                 img.getWidth() - sideSrc, 0, sideSrc, img.getHeight());

    // TWO fills, not one, each sampled from the column just INSIDE its own
    // side patch. One fill taken from the left would meet the right patch with
    // the plate's lighting gradient jumping across the join, and that join is a
    // visible vertical line down a photoreal panel. Sampled this way every
    // seam joins source columns that are a pixel apart. The two fills meet at
    // the centre, which the band is stamped over. Two source pixels wide so
    // the resampler interpolates rather than smearing one column.
    const float midX = dest.getCentreX();
    const int fillL = juce::roundToInt (iw * (kSideCut + 0.01f));
    const int fillR = juce::roundToInt (iw * (1.0f - kSideCut - 0.01f)) - 2;
    const float leftFillX = dest.getX() + sideDst;
    if (midX > leftFillX)
        g.drawImage (img, leftFillX, dest.getY(), midX - leftFillX, dest.getHeight(),
                     fillL, 0, 2, img.getHeight());
    const float rightFillR = dest.getRight() - sideDst;
    if (rightFillR > midX)
        g.drawImage (img, midX, dest.getY(), rightFillR - midX, dest.getHeight(),
                     fillR, 0, 2, img.getHeight());

    const int bandSrcX = juce::roundToInt (iw * kBandL);
    const int bandSrcW = juce::roundToInt (iw * (kBandR - kBandL));
    const float bandDstW = refW * (kBandR - kBandL);
    g.drawImage (img, midX - bandDstW * 0.5f, dest.getY(), bandDstW, dest.getHeight(),
                 bandSrcX, 0, bandSrcW, img.getHeight());
}

//====================================================================== meters
/// Segmented green->amber->red LED column from meter-led-*.png, MASKED by level
/// (the kit art is drawn fully lit; we clip rather than tint so the rendered
/// segment colours survive). Unlit part is shown as a dim ghost.
inline void drawLedMeterV (juce::Graphics& g, juce::Rectangle<float> dest, float level01, bool rightChannel = false)
{
    auto& img = image (rightChannel ? Img::meterRight : Img::meterLeft);
    if (! img.isValid())
    {
        g.setColour (juce::Colour (0xff0A0A09));
        g.fillRoundedRectangle (dest, 3.0f);
        return;
    }
    auto place = [&] (float a, juce::Rectangle<float> clip)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clip.toNearestInt());
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (a);
        g.drawImage (img, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                     0, 0, img.getWidth(), img.getHeight());
    };
    g.setColour (juce::Colour (0xff090908));                         // recessed bed
    g.fillRoundedRectangle (dest, 3.0f);
    place (0.14f, dest);                                             // unlit ghost
    const float t = juce::jlimit (0.0f, 1.0f, level01);
    if (t > 0.004f)
        place (1.0f, dest.withTop (dest.getBottom() - dest.getHeight() * t));
}

/// Same LED column rotated to run left->right (header master meter).
inline void drawLedMeterH (juce::Graphics& g, juce::Rectangle<float> dest, float level01)
{
    auto& img = image (Img::meterLeft);
    if (! img.isValid())
    {
        g.setColour (juce::Colour (0xff0A0A09));
        g.fillRoundedRectangle (dest, 3.0f);
        return;
    }
    // draw the column into a rotated frame: source bottom -> destination left
    auto drawRot = [&] (float a, juce::Rectangle<float> clip)
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (clip.toNearestInt());
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.setOpacity (a);
        // scale the column to (h x w), rotate -90deg (source bottom -> dest left),
        // then shift the resulting (-w..0, 0..h) box onto `dest`
        auto t = juce::AffineTransform::scale (dest.getHeight() / (float) img.getWidth(),
                                               dest.getWidth() / (float) img.getHeight())
                     .rotated (juce::MathConstants<float>::halfPi)
                     .translated (dest.getRight(), dest.getY());
        g.drawImageTransformed (img, t);
    };
    g.setColour (juce::Colour (0xff090908));
    g.fillRoundedRectangle (dest, 3.0f);
    drawRot (0.14f, dest);
    const float t = juce::jlimit (0.0f, 1.0f, level01);
    if (t > 0.004f)
        drawRot (1.0f, dest.withWidth (dest.getWidth() * t));
}

//===================================================================== fittings
inline void drawScrew (juce::Graphics& g, juce::Point<float> centre, float d, float alpha = 1.0f)
{
    drawFitted (g, image (Img::screw), juce::Rectangle<float> (d, d).withCentre (centre), alpha);
}
inline void drawPowerIcon (juce::Graphics& g, juce::Rectangle<float> dest, float alpha)
{
    drawFitted (g, image (Img::power), dest, alpha);
}
inline void drawFlourish (juce::Graphics& g, juce::Rectangle<float> dest, float alpha)
{
    drawFitted (g, image (Img::flourishGold), dest, alpha);
}
inline void drawDivider (juce::Graphics& g, juce::Rectangle<float> dest, float alpha)
{
    auto& img = image (Img::dividerGold);
    if (! img.isValid()) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.setOpacity (alpha);
    g.drawImage (img, dest.getX(), dest.getY(), dest.getWidth(), dest.getHeight(),
                 0, 0, img.getWidth(), img.getHeight());
}
/// L-shaped rack corner, mirrored per corner (0 TL, 1 TR, 2 BL, 3 BR).
inline void drawRackCorner (juce::Graphics& g, juce::Rectangle<float> dest, int corner, float alpha)
{
    auto& img = image (Img::rackCorner);
    if (! img.isValid()) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.setOpacity (alpha);
    const bool flipX = (corner == 1 || corner == 3), flipY = (corner == 2 || corner == 3);
    auto t = juce::AffineTransform::scale (dest.getWidth() / (float) img.getWidth(),
                                           dest.getHeight() / (float) img.getHeight());
    if (flipX) t = t.followedBy (juce::AffineTransform::scale (-1.0f, 1.0f).translated (dest.getWidth(), 0.0f));
    if (flipY) t = t.followedBy (juce::AffineTransform::scale (1.0f, -1.0f).translated (0.0f, dest.getHeight()));
    g.drawImageTransformed (img, t.translated (dest.getX(), dest.getY()));
}

} // namespace jm::kit
