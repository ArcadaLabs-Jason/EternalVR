#include "features/input/input_mapper.hpp"

#include "features/input/input_frames.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <ostream>

// The thumb-rest wheel through the mapper, with the built-in Quest Touch maps: it decides on the frame a
// stick leaves the centre, before any turn or gesture, and leaves the sticks alone otherwise.

using evr::game::contains;
using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::game::Handedness;
using evr::input::Axis2;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperContext;
using evr::input::MapperSettings;
using evr::input::RestWheelMode;
using evr::input::RestWheelPick;
using evr::input::TurnMode;
using evr::test::questTouchProfile;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;
constexpr Axis2 kCentre{};
constexpr Axis2 kUp{0.0f, 1.0f};
constexpr Axis2 kDown{0.0f, -1.0f};
constexpr Axis2 kRight{1.0f, 0.0f};

MapperSettings restSettings(TurnMode turn = TurnMode::Smooth,
                            RestWheelMode mode = RestWheelMode::Edge,
                            RestWheelPick pick = RestWheelPick::Wheel) {
    MapperSettings settings;
    settings.turn.mode = turn;
    settings.restWheel.mode = mode;
    settings.restWheel.pick = pick;
    settings.restSensors = {true, true};
    return settings;
}

InputFrame frame(bool leftRest, bool rightRest, Axis2 left, Axis2 right) {
    InputFrame f = restingFrame();
    f.left.thumbRest = leftRest;
    f.right.thumbRest = rightRest;
    f.left.stick = left;
    f.right.stick = right;
    return f;
}

struct Run {
    float turned = 0.0f;
    float moved = 0.0f; // the largest move magnitude
    GameActionSet everDown;
    GameActionSet everPressed;
    int wheelFrames = 0;
    GameInput last;
};

Run feed(
    InputMapper& mapper, const InputFrame& f, int frames, Run run = {}, const MapperContext& context = {}) {
    for (int i = 0; i < frames; ++i) {
        const GameInput input = mapper.update(f, context, kFrame);
        run.turned += std::fabs(input.turnDegrees);
        run.moved = std::fmax(run.moved, evr::input::magnitude(input.move));
        run.everDown |= input.down;
        run.everPressed |= input.pressed;
        run.wheelFrames += contains(input.down, GameAction::WeaponWheel) ? 1 : 0;
        run.last = input;
    }
    return run;
}

} // namespace

TEST_CASE("a flick of the turn stick right after a deliberate rest snaps nothing and turns nothing") {
    for (const TurnMode turn : {TurnMode::Snap, TurnMode::Smooth}) {
        CAPTURE(static_cast<int>(turn));
        InputMapper mapper(questTouchProfile(), restSettings(turn));
        // The left thumb idle for more than kOwnStickVoidSeconds, then on its rest: a deliberate rest.
        feed(mapper, frame(false, false, kCentre, kCentre), 45);
        feed(mapper, frame(true, false, kCentre, kCentre), 8);
        // From the centre to full right in one frame, held, then back.
        Run run = feed(mapper, frame(true, false, kCentre, kRight), 30);
        run = feed(mapper, frame(true, false, kCentre, kCentre), 30, run);
        CHECK(run.turned == 0.0f);
        CHECK(run.wheelFrames > 0);
        // The next flick, long after, with the thumb lifted: turns as always.
        feed(mapper, frame(false, false, kCentre, kCentre), 60);
        CHECK(feed(mapper, frame(false, false, kCentre, kRight), 30).turned > 0.0f);
    }
}

TEST_CASE("a thumb left resting does not stop the turn stick turning") {
    InputMapper mapper(questTouchProfile(), restSettings());
    feed(mapper, frame(true, false, kCentre, kCentre), 90);
    const Run run = feed(mapper, frame(true, false, kCentre, kRight), 30);
    CHECK(run.turned > 0.0f);
    CHECK(run.wheelFrames == 0);
}

