#include "features/input/capture_chord.hpp"

#include <cstddef>

namespace evr::input {

CaptureChord::CaptureChord(AnalogThresholds trigger, CaptureButtons buttons)
    : triggers_{AnalogButton(trigger), AnalogButton(trigger)}, buttons_(buttons) {}

CaptureChordOutput CaptureChord::update(const InputFrame& frame) {
    CaptureChordOutput out;
    const bool menu = frame.left.menuButton;
    const bool secondary = buttons_ == CaptureButtons::MenuOrSecondary && frame.left.secondaryButton;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const auto i = static_cast<std::size_t>(hand);
        const ButtonState trigger = triggers_[i].update(frame.hand(hand).trigger);
        const bool chord = (menu || secondary) && trigger.pressed;
        out.capture = out.capture || chord;
        // Held back from the moment Menu is down (a trigger already pulled included), or from a pull made
        // while the secondary button is down, until the trigger is up with Menu up too.
        heldBack_[i] = trigger.down && (menu || chord || heldBack_[i]);
        out.triggerHeldBack[i] = menu || heldBack_[i];
    }
    capturedMenu_ = menu && (capturedMenu_ || out.capture);
    capturedSecondary_ = secondary && (capturedSecondary_ || out.capture);
    out.cancelMenu = capturedMenu_;
    out.cancelSecondary = capturedSecondary_;
    return out;
}

} // namespace evr::input
