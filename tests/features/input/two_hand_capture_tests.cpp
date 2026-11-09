// Two-handed aim's capture along the gun, its slide and the off glove's place on the gun
// (features/input/two_hand.hpp).

#include "features/input/two_hand.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

using evr::Quat;
using evr::Vec3;
using evr::input::SupportRow;
using evr::input::TwoHandBlock;
using evr::input::TwoHandEnd;
using evr::input::TwoHandFrame;
using evr::input::TwoHandHold;
using evr::input::TwoHandResult;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDt = 1.0f / 90.0f;

// Rows from data/weapons/viewmodel_offsets.toml.
constexpr SupportRow kShotgun{0.296f, 0.006f, 0.057f};
constexpr SupportRow kPlasma{0.756f, -0.015f, 0.093f};
constexpr SupportRow kRocket{0.163f, 0.003f, -0.004f};
// A neutral weapon hand: 0.2 m right, 0.3 m below and 0.4 m ahead of the head, pointing ahead (-Z).
constexpr Vec3 kGrip{0.2f, -0.3f, -0.4f};

float degrees(float radians) {
    return radians * 180.0f / kPi;
}

float angleBetween(Vec3 a, Vec3 b) {
    return degrees(std::atan2(evr::length(evr::cross(a, b)), evr::dot(a, b)));
}

TwoHandFrame frame(Vec3 offGrip, float grip, SupportRow row) {
    TwoHandFrame f;
    f.block = TwoHandBlock::None;
    f.offGrip = grip;
    f.weaponTracked = true;
    f.offTracked = true;
    f.weaponGrip = kGrip;
    f.offGripPoint = offGrip;
    f.support = row;
    f.dt = kDt;
    return f;
}

// The gun's support direction from the grip with the one-handed aim straight ahead.
Vec3 along(SupportRow row) {
    return evr::normalize(evr::input::supportPoint(kGrip, Quat{}, row) - kGrip);
}

// A unit vector square to the support direction (sideways).
Vec3 side(SupportRow row) {
    return evr::normalize(evr::cross(along(row), Vec3{0.0f, 1.0f, 0.0f}));
}

TwoHandResult grab(TwoHandHold& hold, Vec3 off, SupportRow row) {
    hold.update(frame(off, 0.0f, row));
    return hold.update(frame(off, 1.0f, row));
}

} // namespace

TEST_CASE("two-handed capture: anywhere along the gun's support line, not only at the support point") {
    // The Plasma Rifle's support point is 76 cm ahead of the weapon hand: an off hand 40 cm along its line
    // (36 cm short of it, where an arm reaches) takes the gun, with no turn at the grab.
    TwoHandHold hold;
    const Vec3 off = kGrip + along(kPlasma) * 0.40f;
    auto r = hold.update(frame(off, 0.0f, kPlasma));
    CHECK(r.capturable);
    CHECK(r.along == doctest::Approx(0.40f).epsilon(1e-3));
    CHECK(r.distance < 1e-4f);
    r = hold.update(frame(off, 1.0f, kPlasma));
    REQUIRE(r.engaged);
    CHECK(angleBetween(evr::input::supportPoint(kGrip, r.target, kPlasma) - kGrip, off - kGrip) < 0.01f);
    CHECK(r.slide == doctest::Approx(0.40f - evr::length(evr::input::supportInAim(kPlasma))).epsilon(1e-3));
    // 6 cm to the side there: still taken (within 8 cm of the line and 15 degrees of it).
    TwoHandHold sideways;
    CHECK(grab(sideways, off + side(kPlasma) * 0.06f, kPlasma).engaged);
    // 9 cm to the side: not.
    TwoHandHold far;
    CHECK_FALSE(grab(far, off + side(kPlasma) * 0.09f, kPlasma).engaged);
    // Past the support point by up to the capture radius: taken; further: not.
    TwoHandHold front;
    const float reach = evr::length(evr::input::supportInAim(kShotgun));
    CHECK(grab(front, kGrip + along(kShotgun) * (reach + 0.07f), kShotgun).engaged);
    TwoHandHold beyond;
    CHECK_FALSE(grab(beyond, kGrip + along(kShotgun) * (reach + 0.09f), kShotgun).engaged);
}

