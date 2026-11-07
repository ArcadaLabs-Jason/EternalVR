#include "xr_math/shield_pose.hpp"

#include "support/approx.hpp"
#include "xr_math/head_aim.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <ostream>

using evr::Vec3;
using evr::test::approxEqual;
using evr::xr_math::axisFromAngles;
using evr::xr_math::chaingunMuzzle;
using evr::xr_math::EyeRelativePose;
using evr::xr_math::IdViewAxis;
using evr::xr_math::isOrthonormal;
using evr::xr_math::kShieldEffectReach;
using evr::xr_math::kShieldPastMuzzle;
using evr::xr_math::kShieldWorldScale;
using evr::xr_math::movesShieldPose;
using evr::xr_math::ShieldEffect;
using evr::xr_math::shieldEffectForward;
using evr::xr_math::shieldInFrontOfGun;
using evr::xr_math::ShieldPoseAsk;
using evr::xr_math::ShieldScale;

namespace {

double degreesBetween(Vec3 a, Vec3 b) {
    const double ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z;
    const double cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
    return std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), ax * bx + ay * by + az * bz) * 180.0 /
           std::numbers::pi;
}

// The effect's axis as the game builds it: the view's axis turned 90 degrees about its own up, so its
// forward row is the view's left and its left row the view's back.
IdViewAxis effectAxis(const IdViewAxis& view) {
    return {view.left, -view.forward, view.up};
}

// The shield's centre: the effect's origin plus its reach along its forward.
Vec3 centreOf(const ShieldEffect& effect, float unitsPerMetre = 1.0f) {
    return effect.origin + shieldEffectForward(effect.axis) * (kShieldEffectReach * unitsPerMetre);
}

// A hands rig dump line (simulator, hand aim, game units are metres, right-handed, the chaingun at idle):
// the view at yaw 90, the weapon hand 0.2 m right of, 0.3 m below and 0.4 m ahead of the head, pointing
// ahead; the arms model where the viewmodel hook placed it, and the aim ray.
const IdViewAxis kView = axisFromAngles({0.0f, 90.0f, 0.0f});
const Vec3 kAimStart{0.2f, 0.4f, -0.05f};
EyeRelativePose dumpedArms() {
    return {{0.03411f, -0.21683f, 0.27156f},
            {{-0.00017f, 0.99998f, 0.00559f}, {-1.0f, -0.00017f, 0.0f}, {0.0f, -0.00559f, 0.99998f}}};
}

// The game's effect: at the eye, along the view.
ShieldEffect gameEffect(const IdViewAxis& view = kView) {
    return {{}, effectAxis(view)};
}

} // namespace

TEST_CASE("shieldEffectForward: the effect's axis built from the view gives the view's forward") {
    for (const float yaw : {0.0f, 90.0f, -135.0f, 170.0f}) {
        for (const float pitch : {-60.0f, -10.0f, 0.0f, 25.0f, 80.0f}) {
            const IdViewAxis view = axisFromAngles({pitch, yaw, 0.0f});
            CHECK(approxEqual(shieldEffectForward(effectAxis(view)), view.forward));
        }
    }
}

TEST_CASE("chaingunMuzzle: the dumped arms put the muzzle 1.13 m along the hand's ray, 14 cm off it") {
    const Vec3 muzzle = chaingunMuzzle(dumpedArms(), 1.0f);
    const Vec3 fromStart = muzzle - kAimStart;
    const float along = evr::dot(fromStart, kView.forward);
    CHECK(along == doctest::Approx(1.134f).epsilon(0.002));
    const Vec3 off = fromStart - kView.forward * along;
    CHECK(evr::length(off) == doctest::Approx(0.14f).epsilon(0.05));
    CHECK(evr::dot(off, kView.up) > 0.1f); // the barrels sit above the ray
}