TEST_CASE("pointing up at the wheel is no chainsaw, and the wheel's pointer is the stick") {
    InputMapper mapper(questTouchProfile(), restSettings());
    feed(mapper, frame(true, false, kCentre, kCentre), 8);
    const Run run = feed(mapper, frame(true, false, kCentre, kUp), 30);
    CHECK_FALSE(contains(run.everDown, GameAction::Chainsaw));
    CHECK(contains(run.last.down, GameAction::WeaponWheel));
    CHECK(run.last.wheelPointer == kUp);
    CHECK(run.last.restWheel.wheelDown);
    const Run after = feed(mapper, frame(true, false, kCentre, kCentre), 40);
    CHECK_FALSE(contains(after.last.down, GameAction::WeaponWheel));
    CHECK_FALSE(contains(after.everDown, GameAction::QuickSwitch));
}

TEST_CASE("the move stick picks while the right thumb rests, and moves nothing until it is back") {
    InputMapper mapper(questTouchProfile(), restSettings());
    feed(mapper, frame(false, true, kCentre, kCentre), 8);
    Run run = feed(mapper, frame(false, true, kUp, kCentre), 30);
    CHECK(run.moved == 0.0f);
    CHECK(contains(run.last.down, GameAction::WeaponWheel));
    CHECK(run.last.wheelPointer == kUp);
    // The thumb lifts with the stick still up: the rest was needed only to start, so the wheel stays open
    // and the stick moves nothing.
    run = feed(mapper, frame(false, false, kUp, kCentre), 40);
    CHECK(run.moved == 0.0f);
    CHECK(contains(run.last.down, GameAction::WeaponWheel));
    // Back in the centre the wheel closes; pushed again, the stick moves.
    feed(mapper, frame(false, false, kCentre, kCentre), 2);
    CHECK(feed(mapper, frame(false, false, kUp, kCentre), 5).moved > 0.0f);
}

TEST_CASE("the turn stick's own wheel, quick switch and chainsaw are unchanged without a resting thumb") {
    InputMapper mapper(questTouchProfile(), restSettings());
    Run hold = feed(mapper, frame(false, false, kCentre, kDown), 45);
    CHECK(contains(hold.last.down, GameAction::WeaponWheel));
    CHECK_FALSE(hold.last.restWheel.wheelDown);
    feed(mapper, frame(false, false, kCentre, kCentre), 5);
    Run tap = feed(mapper, frame(false, false, kCentre, kDown), 5);
    tap = feed(mapper, frame(false, false, kCentre, kCentre), 2, tap);
    CHECK(contains(tap.everDown, GameAction::QuickSwitch));
    CHECK(contains(feed(mapper, frame(false, false, kCentre, kUp), 3).everDown, GameAction::Chainsaw));
}

TEST_CASE("a turn stick already held down for the game's wheel keeps it when the thumb lands") {
    InputMapper mapper(questTouchProfile(), restSettings());
    Run run = feed(mapper, frame(false, false, kCentre, kDown), 10);
    // The left thumb lands mid-sweep, and the right stick moves on while the down hold opens the wheel.
    run = feed(mapper, frame(true, false, kCentre, kDown), 40, run);
    CHECK(contains(run.last.down, GameAction::WeaponWheel));
    CHECK_FALSE(run.last.restWheel.wheelDown);
}

TEST_CASE("while touched: a down flick that starts with the touch picks, and is no quick switch or wheel") {
    // The flick reaches the down sweep's claim before the touch is sensed (lead frames) or with it.
    for (const int lead : {0, 2}) {
        InputMapper mapper(questTouchProfile(),
                           restSettings(TurnMode::Smooth, RestWheelMode::Full, RestWheelPick::Slots));
        feed(mapper, frame(false, false, kCentre, kCentre), 5);
        Run run = feed(mapper, frame(false, false, kCentre, kDown), lead);
        run = feed(mapper, frame(true, false, kCentre, kDown), 20, run);
        run = feed(mapper, frame(true, false, kCentre, kCentre), 3, run);
        CHECK_FALSE(contains(run.everDown, GameAction::QuickSwitch));
        CHECK_FALSE(contains(run.everDown, GameAction::WeaponWheel));
        CHECK(contains(run.everDown, GameAction::WeaponSlot3));
    }
}

