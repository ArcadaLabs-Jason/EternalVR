#include "features/input/stick_directions.hpp"

#include "features/input/turn_stick_arbiter.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

// The turn stick split by direction (T-229): which part of the circle each bound direction owns, and the
// arbiter's claims on such a stick.

using evr::game::GameAction;
using evr::input::Axis2;
using evr::input::DirectionOwner;
using evr::input::ownedDirectionAt;
using evr::input::partDegrees;
using evr::input::StickDirection;
using evr::input::StickGesture;
using evr::input::StickGestureBinding;
using evr::input::StickLayout;
using evr::input::stickLayoutOf;
using evr::input::stickLayoutText;
using evr::input::SweepIntent;
using evr::input::TurnStickArbiter;
using evr::input::TurnStickOutput;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

// Stick at `degrees` clockwise from straight up.
Axis2 stickAt(float degrees, float deflection = 1.0f) {
    const float radians = degrees * std::numbers::pi_v<float> / 180.0f;
    return {deflection * std::sin(radians), deflection * std::cos(radians)};
}

StickLayout layoutOf(std::initializer_list<StickGesture> gestures) {
    std::vector<StickGestureBinding> bindings;
    for (const StickGesture gesture : gestures) {
        bindings.push_back({gesture, GameAction::WeaponSlot1});
    }
    return stickLayoutOf(bindings);
}

// The built-in maps' gestures.
const StickLayout kBuiltIn = layoutOf({StickGesture::Up, StickGesture::DownTap, StickGesture::DownHold});
// Up and down as built in, and both sides.
const StickLayout kCardinals = layoutOf({StickGesture::Up, StickGesture::DownTap, StickGesture::DownHold,
                                         StickGesture::Left, StickGesture::Right});
const StickLayout kEighths =
    layoutOf({StickGesture::Up, StickGesture::Down, StickGesture::Left, StickGesture::Right,
              StickGesture::UpLeft, StickGesture::UpRight, StickGesture::DownLeft, StickGesture::DownRight});
// Up and down as built in and the four diagonals: straight left and right still turn.
const StickLayout kDiagonals =
    layoutOf({StickGesture::Up, StickGesture::DownTap, StickGesture::DownHold, StickGesture::UpLeft,
              StickGesture::UpRight, StickGesture::DownLeft, StickGesture::DownRight});

// The intent of a sweep pushed straight out to `stick` from the centre, on a fresh arbiter.
SweepIntent claimAt(const StickLayout& layout, Axis2 stick) {
    TurnStickArbiter arbiter({}, layout);
    arbiter.update(stick, kFrame);
    return arbiter.intent();
}

std::optional<StickDirection> sideAt(const StickLayout& layout, Axis2 stick) {
    TurnStickArbiter arbiter({}, layout);
    return arbiter.update(stick, kFrame).side;
}

} // namespace

TEST_CASE("the built-in maps' gestures keep the claim cones") {
    CHECK_FALSE(kBuiltIn.directions);
    CHECK_FALSE(kBuiltIn.downPress);
    CHECK(stickLayoutText(kBuiltIn).empty());
    // Every push, at every angle and two deflections, is claimed exactly as by the arbiter of before T-229.
    for (int degrees = 0; degrees < 360; ++degrees) {
        for (const float deflection : {0.5f, 1.0f}) {
            CAPTURE(degrees);
            CAPTURE(deflection);
            TurnStickArbiter cones;
            cones.update(stickAt(static_cast<float>(degrees), deflection), kFrame);
            CHECK(claimAt(kBuiltIn, stickAt(static_cast<float>(degrees), deflection)) == cones.intent());
        }
    }
    // 40 degrees right of up is outside the 30-degree up cone: a turn.
    CHECK(claimAt(kBuiltIn, stickAt(40.0f)) == SweepIntent::Turn);
}