TEST_CASE("shieldInFrontOfGun: the game's shield sits inside the gun; moved, all of it is past the muzzle") {
    const Vec3 muzzle = chaingunMuzzle(dumpedArms(), 1.0f);
    const ShieldEffect game = gameEffect();
    // The game's own: 0.25 m from the eye, well short of the muzzle 1.5 m ahead.
    CHECK(evr::dot(centreOf(game) - kAimStart, kView.forward) < evr::dot(muzzle - kAimStart, kView.forward));
    const std::optional<ShieldEffect> moved =
        shieldInFrontOfGun(game, kAimStart, kView.forward, muzzle, 1.0f);
    REQUIRE(moved.has_value());
    const Vec3 centre = centreOf(*moved);
    // On the ray the shots take.
    const Vec3 fromStart = centre - kAimStart;
    const float along = evr::dot(fromStart, kView.forward);
    CHECK(evr::length(fromStart - kView.forward * along) < 1e-4f);
    // kShieldPastMuzzle past the muzzle: the model reaches 0.18 m back from its centre at the world size.
    CHECK(along == doctest::Approx(1.134f + kShieldPastMuzzle).epsilon(0.002));
    CHECK(along - 0.18f > evr::dot(muzzle - kAimStart, kView.forward));
    // Its orientation is the game's (the ray is the view here).
    CHECK(approxEqual(moved->axis.forward, game.axis.forward));
    CHECK(approxEqual(moved->axis.left, game.axis.left));
    CHECK(approxEqual(moved->axis.up, game.axis.up));
}

TEST_CASE("shieldInFrontOfGun: a ray off the game's view turns the shield onto it, without roll added") {
    const IdViewAxis view = axisFromAngles({5.0f, 30.0f, 0.0f});
    const IdViewAxis aim = axisFromAngles({-8.0f, 42.0f, 0.0f});
    const Vec3 start{0.35f, -0.1f, -0.3f};
    const Vec3 muzzle = start + aim.forward * 1.1f + aim.up * 0.13f;
    const std::optional<ShieldEffect> moved =
        shieldInFrontOfGun(gameEffect(view), start, aim.forward, muzzle, 1.0f);
    REQUIRE(moved.has_value());
    CHECK(isOrthonormal(moved->axis));
    CHECK(degreesBetween(shieldEffectForward(moved->axis), aim.forward) < 0.01);
    // The axis the game builds for a view along the ray: no roll, its forward row (the view's left) level.
    CHECK(std::fabs(evr::dot(moved->axis.forward, Vec3{0.0f, 0.0f, 1.0f})) < 1e-4f);
    const IdViewAxis expected = evr::xr_math::shieldEffectAxis(aim);
    CHECK(approxEqual(moved->axis.forward, expected.forward));
    CHECK(approxEqual(moved->axis.left, expected.left));
    CHECK(approxEqual(moved->axis.up, expected.up));
    const Vec3 fromStart = centreOf(*moved) - start;
    CHECK(evr::dot(fromStart, aim.forward) == doctest::Approx(1.1f + kShieldPastMuzzle).epsilon(0.002));
}

TEST_CASE("shieldInFrontOfGun: game units scale every distance") {
    const float upm = 39.37f;
    const EyeRelativePose metres = dumpedArms();
    const EyeRelativePose units{metres.offset * upm, metres.axis};
    const Vec3 start = kAimStart * upm;
    const std::optional<ShieldEffect> moved =
        shieldInFrontOfGun(gameEffect(), start, kView.forward, chaingunMuzzle(units, upm), upm);
    REQUIRE(moved.has_value());
    CHECK(evr::dot(centreOf(*moved, upm) - start, kView.forward) / upm ==
          doctest::Approx(1.134f + kShieldPastMuzzle).epsilon(0.002));
}

TEST_CASE("shieldInFrontOfGun: a muzzle behind the ray's start counts as at it") {
    const Vec3 muzzle = kAimStart - kView.forward * 0.3f;
    const std::optional<ShieldEffect> moved =
        shieldInFrontOfGun(gameEffect(), kAimStart, kView.forward, muzzle, 1.0f);
    REQUIRE(moved.has_value());
    CHECK(evr::dot(centreOf(*moved) - kAimStart, kView.forward) == doctest::Approx(kShieldPastMuzzle));
}