TEST_CASE("nothing arms while a menu holds input or the game holds the wheel back") {
    InputMapper mapper(questTouchProfile(), restSettings());
    feed(mapper, frame(true, false, kCentre, kCentre), 8);
    MapperContext blocked;
    blocked.restWheelBlocked = true;
    Run run = feed(mapper, frame(true, false, kCentre, kUp), 30, {}, blocked);
    CHECK(run.wheelFrames == 0);
    CHECK_FALSE(run.last.restWheel.taken[1]);

    InputMapper menu(questTouchProfile(), restSettings());
    feed(menu, frame(true, false, kCentre, kCentre), 8);
    MapperContext held;
    held.menuHold = true;
    CHECK(feed(menu, frame(true, false, kCentre, kUp), 30, {}, held).last.restWheel.event ==
          evr::input::RestWheelEvent::None);
}

TEST_CASE("weapon by direction presses the slot once, and the game's wheel never opens") {
    InputMapper mapper(questTouchProfile(),
                       restSettings(TurnMode::Smooth, RestWheelMode::Edge, RestWheelPick::Slots));
    feed(mapper, frame(true, false, kCentre, kCentre), 8);
    Run run = feed(mapper, frame(true, false, kCentre, kRight), 20);
    run = feed(mapper, frame(true, false, kCentre, kCentre), 5, run);
    CHECK(run.wheelFrames == 0);
    CHECK(contains(run.everPressed, GameAction::WeaponSlot2));
    CHECK(run.turned == 0.0f);
}

TEST_CASE("left-handed maps: the rest on the move-stick hand arms the turn stick, wherever it is") {
    for (const Handedness handedness : {Handedness::LeftButtonSwap, Handedness::LeftButtonAndStickSwap}) {
        CAPTURE(static_cast<int>(handedness));
        const auto profile = questTouchProfile(handedness);
        REQUIRE(profile.turnStick.has_value());
        const Hand turn = *profile.turnStick;
        const bool leftRest = turn == Hand::Right;
        InputMapper mapper(profile, restSettings(TurnMode::Snap));
        feed(mapper, frame(leftRest, !leftRest, kCentre, kCentre), 8);
        const Axis2 left = turn == Hand::Left ? kRight : kCentre;
        const Axis2 right = turn == Hand::Right ? kRight : kCentre;
        const Run run = feed(mapper, frame(leftRest, !leftRest, left, right), 30);
        CHECK(run.turned == 0.0f);
        CHECK(contains(run.last.down, GameAction::WeaponWheel));
        CHECK(run.last.wheelPointer == kRight);
    }
}

TEST_CASE("the turn stick is the wheel under extreme, and turns while the other thumb rests") {
    InputMapper mapper(questTouchProfile(), restSettings(TurnMode::Smooth, RestWheelMode::Extreme));
    feed(mapper, frame(false, false, kCentre, kCentre), 2);
    Run run = feed(mapper, frame(false, false, kCentre, kRight), 30);
    CHECK(run.turned == 0.0f);
    CHECK(contains(run.last.down, GameAction::WeaponWheel));
    feed(mapper, frame(true, false, kCentre, kCentre), 40);
    run = feed(mapper, frame(true, false, kCentre, kRight), 30);
    CHECK(run.turned > 0.0f);
    CHECK(run.wheelFrames == 0);
}

