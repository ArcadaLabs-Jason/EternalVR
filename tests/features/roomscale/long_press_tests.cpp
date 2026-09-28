#include "features/roomscale/long_press.hpp"

#include <doctest/doctest.h>

#include <ostream>

using evr::roomscale::LongPress;

TEST_CASE("fires once after the hold time and re-arms on release") {
    LongPress press(1.0f);
    CHECK_FALSE(press.update(true, 10.0));
    CHECK_FALSE(press.update(true, 10.5));
    CHECK(press.update(true, 11.0));
    CHECK_FALSE(press.update(true, 11.5));
    CHECK_FALSE(press.update(true, 20.0));
    CHECK_FALSE(press.update(false, 20.1));
    CHECK_FALSE(press.update(true, 21.0));
    CHECK(press.update(true, 22.1));
}

TEST_CASE("a short press never fires") {
    LongPress press(1.0f);
    for (int i = 0; i < 5; ++i) {
        CHECK_FALSE(press.update(true, i * 2.0));
        CHECK_FALSE(press.update(true, i * 2.0 + 0.9));
        CHECK_FALSE(press.update(false, i * 2.0 + 1.0));
    }
}

TEST_CASE("an unusable duration falls back to 1 s") {
    CHECK(LongPress(-1.0f).seconds() == doctest::Approx(1.0f));
    CHECK(LongPress(0.0f).seconds() == doctest::Approx(1.0f));
}
