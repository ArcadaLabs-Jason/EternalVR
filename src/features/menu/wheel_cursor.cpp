#include "features/menu/wheel_cursor.hpp"

namespace evr::menu {

WheelCursorOutput WheelCursor::update(const WheelCursorInput& in) {
    WheelCursorOutput out;
    if (owned_) {
        if (!in.cursorShown) {
            owned_ = false;
            out.ended = true;
        } else if (in.wheelHeld) {
            releasedAt_ = -1.0;
        } else {
            if (releasedAt_ < 0.0) {
                releasedAt_ = in.seconds;
            }
            if (in.seconds - releasedAt_ >= grace_) {
                owned_ = false;
                out.ended = true;
            }
        }
    } else if (in.cursorShown && !wasShown_ && !in.menuUp && in.wheelHeld) {
        // The cursor came up while the wheel is held: the wheel's.
        owned_ = true;
        releasedAt_ = -1.0;
        out.started = true;
    }
    wasShown_ = in.cursorShown;
    out.owned = owned_;
    return out;
}

} // namespace evr::menu
