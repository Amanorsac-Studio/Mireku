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

    /// Cached tileable cloth image for a style+tint. `dna` selects the client's
    /// per-world pattern geometry (0 rhodes, 1 mks, 2 fm, 3 strings,
    /// -1 generic patchwork) — transcribed from assets/pattern-*.svg.
    static const juce::Image& get (Style style, juce::Colour tint, int dna = -1)
    {
        static std::map<juce::uint64, juce::Image> cache;
        static juce::CriticalSection lock;
        const juce::uint64 key = ((juce::uint64) (juce::uint8) (dna + 1) << 40)
                               | ((juce::uint64) (style == Style::Flat ? 1 : 0) << 32) | tint.getARGB();
        const juce::ScopedLock sl (lock);
        auto it = cache.find (key);
        if (it != cache.end()) return it->second;
        return cache.emplace (key, render (style, tint, dna)).first->second;
    }

    /// Fill a rounded rect with tiled cloth (anchored at the component origin).
    static void fill (juce::Graphics& g, juce::Rectangle<float> area, Style style,
                      juce::Colour tint, float alpha = 1.0f, float cornerSize = 6.0f, int dna = -1)
    {
        g.saveState();
        g.setTiledImageFill (get (style, tint, dna), 0, 0, alpha);
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

    static juce::Image render (Style style, juce::Colour tint, int dna = -1)
    {
        const bool flat = style == Style::Flat;
        const float maxBri = flat ? Theme::Kente::flatMaxBri : Theme::Kente::weaveMaxBri;
        if (dna >= 0 && dna <= 3)
            return renderWorldPattern (dna, tint, maxBri);

        const int T = flat ? Theme::Kente::flatTile : Theme::Kente::weaveTile;
        const int B = flat ? Theme::Kente::flatBlock : Theme::Kente::weaveBlock;

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

    //================================================ client world patterns
    // Geometry transcribed from docs/jm-design-reference/kente/assets/
    // pattern-{rhodes,mks,fm,strings}.svg (decision logged: transcription, not
    // SVG parsing, so the clampCloth ≤35%-contrast pipeline and warp/weft
    // threadwork still apply, and each world keeps its Theme tint identity).
    static juce::Image renderWorldPattern (int dna, juce::Colour tint, float maxBri)
    {
        // pull the SVG palette toward the module tint, then clamp to the cloth law
        auto P = [&] (juce::Colour c) { return clampCloth (c.interpolatedWith (tint, 0.35f), maxBri); };
        const auto gold = P (Theme::patGold),  green = P (Theme::patGreen), red = P (Theme::patRed),
                   burg = P (Theme::patBurgundy), dark = clampCloth (Theme::patDark, maxBri * 0.6f),
                   cream = P (Theme::patCream), blue = P (Theme::patBlue),  blk = clampCloth (Theme::patBlack, maxBri * 0.6f);

        const int T = dna == 0 ? 150 : dna == 1 ? 100 : 120;
        juce::Image img (juce::Image::RGB, T, T, true);
        juce::Graphics g (img);
        auto tri = [&] (float x1, float y1, float x2, float y2, float x3, float y3, juce::Colour c)
        {
            juce::Path p; p.addTriangle (x1, y1, x2, y2, x3, y3);
            g.setColour (c); g.fillPath (p);
        };
        auto quad = [&] (float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, juce::Colour c)
        {
            juce::Path p; p.startNewSubPath (x1, y1); p.lineTo (x2, y2); p.lineTo (x3, y3); p.lineTo (x4, y4); p.closeSubPath();
            g.setColour (c); g.fillPath (p);
        };
        auto dashedH = [&] (float x0, float x1, float y, float wth, juce::Colour c)
        {
            g.setColour (c);
            for (float x = x0; x < x1; x += 6.0f) g.fillRect (x, y - wth * 0.5f, 4.0f, wth);
        };

        if (dna == 0)            // RHODES: royal weave stripe bands + split diamonds
        {
            g.fillAll (gold);
            auto stripeBand = [&] (float y0)
            {
                g.setColour (dark); g.fillRect (0.0f, y0, 150.0f, 40.0f);
                g.setColour (green); g.fillRect (0.0f, y0 + 7.0f, 150.0f, 6.0f);
                g.setColour (red);   g.fillRect (0.0f, y0 + 17.0f, 150.0f, 6.0f);
                g.setColour (gold);  g.fillRect (0.0f, y0 + 27.0f, 150.0f, 6.0f);
            };
            stripeBand (0.0f);
            g.setColour (green); g.fillRect (0.0f, 45.0f, 150.0f, 60.0f);
            quad (37.5f, 45, 75, 75, 37.5f, 105, 0, 75, red);      // split diamond 1
            tri (37.5f, 45, 75, 75, 37.5f, 105, dark);
            quad (112.5f, 45, 150, 75, 112.5f, 105, 75, 75, gold); // split diamond 2
            tri (112.5f, 45, 150, 75, 112.5f, 105, dark);
            stripeBand (110.0f);
            g.setColour (burg);
            g.fillRect (0.0f, 40.0f, 150.0f, 5.0f);
            g.fillRect (0.0f, 105.0f, 150.0f, 5.0f);
        }
        else if (dna == 1)       // MKS: checker/diamond column + zigzag column
        {
            g.fillAll (gold);
            g.setColour (burg); g.fillRect (0.0f, 0.0f, 50.0f, 100.0f);
            quad (25, 10, 45, 25, 25, 40, 5, 25, green);
            quad (25, 60, 45, 75, 25, 90, 5, 75, dark);
            g.setColour (gold); g.fillRect (0.0f, 45.0f, 50.0f, 10.0f);
            tri (50, 10, 75, 20, 50, 30, green);
            tri (50, 50, 75, 60, 50, 70, green);
            tri (100, 25, 75, 35, 100, 45, dark);
            tri (100, 75, 75, 85, 100, 95, dark);
            g.setColour (cream.withAlpha (0.55f)); g.fillRect (49.0f, 0.0f, 1.5f, 100.0f);
        }
        else if (dna == 2)       // FM: vertical stripes + offset colored blocks
        {
            g.fillAll (gold);
            g.setColour (dark);
            for (float x : { 15.0f, 45.0f, 75.0f, 105.0f }) g.fillRect (x - 3.0f, 0.0f, 6.0f, 120.0f);
            g.setColour (blue); g.fillRect (0.0f, 20.0f, 60.0f, 40.0f);
            g.setColour (gold); g.fillRect (0.0f, 35.0f, 60.0f, 10.0f);
            g.setColour (red);  g.fillRect (60.0f, 60.0f, 60.0f, 40.0f);
            g.setColour (green); g.fillRect (60.0f, 75.0f, 60.0f, 10.0f);
            dashedH (0, 60, 25.0f, 2.0f, dark);
            dashedH (60, 120, 65.0f, 2.0f, dark);
        }
        else                     // STRINGS: radial anansi-web with shards
        {                        // (fine threadwork — abstract hub, no literal spider)
            g.fillAll (dark);
            tri (0, 0, 60, 60, 0, 60, red);
            tri (120, 0, 60, 60, 120, 60, green);
            tri (60, 60, 0, 120, 120, 120, gold);
            tri (0, 0, 120, 0, 60, 60, blk);
            g.setColour (gold);
            auto dashedLine = [&] (float x0, float y0, float x1, float y1)
            {
                const float len = std::hypot (x1 - x0, y1 - y0);
                const float dx = (x1 - x0) / len, dy = (y1 - y0) / len;
                for (float d = 0; d < len; d += 4.0f)
                    g.drawLine (x0 + dx * d, y0 + dy * d, x0 + dx * juce::jmin (d + 2.5f, len), y0 + dy * juce::jmin (d + 2.5f, len), 1.1f);
            };
            dashedLine (60, 60, 0, 0);   dashedLine (60, 60, 120, 0);
            dashedLine (60, 60, 0, 120); dashedLine (60, 60, 120, 120);
            g.drawLine (60, 0, 60, 120, 1.0f);
            g.drawLine (0, 60, 120, 60, 1.0f);
            auto octagon = [&] (const float* pts, float thickness)
            {
                juce::Path p;
                p.startNewSubPath (pts[0], pts[1]);
                for (int i = 1; i < 8; ++i) p.lineTo (pts[i * 2], pts[i * 2 + 1]);
                p.closeSubPath();
                g.strokePath (p, juce::PathStrokeType (thickness));
            };
            const float o1[] = { 60, 25, 85, 35, 95, 60, 85, 85, 60, 95, 35, 85, 25, 60, 35, 35 };
            const float o2[] = { 60, 10, 95, 25, 110, 60, 95, 95, 60, 110, 25, 95, 10, 60, 25, 25 };
            octagon (o1, 1.2f);
            octagon (o2, 0.9f);
            g.fillEllipse (56.0f, 56.0f, 8.0f, 8.0f);   // web hub
        }

        // shared woven threadwork over the pattern (keeps it reading as cloth)
        g.setColour (clampCloth (Theme::goldDark.interpolatedWith (tint, 0.3f), maxBri).withAlpha (Theme::Kente::warpAlpha * 0.9f));
        for (int x = 0; x < T; x += 2) g.fillRect (x, 0, 1, T);
        g.setColour (juce::Colours::black.withAlpha (Theme::Kente::warpAlpha));
        for (int y = 1; y < T; y += 3) g.fillRect (0, y, T, 1);
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
