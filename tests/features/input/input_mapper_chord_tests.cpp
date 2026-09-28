// The capture chord and the Menu button's pause through the whole mapper (capture_chord.hpp): left Menu, or
// under SteamVR Y, held + a trigger captures without pausing, recentering or firing.

#include "features/input/input_mapper.hpp"

#include "features/input/dashboard_pause.hpp"
#include "features/input/input_frames.hpp"
#include "support/quest_touch.hpp"

#include <doctest/doctest.h>

#include <ostream>

using evr::game::contains;
using evr::game::GameAction;
using evr::game::Handedness;
using evr::input::BindingProfile;
using evr::input::ButtonInput;
using evr::input::GameInput;
using evr::input::Hand;
using evr::input::HandState;
using evr::input::InputFrame;
using evr::input::InputMapper;
using evr::input::MapperSettings;
using evr::input::PressKind;
using evr::test::questTouchProfile;
using evr::test::restingFrame;

namespace {

constexpr float kFrame = 1.0f / 90.0f;

HandState& handOf(InputFrame& frame, Hand hand) {
    return hand == Hand::Left ? frame.left : frame.right;
}

} // namespace

namespace {

struct ChordResult {
    int pauses = 0;
    int captures = 0;
    bool recenter = false;
    bool recenterAfterCapture = false; // the recenter binding still down after the capture
    bool fired = false;                // any trigger binding (fire, equipment) went down
};

// The built-in map with the recenter put back on the Menu button's hold, as a player's own map can.
BindingProfile menuHoldRecenterProfile() {
    BindingProfile profile = questTouchProfile();
    profile.buttons.push_back({Hand::Left, ButtonInput::Menu, PressKind::Hold, GameAction::Recenter});
    return profile;
}

// Left Menu held for `menuSeconds`, a trigger pulled on `triggerHand` from `pullAt` for 0.1 s (never when
// `pullAt` is negative), then 0.5 s released.
ChordResult runChord(const BindingProfile& profile, float menuSeconds, float pullAt, Hand triggerHand) {
    InputMapper mapper(profile);
    ChordResult result;
    const int menuFrames = static_cast<int>(menuSeconds / kFrame);
    const int pullFrom = pullAt < 0.0f ? -1000 : static_cast<int>(pullAt / kFrame);
    const int pullTo = pullFrom + static_cast<int>(0.1f / kFrame);
    for (int i = 0; i < menuFrames + static_cast<int>(0.5f / kFrame); ++i) {
        InputFrame frame = restingFrame();
        frame.left.menuButton = i < menuFrames;
        if (i >= pullFrom && i < pullTo) {
            handOf(frame, triggerHand).trigger = 1.0f;
        }
        const GameInput input = mapper.update(frame, {}, kFrame);
        result.pauses += contains(input.pressed, GameAction::Pause) ? 1 : 0;
        result.captures += input.capture ? 1 : 0;
        result.recenter = result.recenter || contains(input.down, GameAction::Recenter);
        result.recenterAfterCapture = result.recenterAfterCapture ||
                                      (result.captures > 0 && contains(input.down, GameAction::Recenter));
        result.fired = result.fired || contains(input.down, GameAction::Fire) ||
                       contains(input.down, GameAction::Equipment);
    }
    return result;
}

} // namespace

TEST_CASE("left Menu + a trigger captures, without pausing, recentering or firing, in every handedness") {
    for (const Handedness handedness :
         {Handedness::Right, Handedness::LeftButtonSwap, Handedness::LeftButtonAndStickSwap}) {
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            CAPTURE(static_cast<int>(handedness));
            CAPTURE(static_cast<int>(hand));
            const ChordResult quick = runChord(questTouchProfile(handedness), 0.4f, 0.1f, hand);
            CHECK(quick.captures == 1);
            CHECK(quick.pauses == 0);
            CHECK_FALSE(quick.fired);
            const ChordResult held = runChord(questTouchProfile(handedness), 1.5f, 0.1f, hand);
            CHECK(held.captures == 1);
            CHECK_FALSE(held.recenter);
            CHECK(held.pauses == 0);
            CHECK_FALSE(held.fired);
        }
    }
}

