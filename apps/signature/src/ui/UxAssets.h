// UxAssets.h — UX design pack v1.0.0 (docs/svg-pack) runtime assets.
// The kente world surfaces use the pack's EXACT rasters (extracted from the
// *-panel.svg data URIs at vendor time, embedded via BinaryData) with the
// panel/slot overlay treatments transcribed 1:1 from the SVG sources — per the
// pack's own JUCE note ("use the source raster files ... preserve the same
// crop/overlay"; never redraw the pattern). Vector marks/icons/branding load
// through juce::Drawable and are cached.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "JMAssets.h"
#include "../Theme.h"
#include "KenteTexture.h"   // internal fallback only (raster decode failure)

namespace jm::ux {

//=============================================================== raster cache
inline const juce::Image& kenteRaster (int world)
{
    static juce::Image cache[4];
    world = juce::jlimit (0, 3, world);
    if (! cache[world].isValid())
    {
        const char* data = nullptr; int size = 0;
        switch (world)
        {
            case 0: data = JMAssets::kente_rhodes_png;   size = JMAssets::kente_rhodes_pngSize; break;
            case 1: data = JMAssets::kente_mks_jpeg;     size = JMAssets::kente_mks_jpegSize; break;
            case 2: data = JMAssets::kente_fm_jpeg;      size = JMAssets::kente_fm_jpegSize; break;
            default: data = JMAssets::kente_strings_jpeg; size = JMAssets::kente_strings_jpegSize; break;
        }
        cache[world] = juce::ImageFileFormat::loadFrom (data, (size_t) size);
    }
    return cache[world];
}

//=========================================================== drawable cache
inline std::unique_ptr<juce::Drawable> parseSvg (const char* data, int size)
{
    if (auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size)))
        return juce::Drawable::createFromSVG (*xml);
    return nullptr;
}
inline std::unique_ptr<juce::Drawable> parseSvgTinted (const char* data, int size, juce::Colour tint)
{
    auto text = juce::String::fromUTF8 (data, size)
                    .replace ("#D9A648", "#" + tint.toDisplayString (false), true)
                    .replace ("#F3C868", "#" + tint.brighter (0.25f).toDisplayString (false), true);
    if (auto xml = juce::XmlDocument::parse (text))
        return juce::Drawable::createFromSVG (*xml);
    return nullptr;
}

enum class Mark { crossKnot = 0, fourPetal, spiralUnity, starWeave, tripleRing };
inline std::unique_ptr<juce::Drawable> makeMark (Mark m, juce::Colour tint)
{
    switch (m)
    {
        case Mark::crossKnot:  return parseSvgTinted (JMAssets::adinkramarkcrossknot_svg, JMAssets::adinkramarkcrossknot_svgSize, tint);
        case Mark::fourPetal:  return parseSvgTinted (JMAssets::adinkramarkfourpetal_svg, JMAssets::adinkramarkfourpetal_svgSize, tint);
        case Mark::spiralUnity:return parseSvgTinted (JMAssets::adinkramarkspiralunity_svg, JMAssets::adinkramarkspiralunity_svgSize, tint);
        case Mark::starWeave:  return parseSvgTinted (JMAssets::adinkramarkstarweave_svg, JMAssets::adinkramarkstarweave_svgSize, tint);
        default:               return parseSvgTinted (JMAssets::adinkramarktriplering_svg, JMAssets::adinkramarktriplering_svgSize, tint);
    }
}

