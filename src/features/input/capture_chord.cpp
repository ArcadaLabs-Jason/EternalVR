#include "features/input/capture_chord.hpp"

#include <cstddef>

namespace evr::input {

CaptureChord::CaptureChord(AnalogThresholds trigger)
    : triggers_{AnalogButton(trigger), AnalogButton(trigger)} {}

CaptureChordOutput CaptureChord::update(const InputFrame& frame) {
    CaptureChordOutput out;
    const bool menu = frame.left.menuButton;
    for (const Hand hand : {Hand::Left, Hand::Right}) {
        const auto i = static_cast<std::size_t>(hand);
        const ButtonState trigger = triggers_[i].update(frame.hand(hand).trigger);
        const bool down = trigger.down;
        out.capture = out.capture || (menu && trigger.pressed);
        // Held back from the moment Menu is down (a trigger already pulled included) until the trigger is
        // up with Menu up too.
        heldBack_[i] = down && (menu || heldBack_[i]);
        out.triggerHeldBack[i] = menu || heldBack_[i];
    }
    captured_ = menu && (captured_ || out.capture);
    out.cancelMenu = captured_;
    return out;
}

} // namespace evr::input