TEST_CASE("two-handed capture: no grab turns the gun by more than kTwoHandCaptureDegrees") {
    // 7 cm to the side of the Rocket Launcher's support point (16 cm from the grip) is a 23 degree turn: not
    // taken, though it is within 8 cm of the support point.
    const Vec3 support = evr::input::supportPoint(kGrip, Quat{}, kRocket);
    TwoHandHold hold;
    auto r = hold.update(frame(support + side(kRocket) * 0.07f, 0.0f, kRocket));
    CHECK(r.distance < evr::input::kTwoHandCaptureMetres);
    CHECK_FALSE(r.capturable);
    CHECK_FALSE(hold.update(frame(support + side(kRocket) * 0.07f, 1.0f, kRocket)).engaged);
    // 4 cm to the side (14 degrees): taken, and the turn at the grab is under the cap.
    TwoHandHold near;
    r = grab(near, support + side(kRocket) * 0.04f, kRocket);
    REQUIRE(r.engaged);
    const float turned =
        angleBetween(evr::input::supportPoint(kGrip, r.target, kRocket) - kGrip, support - kGrip);
    CHECK(turned > 10.0f);
    CHECK(turned <= evr::input::kTwoHandCaptureDegrees);
    // On the combat shotgun the 8 cm round its support point is what it was (14.9 degrees at most).
    TwoHandHold shotgun;
    const Vec3 s = evr::input::supportPoint(kGrip, Quat{}, kShotgun);
    CHECK(grab(shotgun, s + side(kShotgun) * 0.075f, kShotgun).engaged);
}

TEST_CASE("two-handed hold: sliding back along the gun keeps it, down to the shortest line") {
    TwoHandHold hold;
    const float reach = evr::length(evr::input::supportInAim(kShotgun));
    REQUIRE(grab(hold, kGrip + along(kShotgun) * reach, kShotgun).engaged);
    // Slid back to 12 cm from the grip (18 cm short of the support point): still held.
    auto r = hold.update(frame(kGrip + along(kShotgun) * 0.12f, 1.0f, kShotgun));
    CHECK(r.engaged);
    CHECK(r.slide == doctest::Approx(0.12f - reach).epsilon(1e-3));
    // And forward again past the support point by 15 cm: held; by 17 cm: let go.
    r = hold.update(frame(kGrip + along(kShotgun) * (reach + 0.15f), 1.0f, kShotgun));
    CHECK(r.engaged);
    r = hold.update(frame(kGrip + along(kShotgun) * (reach + 0.17f), 1.0f, kShotgun));
    CHECK_FALSE(r.engaged);
    CHECK(r.ended == TwoHandEnd::TooFar);
    // Back to 9 cm: the shortest line ends it.
    REQUIRE(grab(hold, kGrip + along(kShotgun) * 0.2f, kShotgun).engaged);
    r = hold.update(frame(kGrip + along(kShotgun) * 0.09f, 1.0f, kShotgun));
    CHECK(r.ended == TwoHandEnd::TooClose);
}

TEST_CASE(
    "two-handed hold: the off glove sits where the off hand is along the gun, up to the support point") {
    using evr::input::gloveSlide;
    const Vec3 dir = evr::normalize(Vec3{0.1f, 0.05f, -0.4f});
    // At the support point and past it: no move.
    Vec3 s = gloveSlide(kGrip, kGrip + dir * 0.30f, 0.30f);
    CHECK(evr::length(s) < 1e-6f);
    s = gloveSlide(kGrip, kGrip + dir * 0.40f, 0.30f);
    CHECK(evr::length(s) < 1e-6f);
    // 10 cm short of it: back along the hands' line by 10 cm.
    s = gloveSlide(kGrip, kGrip + dir * 0.20f, 0.30f);
    CHECK(evr::length(s + dir * 0.10f) < 1e-5f);
    // In the model's units (game units per metre) the same.
    s = gloveSlide(kGrip * 40.0f, (kGrip + dir * 0.20f) * 40.0f, 0.30f * 40.0f);
    CHECK(evr::length(s + dir * 4.0f) < 1e-3f);
    // No line, no length or not numbers: nothing.
    CHECK(evr::length(gloveSlide(kGrip, kGrip, 0.30f)) == 0.0f);
    CHECK(evr::length(gloveSlide(kGrip, kGrip + dir * 0.2f, 0.0f)) == 0.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(evr::length(gloveSlide(kGrip, Vec3{nan, nan, nan}, 0.30f)) == 0.0f);
    CHECK(evr::length(gloveSlide(kGrip, kGrip + dir * 0.2f, nan)) == 0.0f);
}

TEST_CASE("two-handed capture: where the off hand is against the gun") {
    const auto at =
        evr::input::onGun(kGrip, Quat{}, kGrip + along(kShotgun) * 0.25f + side(kShotgun) * 0.03f, kShotgun);
    REQUIRE(at);
    CHECK(at->along == doctest::Approx(0.25f).epsilon(1e-3));
    CHECK(at->off == doctest::Approx(0.03f).epsilon(1e-3));
    CHECK(evr::input::withinCapture(*at, kShotgun));
    CHECK_FALSE(evr::input::withinCapture({0.11f, 0.0f}, kShotgun)); // under the line a hold starts at
    CHECK_FALSE(evr::input::onGun(kGrip, Quat{}, kGrip, SupportRow{}));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(evr::input::onGun(kGrip, Quat{}, Vec3{nan, 0.0f, 0.0f}, kShotgun));
}
