// Assets.h — interim icon set (docs/jm-design-reference/kente/assets/*.svg)
// kept for icons the V2 pack ships no counterpart for (search, filter, ...).
// Icons ship with a gold #D9A648 stroke; they are retinted programmatically so
// Theme.h stays the single colour source.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Theme.h"

namespace jm::assets {

enum class Icon { search, filter, star, heart, left, right, down, more, keyboard, cart, key };

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
        // CLIENT REVIEW ROUND 3 (C): the store/cart icon becomes a KEY.
        case Icon::key: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="7.5" cy="15.5" r="4.5"></circle><line x1="10.8" y1="12.2" x2="20.5" y2="2.5"></line><line x1="17.5" y1="5.5" x2="20" y2="8"></line><line x1="14.8" y1="8.2" x2="17" y2="10.4"></line></svg>)svg";
        case Icon::keyboard: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="6" width="20" height="12" rx="2" ry="2"></rect><line x1="6" y1="6" x2="6" y2="14"></line><line x1="10" y1="6" x2="10" y2="14"></line><line x1="14" y1="6" x2="14" y2="14"></line><line x1="18" y1="6" x2="18" y2="14"></line><line x1="2" y1="14" x2="22" y2="14"></line></svg>)svg";
        default: return R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="#D9A648" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="9" cy="21" r="1"></circle><circle cx="20" cy="21" r="1"></circle><path d="M1 1h4l2.68 13.39a2 2 0 0 0 2 1.61h9.72a2 2 0 0 0 2-1.61L23 6H6"></path></svg>)svg";
    }
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