TEST_CASE(
    "under extreme the gamepad look never turns with the wheel's stick, and the picking stick never walks") {
    InputMapper mapper(questTouchProfile(), restSettings(TurnMode::Smooth, RestWheelMode::Extreme));
    feed(mapper, frame(false, false, kCentre, kCentre), 2);
    feed(mapper, frame(false, false, kCentre, kRight), 5);
    CHECK(mapper.turnStick() == Axis2{}); // the virtual gamepad's look (no turn hook) reads it
    // The right thumb rests: the left stick picks and does not walk.
    feed(mapper, frame(false, true, kCentre, kCentre), 40);
    const Run run = feed(mapper, frame(false, true, kRight, kCentre), 30);
    CHECK(run.moved == 0.0f);
    CHECK(contains(run.last.down, GameAction::WeaponWheel));
}

TEST_CASE("without a rest sensor the wheel does nothing") {
    MapperSettings settings = restSettings();
    settings.restSensors = {false, false};
    InputMapper mapper(questTouchProfile(), settings);
    feed(mapper, frame(true, false, kCentre, kCentre), 8);
    const Run run = feed(mapper, frame(true, false, kCentre, kRight), 30);
    CHECK(run.turned > 0.0f);
    CHECK(run.wheelFrames == 0);
}

TEST_CASE("a face button counts as the rest only with face touch on, and not while pressed") {
    MapperSettings settings = restSettings();
    settings.restFaceTouch = true;
    InputMapper mapper(questTouchProfile(), settings);
    InputFrame touching = frame(false, false, kCentre, kCentre);
    touching.left.primaryTouch = true;
    feed(mapper, touching, 8);
    touching.right.stick = kRight;
    CHECK(feed(mapper, touching, 30).wheelFrames > 0);

    InputMapper pressing(questTouchProfile(), settings);
    InputFrame pressed = frame(false, false, kCentre, kCentre);
    pressed.left.primaryTouch = true;
    pressed.left.primaryButton = true;
    feed(pressing, pressed, 8);
    pressed.right.stick = kRight;
    CHECK(feed(pressing, pressed, 30).wheelFrames == 0);

    InputMapper off(questTouchProfile(), restSettings());
    InputFrame face = frame(false, false, kCentre, kCentre);
    face.left.primaryTouch = true;
    feed(off, face, 8);
    face.right.stick = kRight;
    CHECK(feed(off, face, 30).wheelFrames == 0);
}

TEST_CASE("Touch: a rest's touch begun with the thumb on its face buttons or stick waits") {
    MapperSettings settings = restSettings();
    settings.restIsThumbRest = {true, true};
    // The left thumb still on X, or on its stick, when the rest's touch begins: a push three frames later
    // turns (the touch has not registered yet), and the wheel never opens.
    for (const bool stick : {false, true}) {
        CAPTURE(stick);
        InputMapper mapper(questTouchProfile(), settings);
        feed(mapper, frame(false, false, kCentre, kCentre), 45);
        InputFrame elsewhere = frame(true, false, kCentre, kCentre);
        (stick ? elsewhere.left.stickTouch : elsewhere.left.primaryTouch) = true;
        feed(mapper, elsewhere, 3);
        elsewhere.right.stick = kRight;
        const Run run = feed(mapper, elsewhere, 30);
        CHECK(run.wheelFrames == 0);
        CHECK(run.turned > 0.0f);
    }
    // The thumb only on the rest: the touch registers on its first frame, and the same push picks.
    InputMapper mapper(questTouchProfile(), settings);
    feed(mapper, frame(false, false, kCentre, kCentre), 45);
    feed(mapper, frame(true, false, kCentre, kCentre), 3);
    const Run run = feed(mapper, frame(true, false, kCentre, kRight), 30);
    CHECK(run.wheelFrames > 0);
    CHECK(run.turned == 0.0f);
}