TEST_CASE("a capture late in a long Menu hold still cancels a Menu-hold recenter before it completes") {
    const ChordResult r = runChord(menuHoldRecenterProfile(), 1.5f, 0.8f, Hand::Right);
    CHECK(r.recenter); // the binding was down from 0.25 s: the recenter's 1 s had started
    CHECK_FALSE(r.recenterAfterCapture);
    CHECK(r.captures == 1);
    CHECK(r.pauses == 0);
    CHECK_FALSE(r.fired);
}

TEST_CASE("left Menu pauses on release and no longer recenters in the built-in maps") {
    const ChordResult tap = runChord(questTouchProfile(), 0.2f, -1.0f, Hand::Right);
    CHECK(tap.pauses == 1);
    CHECK(tap.captures == 0);
    const ChordResult hold = runChord(questTouchProfile(), 1.5f, -1.0f, Hand::Right);
    CHECK_FALSE(hold.recenter);
    const ChordResult own = runChord(menuHoldRecenterProfile(), 1.5f, -1.0f, Hand::Right); // own map
    CHECK(own.recenter);
    CHECK(own.pauses == 0);
}

TEST_CASE("the pause still goes out on the Menu release frame, with no delay") {
    InputMapper mapper(questTouchProfile());
    InputFrame down = restingFrame();
    down.left.menuButton = true;
    mapper.update(down, {}, kFrame);
    CHECK(contains(mapper.update(restingFrame(), {}, kFrame).pressed, GameAction::Pause));
}

namespace {

struct YChordResult {
    int pauses = 0;
    int captures = 0;
    int modSwitches = 0;
    bool fired = false;
};

// SteamVR: the built-in map with the Y hold turned into a pause, the capture chord on Menu or Y. Y held for
// `ySeconds`, the right trigger pulled from `pullAt` for 0.1 s (never when negative), then 0.5 s released.
YChordResult runYChord(float ySeconds, float pullAt) {
    BindingProfile profile = questTouchProfile();
    evr::input::applyDashboardPause(profile);
    MapperSettings settings;
    settings.captureButtons = evr::input::CaptureButtons::MenuOrSecondary;
    InputMapper mapper(profile, settings);
    YChordResult result;
    const int yFrames = static_cast<int>(ySeconds / kFrame);
    const int pullFrom = pullAt < 0.0f ? -1000 : static_cast<int>(pullAt / kFrame);
    const int pullTo = pullFrom + static_cast<int>(0.1f / kFrame);
    for (int i = 0; i < yFrames + static_cast<int>(0.5f / kFrame); ++i) {
        InputFrame frame = restingFrame();
        frame.left.secondaryButton = i < yFrames;
        if (i >= pullFrom && i < pullTo) {
            frame.right.trigger = 1.0f;
        }
        const GameInput input = mapper.update(frame, {}, kFrame);
        result.pauses += contains(input.pressed, GameAction::Pause) ? 1 : 0;
        result.captures += input.capture ? 1 : 0;
        result.modSwitches += contains(input.pressed, GameAction::SwitchWeaponMod) ? 1 : 0;
        result.fired = result.fired || contains(input.down, GameAction::Fire);
    }
    return result;
}

} // namespace

TEST_CASE("SteamVR: Y + a trigger captures without switching the mod, pausing or firing") {
    const YChordResult quick = runYChord(0.15f, 0.05f);
    CHECK(quick.captures == 1);
    CHECK(quick.modSwitches == 0);
    CHECK(quick.pauses == 0);
    CHECK_FALSE(quick.fired);
    const YChordResult held = runYChord(1.0f, 0.1f); // held past the pause hold after the pull
    CHECK(held.captures == 1);
    CHECK(held.pauses == 0);
    CHECK_FALSE(held.fired);
}

TEST_CASE("SteamVR: Y alone still switches the mod on a tap and pauses on a hold") {
    const YChordResult tap = runYChord(0.1f, -1.0f);
    CHECK(tap.modSwitches == 1);
    CHECK(tap.pauses == 0);
    CHECK(tap.captures == 0);
    const YChordResult hold = runYChord(0.6f, -1.0f);
    CHECK(hold.pauses == 1);
    CHECK(hold.modSwitches == 0);
    CHECK(hold.captures == 0);
}
