#include "features/input/rumble_route.hpp"

#include "features/input/haptics_policy.hpp"

#include <doctest/doctest.h>

#include <ostream>
#include <string_view>

using evr::input::GameRumble;
using evr::input::Hand;
using evr::input::HapticCommand;
using evr::input::HapticCommandKind;
using evr::input::HapticsFrame;
using evr::input::HapticSource;
using evr::input::HapticsPolicy;
using evr::input::kSupportShare;
using evr::input::RumbleCause;
using evr::input::RumbleEvents;
using evr::input::RumbleLevels;
using evr::input::RumbleRouter;

namespace {

constexpr std::size_t kLeft = 0;
constexpr std::size_t kRight = 1;

RumbleLevels route(RumbleRouter& router,
                   double seconds,
                   float low,
                   float high,
                   RumbleEvents events = {},
                   Hand weaponHand = Hand::Right) {
    return router.route(seconds, weaponHand, events, low, high);
}

RumbleEvents firing() {
    RumbleEvents e;
    e.fireHeld = true;
    return e;
}

RumbleEvents meleeBy(Hand hand) {
    RumbleEvents e;
    e.melee[static_cast<std::size_t>(hand)] = true;
    return e;
}

RumbleEvents shoulder() {
    RumbleEvents e;
    e.shoulder = true;
    return e;
}

} // namespace

TEST_CASE("rumble routing: with no cause the game's rumble splits as before") {
    RumbleRouter router;
    const RumbleLevels out = route(router, 1.0, 0.4f, 0.9f);
    CHECK(out.cause == RumbleCause::Body);
    CHECK(out.changed);
    CHECK(out.level[kRight] == doctest::Approx(0.9f)); // the weapon hand: the louder motor
    CHECK(out.level[kLeft] == doctest::Approx(0.4f));  // the other hand: the low motor
}

TEST_CASE("rumble routing: a shot's recoil is the weapon hand's, the other hand feels a little") {
    RumbleRouter router;
    route(router, 1.0, 0.0f, 0.0f, firing());
    const RumbleLevels out = route(router, 1.01, 0.5f, 0.8f, firing());
    CHECK(out.cause == RumbleCause::Weapon);
    CHECK(out.level[kRight] == doctest::Approx(0.8f));
    CHECK(out.level[kLeft] == doctest::Approx(0.5f * kSupportShare));
}

TEST_CASE("rumble routing: a rumble just after the fire action went up is still the weapon's") {
    RumbleRouter router;
    route(router, 1.0, 0.0f, 0.0f, firing());
    CHECK(route(router, 1.1, 0.5f, 0.5f).cause == RumbleCause::Weapon);
    RumbleRouter late;
    route(late, 1.0, 0.0f, 0.0f, firing());
    route(late, 1.01, 0.0f, 0.0f);
    CHECK(route(late, 1.4, 0.5f, 0.5f).cause == RumbleCause::Body);
}

TEST_CASE("rumble routing: a punch's hit is the punching hand's, for a window after the press") {
    RumbleRouter router;
    route(router, 1.0, 0.0f, 0.0f, meleeBy(Hand::Left));
    const RumbleLevels out = route(router, 1.2, 0.6f, 0.9f);
    CHECK(out.cause == RumbleCause::Melee);
    CHECK(out.level[kLeft] == doctest::Approx(0.9f));
    CHECK(out.level[kRight] == doctest::Approx(0.6f * kSupportShare));
    RumbleRouter late;
    route(late, 1.0, 0.0f, 0.0f, meleeBy(Hand::Left));
    CHECK(route(late, 1.6, 0.6f, 0.9f).cause == RumbleCause::Body);
}

TEST_CASE("rumble routing: the shoulder launcher's rumble is the off hand's") {
    RumbleRouter router;
    route(router, 1.0, 0.0f, 0.0f, shoulder());
    const RumbleLevels out = route(router, 1.05, 0.7f, 0.7f);
    CHECK(out.cause == RumbleCause::Shoulder);
    CHECK(out.level[kLeft] == doctest::Approx(0.7f));
    CHECK(out.level[kRight] == doctest::Approx(0.7f * kSupportShare));
}

TEST_CASE("rumble routing: left-handed, the weapon hand and the off hand swap") {
    RumbleRouter router;
    route(router, 1.0, 0.0f, 0.0f, firing(), Hand::Left);
    const RumbleLevels weapon = route(router, 1.01, 0.5f, 0.8f, firing(), Hand::Left);
    CHECK(weapon.level[kLeft] == doctest::Approx(0.8f));
    CHECK(weapon.level[kRight] == doctest::Approx(0.5f * kSupportShare));
    RumbleRouter other;
    route(other, 1.0, 0.0f, 0.0f, shoulder(), Hand::Left);
    const RumbleLevels off = route(other, 1.05, 0.7f, 0.7f, {}, Hand::Left);
    CHECK(off.level[kRight] == doctest::Approx(0.7f));
}

