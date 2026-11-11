// LongPress.h — a finger held still is a right-click (the iPad app).
//
// Several things in MIREKU live on the right mouse button: MIDI Learn on a
// knob, a scene's menu, rename on a preset row, a macro's menu. A touch screen
// has no right button, so on iOS a touch that stays down for a little over half
// a second without moving produces one.
//
// HOW. The right-click handlers are spread over many components, some on
// mouseDown and some on mouseUp, some through mouse listeners. Rather than
// teach each of them about touch, the long press is delivered as a REAL
// right-button press and release at the same point, through the window's own
// event entry (ComponentPeer::handleMouseEvent), on a touch index no finger
// uses. Every handler then runs exactly as it does for a mouse.
//
// The finger that is still down keeps its own press: lifting it afterwards is an
// ordinary release where it was pressed.
//
// Constructed by the editor on iOS only. Desktop builds do not include it in
// their behaviour at all.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace jm {

class LongPressRightClick : public juce::MouseListener, private juce::Timer
{
public:
    explicit LongPressRightClick (juce::Component& root) : root_ (root)
    {
        root_.addMouseListener (this, true);
    }
    ~LongPressRightClick() override { root_.removeMouseListener (this); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.source.getIndex() == kSyntheticTouch || e.mods.isPopupMenu()) return;
        if (! e.source.isTouch()) return;
        // a second finger means a gesture, not a held press
        if (isTimerRunning()) { stopTimer(); return; }
        down_ = e.getScreenPosition().toFloat();
        startTimer (kHoldMs);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.source.getIndex() == kSyntheticTouch) return;
        if (isTimerRunning() && e.getScreenPosition().toFloat().getDistanceFrom (down_) > kSlopPx)
            stopTimer();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.source.getIndex() == kSyntheticTouch) return;
        stopTimer();
    }

private:
    static constexpr int   kHoldMs = 550;
    static constexpr float kSlopPx = 10.0f;
    static constexpr int   kSyntheticTouch = 9;     // a touch index no real finger is given

    void timerCallback() override
    {
        stopTimer();
        auto* peer = root_.getPeer();
        if (peer == nullptr) return;
        const auto pos = peer->globalToLocal (down_);
        const auto now = juce::Time::currentTimeMillis();
        using Type = juce::MouseInputSource::InputSourceType;
        peer->handleMouseEvent (Type::touch, pos, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier),
                                juce::MouseInputSource::defaultPressure, 0.0f, now, {}, kSyntheticTouch);
        peer->handleMouseEvent (Type::touch, pos, juce::ModifierKeys(),
                                juce::MouseInputSource::defaultPressure, 0.0f, now + 1, {}, kSyntheticTouch);
    }

    juce::Component& root_;
    juce::Point<float> down_;
};

} // namespace jm
