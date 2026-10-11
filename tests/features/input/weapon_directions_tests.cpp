#include "features/input/weapon_directions.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <ostream>
#include <string>

using evr::game::GameAction;
using evr::input::Axis2;
using evr::input::kDefaultWeaponDirections;
using evr::input::parseWeaponDirections;
using evr::input::slotDirection;
using evr::input::WeaponDirections;
using evr::input::weaponDirectionsText;
using evr::input::weaponSlotFor;
using evr::input::wheelDirection;
using evr::input::WheelDirection;

namespace {

constexpr float kThreshold = 0.5f;  // the thumb-rest wheel's kSelectThreshold
constexpr float kHysteresis = 7.5f; // and its kOctantHysteresisDegrees
constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;

// A stick `length` out at `degrees` counter-clockwise from the right.
Axis2 at(float degrees, float length = 1.0f) {
    return {length * std::cos(degrees * kDegrees), length * std::sin(degrees * kDegrees)};
}

// Only up, right, down and left have a weapon.
const WeaponDirections kFour =
    parseWeaponDirections("up_right=none,down_right=none,down_left=none,up_left=none").table;

// No direction has a weapon but the one named.
WeaponDirections only(const char* entries) {
    return parseWeaponDirections(std::string("up=0,up_right=0,right=0,down_right=0,down=0,down_left=0,left=0,"
                                             "up_left=0,") +
                                 entries)
        .table;
}

// The thumb-rest wheel's eighths before a direction with no weapon widened its neighbours (rest_wheel.cpp's
// octantOf): the reference for a full table.
WheelDirection eighthOf(Axis2 stick, WheelDirection current) {
    const WheelDirection nearest = wheelDirection(stick, kThreshold);
    if (nearest == WheelDirection::None || current == WheelDirection::None || nearest == current) {
        return nearest;
    }
    const float centre = static_cast<float>(current) * 45.0f * kDegrees;
    const float off = std::remainder(std::atan2(stick.y, stick.x) - centre, 2.0f * std::numbers::pi_v<float>);
    return std::fabs(off) <= (22.5f + kHysteresis) * kDegrees ? current : nearest;
}

WheelDirection
pick(Axis2 stick, const WeaponDirections& table, WheelDirection current = WheelDirection::None) {
    return slotDirection(stick, current, table, kThreshold, kHysteresis);
}

} // namespace

TEST_CASE("the default is the game's own wheel") {
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::Up) == GameAction::WeaponSlot1);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::UpRight) == GameAction::WeaponSlot5);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::Right) == GameAction::WeaponSlot2);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::DownRight) == GameAction::WeaponSlot7);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::Down) == GameAction::WeaponSlot3);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::DownLeft) == GameAction::WeaponSlot6);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::Left) == GameAction::WeaponSlot4);
    CHECK(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::UpLeft) == GameAction::WeaponSlot8);
    CHECK_FALSE(weaponSlotFor(kDefaultWeaponDirections, WheelDirection::None).has_value());
    CHECK(weaponDirectionsText(kDefaultWeaponDirections) ==
          "up=1,up_right=5,right=2,down_right=7,down=3,down_left=6,left=4,up_left=8");
}

TEST_CASE("a table changes the directions it names and keeps the others") {
    const auto result = parseWeaponDirections(" Up = 5, down-left=none ,left=0,right=8");
    CHECK(result.issues.empty());
    CHECK(weaponSlotFor(result.table, WheelDirection::Up) == GameAction::WeaponSlot5);
    CHECK_FALSE(weaponSlotFor(result.table, WheelDirection::DownLeft).has_value());
    CHECK_FALSE(weaponSlotFor(result.table, WheelDirection::Left).has_value());
    CHECK(weaponSlotFor(result.table, WheelDirection::Right) == GameAction::WeaponSlot8);
    CHECK(weaponSlotFor(result.table, WheelDirection::Down) == GameAction::WeaponSlot3);
    CHECK(weaponDirectionsText(result.table) ==
          "up=5,up_right=5,right=8,down_right=7,down=3,down_left=none,left=none,up_left=8");
}

TEST_CASE("entries that cannot be used are reported one by one and change nothing") {
    const auto result = parseWeaponDirections("north=1,up=9,down,left=x,,right=2");
    CHECK(result.issues.size() == 4);
    CHECK(weaponSlotFor(result.table, WheelDirection::Up) == GameAction::WeaponSlot1);
    CHECK(weaponSlotFor(result.table, WheelDirection::Right) == GameAction::WeaponSlot2);
    CHECK(parseWeaponDirections("").issues.empty());
}

TEST_CASE("with four directions set, each takes the quarter of the circle around it") {
    constexpr WheelDirection kCardinals[] = {WheelDirection::Right, WheelDirection::Up, WheelDirection::Left,
                                             WheelDirection::Down};
    for (int i = 0; i < 4; ++i) {
        const float centre = 90.0f * static_cast<float>(i);
        for (const int side : {1, -1}) {
            CAPTURE(i);
            CAPTURE(side);
            const WheelDirection next = kCardinals[(i + 4 + side) % 4];
            const auto off = [&](float degrees) {
                return at(centre + static_cast<float>(side) * degrees);
            };
            CHECK(pick(off(30.0f), kFour) == kCardinals[i]);
            CHECK(pick(off(44.0f), kFour) == kCardinals[i]);
            CHECK(pick(off(46.0f), kFour) == next);
            CHECK(pick(off(60.0f), kFour) == next);
            CHECK(pick(off(89.0f), kFour) == next);
        }
    }
    // Below the threshold, or not finite: nothing.
    CHECK(pick(at(60.0f, 0.4f), kFour) == WheelDirection::None);
    CHECK(pick(Axis2{NAN, 1.0f}, kFour, WheelDirection::Up) == WheelDirection::None);
}

