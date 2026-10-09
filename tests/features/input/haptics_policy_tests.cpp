#include "features/input/haptics_policy.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <ostream>
#include <string_view>

using evr::input::GameRumble;
using evr::input::Hand;
using evr::input::HapticCommand;
using evr::input::HapticCommandKind;
using evr::input::HapticsFrame;
using evr::input::HapticSource;
using evr::input::HapticsPolicy;
using evr::input::kFireRepeatSeconds;
using evr::input::kMenuGapSeconds;
using evr::input::MenuTick;
using evr::input::WheelTick;

namespace {

constexpr std::size_t kLeft = 0;
constexpr std::size_t kRight = 1;

HapticsFrame at(double seconds) {
    HapticsFrame frame;
    frame.seconds = seconds;
    return frame;
}

HapticsFrame firing(double seconds, bool held = true) {
    HapticsFrame frame = at(seconds);
    frame.fireHeld = held;
    return frame;
}

bool pulse(const HapticCommand& c, HapticSource source) {
    return c.kind == HapticCommandKind::Pulse && c.source == source;
}

bool none(const HapticCommand& c) {
    return c.kind == HapticCommandKind::None;
}

} // namespace

TEST_CASE("fire pulses the weapon hand when it goes down, then repeats while held") {
    HapticsPolicy policy(1.0f);
    const auto first = policy.update(firing(1.0));
    CHECK(pulse(first[kRight], HapticSource::Fire));
    CHECK(none(first[kLeft]));
    CHECK(none(policy.update(firing(1.0 + kFireRepeatSeconds / 2))[kRight]));
    const auto repeat = policy.update(firing(1.0 + kFireRepeatSeconds));
    CHECK(pulse(repeat[kRight], HapticSource::Fire));
    CHECK(repeat[kRight].amplitude < first[kRight].amplitude);
    CHECK(none(policy.update(firing(1.3, false))[kRight]));
    CHECK(pulse(policy.update(firing(1.31))[kRight], HapticSource::Fire));
}

TEST_CASE("the weapon hand follows handedness") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = firing(1.0);
    frame.weaponHand = Hand::Left;
    const auto out = policy.update(frame);
    CHECK(pulse(out[kLeft], HapticSource::Fire));
    CHECK(none(out[kRight]));
}

TEST_CASE("held fire repeats at a steady rate through a late frame") {
    HapticsPolicy policy(1.0f);
    policy.update(firing(0.0));
    int pulses = 0;
    for (int i = 1; i <= 90; ++i) {
        pulses += pulse(policy.update(firing(i / 90.0))[kRight], HapticSource::Fire) ? 1 : 0;
    }
    CHECK(pulses == doctest::Approx(1.0 / kFireRepeatSeconds).epsilon(0.2));
}

TEST_CASE("a punch pulses the hand that punched, at full shape") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.punch[kLeft] = true;
    const auto out = policy.update(frame);
    CHECK(pulse(out[kLeft], HapticSource::Punch));
    CHECK(out[kLeft].amplitude == doctest::Approx(1.0f));
    CHECK(none(out[kRight]));
}

TEST_CASE("the capture pulses both hands") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.capture = true;
    const auto out = policy.update(frame);
    CHECK(pulse(out[kLeft], HapticSource::Capture));
    CHECK(pulse(out[kRight], HapticSource::Capture));
}

TEST_CASE("menu ticks are light and at least the gap apart on one hand") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.menu[kRight] = MenuTick::Enter;
    const auto enter = policy.update(frame);
    CHECK(pulse(enter[kRight], HapticSource::Menu));
    CHECK(enter[kRight].amplitude < 0.5f);
    frame = at(1.0 + kMenuGapSeconds / 2);
    frame.menu[kRight] = MenuTick::Click;
    CHECK(none(policy.update(frame)[kRight]));
    frame = at(1.0 + kMenuGapSeconds);
    frame.menu[kRight] = MenuTick::Click;
    const auto click = policy.update(frame);
    CHECK(pulse(click[kRight], HapticSource::Menu));
    CHECK(click[kRight].amplitude > enter[kRight].amplitude);
}

