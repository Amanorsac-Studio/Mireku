// KenteTexture.h — procedural, tileable kente cloth backdrops (design law:
// "Kente is material" — docs/jm-design-reference/README.md). Two styles:
//   Weave — Ewe/Ashanti patchwork: grid of motif blocks (warp stripes, weft
//           bands, checker, zigzag, triangles, fine hatch) + woven micro-lines
//           (study kente/02 and kente/03 for the block rhythm).
//   Flat  — bold flat geometry (triangles, bands, checker, diamonds) after
//           kente/01, for Perform slot strips / master section.
// Every render is deterministic (seeded by style+tint), darkened/desaturated
// to sit behind gold UI (brightness ceilings in Theme::Kente), rendered once
// per (style, tint) and cached. All tints/params live in Theme.h.
#pragma once
#include <map>
#include "../Theme.h"

namespace jm {

class KenteTexture
{
public:
    enum class Style { Weave, Flat };

    /// Cached tileable cloth image for a style+tint.
    static const juce::Image& get (Style style, juce::Colour tint)
    {
        static std::map<juce::uint64, juce::Image> cache;
        static juce::CriticalSection lock;
        const juce::uint64 key = ((juce::uint64) (style == Style::Flat ? 1 : 0) << 32) | tint.getARGB();
        const juce::ScopedLock sl (lock);
        auto it = cache.find (key);
        if (it != cache.end()) return it->second;
        return cache.emplace (key, render (style, tint)).first->second;
    }

    /// Fill a rounded rect with tiled cloth (anchored at the component origin).
    static void fill (juce::Graphics& g, juce::Rectangle<float> area, Style style,
                      juce::Colour tint, float alpha = 1.0f, float cornerSize = 6.0f)
    {
        g.saveState();
        g.setTiledImageFill (get (style, tint), 0, 0, alpha);
        g.fillRoundedRectangle (area, cornerSize);
        g.restoreState();
    }

private:
    //=========================================================== palette
    static juce::Colour clampCloth (juce::Colour c, float maxBri)
    {
        const float h = c.getHue();
        const float s = juce::jmin (0.80f, c.getSaturation() * 1.05f);   // slightly desaturated cap
        const float b = juce::jlimit (Theme::Kente::minBri, maxBri, c.getBrightness() * 0.55f);
        return juce::Colour::fromHSV (h, s, b, 1.0f);
    }

