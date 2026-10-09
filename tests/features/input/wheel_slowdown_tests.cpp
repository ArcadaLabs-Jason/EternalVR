#include "features/input/wheel_slowdown.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <ostream>

using evr::input::kSlowdownRestoreSeconds;
using evr::input::SlowdownAction;
using evr::input::WheelSlowdown;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

int framesFor(float seconds) {
    return static_cast<int>(seconds / kFrame) + 2;
}

} // namespace

TEST_CASE("with the game's slowdown on nothing is ever written") {
    WheelSlowdown slowdown(true);
    CHECK(slowdown.update(true, false, kFrame) == SlowdownAction::None);
    CHECK(slowdown.update(false, false, 1.0f) == SlowdownAction::None);
    CHECK(slowdown.update(false, true, kFrame) == SlowdownAction::None);
    CHECK(slowdown.reset() == SlowdownAction::None);
    CHECK_FALSE(slowdown.held());
}

TEST_CASE("slowdown off: held from the press until a while after the wheel lets go, once") {
    WheelSlowdown slowdown(false);
    CHECK(slowdown.update(false, false, kFrame) == SlowdownAction::None);
    CHECK(slowdown.update(true, false, kFrame) == SlowdownAction::Hold);
    for (int i = 0; i < 30; ++i) {
        CHECK(slowdown.update(true, false, kFrame) == SlowdownAction::None);
    }
    CHECK(slowdown.held());
    int restores = 0;
    int framesToRestore = 0;
    for (int i = 0; i < framesFor(kSlowdownRestoreSeconds) && restores == 0; ++i) {
        ++framesToRestore;
        restores += slowdown.update(false, false, kFrame) == SlowdownAction::Restore ? 1 : 0;
    }
    CHECK(restores == 1);
    CHECK(static_cast<float>(framesToRestore) * kFrame >= kSlowdownRestoreSeconds - kFrame);
    CHECK_FALSE(slowdown.held());
    CHECK(slowdown.update(false, false, 1.0f) == SlowdownAction::None);
}

TEST_CASE("a wheel picked again before the restore keeps the hold without a new write") {
    WheelSlowdown slowdown(false);
    slowdown.update(true, false, kFrame);
    slowdown.update(false, false, kSlowdownRestoreSeconds / 2.0f);
    CHECK(slowdown.update(true, false, kFrame) == SlowdownAction::None);
    CHECK(slowdown.update(false, false, kSlowdownRestoreSeconds / 2.0f) == SlowdownAction::None);
    CHECK(slowdown.held());
}

TEST_CASE("the stick's own wheel taking over gets the game's value back at once") {
    WheelSlowdown slowdown(false);
    slowdown.update(true, false, kFrame);
    CHECK(slowdown.update(false, true, kFrame) == SlowdownAction::Restore);
    CHECK(slowdown.update(false, true, kFrame) == SlowdownAction::None);
}

TEST_CASE("every other way out restores what is held, once") {
    WheelSlowdown slowdown(false);
    CHECK(slowdown.reset() == SlowdownAction::None);
    slowdown.update(true, false, kFrame);
    CHECK(slowdown.reset() == SlowdownAction::Restore);
    CHECK(slowdown.reset() == SlowdownAction::None);
    slowdown.update(true, false, std::numeric_limits<float>::quiet_NaN());
    CHECK(slowdown.held());
    CHECK(slowdown.update(false, false, std::numeric_limits<float>::infinity()) == SlowdownAction::None);
}