TEST_CASE("the thumb-rest wheel ticks the picking hand, lightly to start and firmer on a pick") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.wheel[kLeft] = WheelTick::Arm;
    const auto arm = policy.update(frame);
    CHECK(pulse(arm[kLeft], HapticSource::Wheel));
    CHECK(none(arm[kRight]));
    frame = at(2.0);
    frame.wheel[kLeft] = WheelTick::Pick;
    const auto pick = policy.update(frame);
    CHECK(pulse(pick[kLeft], HapticSource::Wheel));
    CHECK(pick[kLeft].amplitude > arm[kLeft].amplitude);
    CHECK(pick[kLeft].seconds > arm[kLeft].seconds);
    // Scaled by the strength like every pulse.
    HapticsPolicy off(0.0f);
    CHECK(none(off.update(frame)[kLeft]));
}

TEST_CASE("the strength scales every pulse, and 0 gives nothing") {
    HapticsPolicy full(1.0f);
    HapticsPolicy light(0.35f);
    HapticsPolicy off(0.0f);
    HapticsFrame frame = at(1.0);
    frame.punch[kRight] = true;
    frame.capture = true;
    frame.menu[kLeft] = MenuTick::Click;
    frame.fireHeld = true;
    frame.rumble = GameRumble{1.0f, 1.0f};
    const auto strong = full.update(frame);
    const auto weak = light.update(frame);
    CHECK(weak[kRight].amplitude == doctest::Approx(strong[kRight].amplitude * 0.35f));
    const auto nothing = off.update(frame);
    CHECK(none(nothing[kLeft]));
    CHECK(none(nothing[kRight]));
}

TEST_CASE("an unusable strength falls back to the default") {
    CHECK(HapticsPolicy(-0.5f).strength() == evr::input::kDefaultHapticStrength);
    CHECK(HapticsPolicy(1.5f).strength() == evr::input::kDefaultHapticStrength);
    CHECK(HapticsPolicy(std::numeric_limits<float>::quiet_NaN()).strength() ==
          evr::input::kDefaultHapticStrength);
    CHECK(HapticsPolicy().strength() == evr::input::kDefaultHapticStrength);
}

TEST_CASE("the game's low motor goes to both hands, the high one to the weapon hand") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.rumble = GameRumble{0.2f, 0.9f};
    const auto out = policy.update(frame);
    REQUIRE(pulse(out[kLeft], HapticSource::Game));
    REQUIRE(pulse(out[kRight], HapticSource::Game));
    CHECK(out[kLeft].amplitude == doctest::Approx(0.2f));
    CHECK(out[kRight].amplitude == doctest::Approx(0.9f));
    CHECK(out[kRight].seconds == doctest::Approx(evr::input::kRumbleHoldSeconds));

    frame = at(1.01);
    frame.rumble = GameRumble{0.0f, 0.7f};
    const auto highOnly = policy.update(frame);
    CHECK(pulse(highOnly[kRight], HapticSource::Game));
    CHECK(highOnly[kLeft].kind == HapticCommandKind::Stop); // the left hand's low motor went to 0
}

TEST_CASE("a steady game rumble is renewed before its pulse ends, not every frame") {
    HapticsPolicy policy(1.0f);
    int sent = 0;
    for (int i = 0; i < 90; ++i) {
        HapticsFrame frame = at(1.0 + i / 90.0);
        frame.rumble = GameRumble{0.5f, 0.5f};
        const auto out = policy.update(frame);
        CHECK(out[kLeft].kind != HapticCommandKind::Stop);
        sent += pulse(out[kLeft], HapticSource::Game) ? 1 : 0;
    }
    // One second of rumble: a pulse about every kRumbleHoldSeconds less the renewal margin.
    CHECK(sent >= 10);
    CHECK(sent <= 25);
}

