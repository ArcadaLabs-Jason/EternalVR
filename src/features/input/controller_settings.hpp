#pragma once

// The motion-controller settings the layer reads at start-up (docs/VR_CONTROLLERS.md), from environment
// variables set by the launcher or the rig scripts. Parsing is here, apart from the environment, so every
// rule is tested; a value that cannot be used is reported and the default kept.
//
//   ETERNALVR_CONTROLLERS    1 / 0              controllers drive the game (on by default)
//   ETERNALVR_AIM            head / hand / view the view angles follow the head, the weapon hand, or
//                                               stay the game's own (view)
//   ETERNALVR_DEMON_AIM      head / hand        what aims a piloted demon (the Cultist Base Revenant): the
//                                               head or the weapon hand; unset, as ETERNALVR_AIM
//   ETERNALVR_LOCOMOTION     head / hand        "forward" on the move stick: the head or the off hand
//   ETERNALVR_TURN           smooth / snap / off
//   ETERNALVR_TURN_RATE      degrees per second for smooth turning (150 to 400)
//   ETERNALVR_SNAP_DEGREES   30, 45 or 90 (any value from 15 to 90 is accepted)
//   ETERNALVR_HANDEDNESS     right / left / left_mirror
//   ETERNALVR_DOSSIER        hold / tap         X hold opens the Dossier and a tap switches equipment
//                                               (hold), or the other way round (tap); dossier_press.hpp
//   ETERNALVR_MAP_STICKS     weapon / other     the stick that pans the Dossier's map: the weapon hand's
//                                               (weapon) or the other one (other); the second stick zooms
//                                               and rotates (map_sticks.hpp)
//   ETERNALVR_WHEEL_SELECT   stick / hand       what points at the weapon wheel: the stick that holds it
//                                               (stick), or the weapon hand (hand); wheel_hand.hpp
//   ETERNALVR_WHEEL_HAND_DEGREES  5 to 45       the hand's turn that reaches the wheel's rim (20)
//   ETERNALVR_THROW          1 / 0              wind up the off hand beside the head and throw: the equipment
//                                               launcher (off by default; arm_gestures.hpp)
//   ETERNALVR_THROW_SPEED    1 to 5             the throw's forward speed, metres per second (2)
//   ETERNALVR_SWING          1 / 0              raise the weapon hand above the head and swing it down: the
//                                               Crucible (off by default; arm_gestures.hpp)
//   ETERNALVR_SWING_SPEED    1 to 5             the swing's downward speed, metres per second (2.5)
//   ETERNALVR_HANDS_JUMP     1 / 0              throw both hands up above the head to jump (off by default,
//                                               never when seated; hands_jump.hpp)
//   ETERNALVR_XINPUT         auto / 1 / 0       the virtual gamepad: only when the user-command hooks
//                                               cannot be installed (auto), instead of them (1), never (0)
//   ETERNALVR_SHOT_ORIGIN    hand / eye         where shots start under hand aim
//   ETERNALVR_AIM_SMOOTHING  0 to 1             hand-aim smoothing: 0 off, 1 the strongest (0.3)
//   ETERNALVR_HAPTICS        0 to 1             controller vibration strength: 0 off (0.6;
//                                               haptics_policy.hpp)
//   ETERNALVR_BHAPTICS       1 / 0              bHaptics suits and sleeves through the bHaptics Player
//                                               (off by default; docs/BHAPTICS.md)
//   ETERNALVR_BHAPTICS_INTENSITY  0 to 1        the bHaptics effects' strength (1)
//   ETERNALVR_VIEWMODEL      1 / 0              the game's weapon and arms at the controller
//   ETERNALVR_BUTTON_PROMPTS 1 / 0              the game's prompts name the VR buttons, not keys
//   ETERNALVR_WEAPON_FOV     1 / 0              the weapon drawn with the headset's FOV
//   ETERNALVR_VIEWMODEL_OFFSET  f,l,u[,pitch,yaw,roll]  one offset for every weapon (tuning)
//   ETERNALVR_SEATED         1 / 0              the seated viewmodel offsets (T-074)
//   ETERNALVR_CONTROLLER_DATA   path            a player's controller data file, or a folder of them (every
//                                               *.toml in it), instead of the built-in
//                                               (player_controller_data.hpp)
//   ETERNALVR_TEST_INPUT        path            scripted controller input for rig tests (test_input.hpp)
//   ETERNALVR_CONTROLLERS_TRACE 1 / 0           log the player's eye, view angles and the hand 4 times a
//   second
//   ETERNALVR_OFFHAND        game / free / probe   who drives the game's left arm (offhand_policy.hpp)
//   ETERNALVR_OFFHAND_OFFSET f,l,u[,pitch,yaw,roll]  the wrist (LeftHand joint) from the off-hand grip, in
//                            the grip's forward/left/up (metres, degrees; default -0.08,0.035,0)
//   ETERNALVR_OFFHAND_SHOULDER head / model / f,l,u  where the arm's IK starts: a point fixed to the head
//                            (default, f,l,u from the head in its yaw frame: -0.08,0.18,-0.24) or the
//                            game's animated shoulder in the arms model
//   ETERNALVR_OFFHAND_ELBOW  f,l,u              the elbow's bend direction in the head's yaw frame
//                            (default -0.2,0.6,-1: down, out, a little back)
//   ETERNALVR_OFFHAND_PROBE  f,l,u              probe mode: added to the game's left-hand modifier (metres)
//   ETERNALVR_OFFHAND_BLEND  seconds            hand-over blend (0 to 1; default 0.15)
//   ETERNALVR_OFFHAND_TRACE  1 / 0              log the left-arm signals when they change

