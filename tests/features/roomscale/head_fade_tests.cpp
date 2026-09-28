#include "features/roomscale/head_fade.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <ostream>

using evr::Vec3;
using namespace evr::roomscale;

namespace {

constexpr double kFrame = 1.0 / 90.0;

// Seconds until the fade first reaches 1 while `depthAt(t)` is fed; -1 if it never does within `limit`.
template <typename DepthAt>
double secondsToBlack(HeadFade& fade, DepthAt depthAt, double limit = 2.0) {
    for (int frame = 1; frame * kFrame <= limit; ++frame) {
        const double t = frame * kFrame;
        if (fade.update(depthAt(t), kFrame) >= 1.0f) {
            return t;
        }
    }
    return -1.0;
}

} // namespace

TEST_CASE("the fade target follows the depth") {
    const FadeTiming timing;
    CHECK(fadeTarget(0.0f, timing) == 0.0f);
    CHECK(fadeTarget(-1.0f, timing) == 0.0f);
    CHECK(fadeTarget(0.05f, timing) == doctest::Approx(0.5f));
    CHECK(fadeTarget(0.10f, timing) == 1.0f);
    CHECK(fadeTarget(0.40f, timing) == 1.0f);
    CHECK(fadeTarget(std::nanf(""), timing) == 0.0f);
}

TEST_CASE("a head put into a wall is black within 150 ms") {
    HeadFade fade;
    const double t = secondsToBlack(fade, [](double) { return 0.2f; });
    CHECK(t > 0.0);
    CHECK(t <= 0.150);
}

TEST_CASE("a head walked into a wall at 1 m/s is black within 150 ms of touching") {
    HeadFade fade;
    const double t = secondsToBlack(fade, [](double s) { return static_cast<float>(1.0 * s); });
    CHECK(t > 0.0);
    CHECK(t <= 0.150);
}

TEST_CASE("the fade is partial at partial depth and clears when the head comes out") {
    HeadFade fade;
    for (int i = 0; i < 90; ++i) {
        fade.update(0.03f, kFrame);
    }
    CHECK(fade.value() == doctest::Approx(0.3f));
    for (int i = 0; i < 3; ++i) {
        fade.update(0.0f, kFrame);
    }
    CHECK(fade.value() < 0.3f);
    CHECK(fade.value() > 0.0f); // never snaps clear in one frame
    for (int i = 0; i < 90; ++i) {
        fade.update(0.0f, kFrame);
    }
    CHECK(fade.value() == 0.0f);
}

TEST_CASE("a bad time step changes nothing") {
    HeadFade fade;
    fade.update(1.0f, 0.05);
    const float v = fade.value();
    CHECK(fade.update(1.0f, -1.0) == v);
    CHECK(fade.update(1.0f, std::nan("")) == v);
}

TEST_CASE("clearance: a clear sweep is valid, a hit gives the first contact") {
    HeadClearance clearance;
    const ClearanceStep clear = clearance.update({0.2f, 0.0f, 0.0f}, std::nullopt, 1.0f);
    CHECK_FALSE(clear.blocked);
    CHECK(clear.validOffset.x == doctest::Approx(0.2f));
    CHECK(clear.penetrationMetres == 0.0f);
    // The head moves 0.5 m out; the sphere touches at 60 % of the way: 0.2 m inside.
    const ClearanceStep hit = clearance.update({0.5f, 0.0f, 0.0f}, 0.6f, 1.0f);
    CHECK(hit.blocked);
    CHECK(hit.penetrationMetres == doctest::Approx(0.2f));
    CHECK(hit.validOffset.x == doctest::Approx(0.3f));
    // In game units at 2 units per metre, the depth is still in metres.
    CHECK(clearance.update({1.0f, 0.0f, 0.0f}, 0.6f, 2.0f).penetrationMetres == doctest::Approx(0.2f));
    // A fraction out of range counts as clear.
    CHECK_FALSE(clearance.update({0.1f, 0.0f, 0.0f}, 1.5f, 1.0f).blocked);
    CHECK(clearance.lastClear().x == doctest::Approx(0.1f));
}

TEST_CASE("shots start at the contact, never inside the wall, while the head is in it") {
    HeadClearance clearance;
    // The body walks up to a wall with the head 0.6 m ahead: the offset was clear the frame before, but now
    // the wall is 0.3 m ahead, so the contact (not the old offset) is where shots may start.
    clearance.update({0.6f, 0.0f, 0.0f}, std::nullopt, 1.0f);
    for (int i = 0; i < 10; ++i) {
        const float fraction = 0.5f - 0.02f * static_cast<float>(i);
        const ClearanceStep s = clearance.update({0.6f, 0.0f, 0.0f}, fraction, 1.0f);
        CHECK(s.blocked);
        CHECK(s.validOffset.x == doctest::Approx(0.6f * fraction));
        CHECK(s.validOffset.x < 0.6f * fraction + 1e-6f);
    }
}