TEST_CASE("a game rumble that changes level is sent again at once") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.rumble = GameRumble{0.8f, 0.0f};
    policy.update(frame);
    frame = at(1.011);
    frame.rumble = GameRumble{0.4f, 0.0f};
    const auto out = policy.update(frame);
    CHECK(pulse(out[kLeft], HapticSource::Game));
    CHECK(out[kLeft].amplitude == doctest::Approx(0.4f));
}

TEST_CASE("the game's rumble ending stops what it started, and nothing else") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.rumble = GameRumble{0.5f, 0.5f};
    policy.update(frame);
    const auto stop = policy.update(at(1.02));
    CHECK(stop[kLeft].kind == HapticCommandKind::Stop);
    CHECK(stop[kRight].kind == HapticCommandKind::Stop);
    CHECK(none(policy.update(at(1.03))[kLeft])); // stopped once

    frame = at(3.0);
    frame.punch[kLeft] = true;
    policy.update(frame);
    CHECK(none(policy.update(at(3.01))[kLeft])); // the punch keeps playing
}

TEST_CASE("game motors out of range are limited") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.rumble = GameRumble{2.0f, std::numeric_limits<float>::quiet_NaN()};
    const auto out = policy.update(frame);
    CHECK(out[kLeft].amplitude == doctest::Approx(1.0f));
    CHECK(out[kRight].amplitude == doctest::Approx(1.0f));
    frame = at(1.02);
    frame.rumble = GameRumble{-1.0f, 0.0f};
    CHECK(policy.update(frame)[kLeft].kind == HapticCommandKind::Stop);
}

TEST_CASE("the game's rumble resumes after a stronger pulse from another source") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.punch[kLeft] = true;
    frame.rumble = GameRumble{0.3f, 0.0f};
    CHECK(pulse(policy.update(frame)[kLeft], HapticSource::Punch));
    frame = at(1.02);
    frame.rumble = GameRumble{0.3f, 0.0f};
    CHECK(none(policy.update(frame)[kLeft]));
    frame = at(1.1);
    frame.rumble = GameRumble{0.3f, 0.0f};
    CHECK(pulse(policy.update(frame)[kLeft], HapticSource::Game));
}

TEST_CASE("a weaker pulse does not cut a stronger one from another source short") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = at(1.0);
    frame.punch[kRight] = true;
    CHECK(pulse(policy.update(frame)[kRight], HapticSource::Punch));
    CHECK(none(policy.update(firing(1.02))[kRight])); // the punch (0.08 s) still plays
    CHECK(pulse(policy.update(firing(1.02 + kFireRepeatSeconds))[kRight], HapticSource::Fire));
}

TEST_CASE("one command per hand: the strongest pulse asked for") {
    HapticsPolicy policy(1.0f);
    HapticsFrame frame = firing(1.0);
    frame.punch[kRight] = true;
    frame.menu[kRight] = MenuTick::Click;
    CHECK(pulse(policy.update(frame)[kRight], HapticSource::Punch));
}

TEST_CASE("reset forgets the held fire and what plays") {
    HapticsPolicy policy(1.0f);
    policy.update(firing(1.0));
    policy.reset();
    CHECK(pulse(policy.update(firing(1.01))[kRight], HapticSource::Fire));
}

TEST_CASE("every source has a name for the log") {
    CHECK(std::string_view(evr::input::hapticSourceName(HapticSource::Fire)) == "fire");
    CHECK(std::string_view(evr::input::hapticSourceName(HapticSource::Game)) == "game");
    CHECK(std::string_view(evr::input::hapticSourceName(HapticSource::Capture)) == "capture");
    CHECK(std::string_view(evr::input::hapticSourceName(HapticSource::Wheel)) == "wheel");
}
