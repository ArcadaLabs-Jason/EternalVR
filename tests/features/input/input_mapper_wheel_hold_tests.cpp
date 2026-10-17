#include "features/input/input_mapper.hpp"

#include "features/input/binding_compiler.hpp"
#include "features/input/binding_text.hpp"
#include "features/input/input_frames.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <string>

// The turn stick's down hold through the mapper: the weapon wheel's own, shorter hold when the wheel is all
// the down hold opens (the built-in maps), the longer one when a player's map puts something else there.

using evr::game::contains;
using evr::game::GameAction;
using evr::input::Axis2;
using evr::input::BindingProfile;
using evr::input::GameInput;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperSettings;
using evr::test::questTouchProfile;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

struct DownRun {
    float wheelAt = -1.0f; // seconds from the push to WeaponWheel down, -1 for never
    bool quickSwitch = false;
    bool chainsawDown = false;
};

// The right stick pushed straight down for `seconds`, then let go.
DownRun pushDown(InputMapper& mapper, float seconds) {
    DownRun run;
    InputFrame down = restingFrame();
    down.right.stick = {0.0f, -1.0f};
    const int frames = static_cast<int>(seconds / kFrame + 0.5f);
    for (int i = 0; i < frames; ++i) {
        const GameInput input = mapper.update(down, {}, kFrame);
        if (contains(input.down, GameAction::WeaponWheel) && run.wheelAt < 0.0f) {
            run.wheelAt = static_cast<float>(i) * kFrame;
        }
        run.chainsawDown = run.chainsawDown || contains(input.down, GameAction::Chainsaw);
    }
    for (int i = 0; i < 3; ++i) {
        const GameInput input = mapper.update(restingFrame(), {}, kFrame);
        run.quickSwitch = run.quickSwitch || contains(input.down, GameAction::QuickSwitch);
        run.chainsawDown = run.chainsawDown || contains(input.down, GameAction::Chainsaw);
    }
    return run;
}

BindingProfile profileFrom(const std::string& text) {
    return evr::input::buildBindingProfile(evr::input::parseBindingText(text).entries).profile;
}

} // namespace

TEST_CASE("the built-in map's down hold is the weapon wheel's 0.15 s: a 0.1 s flick is a quick switch") {
    const BindingProfile profile = questTouchProfile();
    CHECK(evr::input::downHoldOnlyOpensWheel(profile));
    InputMapper mapper(profile);
    CHECK(mapper.settings().turnStick.earlyHoldSeconds ==
          doctest::Approx(evr::input::kWheelStickHoldSeconds));
    CHECK(mapper.settings().turnStick.holdSeconds == doctest::Approx(0.30f));
    const DownRun flick = pushDown(mapper, 0.1f);
    CHECK(flick.quickSwitch);
    CHECK(flick.wheelAt < 0.0f);
    const DownRun hold = pushDown(mapper, 0.3f);
    CHECK_FALSE(hold.quickSwitch);
    // The claim frame is time zero; the hold is reached on the first frame at or past 0.15 s.
    CHECK(hold.wheelAt == doctest::Approx(0.15f).epsilon(0.08));
}

TEST_CASE("a 0.2 s push opened the wheel's hold only after 0.3 s before; now at 0.15 s") {
    MapperSettings before;
    before.wheelStickHoldSeconds = 0.30f; // the stick's hold up to 0.1.50 (the launcher's Medium, + 0.05 s)
    InputMapper old(questTouchProfile(), before);
    const DownRun oldRun = pushDown(old, 0.2f);
    CHECK(oldRun.quickSwitch);
    CHECK(oldRun.wheelAt < 0.0f);
    InputMapper mapper(questTouchProfile());
    const DownRun run = pushDown(mapper, 0.2f);
    CHECK_FALSE(run.quickSwitch);
    CHECK(run.wheelAt >= 0.0f);
}

TEST_CASE("a player's down hold bound to something else keeps the longer hold") {
    const std::string text = R"([bindings]
"weapon_hand" = "right"
"left.stick.role" = "move"
"right.stick.role" = "turn"
"right.trigger.press" = "fire"
"right.stick.down_tap" = "quick_switch"
"right.stick.down_hold" = "chainsaw"
)";
    const BindingProfile profile = profileFrom(text);
    REQUIRE(profile.turnStick.has_value());
    CHECK_FALSE(evr::input::downHoldOnlyOpensWheel(profile));
    InputMapper mapper(profile);
    CHECK(mapper.settings().turnStick.holdSeconds == doctest::Approx(0.30f));
    CHECK(mapper.settings().turnStick.earlyHoldSeconds >= mapper.settings().turnStick.holdSeconds);
    const DownRun run = pushDown(mapper, 0.2f);
    CHECK(run.quickSwitch);
    CHECK_FALSE(run.chainsawDown);
}

TEST_CASE("a wheel stick hold that is not finite or out of range falls back to 0.15 s") {
    MapperSettings settings;
    settings.wheelStickHoldSeconds = -1.0f;
    CHECK(InputMapper(questTouchProfile(), settings).settings().turnStick.earlyHoldSeconds ==
          doctest::Approx(evr::input::kWheelStickHoldSeconds));
}
