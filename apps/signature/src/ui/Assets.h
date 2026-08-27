// Assets.h — client UX asset pack (docs/jm-design-reference/kente/assets/*.svg)
// embedded as inline string constants and loaded via juce::Drawable::createFromSVG.
// Icons ship with a gold #D9A648 stroke; they are retinted programmatically so
// Theme.h stays the single colour source. The knob engraving is a swappable
// asset (client will deliver more adinkra symbols).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Theme.h"

namespace jm::assets {

enum class Icon { search, filter, star, heart, left, right, down, more, keyboard, cart };

inline const char* iconSvg (Icon ic)
{
    switch (ic)
    {
        case Icon::search: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="11" cy="11" r="8"></circle><line x1="21" y1="21" x2="16.65" y2="16.65"></line></svg>)svg";
        case Icon::filter: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="22 3 2 3 10 12.46 10 19 14 21 14 12.46 22 3"></polygon></svg>)svg";
        case Icon::star: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="#D9A648" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2"></polygon></svg>)svg";
        case Icon::heart: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z"></path></svg>)svg";
        case Icon::left: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="15 18 9 12 15 6"></polyline></svg>)svg";
        // right = client icon-left mirrored (not a separate pack asset)
        case Icon::right: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg>)svg";
        case Icon::down: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="6 9 12 15 18 9"></polyline></svg>)svg";
        case Icon::more: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="#D9A648" stroke="none"><circle cx="12" cy="5" r="2"></circle><circle cx="12" cy="12" r="2"></circle><circle cx="12" cy="19" r="2"></circle></svg>)svg";
        case Icon::keyboard: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="6" width="20" height="12" rx="2" ry="2"></rect><line x1="6" y1="6" x2="6" y2="14"></line><line x1="10" y1="6" x2="10" y2="14"></line><line x1="14" y1="6" x2="14" y2="14"></line><line x1="18" y1="6" x2="18" y2="14"></line><line x1="2" y1="14" x2="22" y2="14"></line></svg>)svg";
        default: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="9" cy="21" r="1"></circle><circle cx="20" cy="21" r="1"></circle><path d="M1 1h4l2.68 13.39a2 2 0 0 0 2 1.61h9.72a2 2 0 0 0 2-1.61L23 6H6"></path></svg>)svg";
    }
}

// client logo.svg (kept verbatim incl. its own gold gradient)
inline const char* logoSvg()
{
    return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 120" width="100" height="120">
  <defs><linearGradient id="gold-grad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#F2D06B"/><stop offset="45%" stop-color="#D9A648"/><stop offset="100%" stop-color="#8C6222"/>
  </linearGradient></defs>
  <path d="M50 4 L94 20 L94 65 C94 95 50 116 50 116 C50 116 6 95 6 65 L6 20 Z" fill="none" stroke="url(#gold-grad)" stroke-width="3"/>
  <path d="M50 12 L86 25 L86 63 C86 87 50 105 50 105 C50 105 14 87 14 63 L14 25 Z" fill="none" stroke="url(#gold-grad)" stroke-width="1.5" stroke-dasharray="4 2"/>
  <text x="50" y="72" font-family="Times New Roman, serif" font-weight="bold" font-size="42" letter-spacing="-2" fill="url(#gold-grad)" text-anchor="middle">JM</text>
</svg>)svg";
}

// client adinkra-symbol.svg — the current knob engraving. Swappable: the client
// will deliver more symbols; assign per-control by replacing/extending this.
inline juce::String& knobEngravingSvg()
{
    static juce::String svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100">
  <g stroke="#D9A648" stroke-width="4" fill="none" stroke-linecap="round" stroke-linejoin="round">
    <path d="M50 45 C50 45 35 20 50 15 C65 10 70 25 50 45 Z"/>
    <path d="M50 55 C50 55 65 80 50 85 C35 90 30 75 50 55 Z"/>
    <path d="M45 50 C45 50 20 65 15 50 C10 35 25 30 45 50 Z"/>
    <path d="M55 50 C55 50 80 35 85 50 C90 65 75 70 55 50 Z"/>
    <circle cx="50" cy="50" r="4" fill="#D9A648"/>
  </g>
</svg>)svg";
    return svg;
}

//==============================================================================
inline std::unique_ptr<juce::Drawable> drawableFromSvg (const juce::String& svg)
{
    if (auto xml = juce::XmlDocument::parse (svg))
        return juce::Drawable::createFromSVG (*xml);
    return nullptr;
}

/// Retint an asset that ships with the gold #D9A648 stroke/fill.
inline std::unique_ptr<juce::Drawable> makeTinted (const juce::String& svg, juce::Colour tint)
{
    return drawableFromSvg (juce::String (svg).replace ("#D9A648", "#" + tint.toDisplayString (false), true));
}

inline std::unique_ptr<juce::Drawable> makeIcon (Icon ic, juce::Colour tint = Theme::gold)
{
    return makeTinted (iconSvg (ic), tint);
}

inline std::unique_ptr<juce::Drawable> makeLogo() { return drawableFromSvg (logoSvg()); }

inline std::unique_ptr<juce::Drawable> makeEngraving (juce::Colour tint)
{
    return makeTinted (knobEngravingSvg(), tint);
}

/// Icon button with normal/over/down tints from Theme.
inline std::unique_ptr<juce::DrawableButton> makeIconButton (const juce::String& name, Icon ic,
                                                             juce::Colour normal = Theme::gold)
{
    auto b = std::make_unique<juce::DrawableButton> (name, juce::DrawableButton::ImageFitted);
    auto n = makeIcon (ic, normal);
    auto over = makeIcon (ic, Theme::goldHi);
    auto down = makeIcon (ic, Theme::goldHi);
    b->setImages (n.get(), over.get(), down.get());
    b->setColour (juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    b->setColour (juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
    return b;
}

} // namespace jm::assets