enum class PackIcon { chevronLeft, chevronRight, gear, heart, kebab, star };
inline std::unique_ptr<juce::Drawable> makePackIcon (PackIcon ic, juce::Colour tint = Theme::gold)
{
    switch (ic)
    {
        case PackIcon::chevronLeft:  return parseSvgTinted (JMAssets::chevronleft_svg, JMAssets::chevronleft_svgSize, tint);
        case PackIcon::chevronRight: return parseSvgTinted (JMAssets::chevronright_svg, JMAssets::chevronright_svgSize, tint);
        case PackIcon::gear:         return parseSvgTinted (JMAssets::gear_svg, JMAssets::gear_svgSize, tint);
        case PackIcon::heart:        return parseSvgTinted (JMAssets::heart_svg, JMAssets::heart_svgSize, tint);
        case PackIcon::kebab:        return parseSvgTinted (JMAssets::kebab_svg, JMAssets::kebab_svgSize, tint);
        default:                     return parseSvgTinted (JMAssets::star_svg, JMAssets::star_svgSize, tint);
    }
}
inline std::unique_ptr<juce::Drawable> makeShield()
{
    return parseSvg (JMAssets::jmshieldreference_svg, JMAssets::jmshieldreference_svgSize);
}
inline std::unique_ptr<juce::DrawableButton> makePackIconButton (const juce::String& name, PackIcon ic,
                                                                 juce::Colour normal = Theme::gold)
{
    auto b = std::make_unique<juce::DrawableButton> (name, juce::DrawableButton::ImageFitted);
    auto n = makePackIcon (ic, normal);
    auto over = makePackIcon (ic, Theme::goldHi);
    b->setImages (n.get(), over.get(), over.get());
    b->setColour (juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    b->setColour (juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
    return b;
}

//===================================================== image cover helper
inline void drawCover (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> area, float alpha)
{
    if (! img.isValid()) return;
    const float s = juce::jmax (area.getWidth() / (float) img.getWidth(),
                                area.getHeight() / (float) img.getHeight());
    const float w = (float) img.getWidth() * s, h = (float) img.getHeight() * s;
    auto t = juce::AffineTransform::scale (s)
                 .translated (area.getX() - (w - area.getWidth()) * 0.5f,
                              area.getY() - (h - area.getHeight()) * 0.5f);
    g.setOpacity (alpha);
    g.drawImageTransformed (img, t);
    g.setOpacity (1.0f);
}

//===================================================== world panel backdrop
// Transcribed from kente-{world}-panel.svg: #080807 base; exact raster,
// cover-fit, x0.88 brightness (feColorMatrix -> equivalent 12% black scrim);
// vertical shade 0.30->0.68; radial world-accent warmth 0.10.
// `opacity` follows the pack recommendation 0.55-0.75 behind controls.
inline void drawWorldPanel (juce::Graphics& g, juce::Rectangle<float> area, int world,
                            float opacity = 0.70f, float corner = 6.0f)
{
    juce::Path clip;
    clip.addRoundedRectangle (area, corner);
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (clip);
    g.setColour (Theme::chassisBase);
    g.fillRect (area);
    auto& img = kenteRaster (world);
    if (img.isValid())
    {
        drawCover (g, img, area, opacity);
        g.setColour (juce::Colours::black.withAlpha (0.12f * opacity));    // x0.88 brightness
        g.fillRect (area);
        juce::ColourGradient shade (juce::Colours::black.withAlpha (0.30f * opacity),
                                    area.getX(), area.getY(),
                                    juce::Colours::black.withAlpha (0.68f * opacity),
                                    area.getX(), area.getBottom(), false);
        g.setGradientFill (shade);
        g.fillRect (area);
        juce::ColourGradient warm (Theme::worldAccent (world).withAlpha (0.10f * opacity),
                                   area.getCentreX(), area.getCentreY(),
                                   juce::Colours::transparentBlack,
                                   area.getX(), area.getY(), true);
        g.setGradientFill (warm);
        g.fillRect (area);
    }
    else    // fallback only: procedural cloth (logged; should not happen)
        KenteTexture::fill (g, area, KenteTexture::Style::Weave, Theme::worldAccent (world), 0.8f, corner, world);
}

//====================================================== perform slot backdrop
// Transcribed from perform-slot-{world}.svg: exact raster cover-fit,
// horizontal shade 0.25->0.62 (L->R), 8% accent wash. Border/state handled
// by the caller per the pack's OFF/ON/SELECTED rules.
inline void drawPerformSlot (juce::Graphics& g, juce::Rectangle<float> area, int world, float corner = 6.0f)
{
    juce::Path clip;
    clip.addRoundedRectangle (area, corner);
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (clip);
    g.setColour (Theme::chassisBase);
    g.fillRect (area);
    auto& img = kenteRaster (world);
    if (img.isValid())
    {
        drawCover (g, img, area, 1.0f);
        juce::ColourGradient shade (juce::Colour (0xff050505).withAlpha (0.25f), area.getX(), area.getY(),
                                    juce::Colours::black.withAlpha (0.62f), area.getRight(), area.getY(), false);
        g.setGradientFill (shade);
        g.fillRect (area);
        g.setColour (Theme::worldAccent (world).withAlpha (0.08f));
        g.fillRect (area);
    }
    else
        KenteTexture::fill (g, area, KenteTexture::Style::Flat, Theme::worldAccent (world), 0.9f, corner, world);
}

//============================================================ micro weave
// kente-micro-weave.svg transcription (pure vector, 64px tile).
inline const juce::Image& microWeaveTile()
{
    static juce::Image tile = []
    {
        juce::Image img (juce::Image::ARGB, 64, 64, true);
        juce::Graphics g (img);
        g.setColour (Theme::gold.withAlpha (0.08f));
        for (int v : { 8, 24, 40, 56 }) { g.fillRect (0, v, 64, 1); g.fillRect (v, 0, 1, 64); }
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        for (int v : { 16, 48 }) { g.fillRect (0, v - 1, 64, 2); g.fillRect (v - 1, 0, 2, 64); }
        return img;
    }();
    return tile;
}
inline void drawMicroWeave (juce::Graphics& g, juce::Rectangle<float> area, float alpha, float corner = 6.0f)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.setTiledImageFill (microWeaveTile(), 0, 0, alpha);
    g.fillRoundedRectangle (area, corner);
}

//============================================================== frames
// panel-frame / preset-browser-frame / master-fx-frame share one language:
// #0B0B0A body, vertical #312817->#100D08 2px edge, inner gold 40% line.
inline void drawFrame (juce::Graphics& g, juce::Rectangle<float> area, bool fillBody = true,
                       float corner = 6.0f, float inset = 7.0f)
{
    if (fillBody)
    {
        g.setColour (juce::Colour (0xff0B0B0A));
        g.fillRoundedRectangle (area, corner);
    }
    juce::ColourGradient edge (juce::Colour (0xff312817), area.getX(), area.getY(),
                               juce::Colour (0xff100D08), area.getX(), area.getBottom(), false);
    g.setGradientFill (edge);
    g.drawRoundedRectangle (area.reduced (1.0f), corner, 2.0f);
    g.setColour (Theme::gold.withAlpha (0.40f));
    g.drawRoundedRectangle (area.reduced (inset), corner * 0.66f, 1.0f);
}

// fx-module-*.svg: vertical color->#0B0B0A gradient plate, #6B5128 border,
// inner #E9C56A 25% line, 4 corner screws. (feTurbulence noise approximated
// with a sparse deterministic speckle.)
inline void drawFxModuleFrame (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour top,
                               bool goldOverlay = false)
{
    juce::ColourGradient grad (top, area.getX(), area.getY(),
                               juce::Colour (0xff0B0B0A), area.getX(), area.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (area, 5.0f);
    if (goldOverlay)
    {
        g.setColour (Theme::gold.withAlpha (0.25f));
        g.fillRoundedRectangle (area, 5.0f);
    }
    juce::Random rnd ((juce::int64) top.getARGB());          // grain speckle
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    for (int i = 0; i < (int) (area.getWidth() * area.getHeight() / 220.0f); ++i)
        g.fillRect (area.getX() + rnd.nextFloat() * area.getWidth(),
                    area.getY() + rnd.nextFloat() * area.getHeight(), 1.0f, 1.0f);
    g.setColour (juce::Colour (0xff6B5128));
    g.drawRoundedRectangle (area.reduced (1.0f), 5.0f, 1.0f);
    g.setColour (juce::Colour (0xffE9C56A).withAlpha (0.25f));
    g.drawRoundedRectangle (area.reduced (7.0f), 3.0f, 1.0f);
    g.setColour (juce::Colour (0xffA88B51));
    for (auto p : { juce::Point<float> (area.getX() + 10.0f, area.getY() + 10.0f),
                    juce::Point<float> (area.getRight() - 10.0f, area.getY() + 10.0f),
                    juce::Point<float> (area.getX() + 10.0f, area.getBottom() - 10.0f),
                    juce::Point<float> (area.getRight() - 10.0f, area.getBottom() - 10.0f) })
        g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
}

// preset-row-active.svg: #3B260A -> #16100A horizontal gradient, gold 70% stroke.
inline void drawPresetRowActive (juce::Graphics& g, juce::Rectangle<float> area)
{
    juce::ColourGradient grad (juce::Colour (0xff3B260A), area.getX(), area.getY(),
                               juce::Colour (0xff16100A), area.getRight(), area.getY(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (area, 3.0f);
    g.setColour (Theme::gold.withAlpha (0.7f));
    g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);
}

// macro-ring-*.svg: #2B2418 base ring + world-colored glowing arc (r46/104).
inline void drawMacroRing (juce::Graphics& g, juce::Rectangle<float> knobBounds, juce::Colour accent, float value01)
{
    const float size = juce::jmin (knobBounds.getWidth(), knobBounds.getHeight());
    auto c = knobBounds.getCentre();
    const float r = size * 0.5f * (46.0f / 52.0f);
    const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);
    juce::Path base;
    base.addCentredArc (c.x, c.y, r, r, 0, a0, a1, true);
    g.setColour (juce::Colour (0xff2B2418));
    g.strokePath (base, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r, r, 0, a0, a0 + (a1 - a0) * juce::jlimit (0.0f, 1.0f, value01), true);
    g.setColour (accent.withAlpha (0.35f));         // cheap glow halo
    g.strokePath (arc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace jm::ux