    static juce::Image render (Style style, juce::Colour tint)
    {
        const bool flat = style == Style::Flat;
        const int T = flat ? Theme::Kente::flatTile : Theme::Kente::weaveTile;
        const int B = flat ? Theme::Kente::flatBlock : Theme::Kente::weaveBlock;
        const float maxBri = flat ? Theme::Kente::flatMaxBri : Theme::Kente::weaveMaxBri;

        juce::Image img (juce::Image::RGB, T, T, true);
        juce::Graphics g (img);
        juce::Random rnd ((juce::int64) (((juce::uint64) tint.getARGB() * 2654435761ULL) ^ (flat ? 0x9E3779B9ULL : 0x85EBCA6BULL)));

        // cloth palette derived from the module tint
        const juce::Colour bg = clampCloth (tint.darker (1.1f), maxBri * 0.72f);
        const std::array<juce::Colour, 5> pal {
            clampCloth (tint.darker (0.35f), maxBri),
            clampCloth (tint, maxBri),
            clampCloth (tint.brighter (0.55f), maxBri),
            clampCloth (Theme::goldDark.interpolatedWith (tint, 0.35f).brighter (0.25f), maxBri), // gold thread
            clampCloth (juce::Colour (0xff191512), maxBri * 0.6f)                                 // near-black thread
        };
        g.fillAll (bg);

        // ------------------------------------------------ patchwork blocks
        const int cells = T / B;
        for (int by = 0; by < cells; ++by)
            for (int bx = 0; bx < cells; ++bx)
            {
                const juce::Rectangle<int> cell (bx * B, by * B, B, B);
                g.saveState();
                g.reduceClipRegion (cell);
                const int motif = rnd.nextInt (flat ? 4 : 6);
                const auto& c1 = pal[(size_t) rnd.nextInt (3)];
                const auto& c2 = pal[(size_t) (2 + rnd.nextInt (3))];
                drawMotif (g, cell.toFloat(), motif, flat, c1, c2, pal[4], rnd);
                g.restoreState();
            }

        // ------------------------------------------ weft seams between blocks
        g.setColour (pal[3].withAlpha (Theme::Kente::weftAlpha));
        for (int y = 0; y <= cells; ++y)
            g.fillRect (0, y * B - (y == cells ? 1 : 0), T, 1);
        if (! flat)
        {
            g.setColour (pal[4].withAlpha (Theme::Kente::weftAlpha * 0.7f));
            for (int x = 0; x <= cells; ++x)
                g.fillRect (x * B - (x == cells ? 1 : 0), 0, 1, T);
        }

        // ------------------------------------------------- warp micro-threads
        for (int x = 0; x < T; x += 2)
        {
            const bool lit = (x / 2) % 4 == 0;
            g.setColour ((lit ? pal[3] : juce::Colours::black)
                             .withAlpha (Theme::Kente::warpAlpha * (lit ? 0.8f : 1.0f)));
            g.fillRect (x, 0, 1, T);
        }
        // faint horizontal weave shading rows (fibre irregularity)
        for (int y = 0; y < T; y += 3)
            if (rnd.nextInt (5) == 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.04f));
                g.fillRect (0, y, T, 1);
            }
        return img;
    }

    //================================================================ motifs
    static void drawMotif (juce::Graphics& g, juce::Rectangle<float> r, int motif, bool flat,
                           juce::Colour c1, juce::Colour c2, juce::Colour dark, juce::Random& rnd)
    {
        switch (motif)
        {
            case 0:     // vertical warp stripes
            {
                const float w = flat ? (float) (8 + rnd.nextInt (8)) : (float) (3 + rnd.nextInt (4));
                int i = 0;
                for (float x = r.getX(); x < r.getRight(); x += w, ++i)
                    { g.setColour (i % 3 == 2 ? dark : (i % 2 == 0 ? c1 : c2)); g.fillRect (x, r.getY(), w, r.getHeight()); }
                break;
            }
            case 1:     // horizontal bands with thin threads
            {
                const float h = flat ? (float) (10 + rnd.nextInt (8)) : (float) (4 + rnd.nextInt (4));
                int i = 0;
                for (float y = r.getY(); y < r.getBottom(); y += h, ++i)
                {
                    g.setColour (i % 2 == 0 ? c1 : c2);
                    g.fillRect (r.getX(), y, r.getWidth(), h);
                    if (i % 2 == 1) { g.setColour (dark); g.fillRect (r.getX(), y + h * 0.4f, r.getWidth(), 1.0f); }
                }
                break;
            }
            case 2:     // checker
            {
                const float s = flat ? 16.0f : (float) (6 + rnd.nextInt (5));
                int row = 0;
                for (float y = r.getY(); y < r.getBottom(); y += s, ++row)
                {
                    int col = 0;
                    for (float x = r.getX(); x < r.getRight(); x += s, ++col)
                        { g.setColour ((row + col) % 2 == 0 ? c1 : c2); g.fillRect (x, y, s, s); }
                }
                break;
            }
            case 3:     // triangles (flat: big paired triangles; weave: sawtooth rows)
            {
                g.setColour (c1);
                g.fillRect (r);
                g.setColour (c2);
                const float tw = flat ? r.getWidth() / 2.0f : 14.0f;
                const float th = flat ? r.getHeight() / 2.0f : 12.0f;
                for (float y = r.getY(); y < r.getBottom(); y += th)
                    for (float x = r.getX(); x < r.getRight(); x += tw)
                    {
                        juce::Path p;
                        const bool up = ((int) ((x + y) / (tw + th)) % 2) == 0;
                        if (up) { p.addTriangle (x, y + th, x + tw * 0.5f, y, x + tw, y + th); }
                        else    { p.addTriangle (x, y, x + tw, y, x + tw * 0.5f, y + th); }
                        g.fillPath (p);
                    }
                break;
            }
            case 4:     // zigzag / chevron rows (weave only)
            {
                g.setColour (c1);
                g.fillRect (r);
                g.setColour (c2);
                const float amp = 5.0f, period = 12.0f, gap = 9.0f;
                for (float y0 = r.getY() + amp; y0 < r.getBottom() + amp; y0 += gap)
                {
                    juce::Path p;
                    p.startNewSubPath (r.getX(), y0);
                    bool up = true;
                    for (float x = r.getX(); x < r.getRight() + period; x += period * 0.5f, up = ! up)
                        p.lineTo (x + period * 0.5f, up ? y0 - amp : y0 + amp);
                    g.strokePath (p, juce::PathStrokeType (2.4f));
                }
                break;
            }
            default:    // fine dense hatch (plain-weave field)
            {
                int i = 0;
                for (float x = r.getX(); x < r.getRight(); x += 2.0f, ++i)
                    { g.setColour (i % 2 == 0 ? c1 : c1.darker (0.25f)); g.fillRect (x, r.getY(), 2.0f, r.getHeight()); }
                g.setColour (c2.withAlpha (0.5f));
                for (float y = r.getY() + 5.0f; y < r.getBottom(); y += 11.0f)
                    g.fillRect (r.getX(), y, r.getWidth(), 2.0f);
                break;
            }
        }
    }
};

} // namespace jm