TEST_CASE("Touch with face touch on: the face buttons' touch is the thumb elsewhere, not the rest") {
    MapperSettings settings = restSettings();
    settings.restFaceTouch = true;
    settings.restIsThumbRest = {true, true};
    InputMapper mapper(questTouchProfile(), settings);
    InputFrame touching = frame(false, false, kCentre, kCentre);
    touching.left.primaryTouch = true;
    feed(mapper, touching, 8);
    touching.right.stick = kRight;
    CHECK(feed(mapper, touching, 30).wheelFrames == 0);
}

TEST_CASE("face touch as the rest: the stick's touch holds it back, and a press lifts it at once") {
    MapperSettings settings = restSettings();
    settings.restFaceTouch = true;
    // X touched with the stick touched too: not registered three frames on.
    InputMapper held(questTouchProfile(), settings);
    InputFrame both = frame(false, false, kCentre, kCentre);
    both.left.primaryTouch = true;
    both.left.stickTouch = true;
    feed(held, both, 3);
    CHECK(held.restWheel().stats().landings[0] == 0);
    CHECK(held.restWheel().stats().vetoedLandings[0] == 1);

    // X touched alone is the rest at once; pressing it (a jump) lifts it on that frame, with no hold-over.
    InputMapper mapper(questTouchProfile(), settings);
    InputFrame touching = frame(false, false, kCentre, kCentre);
    touching.left.primaryTouch = true;
    feed(mapper, touching, 10);
    CHECK(mapper.restWheel().stats().landings[0] == 1);
    touching.left.primaryButton = true;
    feed(mapper, touching, 1);
    CHECK(mapper.restWheel().stats().cutLifts[0] == 1);

    // Index: X touched with the trackpad touched is no landing yet either.
    InputMapper trackpad(questTouchProfile(), settings);
    InputFrame pad = frame(false, false, kCentre, kCentre);
    pad.left.primaryTouch = true;
    pad.left.trackpadTouch = true;
    feed(trackpad, pad, 3);
    CHECK(trackpad.restWheel().stats().landings[0] == 0);
    CHECK(trackpad.restWheel().stats().vetoedLandings[0] == 1);
}

TEST_CASE(
    "Touch: a jump from the rest lifts it at once, and the thumb back on it while still touching A waits") {
    MapperSettings settings = restSettings();
    settings.restIsThumbRest = {true, true};
    InputMapper mapper(questTouchProfile(), settings);
    feed(mapper, frame(false, false, kCentre, kCentre), 45);
    feed(mapper, frame(false, true, kCentre, kCentre), 20);
    REQUIRE(mapper.restWheel().stats().landings[1] == 1);
    // A touched and pressed: the jump goes out and the rest lifts on that frame.
    InputFrame jump = frame(false, false, kCentre, kCentre);
    jump.right.primaryTouch = true;
    jump.right.primaryButton = true;
    CHECK(contains(feed(mapper, jump, 1).everDown, GameAction::Jump));
    CHECK(mapper.restWheel().stats().cutLifts[1] == 1);
    // Back on the rest with A still touched: no landing until A is let go.
    InputFrame back = frame(false, true, kCentre, kCentre);
    back.right.primaryTouch = true;
    feed(mapper, back, 3);
    CHECK(mapper.restWheel().stats().landings[1] == 1);
    CHECK(mapper.restWheel().stats().vetoedLandings[1] == 1);
    feed(mapper, frame(false, true, kCentre, kCentre), 1);
    CHECK(mapper.restWheel().stats().landings[1] == 2);
}

