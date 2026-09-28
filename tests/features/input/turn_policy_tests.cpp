#include "features/input/turn_policy.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <ostream>

using evr::input::Axis2;
using evr::input::TurnMode;
using evr::input::TurnPolicy;
using evr::input::TurnSettings;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

TurnSettings snapSettings(float degrees = 45.0f) {
    TurnSettings settings;
    settings.mode = TurnMode::Snap;
    settings.snapDegrees = degrees;
    return settings;
}

} // namespace

TEST_CASE("smooth turn integrates the rate over time") {
    TurnPolicy policy;
    // Full right for one second at the default 230 deg/s turns 230 degrees clockwise.
    float total = 0.0f;
    for (int i = 0; i < 90; ++i) {
        total += policy.update({1.0f, 0.0f}, kFrame, true);
    }
    CHECK(total == doctest::Approx(-230.0f).epsilon(0.001));
}

TEST_CASE("smooth turn to the left is positive yaw") {
    TurnPolicy policy;
    CHECK(policy.update({-1.0f, 0.0f}, 0.1f, true) == doctest::Approx(23.0f));
}

TEST_CASE("smooth turn respects the deadzone and the block") {
    TurnPolicy policy;
    CHECK(policy.update({0.1f, 0.0f}, 0.1f, true) == 0.0f);
    CHECK(policy.update({1.0f, 0.0f}, 0.1f, false) == 0.0f);
}

TEST_CASE("smooth rate is clamped to the offered range") {
    TurnSettings slow;
    slow.smoothDegreesPerSecond = 50.0f;
    CHECK(TurnPolicy(slow).settings().smoothDegreesPerSecond == 150.0f);

    TurnSettings fast;
    fast.smoothDegreesPerSecond = 1000.0f;
    CHECK(TurnPolicy(fast).settings().smoothDegreesPerSecond == 400.0f);
}

TEST_CASE("snap turn fires once per flick, however long the stick is held") {
    TurnPolicy policy(snapSettings());
    CHECK(policy.update({0.9f, 0.0f}, kFrame, true) == doctest::Approx(-45.0f));
    for (int i = 0; i < 200; ++i) {
        CHECK(policy.update({1.0f, 0.0f}, kFrame, true) == 0.0f);
    }
}

TEST_CASE("snap turn does not repeat until the stick is centred") {
    TurnPolicy policy(snapSettings(30.0f));
    CHECK(policy.update({1.0f, 0.0f}, kFrame, true) == doctest::Approx(-30.0f));
    // Easing off but not centring, then pushing again: no second snap.
    CHECK(policy.update({0.3f, 0.0f}, kFrame, true) == 0.0f);
    // Wandering down with little x is still not centred.
    CHECK(policy.update({0.1f, -0.8f}, kFrame, true) == 0.0f);
    CHECK(policy.update({1.0f, 0.0f}, kFrame, true) == 0.0f);
    // Centred, then a second flick the other way.
    CHECK(policy.update({0.0f, 0.0f}, kFrame, true) == 0.0f);
    CHECK(policy.update({-1.0f, 0.0f}, kFrame, true) == doctest::Approx(30.0f));
}

TEST_CASE("a flick made while turning is blocked does not snap late") {
    TurnPolicy policy(snapSettings());
    CHECK(policy.update({1.0f, 0.0f}, kFrame, false) == 0.0f);
    CHECK(policy.update({1.0f, 0.0f}, kFrame, true) == 0.0f);
}

TEST_CASE("off never turns") {
    TurnSettings settings;
    settings.mode = TurnMode::Off;
    TurnPolicy policy(settings);
    CHECK(policy.update({1.0f, 0.0f}, 1.0f, true) == 0.0f);
}

TEST_CASE("a non-finite stick mid flick neither turns nor re-arms the snap") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    TurnPolicy policy(snapSettings());
    CHECK(policy.update({1.0f, 0.0f}, kFrame, true) == doctest::Approx(-45.0f));
    CHECK(policy.update({nan, nan}, kFrame, true) == 0.0f);
    // Still the same flick: no second snap.
    CHECK(policy.update({1.0f, 0.0f}, kFrame, true) == 0.0f);

    TurnPolicy smooth;
    CHECK(smooth.update({nan, 0.0f}, kFrame, true) == 0.0f);
}

TEST_CASE("non-finite or inconsistent turn settings fall back to the defaults") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const TurnSettings defaults;

    TurnSettings broken;
    broken.smoothDegreesPerSecond = nan;
    broken.snapDegrees = std::numeric_limits<float>::infinity();
    broken.smoothResponse = {-0.3f, 0.95f, 1.5f};
    broken.snapEngage = 0.2f;
    broken.snapRearm = 0.5f; // Re-arm outside the engage deflection.
    const TurnPolicy policy(broken);
    CHECK(policy.settings().smoothDegreesPerSecond == defaults.smoothDegreesPerSecond);
    CHECK(policy.settings().snapDegrees == defaults.snapDegrees);
    CHECK(policy.settings().smoothResponse.deadzone == defaults.smoothResponse.deadzone);
    CHECK(policy.settings().snapEngage == defaults.snapEngage);
    CHECK(policy.settings().snapRearm == defaults.snapRearm);

    TurnSettings nanThresholds;
    nanThresholds.snapEngage = nan;
    CHECK(TurnPolicy(nanThresholds).settings().snapEngage == defaults.snapEngage);

    // Rates and angles that are merely out of range are still clamped, not reset.
    TurnSettings snappy;
    snappy.snapDegrees = 120.0f;
    CHECK(TurnPolicy(snappy).settings().snapDegrees == 90.0f);
}
