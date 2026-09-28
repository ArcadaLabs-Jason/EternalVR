#include "features/menu/menu_router.hpp"
#include "features/menu/wheel_cursor.hpp"

#include <doctest/doctest.h>

using evr::menu::MenuRouter;
using evr::menu::WheelCursor;
using evr::menu::WheelCursorInput;
using evr::menu::WheelCursorOutput;

namespace {

WheelCursorInput frame(double t, bool cursorShown, bool wheelHeld, bool menuUp = false) {
    WheelCursorInput in;
    in.seconds = t;
    in.cursorShown = cursorShown;
    in.wheelHeld = wheelHeld;
    in.menuUp = menuUp;
    return in;
}

} // namespace

TEST_CASE("a cursor that comes up while the wheel is held is the wheel's, not a menu") {
    WheelCursor wheel;
    CHECK_FALSE(wheel.update(frame(0.0, false, true)).owned);
    const WheelCursorOutput up = wheel.update(frame(0.2, true, true));
    CHECK(up.owned);
    CHECK(up.started);
    const WheelCursorOutput still = wheel.update(frame(0.3, true, true));
    CHECK(still.owned);
    CHECK_FALSE(still.started);
    // The wheel closes on the release and takes its cursor with it.
    const WheelCursorOutput gone = wheel.update(frame(0.5, false, false));
    CHECK_FALSE(gone.owned);
    CHECK(gone.ended);
}

TEST_CASE("menus are unchanged: a cursor without the wheel, or a menu already up, stays a menu") {
    WheelCursor wheel;
    // Pause, a popup, the Dossier: the wheel is not held.
    CHECK_FALSE(wheel.update(frame(0.0, true, false)).owned);
    CHECK_FALSE(wheel.update(frame(0.1, true, true)).owned); // held later: the cursor was already up
    wheel.update(frame(0.2, false, false));
    // A menu the router already has (its panel held while the cursor blinks) is not taken over.
    CHECK_FALSE(wheel.update(frame(0.3, true, true, true)).owned);
}

TEST_CASE("a cursor that outlives the wheel becomes a menu after the grace") {
    WheelCursor wheel(0.3);
    CHECK(wheel.update(frame(0.0, true, true)).owned);
    CHECK(wheel.update(frame(0.1, true, false)).owned); // released: the game closes the wheel
    CHECK(wheel.update(frame(0.35, true, false)).owned);
    const WheelCursorOutput after = wheel.update(frame(0.41, true, false));
    CHECK_FALSE(after.owned);
    CHECK(after.ended);
    // From here on the cursor is the router's; holding the wheel again does not take it back.
    CHECK_FALSE(wheel.update(frame(0.5, true, true)).owned);
}

TEST_CASE("pressing the wheel again within the grace keeps the cursor the wheel's") {
    WheelCursor wheel(0.3);
    wheel.update(frame(0.0, true, true));
    wheel.update(frame(0.1, true, false));
    CHECK(wheel.update(frame(0.2, true, true)).owned);
    CHECK(wheel.update(frame(0.6, true, true)).owned);
}

TEST_CASE("the router carries the wheel's cursor state and stays out of it") {
    MenuRouter router;
    CHECK(router.wheelCursor().update(frame(0.0, true, true)).owned);
    CHECK(router.wheelCursor().owned());
    // The presenter then gives the router no menu: nothing is sent and gameplay is not held back.
    evr::menu::RouterInput in;
    in.seconds = 0.0;
    in.menuActive = false;
    const evr::menu::RouterOutput out = router.update(in);
    CHECK(out.events.empty());
    CHECK_FALSE(out.suppressGameplay);
    CHECK_FALSE(out.pointerVisible);
}