namespace {

// Frames in which the snap turn fired.
int snaps(InputMapper& mapper, const InputFrame& f, int frames) {
    int count = 0;
    for (int i = 0; i < frames; ++i) {
        count += mapper.update(f, {}, kFrame).turnDegrees != 0.0f ? 1 : 0;
    }
    return count;
}

// Frames in which `action` was pressed (went down).
int presses(InputMapper& mapper, const InputFrame& f, int frames, GameAction action) {
    int count = 0;
    for (int i = 0; i < frames; ++i) {
        count += contains(mapper.update(f, {}, kFrame).pressed, action) ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_CASE("a turn-stick push during the rest's hold-over snaps exactly once, held out or back at once") {
    // Held out: held through the hold-over, then the game has it as a fresh push.
    InputMapper out(questTouchProfile(), restSettings(TurnMode::Snap, RestWheelMode::Full));
    feed(out, frame(true, false, kCentre, kCentre), 30);
    feed(out, frame(false, false, kCentre, kCentre), 3);
    CHECK(snaps(out, frame(false, false, kCentre, kRight), 8) == 0);
    CHECK(snaps(out, frame(false, false, kCentre, kRight), 22) == 1);

    // A one-frame flick back in the centre before the hold-over lapses: played once when it does.
    InputMapper flick(questTouchProfile(), restSettings(TurnMode::Snap, RestWheelMode::Full));
    feed(flick, frame(true, false, kCentre, kCentre), 30);
    feed(flick, frame(false, false, kCentre, kCentre), 3);
    int count = snaps(flick, frame(false, false, kCentre, kRight), 1);
    count += snaps(flick, frame(false, false, kCentre, kCentre), 30);
    CHECK(count == 1);
    CHECK(flick.restWheel().stats().landings[0] == 1);
}

TEST_CASE("while touched: turning just before the thumb leaves for its own stick turns, and opens no wheel") {
    InputMapper mapper(questTouchProfile(), restSettings(TurnMode::Smooth, RestWheelMode::Full));
    feed(mapper, frame(true, false, kCentre, kCentre), 30);
    Run run = feed(mapper, frame(true, false, kCentre, kRight), 2);
    // The left thumb from its rest to its own stick: the pick just started is cancelled, the turn goes on.
    run = feed(mapper, frame(false, false, kUp, kRight), 30, run);
    CHECK(run.turned > 0.0f);
    CHECK(run.wheelFrames == 0);
}

TEST_CASE("while touched: a flicker as the thumb leaves for its own stick does not make a turn a pick") {
    InputMapper mapper(questTouchProfile(), restSettings(TurnMode::Smooth, RestWheelMode::Full));
    feed(mapper, frame(true, false, kCentre, kCentre), 30);
    Run run = feed(mapper, frame(true, false, kCentre, kRight), 2);
    // The touch goes, flickers back for a frame, goes again, and the left thumb pushes its own stick: the
    // loss is timed from when the touch first went, so the push still counts as begun just before it.
    run = feed(mapper, frame(false, false, kCentre, kRight), 1, run);
    run = feed(mapper, frame(true, false, kCentre, kRight), 1, run);
    run = feed(mapper, frame(false, false, kCentre, kRight), 1, run);
    run = feed(mapper, frame(false, false, kUp, kRight), 30, run);
    CHECK(run.turned > 0.0f);
    CHECK(run.wheelFrames == 0);
}

TEST_CASE("a held push that slides round the rim plays where it is, not its peak") {
    // Right at full, then down at 0.99 when the hold-over lapses: down, so a quick switch and no snap.
    const Axis2 rimDown{0.0f, -0.99f};
    InputMapper mapper(questTouchProfile(), restSettings(TurnMode::Snap, RestWheelMode::Full));
    feed(mapper, frame(true, false, kCentre, kCentre), 30);
    feed(mapper, frame(false, false, kCentre, kCentre), 3);
    int quick = presses(mapper, frame(false, false, kCentre, kRight), 6, GameAction::QuickSwitch);
    quick += presses(mapper, frame(false, false, kCentre, rimDown), 10, GameAction::QuickSwitch);
    quick += presses(mapper, frame(false, false, kCentre, kCentre), 10, GameAction::QuickSwitch);
    CHECK(quick == 1);
}

TEST_CASE("a held push on its way back when the hold-over lapses still snaps, or quick-switches, once") {
    // Full out, then half way back on the frame the hold-over lapses (0.15 s after the loss), then centred.
    const Axis2 half{0.5f, 0.0f};
    InputMapper snap(questTouchProfile(), restSettings(TurnMode::Snap, RestWheelMode::Full));
    feed(snap, frame(true, false, kCentre, kCentre), 30);
    feed(snap, frame(false, false, kCentre, kCentre), 3);
    int count = snaps(snap, frame(false, false, kCentre, kRight), 6);
    count += snaps(snap, frame(false, false, kCentre, half), 10);
    count += snaps(snap, frame(false, false, kCentre, kCentre), 10);
    CHECK(count == 1);

    // A down flick at 0.5 (short of the down claim) when it lapses: one quick switch.
    const Axis2 halfDown{0.0f, -0.5f};
    InputMapper down(questTouchProfile(), restSettings(TurnMode::Snap, RestWheelMode::Full));
    feed(down, frame(true, false, kCentre, kCentre), 30);
    feed(down, frame(false, false, kCentre, kCentre), 3);
    int quick = presses(down, frame(false, false, kCentre, kDown), 6, GameAction::QuickSwitch);
    quick += presses(down, frame(false, false, kCentre, halfDown), 10, GameAction::QuickSwitch);
    quick += presses(down, frame(false, false, kCentre, kCentre), 10, GameAction::QuickSwitch);
    CHECK(quick == 1);
}

TEST_CASE(
    "while touched: a pick under way, then a jump with the other thumb: cancelled, the stick out of play") {
    // Right, then down: neither snaps nor quick-switches when the pick is cancelled, and the stick is the
    // game's again once it has been back in the centre.
    for (const Axis2 push : {kRight, kDown}) {
        CAPTURE(push.y);
        InputMapper mapper(questTouchProfile(), restSettings(TurnMode::Snap, RestWheelMode::Full));
        feed(mapper, frame(true, false, kCentre, kCentre), 30);
        // Picking for 0.05 s (the tick felt), then X pressed with the thumb off its rest.
        Run run = feed(mapper, frame(true, false, kCentre, push), 5);
        REQUIRE(run.last.restWheel.taken[1]);
        InputFrame jump = frame(false, false, kCentre, push);
        jump.left.primaryTouch = true;
        jump.left.primaryButton = true;
        run = feed(mapper, jump, 1, run);
        CHECK(run.last.restWheel.event == evr::input::RestWheelEvent::Cancelled);
        CHECK(run.last.restWheel.taken[1]);
        run = feed(mapper, frame(false, false, kCentre, push), 20, run);
        CHECK(run.last.restWheel.taken[1]);
        run = feed(mapper, frame(false, false, kCentre, kCentre), 5, run);
        CHECK(run.turned == 0.0f);
        CHECK_FALSE(contains(run.everDown, GameAction::QuickSwitch));
        CHECK(run.wheelFrames == 0);
        CHECK_FALSE(run.last.restWheel.taken[1]);
    }
}

TEST_CASE("a quick switch flicked during the rest's hold-over and back is played once") {
    InputMapper mapper(questTouchProfile(), restSettings(TurnMode::Snap, RestWheelMode::Full));
    feed(mapper, frame(true, false, kCentre, kCentre), 30);
    feed(mapper, frame(false, false, kCentre, kCentre), 3);
    Run run = feed(mapper, frame(false, false, kCentre, kDown), 2);
    run = feed(mapper, frame(false, false, kCentre, kCentre), 30, run);
    CHECK(contains(run.everDown, GameAction::QuickSwitch));
    CHECK_FALSE(contains(run.everDown, GameAction::WeaponWheel));
}

TEST_CASE("a thumb that lands straight from a jump leaves the move stick moving") {
    InputMapper mapper(questTouchProfile(), restSettings());
    feed(mapper, frame(false, false, kCentre, kCentre), 45);
    // A (jump) pressed and let go, then the right thumb on its rest at once, and walking within the
    // window.
    InputFrame jump = frame(false, false, kCentre, kCentre);
    jump.right.primaryButton = true;
    feed(mapper, jump, 10);
    feed(mapper, frame(false, true, kCentre, kCentre), 8);
    const Run run = feed(mapper, frame(false, true, kUp, kCentre), 30);
    CHECK(run.moved > 0.0f);
    CHECK(run.wheelFrames == 0);
    CHECK_FALSE(run.last.restWheel.taken[0]);
}

TEST_CASE("turning, then resting the thumb, then walking walks") {
    InputMapper mapper(questTouchProfile(), restSettings());
    feed(mapper, frame(false, false, kCentre, kRight), 20);
    feed(mapper, frame(false, false, kCentre, kCentre), 2);
    feed(mapper, frame(false, true, kCentre, kCentre), 8);
    const Run run = feed(mapper, frame(false, true, kUp, kCentre), 30);
    CHECK(run.moved > 0.0f);
    CHECK(run.wheelFrames == 0);
}

TEST_CASE("walking, then resting the thumb, then a snap turns") {
    InputMapper mapper(questTouchProfile(), restSettings(TurnMode::Snap));
    feed(mapper, frame(false, false, kUp, kCentre), 20);
    feed(mapper, frame(false, false, kCentre, kCentre), 2);
    feed(mapper, frame(true, false, kCentre, kCentre), 8);
    const Run run = feed(mapper, frame(true, false, kCentre, kRight), 30);
    CHECK(run.turned > 0.0f);
    CHECK(run.wheelFrames == 0);
}

TEST_CASE("a rest with the thumb idle for longer than the void time still opens the wheel") {
    InputMapper mapper(questTouchProfile(), restSettings());
    InputFrame jump = frame(false, false, kCentre, kCentre);
    jump.right.primaryButton = true;
    feed(mapper, jump, 10);
    // A let go, the thumb idle for half a second, then on its rest.
    feed(mapper, frame(false, false, kCentre, kCentre), 45);
    feed(mapper, frame(false, true, kCentre, kCentre), 8);
    const Run run = feed(mapper, frame(false, true, kUp, kCentre), 30);
    CHECK(run.moved == 0.0f);
    CHECK(contains(run.last.down, GameAction::WeaponWheel));
}

TEST_CASE("a cancelled flick does not reopen the window: the next flick turns") {
    InputMapper mapper(questTouchProfile(), restSettings(TurnMode::Snap));
    feed(mapper, frame(true, false, kCentre, kCentre), 8);
    // Shorter than the dwell: picking starts and is cancelled.
    Run run = feed(mapper, frame(true, false, kCentre, kRight), 4);
    run = feed(mapper, frame(true, false, kCentre, kCentre), 10, run);
    CHECK(run.turned == 0.0f);
    CHECK(run.last.restWheel.event == evr::input::RestWheelEvent::None);
    // The same flick again, the thumb still resting and well inside the first window: it snaps.
    run = feed(mapper, frame(true, false, kCentre, kRight), 4);
    CHECK(run.turned > 0.0f);
    CHECK(run.wheelFrames == 0);
}

TEST_CASE("the wheel is held for the game's open delay and its margin") {
    MapperContext slow;
    slow.restWheelHoldSeconds = evr::input::wheelHoldSeconds(500.0f);
    InputMapper mapper(questTouchProfile(), restSettings());
    feed(mapper, frame(true, false, kCentre, kCentre), 8, {}, slow);
    Run run = feed(mapper, frame(true, false, kCentre, kUp), 11, {}, slow);
    run = feed(mapper, frame(true, false, kCentre, kCentre), 90, run, slow);
    // From the press (after the dwell) to the release: 0.5 s + 0.12 s.
    CHECK(static_cast<float>(run.wheelFrames) * kFrame >= 0.62f - kFrame);
    CHECK(static_cast<float>(run.wheelFrames) * kFrame < 0.62f + 2.0f * kFrame);
}