TEST_CASE("shieldInFrontOfGun: unusable inputs leave the game's shield") {
    const Vec3 muzzle = chaingunMuzzle(dumpedArms(), 1.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(shieldInFrontOfGun(gameEffect(), kAimStart, Vec3{}, muzzle, 1.0f).has_value());
    CHECK_FALSE(shieldInFrontOfGun(gameEffect(), kAimStart, -kView.forward, muzzle, 1.0f).has_value());
    // A ray well off the game's view still takes the shield; one more than 120 degrees off does not.
    const Vec3 off100 = axisFromAngles({0.0f, 190.0f, 0.0f}).forward;
    const Vec3 off130 = axisFromAngles({0.0f, 220.0f, 0.0f}).forward;
    CHECK(shieldInFrontOfGun(gameEffect(), kAimStart, off100, muzzle, 1.0f).has_value());
    CHECK_FALSE(shieldInFrontOfGun(gameEffect(), kAimStart, off130, muzzle, 1.0f).has_value());
    CHECK_FALSE(
        shieldInFrontOfGun(gameEffect(), Vec3{nan, 0.0f, 0.0f}, kView.forward, muzzle, 1.0f).has_value());
    CHECK_FALSE(shieldInFrontOfGun(gameEffect(), kAimStart, kView.forward, muzzle, 0.0f).has_value());
    ShieldEffect skewed = gameEffect();
    skewed.axis.up = skewed.axis.up * 2.0f;
    CHECK_FALSE(shieldInFrontOfGun(skewed, kAimStart, kView.forward, muzzle, 1.0f).has_value());
}

TEST_CASE("ShieldScale: the world size while placed, the game's 1 once after a held miss") {
    ShieldScale scale;
    CHECK_FALSE(scale.next(false).send.has_value()); // never placed: the game's own, nothing sent
    CHECK_FALSE(scale.next(false).draw);
    ShieldScale::Step step = scale.next(true);
    CHECK(step.send == kShieldWorldScale);
    CHECK(step.draw);
    CHECK(scale.next(true).send == kShieldWorldScale); // every think: a shield made again starts at 1
    // A missed placement holds the last one, nothing sent, for kHoldThinks thinks.
    for (int i = 0; i < ShieldScale::kHoldThinks; ++i) {
        step = scale.next(false);
        CHECK_FALSE(step.send.has_value());
        CHECK(step.draw);
    }
    step = scale.next(false);
    CHECK(step.send == 1.0f);
    CHECK_FALSE(step.draw);
    CHECK_FALSE(scale.next(false).send.has_value());
    CHECK(kShieldWorldScale * 0.165f == doctest::Approx(1.78f)); // the third-person effect's size
}

TEST_CASE("ShieldScale: one missed tick neither flicks the size nor sends the game's 1") {
    ShieldScale scale;
    scale.next(true);
    const ShieldScale::Step miss = scale.next(false);
    CHECK_FALSE(miss.send.has_value());
    CHECK(miss.draw);
    CHECK(scale.next(true).send == kShieldWorldScale);
    // The hold starts again after a placement.
    for (int i = 0; i < ShieldScale::kHoldThinks; ++i) {
        CHECK_FALSE(scale.next(false).send.has_value());
    }
    CHECK(scale.next(false).send == 1.0f);
}

TEST_CASE("ShieldScale: a scale the guard kept from going out is made up") {
    ShieldScale scale;
    scale.next(true);
    for (int i = 0; i < ShieldScale::kHoldThinks; ++i) {
        scale.next(false);
    }
    const ShieldScale::Step back = scale.next(false);
    REQUIRE(back.send == 1.0f);
    scale.notSent(back); // refused between the decision and the call
    const ShieldScale::Step again = scale.next(false);
    CHECK(again.send == 1.0f); // at once, no second hold
    CHECK_FALSE(scale.next(false).send.has_value());
    // Refused before asking, with the world scale out: the game's 1 at the next think without a placement.
    scale.next(true);
    scale.refused();
    CHECK(scale.next(false).send == 1.0f);
    // Refused with nothing ever sent: nothing to take back.
    ShieldScale fresh;
    fresh.refused();
    CHECK_FALSE(fresh.next(false).send.has_value());
}

TEST_CASE("movesShieldPose: the placed shield, its class, fresh, asked by its effect") {
    const ShieldPoseAsk ok{true, true, 0.016, true};
    CHECK(movesShieldPose(ok));
    ShieldPoseAsk ask = ok;
    ask.placedShield = false; // another entity
    CHECK_FALSE(movesShieldPose(ask));
    ask = ok;
    ask.shieldClass = false; // the dead shield's slot reused by another class
    CHECK_FALSE(movesShieldPose(ask));
    ask = ok;
    ask.sincePlaced = 0.15; // the shield stopped thinking (switched away, died, a load)
    CHECK_FALSE(movesShieldPose(ask));
    ask.sincePlaced = -0.01;
    CHECK_FALSE(movesShieldPose(ask));
    ask = ok;
    ask.fromEffect = false; // any other reader of the entity's pose
    CHECK_FALSE(movesShieldPose(ask));
    ask = ok;
    ask.sincePlaced = evr::xr_math::kShieldPlacedSeconds;
    CHECK(movesShieldPose(ask));
}