TEST_CASE("binding a side splits the stick; with up, down, left and right each owns 90 degrees") {
    REQUIRE(kCardinals.directions);
    for (const StickDirection d :
         {StickDirection::Up, StickDirection::Right, StickDirection::Down, StickDirection::Left}) {
        CHECK(partDegrees(kCardinals, d) == doctest::Approx(90.0f));
    }
    CHECK(partDegrees(kCardinals, StickDirection::UpRight) == 0.0f);
    CHECK(stickLayoutText(kCardinals) == "up 90 deg, right 90 deg, down 90 deg, left 90 deg");
    CHECK(claimAt(kCardinals, stickAt(40.0f)) == SweepIntent::Up); // a turn under the cones
    CHECK(claimAt(kCardinals, stickAt(44.0f)) == SweepIntent::Up);
    CHECK(sideAt(kCardinals, stickAt(46.0f)) == StickDirection::Right);
    CHECK(sideAt(kCardinals, stickAt(134.0f)) == StickDirection::Right);
    CHECK(claimAt(kCardinals, stickAt(136.0f)) == SweepIntent::Down);
    CHECK(claimAt(kCardinals, stickAt(224.0f)) == SweepIntent::Down);
    CHECK(sideAt(kCardinals, stickAt(226.0f)) == StickDirection::Left);
    CHECK(sideAt(kCardinals, stickAt(314.0f)) == StickDirection::Left);
    CHECK(claimAt(kCardinals, stickAt(316.0f)) == SweepIntent::Up);
}

TEST_CASE("all eight bound: each direction owns its 45 degrees") {
    for (int d = 0; d < 8; ++d) {
        CHECK(partDegrees(kEighths, static_cast<StickDirection>(d)) == doctest::Approx(45.0f));
    }
    CHECK(stickLayoutText(kEighths) ==
          "up 45 deg, up_right 45 deg, right 45 deg, down_right 45 deg, down 45 deg, "
          "down_left 45 deg, left 45 deg, up_left 45 deg");
    CHECK(claimAt(kEighths, stickAt(22.0f)) == SweepIntent::Up);
    CHECK(sideAt(kEighths, stickAt(23.0f)) == StickDirection::UpRight);
    CHECK(sideAt(kEighths, stickAt(67.0f)) == StickDirection::UpRight);
    CHECK(sideAt(kEighths, stickAt(68.0f)) == StickDirection::Right);
    CHECK(sideAt(kEighths, stickAt(157.0f)) == StickDirection::DownRight);
    CHECK(claimAt(kEighths, stickAt(158.0f)) == SweepIntent::Down);
    CHECK(claimAt(kEighths, stickAt(202.0f)) == SweepIntent::Down);
    CHECK(sideAt(kEighths, stickAt(203.0f)) == StickDirection::DownLeft);
    CHECK(sideAt(kEighths, stickAt(292.0f)) == StickDirection::Left);
    CHECK(sideAt(kEighths, stickAt(293.0f)) == StickDirection::UpLeft);
    CHECK(sideAt(kEighths, stickAt(337.0f)) == StickDirection::UpLeft);
    CHECK(claimAt(kEighths, stickAt(338.0f)) == SweepIntent::Up);
    // Nothing turns.
    for (int degrees = 0; degrees < 360; ++degrees) {
        CHECK(claimAt(kEighths, stickAt(static_cast<float>(degrees))) != SweepIntent::Turn);
    }
}

TEST_CASE("a direction nobody binds is shared by its nearest bound neighbours, halfway each") {
    // Up and down built in, up_right bound; left and right turn. Up-left goes half to up, half to the left
    // turn; down-right and down-left half to down and half to the turn beside them.
    const StickLayout layout =
        layoutOf({StickGesture::Up, StickGesture::DownTap, StickGesture::DownHold, StickGesture::UpRight});
    CHECK(layout.owners[static_cast<std::size_t>(StickDirection::UpLeft)] == DirectionOwner::None);
    CHECK(layout.owners[static_cast<std::size_t>(StickDirection::Left)] == DirectionOwner::Turn);
    CHECK(stickLayoutText(layout) ==
          "up 67.5 deg, up_right 45 deg, right 67.5 deg (turn), down 90 deg, left 90 deg (turn)");
    CHECK(claimAt(layout, stickAt(314.0f, 0.5f)) == SweepIntent::Turn);
    CHECK(claimAt(layout, stickAt(316.0f)) == SweepIntent::Up);
    CHECK(claimAt(layout, stickAt(21.0f)) == SweepIntent::Up);
    CHECK(sideAt(layout, stickAt(24.0f)) == StickDirection::UpRight);
    CHECK(claimAt(layout, stickAt(134.0f)) == SweepIntent::Turn);
    CHECK(claimAt(layout, stickAt(136.0f)) == SweepIntent::Down);
}

