#include "features/input/tap_hold.hpp"

namespace evr::input {

TapHoldOutput TapHoldDetector::update(bool down, float dtSeconds) {
    TapHoldOutput output;
    if (down) {
        heldSeconds_ = wasDown_ ? heldSeconds_ + dtSeconds : 0.0f;
        output.hold = !cancelled_ && heldSeconds_ >= holdSeconds_;
    } else if (wasDown_) {
        output.tap = !cancelled_ && heldSeconds_ < tapSeconds_;
        heldSeconds_ = 0.0f;
        cancelled_ = false;
    } else {
        cancelled_ = false; // a cancel with the button up applies to no press
    }
    wasDown_ = down;
    return output;
}

} // namespace evr::input
