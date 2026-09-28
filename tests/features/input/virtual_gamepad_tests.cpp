#include "features/input/virtual_gamepad.hpp"

#include <doctest/doctest.h>

#include <limits>
#include <ostream>

using evr::game::add;
using evr::game::GameAction;
using evr::game::GameActionSet;
using evr::input::Axis2;
using evr::input::mergePads;
using evr::input::PadState;
using evr::input::padStateFor;
namespace pad = evr::input::pad_button;

namespace {

PadState padFor(GameAction action) {
    GameActionSet set;
    add(set, action);
    return padStateFor(set, {}, {});
}

} // namespace

TEST_CASE("actions press the game's default pad binds") {
    CHECK(padFor(GameAction::Jump).buttons == pad::kA);
    CHECK(padFor(GameAction::Dash).buttons == pad::kB);
    CHECK(padFor(GameAction::Chainsaw).buttons == pad::kX);
    CHECK(padFor(GameAction::FlameBelch).buttons == pad::kY);
    CHECK(padFor(GameAction::Equipment).buttons == pad::kLeftShoulder);
    CHECK(padFor(GameAction::QuickSwitch).buttons == pad::kRightShoulder);
    CHECK(padFor(GameAction::WeaponWheel).buttons == pad::kRightShoulder);
    CHECK(padFor(GameAction::Melee).buttons == pad::kRightThumb);
    CHECK(padFor(GameAction::Pause).buttons == pad::kStart);
    CHECK(padFor(GameAction::Dossier).buttons == pad::kBack);
    CHECK(padFor(GameAction::SwitchWeaponMod).buttons == pad::kDpadUp);
    CHECK(padFor(GameAction::MissionInfo).buttons == pad::kDpadDown);
    CHECK(padFor(GameAction::SwitchEquipment).buttons == pad::kDpadLeft);
    CHECK(padFor(GameAction::Crucible).buttons == pad::kDpadRight);
    CHECK(padFor(GameAction::Fire).rightTrigger == 255);
    CHECK(padFor(GameAction::Fire).buttons == 0);
    CHECK(padFor(GameAction::WeaponMod).leftTrigger == 255);
    // No pad bind: not available through the gamepad.
    CHECK(padFor(GameAction::WeaponSlot3) == PadState{});
    CHECK(padFor(GameAction::NextWeapon) == PadState{});
}

TEST_CASE("the move goes to the left stick and the look input to the right, inside the unit circle") {
    const PadState forward = padStateFor({}, Axis2{0.0f, 1.0f}, {});
    CHECK(forward.thumbLY == 32767);
    CHECK(forward.thumbLX == 0);
    const PadState diagonal = padStateFor({}, Axis2{1.0f, 1.0f}, Axis2{-1.0f, 0.0f});
    CHECK(diagonal.thumbLX == 23170);
    CHECK(diagonal.thumbLY == 23170);
    CHECK(diagonal.thumbRX == -32767);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(padStateFor({}, Axis2{nan, 0.5f}, {}) == PadState{});
}

TEST_CASE("a real pad keeps working: buttons ORed, triggers the larger, sticks added and clamped") {
    PadState real;
    real.buttons = pad::kA;
    real.leftTrigger = 100;
    real.rightTrigger = 200;
    real.thumbLY = 30000;
    real.thumbRX = -30000;
    PadState ours;
    ours.buttons = pad::kB;
    ours.leftTrigger = 255;
    ours.rightTrigger = 0;
    ours.thumbLY = 10000;
    ours.thumbRX = -10000;
    const PadState merged = mergePads(real, ours);
    CHECK(merged.buttons == (pad::kA | pad::kB));
    CHECK(merged.leftTrigger == 255);
    CHECK(merged.rightTrigger == 200);
    CHECK(merged.thumbLY == 32767);
    CHECK(merged.thumbRX == -32768);
    CHECK(mergePads(real, PadState{}) == real);
}