#include "features/input/aim_smoothing.hpp"
#include "features/input/arm_gestures.hpp"
#include "features/input/dossier_press.hpp"
#include "features/input/hands_jump.hpp"
#include "features/input/haptics_policy.hpp"
#include "features/input/locomotion_direction.hpp"
#include "features/input/map_sticks.hpp"
#include "features/input/offhand_policy.hpp"
#include "features/input/turn_policy.hpp"
#include "features/input/wheel_hand.hpp"
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

// Where the off-hand arm's IK starts (docs/VR_HANDS_HUD.md, "Off hand").
enum class ShoulderAnchor : std::uint8_t {
    Head,  // a point fixed to the tracked head (offhandShoulderOffset)
    Model, // the game's animated shoulder, which moves with the arms model at the weapon hand
};

const char* shoulderAnchorName(ShoulderAnchor anchor);

// The off-hand arm's offsets (wrist, shoulder, elbow) are given for the left hand, with the weapon in the
// right. With the weapon in the left hand the off hand is the right one, so they are mirrored left to
// right: the left distance, the yaw and the roll change sign. The arms model is not mirrored: its left
// arm reaches for the right controller with the thumb up and the fingers along the grip, the palm out.
game::WeaponOffset offhandOffsetFor(const game::WeaponOffset& offset, game::Handedness handedness);

// The shoulder anchor for the handedness. The game's animated shoulder is the arms model's left one,
// beside the weapon when the weapon is in the left hand, so the head's point is used there instead.
ShoulderAnchor shoulderAnchorFor(ShoulderAnchor anchor, game::Handedness handedness);

// Defaults of the off-hand arm (metres, degrees; the grip's or the head's forward, left, up).
inline constexpr game::WeaponOffset kDefaultOffhandOffset{-0.08f, 0.035f, 0.0f, 0.0f, 0.0f, 0.0f};
inline constexpr game::WeaponOffset kDefaultOffhandShoulder{-0.08f, 0.18f, -0.24f, 0.0f, 0.0f, 0.0f};
inline constexpr game::WeaponOffset kDefaultOffhandElbow{-0.2f, 0.6f, -1.0f, 0.0f, 0.0f, 0.0f};

struct ControllerSettings {
    bool enabled = true;
    AimSource aim = AimSource::Head;
    std::optional<AimSource> demonAim; // head or hand; nullopt follows `aim` (demonAimSource)
    LocomotionFrame locomotion = LocomotionFrame::Head;
    TurnSettings turn;
    game::Handedness handedness = game::Handedness::Right;
    DossierPress dossier = DossierPress::Hold;
    MapSticks mapSticks = MapSticks::WeaponPans; // map_sticks.hpp
    WheelSelect wheelSelect = WheelSelect::Stick;
    float wheelHandDegrees = kDefaultWheelHandDegrees;
    ThrowSettings throwGesture; // arm_gestures.hpp
    SwingSettings swing;
    HandsJumpSettings handsJump; // hands_jump.hpp
    InputPath path = InputPath::Auto;
    ShotOrigin shotOrigin = ShotOrigin::Hand;
    float aimSmoothing = kDefaultAimSmoothing; // aim_smoothing.hpp
    float haptics = kDefaultHapticStrength;    // haptics_policy.hpp
    bool bhaptics = false;                     // docs/BHAPTICS.md
    float bhapticsIntensity = 1.0f;
    bool viewmodel = true;
    bool buttonPrompts = true;
    bool weaponFov = true;
    bool seated = false;
    std::optional<game::WeaponOffset> viewmodelOffset;
    std::string controllerDataPath;
    std::string testInputPath;
    bool trace = false;
    // The off hand and the game's left arm (docs/VR_HANDS_HUD.md).
    OffhandMode offhand = OffhandMode::Game;
    game::WeaponOffset offhandOffset = kDefaultOffhandOffset;
    ShoulderAnchor offhandShoulder = ShoulderAnchor::Head;
    game::WeaponOffset offhandShoulderOffset = kDefaultOffhandShoulder;
    game::WeaponOffset offhandElbow = kDefaultOffhandElbow;
    game::WeaponOffset offhandProbe{0.10f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float offhandBlendSeconds = 0.15f;
    float offhandHoldSeconds = 0.25f;
    bool offhandTrace = false;
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

// What aims a piloted demon: ETERNALVR_DEMON_AIM, or the main aim when it is unset. Under view aim nothing
// is written for the Slayer or the demon, so the demon keeps the game's own aim whatever ETERNALVR_DEMON_AIM
// says.
AimSource demonAimSource(const ControllerSettings& settings);
const char* inputPathName(InputPath path);

} // namespace evr::input
