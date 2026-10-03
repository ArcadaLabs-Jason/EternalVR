#include "features/menu/drag_cursor_hide.hpp"

#include <doctest/doctest.h>

#include <limits>

using evr::menu::DragCursorHide;
using evr::menu::DragCursorHideInput;
using evr::menu::DragCursorHideTuning;

namespace {

DragCursorHideInput frame(double t, bool drag, bool settled = true, bool menu = true) {
    DragCursorHideInput in;
    in.seconds = t;
    in.menuActive = menu;
    in.dragOwnsCursor = drag;
    in.cursorSettled = settled;
    return in;
}

} // namespace

TEST_CASE("drag cursor hide: shown until a stroke owns the cursor, hidden from that frame") {
    DragCursorHide hide;
    CHECK_FALSE(hide.update(frame(0.0, false)));
    CHECK_FALSE(hide.update(frame(0.1, false)));
    CHECK(hide.update(frame(0.2, true)));
    CHECK(hide.hidden());
    CHECK(hide.update(frame(0.3, true, false)));
}

TEST_CASE("drag cursor hide: after the last stroke it stays hidden until the cursor settles and the linger "
          "passes") {
    DragCursorHide hide; // linger 0.15, maxHold 0.5
    hide.update(frame(0.0, true));
    CHECK(hide.update(frame(1.0, false, false))); // the stroke ended; the move back to the ray is in flight
    CHECK(hide.update(frame(1.1, false, true)));  // settled, but within the linger
    CHECK_FALSE(hide.update(frame(1.16, false, true)));
    CHECK_FALSE(hide.update(frame(1.2, false, true)));
}

TEST_CASE("drag cursor hide: a cursor that never settles is shown after maxHold") {
    DragCursorHide hide;
    hide.update(frame(0.0, true));
    CHECK(hide.update(frame(1.0, false, false)));
    CHECK(hide.update(frame(1.4, false, false)));
    CHECK_FALSE(hide.update(frame(1.5, false, false)));
}

TEST_CASE(
    "drag cursor hide: a stick dipping through its deadzone between strokes does not flash the cursor") {
    DragCursorHide hide;
    double t = 0.0;
    bool alwaysHidden = true;
    for (int i = 0; i < 90; ++i, t += 1.0 / 90.0) {
        const bool drag = i % 10 != 9; // one idle frame every ten
        alwaysHidden = alwaysHidden && hide.update(frame(t, drag, i % 10 == 9));
    }
    CHECK(alwaysHidden);
}

TEST_CASE("drag cursor hide: the menu going shows the cursor at once and starts afresh") {
    DragCursorHide hide;
    CHECK(hide.update(frame(0.0, true)));
    CHECK_FALSE(hide.update(frame(0.01, true, true, false)));
    CHECK_FALSE(hide.hidden());
    CHECK_FALSE(hide.update(frame(0.02, false)));
    CHECK(hide.update(frame(0.03, true)));
    hide.reset();
    CHECK_FALSE(hide.hidden());
}

TEST_CASE("drag cursor hide: time going back or not finite restarts or ends the linger, never sticks") {
    DragCursorHide hide;
    hide.update(frame(5.0, true));
    CHECK(hide.update(frame(5.1, false, true)));
    CHECK(hide.update(frame(2.0, false, true))); // the clock went back: the linger starts again
    CHECK_FALSE(hide.update(frame(2.2, false, true)));
    hide.update(frame(3.0, true));
    CHECK_FALSE(hide.update(frame(std::numeric_limits<double>::quiet_NaN(), false, false)));
}

TEST_CASE("drag cursor hide: bad tuning falls back to the defaults") {
    DragCursorHideTuning t;
    t.linger = -1.0;
    t.maxHold = std::numeric_limits<double>::infinity();
    DragCursorHide hide(t);
    hide.update(frame(0.0, true));
    CHECK(hide.update(frame(1.0, false, true)));
    CHECK(hide.update(frame(1.1, false, true)));
    CHECK_FALSE(hide.update(frame(1.2, false, true)));
}
