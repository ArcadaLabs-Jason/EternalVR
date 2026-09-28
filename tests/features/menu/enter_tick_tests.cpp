#include "features/menu/enter_tick.hpp"

#include <doctest/doctest.h>

using evr::menu::EnterTick;

TEST_CASE("the enter tick plays once as the ray comes onto the panel") {
    EnterTick tick;
    CHECK_FALSE(tick.update(false, 0.0));
    CHECK(tick.update(true, 0.1));
    CHECK_FALSE(tick.update(true, 0.2));
    CHECK_FALSE(tick.update(true, 5.0));
}

TEST_CASE("frames without controller data neither end nor start a stay on the panel") {
    EnterTick tick;
    CHECK(tick.update(true, 0.0));
    // A stale snapshot every few frames: no repeat ticks (the main-menu buzz).
    for (int i = 1; i < 90; ++i) {
        const double t = i / 90.0;
        CHECK_FALSE(tick.update(i % 6 == 0 ? std::nullopt : std::optional<bool>(true), t));
    }
    CHECK_FALSE(tick.update(std::nullopt, 2.0));
}

TEST_CASE("a short miss at the panel's edge does not tick again") {
    EnterTick tick;
    CHECK(tick.update(true, 0.0));
    CHECK_FALSE(tick.update(false, 0.011));
    CHECK_FALSE(tick.update(true, 0.022));
    CHECK_FALSE(tick.update(false, 0.1));
    CHECK_FALSE(tick.update(true, 0.2)); // off since 0.022 on-frame: 0.178 s
}

TEST_CASE("the ray coming back after a real stay off the panel ticks again") {
    EnterTick tick;
    CHECK(tick.update(true, 0.0));
    CHECK_FALSE(tick.update(false, 0.1));
    CHECK(tick.update(true, 0.4));
}

TEST_CASE("reset lets the next panel tick at once") {
    EnterTick tick;
    CHECK(tick.update(true, 0.0));
    tick.reset();
    CHECK(tick.update(true, 0.05));
}