TEST_CASE("the owned direction nearest the stick, with an exact tie going counter-clockwise") {
    CHECK_FALSE(ownedDirectionAt({}, kCardinals).has_value());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(ownedDirectionAt({nan, 1.0f}, kCardinals).has_value());
    CHECK_FALSE(ownedDirectionAt({1.0f, 0.0f}, StickLayout{}).has_value());
    // Exactly between right and up: up, counter-clockwise of the stick.
    CHECK(ownedDirectionAt({1.0f, 1.0f}, kCardinals) == StickDirection::Up);
    // A lone direction owns the whole circle.
    StickLayout lone;
    lone.owners[static_cast<std::size_t>(StickDirection::Down)] = DirectionOwner::Gesture;
    CHECK(partDegrees(lone, StickDirection::Down) == doctest::Approx(360.0f));
    CHECK(ownedDirectionAt({0.0f, 1.0f}, lone) == StickDirection::Down);
}

TEST_CASE("a side claims at the gesture deflection and lasts until the stick is back in the centre") {
    TurnStickArbiter arbiter({}, kCardinals);
    // Below the 0.75 engage nothing, and no turning either: the part is the side's.
    TurnStickOutput out = arbiter.update({-0.7f, 0.0f}, kFrame);
    CHECK_FALSE(out.side.has_value());
    CHECK_FALSE(out.turnAllowed);
    CHECK(arbiter.intent() == SweepIntent::None);
    out = arbiter.update({-0.76f, 0.0f}, kFrame);
    CHECK(out.side == StickDirection::Left);
    // Held on the way back in, down to the centre radius (0.25).
    CHECK(arbiter.update({-0.3f, 0.0f}, kFrame).side == StickDirection::Left);
    CHECK(arbiter.update({-0.26f, 0.0f}, kFrame).side == StickDirection::Left);
    out = arbiter.update({-0.25f, 0.0f}, kFrame);
    CHECK_FALSE(out.side.has_value());
    CHECK(arbiter.intent() == SweepIntent::None);
    // A wobble back out after the centre is a new sweep, claimed afresh.
    CHECK_FALSE(arbiter.update({-0.5f, 0.0f}, kFrame).side.has_value());
}

TEST_CASE("a side's sweep keeps its claim as the stick rolls round, and never turns") {
    TurnStickArbiter arbiter({}, kCardinals);
    for (int degrees = 270; degrees <= 450; degrees += 5) {
        const TurnStickOutput out = arbiter.update(stickAt(static_cast<float>(degrees)), kFrame);
        CHECK(out.side == StickDirection::Left);
        CHECK_FALSE(out.up);
        CHECK_FALSE(out.turnAllowed);
    }
    CHECK_FALSE(arbiter.update({}, kFrame).downTap);
}

TEST_CASE(
    "with only the diagonals bound, straight left and right still turn, and a turn never fires a side") {
    CHECK(partDegrees(kDiagonals, StickDirection::Right) == doctest::Approx(45.0f));
    CHECK(claimAt(kDiagonals, stickAt(90.0f, 0.4f)) == SweepIntent::Turn);
    CHECK(claimAt(kDiagonals, stickAt(270.0f, 0.4f)) == SweepIntent::Turn);
    // 30 degrees up from straight right is up-right's: not a turn below the engage, the side above it.
    CHECK(claimAt(kDiagonals, stickAt(60.0f, 0.6f)) == SweepIntent::None);
    CHECK(sideAt(kDiagonals, stickAt(60.0f, 0.8f)) == StickDirection::UpRight);
    // A turn that rolls into up-right's part keeps turning.
    TurnStickArbiter arbiter({}, kDiagonals);
    for (int degrees = 90; degrees >= 30; degrees -= 3) {
        const TurnStickOutput out = arbiter.update(stickAt(static_cast<float>(degrees)), kFrame);
        CHECK(out.turnAllowed);
        CHECK_FALSE(out.side.has_value());
    }
}