TEST_CASE("a direction is kept until the stick is the hysteresis past the edge of its part of the circle") {
    // Up's quarter ends 45 degrees from the right, right's starts there.
    CHECK(pick(at(44.0f), kFour, WheelDirection::Up) == WheelDirection::Up);
    CHECK(pick(at(39.0f), kFour, WheelDirection::Up) == WheelDirection::Up);
    CHECK(pick(at(36.0f), kFour, WheelDirection::Up) == WheelDirection::Right);
    CHECK(pick(at(51.0f), kFour, WheelDirection::Right) == WheelDirection::Right);
    CHECK(pick(at(54.0f), kFour, WheelDirection::Right) == WheelDirection::Up);
    // A direction with no weapon has no part to keep.
    CHECK(pick(at(46.0f), kFour, WheelDirection::UpRight) == WheelDirection::Up);
    CHECK(pick(at(44.0f), kFour, WheelDirection::UpRight) == WheelDirection::Right);
}

TEST_CASE("with all eight set, the directions are the eighths as before, hysteresis included") {
    WeaponDirections ones{};
    ones.fill(1);
    constexpr WheelDirection kCurrents[] = {
        WheelDirection::None,     WheelDirection::Right,  WheelDirection::UpRight,
        WheelDirection::Up,       WheelDirection::UpLeft, WheelDirection::Left,
        WheelDirection::DownLeft, WheelDirection::Down,   WheelDirection::DownRight,
    };
    int checked = 0;
    int differ = 0;
    // Every quarter degree around the circle (the eighths' edges included), below and past the threshold.
    for (int step = 0; step <= 1440; ++step) {
        for (const float length : {0.45f, 0.5f, 0.7f, 1.0f}) {
            const Axis2 stick = at(static_cast<float>(step) * 0.25f - 180.0f, length);
            for (const WheelDirection current : kCurrents) {
                const WheelDirection expected = eighthOf(stick, current);
                differ += pick(stick, kDefaultWeaponDirections, current) != expected ? 1 : 0;
                differ += pick(stick, ones, current) != expected ? 1 : 0;
                checked += 2;
            }
        }
    }
    CHECK(checked == 1441 * 4 * 9 * 2);
    CHECK(differ == 0);
}

TEST_CASE("one direction set takes the whole circle; none set picks nothing") {
    const WeaponDirections one = only("down_left=6");
    const WeaponDirections none = only("");
    for (int degrees = 0; degrees < 360; degrees += 5) {
        CAPTURE(degrees);
        const Axis2 stick = at(static_cast<float>(degrees));
        CHECK(pick(stick, one) == WheelDirection::DownLeft);
        CHECK(pick(stick, one, WheelDirection::DownLeft) == WheelDirection::DownLeft);
        CHECK(pick(stick, none) == WheelDirection::None);
        CHECK(pick(stick, none, WheelDirection::Up) == WheelDirection::None);
    }
    CHECK(pick(at(45.0f, 0.3f), one) == WheelDirection::None);
}

TEST_CASE("two directions side by side split the circle at their two midpoints") {
    // Up (90 degrees) and up-right (45): they meet at 67.5 the short way and at 247.5 the long way.
    const WeaponDirections pair = only("up=1,up_right=5");
    CHECK(pick(at(70.0f), pair) == WheelDirection::Up);
    CHECK(pick(at(65.0f), pair) == WheelDirection::UpRight);
    CHECK(pick(at(180.0f), pair) == WheelDirection::Up);
    CHECK(pick(at(240.0f), pair) == WheelDirection::Up);
    CHECK(pick(at(255.0f), pair) == WheelDirection::UpRight);
    CHECK(pick(at(0.0f), pair) == WheelDirection::UpRight);
    // On each side a direction is kept up to the hysteresis past its own edge.
    CHECK(pick(at(62.0f), pair, WheelDirection::Up) == WheelDirection::Up);
    CHECK(pick(at(59.0f), pair, WheelDirection::Up) == WheelDirection::UpRight);
    CHECK(pick(at(253.0f), pair, WheelDirection::Up) == WheelDirection::Up);
    CHECK(pick(at(256.0f), pair, WheelDirection::Up) == WheelDirection::UpRight);
    CHECK(pick(at(242.0f), pair, WheelDirection::UpRight) == WheelDirection::UpRight);
    CHECK(pick(at(239.0f), pair, WheelDirection::UpRight) == WheelDirection::Up);
    // An exact tie (up and down only, the stick straight right) goes counter-clockwise.
    CHECK(pick(Axis2{1.0f, 0.0f}, only("up=1,down=3")) == WheelDirection::Up);
}