TEST_CASE("rumble routing: the most recent event is the cause") {
    RumbleRouter router;
    route(router, 1.0, 0.0f, 0.0f, firing());
    route(router, 1.04, 0.0f, 0.0f, firing());
    route(router, 1.06, 0.0f, 0.0f, [] {
        RumbleEvents e;
        e.fireHeld = true;
        e.shoulder = true;
        return e;
    }());
    // Fire has been down since 1.0, the launcher went down at 1.06: the launcher is the later press.
    CHECK(route(router, 1.08, 0.6f, 0.6f, firing()).cause == RumbleCause::Shoulder);
}

TEST_CASE("rumble routing: the cause is kept while the rumble plays and chosen again when it starts afresh") {
    RumbleRouter router;
    route(router, 1.0, 0.0f, 0.0f, firing());
    CHECK(route(router, 1.01, 0.5f, 0.5f, firing()).cause == RumbleCause::Weapon);
    // The fire action goes up and a window passes: the same rumble is still the weapon's.
    CHECK(route(router, 1.02, 0.5f, 0.5f).cause == RumbleCause::Weapon);
    const RumbleLevels later = route(router, 1.6, 0.45f, 0.5f);
    CHECK(later.cause == RumbleCause::Weapon);
    CHECK_FALSE(later.changed);
    // It dies away and a damage rumble starts with nothing pressed.
    route(router, 1.7, 0.0f, 0.0f);
    const RumbleLevels damage = route(router, 2.0, 0.8f, 0.2f);
    CHECK(damage.cause == RumbleCause::Body);
    CHECK(damage.changed);
}

TEST_CASE("rumble routing: a rumble that stacks on a playing one is judged again") {
    RumbleRouter router;
    route(router, 1.0, 0.0f, 0.0f, firing());
    CHECK(route(router, 1.01, 0.3f, 0.3f, firing()).cause == RumbleCause::Weapon);
    // Fire up for good; a launcher press stacks a bigger rumble on the weapon's.
    route(router, 1.3, 0.3f, 0.3f);
    route(router, 1.31, 0.3f, 0.3f, shoulder());
    const RumbleLevels stacked = route(router, 1.35, 0.8f, 0.8f);
    CHECK(stacked.cause == RumbleCause::Shoulder);
    CHECK(stacked.changed);
}

TEST_CASE("rumble routing: a rumble under the quiet level starts nothing") {
    RumbleRouter router;
    route(router, 1.0, 0.0f, 0.0f, firing());
    const RumbleLevels faint = route(router, 1.01, 0.01f, 0.01f, firing());
    CHECK_FALSE(faint.changed);
    CHECK(route(router, 1.02, 0.5f, 0.5f, firing()).cause == RumbleCause::Weapon);
}

TEST_CASE("rumble routing: the policy sends a shot's rumble to the weapon hand alone") {
    HapticsPolicy routed(1.0f, true);
    HapticsFrame frame;
    frame.seconds = 1.0;
    frame.fireHeld = true;
    routed.update(frame);
    frame.seconds = 1.01;
    frame.rumble = GameRumble{0.0f, 0.9f};
    const auto out = routed.update(frame);
    CHECK(out[kRight].kind == HapticCommandKind::Pulse);
    CHECK(out[kRight].source == HapticSource::Game);
    CHECK(out[kLeft].kind == HapticCommandKind::None); // no low motor: the other hand's share is nothing
    REQUIRE(routed.takeRoutedCause());
    CHECK_FALSE(routed.takeRoutedCause());
}

TEST_CASE("rumble routing: the policy sends the shoulder launcher's rumble to the off hand") {
    HapticsPolicy routed(1.0f, true);
    HapticsFrame frame;
    frame.seconds = 1.0;
    frame.shoulder = true;
    routed.update(frame);
    frame.seconds = 1.05;
    frame.shoulder = false;
    frame.rumble = GameRumble{0.6f, 0.6f};
    const auto out = routed.update(frame);
    CHECK(out[kLeft].kind == HapticCommandKind::Pulse);
    CHECK(out[kLeft].amplitude == doctest::Approx(0.6f));
    CHECK(out[kRight].amplitude == doctest::Approx(0.6f * kSupportShare));
}

TEST_CASE("rumble routing: without it the policy keeps the old split, whatever was pressed") {
    HapticsPolicy plain(1.0f, false);
    CHECK_FALSE(plain.routesRumble());
    HapticsFrame frame;
    frame.seconds = 1.0;
    frame.shoulder = true;
    frame.rumble = GameRumble{0.6f, 0.9f};
    const auto out = plain.update(frame);
    CHECK(out[kRight].amplitude == doctest::Approx(0.9f)); // the weapon hand: the louder motor
    CHECK(out[kLeft].amplitude == doctest::Approx(0.6f));  // the other: the low motor
    CHECK_FALSE(plain.takeRoutedCause());
    CHECK(std::string_view(evr::input::rumbleCauseName(RumbleCause::Shoulder)) == "the shoulder launcher");
}