TEST_CASE("binding both sides leaves no part to turn in") {
    const StickLayout sides = layoutOf({StickGesture::Left, StickGesture::Right});
    CHECK(stickLayoutText(sides) == "right 180 deg, left 180 deg");
    for (int degrees = 0; degrees < 360; ++degrees) {
        for (const float deflection : {0.4f, 1.0f}) {
            CHECK(claimAt(sides, stickAt(static_cast<float>(degrees), deflection)) != SweepIntent::Turn);
        }
    }
    // One side bound: the other still turns.
    const StickLayout left = layoutOf({StickGesture::Left});
    CHECK(claimAt(left, stickAt(90.0f, 0.4f)) == SweepIntent::Turn);
    CHECK(claimAt(left, stickAt(270.0f, 0.4f)) == SweepIntent::None);
}

TEST_CASE("down bound as a press lasts the whole down sweep: no tap, no hold, no cancel") {
    for (const StickLayout& layout :
         {layoutOf({StickGesture::Down}), layoutOf({StickGesture::Down, StickGesture::Left})}) {
        CHECK(layout.downPress);
        TurnStickArbiter arbiter({}, layout);
        int downFrames = 0;
        int holdFrames = 0;
        int taps = 0;
        // 0.5 s straight down, then a roll out of the 45-degree stay cone.
        for (int i = 0; i < 45; ++i) {
            const TurnStickOutput out = arbiter.update({0.0f, -1.0f}, kFrame);
            downFrames += out.down ? 1 : 0;
            holdFrames += out.downHold ? 1 : 0;
        }
        for (int degrees = 180; degrees >= 100; degrees -= 10) {
            const TurnStickOutput out = arbiter.update(stickAt(static_cast<float>(degrees)), kFrame);
            downFrames += out.down ? 1 : 0;
            CHECK_FALSE(out.turnAllowed);
        }
        const TurnStickOutput end = arbiter.update({}, kFrame);
        taps += end.downTap ? 1 : 0;
        CHECK_FALSE(end.down);
        CHECK(downFrames == 54);
        CHECK(holdFrames == 0);
        CHECK(taps == 0);
    }
    // A short flick is a press too, not a tap on the release.
    TurnStickArbiter flick({}, layoutOf({StickGesture::Down}));
    CHECK(flick.update({0.0f, -1.0f}, kFrame).down);
    CHECK_FALSE(flick.update({}, kFrame).downTap);
}

TEST_CASE("the wheel's button and a cancelled sweep end a side's sweep until the stick recentres") {
    TurnStickArbiter arbiter({}, kCardinals);
    REQUIRE(arbiter.update({-1.0f, 0.0f}, kFrame).side == StickDirection::Left);
    TurnStickOutput out = arbiter.update({-1.0f, 0.0f}, kFrame, true);
    CHECK_FALSE(out.side.has_value());
    CHECK(out.wheelPointer == Axis2{-1.0f, 0.0f});
    CHECK_FALSE(arbiter.update({-1.0f, 0.0f}, kFrame).side.has_value());
    arbiter.update({}, kFrame);
    REQUIRE(arbiter.update({1.0f, 0.0f}, kFrame).side == StickDirection::Right);
    arbiter.cancelSweep();
    CHECK_FALSE(arbiter.update({1.0f, 0.0f}, kFrame).side.has_value());
    arbiter.update({}, kFrame);
    CHECK(arbiter.update({1.0f, 0.0f}, kFrame).side == StickDirection::Right);
}
