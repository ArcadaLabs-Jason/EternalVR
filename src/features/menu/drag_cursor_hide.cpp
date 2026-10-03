#include "features/menu/drag_cursor_hide.hpp"

#include <cmath>

namespace evr::menu {

DragCursorHide::DragCursorHide(DragCursorHideTuning tuning) : tuning_(tuning) {
    const DragCursorHideTuning defaults;
    if (!(std::isfinite(tuning_.linger) && tuning_.linger >= 0.0)) {
        tuning_.linger = defaults.linger;
    }
    if (!(std::isfinite(tuning_.maxHold) && tuning_.maxHold >= tuning_.linger)) {
        tuning_.maxHold = tuning_.linger > defaults.maxHold ? tuning_.linger : defaults.maxHold;
    }
}

void DragCursorHide::reset() {
    hidden_ = false;
    releasedAt_ = -1.0;
}

bool DragCursorHide::update(const DragCursorHideInput& in) {
    if (!in.menuActive) {
        reset();
        return false;
    }
    if (in.dragOwnsCursor) {
        hidden_ = true;
        releasedAt_ = -1.0;
        return true;
    }
    if (!hidden_) {
        return false;
    }
    if (releasedAt_ < 0.0 || !std::isfinite(in.seconds) || in.seconds < releasedAt_) {
        releasedAt_ = in.seconds;
    }
    const double since = in.seconds - releasedAt_;
    if ((in.cursorSettled && since >= tuning_.linger) || since >= tuning_.maxHold || !std::isfinite(since)) {
        reset();
    }
    return hidden_;
}

} // namespace evr::menu
