#pragma once

// The motion-controller settings the layer reads at start-up (docs/VR_CONTROLLERS.md), from environment
// variables set by the launcher or the rig scripts. Parsing is here, apart from the environment, so every
// rule is tested; a value that cannot be used is reported and the default kept.
//
//   ETERNALVR_CONTROLLERS    1 / 0              controllers drive the game (on by default)
//   ETERNALVR_AIM            head / hand / view the view angles follow the head, the weapon hand, or
//                                               stay the game's own (view)
//   ETERNALVR_LOCOMOTION     head / hand        "forward" on the move stick: the head or the off hand
//   ETERNALVR_TURN           smooth / snap / off
//   ETERNALVR_TURN_RATE      degrees per second for smooth turning (150 to 400)
//   ETERNALVR_SNAP_DEGREES   30, 45 or 90 (any value from 15 to 90 is accepted)
//   ETERNALVR_HANDEDNESS     right / left / left_mirror
//   ETERNALVR_DOSSIER        hold / tap         X hold opens the Dossier and a tap switches equipment
//                                               (hold), or the other way round (tap); dossier_press.hpp
//   ETERNALVR_XINPUT         auto / 1 / 0       the virtual gamepad: only when the user-command hooks
//                                               cannot be installed (auto), instead of them (1), never (0)
//   ETERNALVR_SHOT_ORIGIN    hand / eye         where shots start under hand aim
//   ETERNALVR_AIM_SMOOTHING  0 to 1             hand-aim smoothing: 0 off, 1 the strongest (0.3)
//   ETERNALVR_VIEWMODEL      1 / 0              the game's weapon and arms at the controller
//   ETERNALVR_WEAPON_FOV     1 / 0              the weapon drawn with the headset's FOV
//   ETERNALVR_VIEWMODEL_OFFSET  f,l,u[,pitch,yaw,roll]  one offset for every weapon (tuning)
//   ETERNALVR_SEATED         1 / 0              the seated viewmodel offsets (T-074)
//   ETERNALVR_CONTROLLER_DATA   path            a player's controller data file instead of the built-in
//   ETERNALVR_TEST_INPUT        path            scripted controller input for rig tests (test_input.hpp)
//   ETERNALVR_CONTROLLERS_TRACE 1 / 0           log the player's eye, view angles and the hand 4 times a
//   second

#include "features/input/aim_smoothing.hpp"
#include "features/input/dossier_press.hpp"
#include "features/input/locomotion_direction.hpp"
#include "features/input/turn_policy.hpp"
#include "game/eternal/quest_touch_bindings.hpp"
#include "game/eternal/weapon_offsets.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::input {

enum class AimSource : std::uint8_t {
    Head,
    Hand,
    View, // the game's own aim; nothing is written
};

enum class InputPath : std::uint8_t {
    Auto,    // the user-command hooks; the virtual gamepad only if they cannot be installed
    UserCmd, // the user-command hooks only
    XInput,  // the virtual gamepad only
};

enum class ShotOrigin : std::uint8_t {
    Hand, // shots start at the tracked muzzle
    Eye,  // shots start where the game starts them; only their direction follows the hand
};

struct ControllerSettings {
    bool enabled = true;
    AimSource aim = AimSource::Head;
    LocomotionFrame locomotion = LocomotionFrame::Head;
    TurnSettings turn;
    game::Handedness handedness = game::Handedness::Right;
    DossierPress dossier = DossierPress::Hold;
    InputPath path = InputPath::Auto;
    ShotOrigin shotOrigin = ShotOrigin::Hand;
    float aimSmoothing = kDefaultAimSmoothing; // aim_smoothing.hpp
    bool viewmodel = true;
    bool weaponFov = true;
    bool seated = false;
    std::optional<game::WeaponOffset> viewmodelOffset;
    std::string controllerDataPath;
    std::string testInputPath;
    bool trace = false;
};

struct SettingsIssue {
    std::string name;  // the variable
    std::string value; // as given
    std::string message;
};

struct ControllerSettingsResult {
    ControllerSettings settings;
    std::vector<SettingsIssue> issues;
};

// `lookup` returns a variable's value, or nullopt when it is not set. Empty values count as not set.
using SettingLookup = std::function<std::optional<std::string>(std::string_view name)>;

ControllerSettingsResult parseControllerSettings(const SettingLookup& lookup);

const char* aimSourceName(AimSource aim);
const char* inputPathName(InputPath path);

} // namespace evr::input
