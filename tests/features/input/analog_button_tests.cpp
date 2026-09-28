#include "features/input/analog_button.hpp"

#include <doctest/doctest.h>

#include <initializer_list>
#include <limits>
#include <ostream>

using evr::input::AnalogButton;
using evr::input::AnalogThresholds;
using evr::input::ButtonState;

TEST_CASE("press and release thresholds give one clean press") {
    AnalogButton trigger({0.55f, 0.35f});

    CHECK_FALSE(trigger.update(0.5f).down);
    const ButtonState press = trigger.update(0.6f);
    CHECK(press.down);
    CHECK(press.pressed);
    CHECK_FALSE(press.released);

    // Hovering between the thresholds holds the button without new edges.
    for (const float value : {0.5f, 0.4f, 0.54f, 0.36f}) {
        const ButtonState state = trigger.update(value);
        CHECK(state.down);
        CHECK_FALSE(state.pressed);
        CHECK_FALSE(state.released);
    }

    const ButtonState release = trigger.update(0.3f);
    CHECK_FALSE(release.down);
    CHECK(release.released);
    CHECK_FALSE(release.pressed);

    // Back in the band from below: still up.
    CHECK_FALSE(trigger.update(0.5f).down);
}

TEST_CASE("chatter around a single value produces no extra edges") {
    AnalogButton trigger({0.55f, 0.35f});
    int presses = 0;
    for (int i = 0; i < 20; ++i) {
        presses += trigger.update(i % 2 == 0 ? 0.56f : 0.52f).pressed ? 1 : 0;
    }
    CHECK(presses == 1);
}

TEST_CASE("non-finite values release the button") {
    AnalogButton grip;
    grip.update(1.0f);
    const ButtonState state = grip.update(std::numeric_limits<float>::quiet_NaN());
    CHECK_FALSE(state.down);
    CHECK(state.released);
}

TEST_CASE("unusable thresholds fall back") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const AnalogThresholds& bad :
         {AnalogThresholds{nan, 0.3f}, AnalogThresholds{0.5f, nan}, AnalogThresholds{0.3f, 0.6f},
          AnalogThresholds{0.0f, 0.0f}, AnalogThresholds{1.5f, 0.3f}, AnalogThresholds{0.5f, -0.1f}}) {
        const AnalogThresholds result = evr::input::sanitizedThresholds(bad, evr::input::kGripThresholds);
        CHECK(result.press == evr::input::kGripThresholds.press);
        CHECK(result.release == evr::input::kGripThresholds.release);
    }
    // A NaN press threshold would leave the button impossible to press.
    AnalogButton button({nan, 0.3f});
    CHECK(button.update(1.0f).pressed);
}
