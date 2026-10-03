#include "features/input/capture_chord.hpp"

#include <cstddef>

namespace evr::input {

CaptureChord::CaptureChord(AnalogThresholds trigger, CaptureButtons buttons, float holdSeconds)
    : triggers_{AnalogButton(trigger), AnalogButton(trigger)}, buttons_(buttons), sticks_(holdSeconds) {}

CaptureChordOutput CaptureChord::update(const InputFrame& frame, float dtSeconds) {
    CaptureChordOutput out;
    const bool menu = frame.left.menuButton;
    StickChordOutput sticks;
    if (buttons_ == CaptureButtons::MenuOrSticks) {
        sticks = sticks_.update(frame.left.stickClick, frame.right.stickClick, dtSeconds);
    }
    // The sticks count from the hold time on, like the recenter: a quick two-stick click is not the chord.
    const bool sticksHeld = sticks.recenter;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const auto i = static_cast<std::size_t>(hand);
        const ButtonState trigger = triggers_[i].update(frame.hand(hand).trigger);
        const bool chord = (menu || sticksHeld) && trigger.pressed;
        out.capture = out.capture || chord;
        // Held back from the moment Menu is down (a trigger already pulled included), or from a pull made
        // while the sticks are held, until the trigger is up with Menu up too.
        heldBack_[i] = trigger.down && (menu || chord || heldBack_[i]);
        out.triggerHeldBack[i] = menu || heldBack_[i];
    }
    capturedMenu_ = menu && (capturedMenu_ || out.capture);
    capturedSticks_ = sticks.chord && (capturedSticks_ || (sticksHeld && out.capture));
    out.cancelMenu = capturedMenu_;
    out.cancelSticks = capturedSticks_;
    return out;
}

} // namespace evr::input
