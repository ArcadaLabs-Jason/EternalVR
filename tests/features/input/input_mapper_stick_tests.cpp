#include "features/input/input_mapper.hpp"

#include "features/input/input_frames.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

// The sticks through the mapper, with the built-in Quest Touch map: the recenter chord (both sticks held),
// and the turn stick pointing at a weapon wheel a button holds.

using evr::game::contains;
using evr::game::GameAction;
using evr::input::Axis2;
using evr::input::BindingProfile;
using evr::input::ButtonInput;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperSettings;
using evr::input::PressKind;
using evr::test::questTouchProfile;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

} // namespace

namespace {

struct StickRun {
    int melee = 0;    // presses of the right stick's melee
    int crucible = 0; // presses of the left stick's crucible
    bool recenter = false;
    float recenterAt = -1.0f; // seconds from the start when Recenter first went down
};

// Each stick held over [from, to) seconds (never when from < 0), for `total` seconds.
StickRun
runSticks(float leftFrom, float leftTo, float rightFrom, float rightTo, float total, bool chordOn = true) {
    MapperSettings settings;
    settings.stickChordRecenter = chordOn;
    InputMapper mapper(questTouchProfile(), settings);
    StickRun run;
    const int frames = static_cast<int>(total / kFrame);
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) * kFrame;
        InputFrame frame = restingFrame();
        frame.left.stickClick = leftFrom >= 0.0f && t >= leftFrom && t < leftTo;
        frame.right.stickClick = rightFrom >= 0.0f && t >= rightFrom && t < rightTo;
        const GameInput input = mapper.update(frame, {}, kFrame);
        run.melee += contains(input.pressed, GameAction::Melee) ? 1 : 0;
        run.crucible += contains(input.pressed, GameAction::Crucible) ? 1 : 0;
        if (contains(input.down, GameAction::Recenter) && !run.recenter) {
            run.recenter = true;
            run.recenterAt = t;
        }
    }
    return run;
}

} // namespace

TEST_CASE("a single stick click still melees on its first frame") {
    MapperSettings settings;
    InputMapper mapper(questTouchProfile(), settings);
    InputFrame frame = restingFrame();
    frame.right.stickClick = true;
    CHECK(contains(mapper.update(frame, {}, kFrame).pressed, GameAction::Melee));
}

TEST_CASE("both sticks pressed together are the recenter chord, not melee and crucible") {
    const StickRun together = runSticks(0.0f, 1.0f, 0.0f, 1.0f, 1.2f);
    CHECK(together.melee == 0);
    CHECK(together.crucible == 0);
    CHECK(together.recenter);
    // The binding goes down after the hold time; the room-scale long press counts the rest.
    CHECK(together.recenterAt == doctest::Approx(0.25f).epsilon(0.1));
    // Off (ETERNALVR_RECENTER_HOLD=0): no recenter, and still no melee or crucible from the chord.
    const StickRun off = runSticks(0.0f, 1.0f, 0.0f, 1.0f, 1.2f, false);
    CHECK_FALSE(off.recenter);
    CHECK(off.melee == 0);
}

TEST_CASE("a second stick pressed a little after the first joins the chord; the first already fired") {
    const StickRun run = runSticks(0.0f, 1.0f, 0.1f, 1.0f, 1.2f);
    CHECK(run.crucible == 1); // instant, before the second stick came
    CHECK(run.melee == 0);
    CHECK(run.recenter);
}

TEST_CASE("a quick click of one stick while the other is held is still sent") {
    // Left held from 0 to 1 s; the right clicked at 0.5 s for 0.05 s.
    const StickRun run = runSticks(0.0f, 1.0f, 0.5f, 0.55f, 1.2f);
    CHECK(run.crucible == 1);
    CHECK(run.melee == 1);
    CHECK_FALSE(run.recenter);
}

TEST_CASE("a stick held past the window with the other is the chord") {
    const StickRun run = runSticks(0.0f, 2.0f, 0.5f, 2.0f, 2.2f);
    CHECK(run.crucible == 1);
    CHECK(run.melee == 0);
    CHECK(run.recenter);
    CHECK(run.recenterAt == doctest::Approx(0.75f).epsilon(0.1));
}

namespace {

// The Touch map with the weapon wheel on a right bumper hold too.
BindingProfile bumperWheelProfile() {
    BindingProfile profile = questTouchProfile();
    profile.buttons.push_back({Hand::Right, ButtonInput::Shoulder, PressKind::Hold, GameAction::WeaponWheel});
    return profile;
}

InputFrame bumperAndStick(bool bumper, Axis2 stick) {
    InputFrame frame = restingFrame();
    frame.right.shoulderButton = bumper;
    frame.right.stick = stick;
    return frame;
}

} // namespace

TEST_CASE("a wheel held by a button is pointed at with the turn stick, which does not turn meanwhile") {
    InputMapper mapper(bumperWheelProfile());
    for (int i = 0; i < 40; ++i) {
        mapper.update(bumperAndStick(true, {}), {}, kFrame);
    }
    const GameInput right = mapper.update(bumperAndStick(true, {1.0f, 0.0f}), {}, kFrame);
    CHECK(contains(right.down, GameAction::WeaponWheel));
    CHECK(right.wheelPointer == Axis2{1.0f, 0.0f});
    CHECK(right.turnDegrees == 0.0f);
    // Straight up points at the top of the wheel; it is not the chainsaw.
    const GameInput up = mapper.update(bumperAndStick(true, {0.0f, 1.0f}), {}, kFrame);
    CHECK(up.wheelPointer == Axis2{0.0f, 1.0f});
    CHECK_FALSE(contains(up.down, GameAction::Chainsaw));
}

TEST_CASE("after a button's wheel, the turn stick waits for the centre before it turns or gestures again") {
    InputMapper mapper(bumperWheelProfile());
    for (int i = 0; i < 40; ++i) {
        mapper.update(bumperAndStick(true, {0.0f, 1.0f}), {}, kFrame);
    }
    // The bumper lets go first, with the stick still up and then sideways.
    for (const Axis2 stick : {Axis2{0.0f, 1.0f}, Axis2{1.0f, 0.0f}}) {
        const GameInput held = mapper.update(bumperAndStick(false, stick), {}, kFrame);
        CHECK_FALSE(contains(held.down, GameAction::WeaponWheel));
        CHECK_FALSE(contains(held.down, GameAction::Chainsaw));
        CHECK(held.turnDegrees == 0.0f);
        CHECK(held.wheelPointer == Axis2{});
    }
    mapper.update(bumperAndStick(false, {}), {}, kFrame);
    CHECK(mapper.update(bumperAndStick(false, {1.0f, 0.0f}), {}, kFrame).turnDegrees < 0.0f);
    mapper.update(bumperAndStick(false, {}), {}, kFrame);
    CHECK(
        contains(mapper.update(bumperAndStick(false, {0.0f, 1.0f}), {}, kFrame).down, GameAction::Chainsaw));
}
