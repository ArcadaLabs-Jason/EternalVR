#include "features/input/controller_settings.hpp"

#include <doctest/doctest.h>

#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

using evr::game::Handedness;
using evr::input::AimSource;
using evr::input::ControllerSettingsResult;
using evr::input::InputPath;
using evr::input::LocomotionFrame;
using evr::input::parseControllerSettings;
using evr::input::ShotOrigin;
using evr::input::TurnMode;

namespace {

ControllerSettingsResult parse(const std::map<std::string, std::string, std::less<>>& values) {
    return parseControllerSettings([&values](std::string_view name) -> std::optional<std::string> {
        const auto it = values.find(name);
        if (it == values.end()) {
            return std::nullopt;
        }
        return it->second;
    });
}

} // namespace

TEST_CASE("defaults: on, head aim and head locomotion, smooth turning, right-handed") {
    const auto result = parse({});
    CHECK(result.issues.empty());
    const auto& s = result.settings;
    CHECK(s.enabled);
    CHECK(s.aim == AimSource::Head);
    CHECK(s.locomotion == LocomotionFrame::Head);
    CHECK(s.turn.mode == TurnMode::Smooth);
    CHECK(s.turn.smoothDegreesPerSecond == 230.0f);
    CHECK(s.turn.snapDegrees == 45.0f);
    CHECK(s.handedness == Handedness::Right);
    CHECK(s.dossier == evr::input::DossierPress::Hold);
    CHECK(s.mapSticks == evr::input::MapSticks::WeaponPans);
    CHECK(s.path == InputPath::Auto);
    CHECK(s.shotOrigin == ShotOrigin::Hand);
    CHECK(s.viewmodel);
    CHECK(s.weaponFov);
    CHECK_FALSE(s.seated);
    CHECK_FALSE(s.viewmodelOffset.has_value());
    CHECK(s.controllerDataPath.empty());
    CHECK(s.testInputPath.empty());
}

TEST_CASE("locomotion: look, left or right, and the values from before them") {
    CHECK(parse({{"ETERNALVR_LOCOMOTION", "look"}}).settings.locomotion == LocomotionFrame::Head);
    CHECK(parse({{"ETERNALVR_LOCOMOTION", "left"}}).settings.locomotion == LocomotionFrame::LeftHand);
    CHECK(parse({{"ETERNALVR_LOCOMOTION", "right"}}).settings.locomotion == LocomotionFrame::RightHand);
    // head is look; hand is the hand with the move stick, as before.
    CHECK(parse({{"ETERNALVR_LOCOMOTION", "head"}}).settings.locomotion == LocomotionFrame::Head);
    CHECK(parse({{"ETERNALVR_LOCOMOTION", "hand"}}).settings.locomotion == LocomotionFrame::MoveHand);
    for (const char* value : {"look", "left", "right", "head", "hand"}) {
        CAPTURE(value);
        CHECK(parse({{"ETERNALVR_LOCOMOTION", value}}).issues.empty());
    }
    const auto bad = parse({{"ETERNALVR_LOCOMOTION", "toward"}});
    CHECK(bad.issues.size() == 1);
    CHECK(bad.settings.locomotion == LocomotionFrame::Head);
}

TEST_CASE("every setting reads its documented values") {
    const auto result = parse({{"ETERNALVR_CONTROLLERS", "0"},
                               {"ETERNALVR_AIM", "hand"},
                               {"ETERNALVR_LOCOMOTION", "right"},
                               {"ETERNALVR_TURN", "snap"},
                               {"ETERNALVR_TURN_RATE", "300"},
                               {"ETERNALVR_SNAP_DEGREES", "30"},
                               {"ETERNALVR_HANDEDNESS", "left_mirror"},
                               {"ETERNALVR_DOSSIER", "tap"},
                               {"ETERNALVR_MAP_STICKS", "other"},
                               {"ETERNALVR_XINPUT", "1"},
                               {"ETERNALVR_SHOT_ORIGIN", "eye"},
                               {"ETERNALVR_AIM_SMOOTHING", "0.6"},
                               {"ETERNALVR_VIEWMODEL", "0"},
                               {"ETERNALVR_WEAPON_FOV", "off"},
                               {"ETERNALVR_SEATED", "yes"},
                               {"ETERNALVR_VIEWMODEL_OFFSET", "-0.2,0.1,0.15"},
                               {"ETERNALVR_CONTROLLER_DATA", "E:\\data\\My Touch.toml"},
                               {"ETERNALVR_TEST_INPUT", "E:\\runs\\input.txt"}});
    CHECK(result.issues.empty());
    const auto& s = result.settings;
    CHECK_FALSE(s.enabled);
    CHECK(s.aim == AimSource::Hand);
    CHECK(s.locomotion == LocomotionFrame::RightHand);
    CHECK(s.turn.mode == TurnMode::Snap);
    CHECK(s.turn.smoothDegreesPerSecond == 300.0f);
    CHECK(s.turn.snapDegrees == 30.0f);
    CHECK(s.handedness == Handedness::LeftButtonAndStickSwap);
    CHECK(s.dossier == evr::input::DossierPress::Tap);
    CHECK(s.mapSticks == evr::input::MapSticks::OtherPans);
    CHECK(s.path == InputPath::XInput);
    CHECK(s.shotOrigin == ShotOrigin::Eye);
    CHECK(s.aimSmoothing == 0.6f);
    CHECK_FALSE(s.viewmodel);
    CHECK_FALSE(s.weaponFov);
    CHECK(s.seated);
    REQUIRE(s.viewmodelOffset.has_value());
    CHECK(s.viewmodelOffset->forward == -0.2f);
    // Paths keep their case and spaces.
    CHECK(s.controllerDataPath == "E:\\data\\My Touch.toml");
    CHECK(s.testInputPath == "E:\\runs\\input.txt");
}

TEST_CASE("values are read without regard to case or surrounding blanks") {
    const auto result =
        parse({{"ETERNALVR_AIM", " Hand "}, {"ETERNALVR_TURN", "SNAP"}, {"ETERNALVR_XINPUT", "0"}});
    CHECK(result.issues.empty());
    CHECK(result.settings.aim == AimSource::Hand);
    CHECK(result.settings.turn.mode == TurnMode::Snap);
    CHECK(result.settings.path == InputPath::UserCmd);
}

TEST_CASE("the snap angles offered are 30, 45 and 90; the rate range is 150 to 400") {
    for (const char* angle : {"30", "45", "90"}) {
        const auto result = parse({{"ETERNALVR_SNAP_DEGREES", angle}});
        CHECK(result.issues.empty());
    }
    CHECK(parse({{"ETERNALVR_SNAP_DEGREES", "120"}}).issues.size() == 1);
    CHECK(parse({{"ETERNALVR_SNAP_DEGREES", "120"}}).settings.turn.snapDegrees == 45.0f);
    CHECK(parse({{"ETERNALVR_TURN_RATE", "100"}}).settings.turn.smoothDegreesPerSecond == 230.0f);
    CHECK(parse({{"ETERNALVR_TURN_RATE", "fast"}}).issues.size() == 1);
}

TEST_CASE("aim smoothing is light by default, 0 turns it off, and it is kept within 0 to 1") {
    CHECK(parse({}).settings.aimSmoothing == evr::input::kDefaultAimSmoothing);
    CHECK(parse({{"ETERNALVR_AIM_SMOOTHING", "0"}}).settings.aimSmoothing == 0.0f);
    CHECK(parse({{"ETERNALVR_AIM_SMOOTHING", "1"}}).settings.aimSmoothing == 1.0f);
    const auto over = parse({{"ETERNALVR_AIM_SMOOTHING", "1.5"}});
    CHECK(over.issues.size() == 1);
    CHECK(over.settings.aimSmoothing == evr::input::kDefaultAimSmoothing);
    CHECK(parse({{"ETERNALVR_AIM_SMOOTHING", "strong"}}).issues.size() == 1);
}

TEST_CASE("vibration strength is 0 to 1, 0.6 by default") {
    CHECK(parse({}).settings.haptics == evr::input::kDefaultHapticStrength);
    CHECK(parse({{"ETERNALVR_HAPTICS", "0"}}).settings.haptics == 0.0f);
    CHECK(parse({{"ETERNALVR_HAPTICS", "0.35"}}).settings.haptics == doctest::Approx(0.35f));
    const auto over = parse({{"ETERNALVR_HAPTICS", "2"}});
    CHECK(over.issues.size() == 1);
    CHECK(over.settings.haptics == evr::input::kDefaultHapticStrength);
    CHECK(parse({{"ETERNALVR_HAPTICS", "on"}}).issues.size() == 1);
}

TEST_CASE("bHaptics is off by default, at full intensity, and the intensity is kept within 0 to 1") {
    CHECK_FALSE(parse({}).settings.bhaptics);
    CHECK(parse({}).settings.bhapticsIntensity == 1.0f);
    CHECK(parse({{"ETERNALVR_BHAPTICS", "1"}}).settings.bhaptics);
    CHECK(parse({{"ETERNALVR_BHAPTICS_INTENSITY", "0.4"}}).settings.bhapticsIntensity ==
          doctest::Approx(0.4f));
    const auto over = parse({{"ETERNALVR_BHAPTICS_INTENSITY", "3"}});
    CHECK(over.issues.size() == 1);
    CHECK(over.settings.bhapticsIntensity == 1.0f);
    CHECK(parse({{"ETERNALVR_BHAPTICS", "vest"}}).issues.size() == 1);
}

TEST_CASE("an unusable value is reported by name and value, and the default kept") {
    const auto result = parse({{"ETERNALVR_AIM", "feet"}, {"ETERNALVR_CONTROLLERS", "maybe"}});
    REQUIRE(result.issues.size() == 2);
    CHECK(result.settings.aim == AimSource::Head);
    CHECK(result.settings.enabled);
    bool sawAim = false;
    for (const auto& issue : result.issues) {
        if (issue.name == "ETERNALVR_AIM") {
            sawAim = true;
            CHECK(issue.value == "feet");
            CHECK(issue.message.find("head, hand, view") != std::string::npos);
        }
    }
    CHECK(sawAim);
}

TEST_CASE("the demon's aim follows the main aim unless ETERNALVR_DEMON_AIM sets it") {
    using evr::input::demonAimSource;
    // Unset or empty: as ETERNALVR_AIM, whatever it is.
    for (const char* aim : {"head", "hand", "view"}) {
        const auto result = parse({{"ETERNALVR_AIM", aim}});
        CHECK_FALSE(result.settings.demonAim.has_value());
        CHECK(demonAimSource(result.settings) == result.settings.aim);
        CHECK(demonAimSource(parse({{"ETERNALVR_AIM", aim}, {"ETERNALVR_DEMON_AIM", " "}}).settings) ==
              result.settings.aim);
    }
    // Set: head or hand, independent of the main aim.
    const auto head = parse({{"ETERNALVR_AIM", "hand"}, {"ETERNALVR_DEMON_AIM", "head"}});
    CHECK(head.issues.empty());
    CHECK(head.settings.aim == AimSource::Hand);
    CHECK(demonAimSource(head.settings) == AimSource::Head);
    const auto hand = parse({{"ETERNALVR_AIM", "head"}, {"ETERNALVR_DEMON_AIM", "Hand"}});
    CHECK(hand.issues.empty());
    CHECK(hand.settings.aim == AimSource::Head);
    CHECK(demonAimSource(hand.settings) == AimSource::Hand);
    CHECK(demonAimSource(parse({{"ETERNALVR_DEMON_AIM", "hand"}}).settings) == AimSource::Hand);
    // Under view aim nothing is aimed: the demon keeps the game's aim.
    CHECK(demonAimSource(parse({{"ETERNALVR_AIM", "view"}, {"ETERNALVR_DEMON_AIM", "hand"}}).settings) ==
          AimSource::View);
    // View is not a demon aim; an unknown value is reported and the demon follows the main aim.
    for (const char* bad : {"view", "feet"}) {
        const auto result = parse({{"ETERNALVR_AIM", "hand"}, {"ETERNALVR_DEMON_AIM", bad}});
        REQUIRE(result.issues.size() == 1);
        CHECK(result.issues[0].name == "ETERNALVR_DEMON_AIM");
        CHECK(result.issues[0].message.find("head, hand") != std::string::npos);
        CHECK_FALSE(result.settings.demonAim.has_value());
        CHECK(demonAimSource(result.settings) == AimSource::Hand);
    }
}

TEST_CASE("a bad viewmodel offset is reported and the table used") {
    const auto result = parse({{"ETERNALVR_VIEWMODEL_OFFSET", "1,2"}});
    CHECK(result.issues.size() == 1);
    CHECK_FALSE(result.settings.viewmodelOffset.has_value());
}

TEST_CASE("empty values count as unset") {
    const auto result = parse({{"ETERNALVR_AIM", ""}, {"ETERNALVR_CONTROLLERS", "  "}});
    CHECK(result.issues.empty());
    CHECK(result.settings.aim == AimSource::Head);
}

TEST_CASE("the Dossier press is hold or tap; anything else keeps hold") {
    CHECK(parse({{"ETERNALVR_DOSSIER", "Hold"}}).settings.dossier == evr::input::DossierPress::Hold);
    const auto bad = parse({{"ETERNALVR_DOSSIER", "double"}});
    CHECK(bad.issues.size() == 1);
    CHECK(bad.settings.dossier == evr::input::DossierPress::Hold);
}

TEST_CASE("the Dossier map's sticks are weapon or other; anything else keeps weapon") {
    using evr::input::MapSticks;
    CHECK(parse({{"ETERNALVR_MAP_STICKS", " Weapon "}}).settings.mapSticks == MapSticks::WeaponPans);
    CHECK(parse({{"ETERNALVR_MAP_STICKS", "OTHER"}}).settings.mapSticks == MapSticks::OtherPans);
    const auto bad = parse({{"ETERNALVR_MAP_STICKS", "left"}});
    CHECK(bad.issues.size() == 1);
    CHECK(bad.settings.mapSticks == MapSticks::WeaponPans);
}

TEST_CASE("the weapon wheel is pointed at with the stick unless hand is chosen") {
    using evr::input::WheelSelect;
    CHECK(parse({}).settings.wheelSelect == WheelSelect::Stick);
    CHECK(parse({}).settings.wheelHandDegrees == evr::input::kDefaultWheelHandDegrees);
    const auto hand = parse({{"ETERNALVR_WHEEL_SELECT", " Hand "}, {"ETERNALVR_WHEEL_HAND_DEGREES", "15"}});
    CHECK(hand.issues.empty());
    CHECK(hand.settings.wheelSelect == WheelSelect::Hand);
    CHECK(hand.settings.wheelHandDegrees == 15.0f);
    CHECK(parse({{"ETERNALVR_WHEEL_SELECT", "stick"}}).settings.wheelSelect == WheelSelect::Stick);
    const auto bad = parse({{"ETERNALVR_WHEEL_SELECT", "head"}, {"ETERNALVR_WHEEL_HAND_DEGREES", "90"}});
    CHECK(bad.issues.size() == 2);
    CHECK(bad.settings.wheelSelect == WheelSelect::Stick);
    CHECK(bad.settings.wheelHandDegrees == evr::input::kDefaultWheelHandDegrees);
}

TEST_CASE("the throw and the overhead swing are off unless turned on") {
    const auto defaults = parse({});
    CHECK_FALSE(defaults.settings.throwGesture.enabled);
    CHECK_FALSE(defaults.settings.swing.enabled);
    CHECK(defaults.settings.throwGesture.speed == evr::input::ThrowSettings{}.speed);
    CHECK(defaults.settings.swing.speed == evr::input::SwingSettings{}.speed);
    const auto on = parse({{"ETERNALVR_THROW", "1"},
                           {"ETERNALVR_THROW_SPEED", "2.4"},
                           {"ETERNALVR_SWING", "on"},
                           {"ETERNALVR_SWING_SPEED", "3"}});
    CHECK(on.issues.empty());
    CHECK(on.settings.throwGesture.enabled);
    CHECK(on.settings.throwGesture.speed == doctest::Approx(2.4f));
    CHECK(on.settings.swing.enabled);
    CHECK(on.settings.swing.speed == 3.0f);
    const auto bad = parse(
        {{"ETERNALVR_THROW", "maybe"}, {"ETERNALVR_THROW_SPEED", "0.2"}, {"ETERNALVR_SWING_SPEED", "nine"}});
    CHECK(bad.issues.size() == 3);
    CHECK_FALSE(bad.settings.throwGesture.enabled);
    CHECK(bad.settings.throwGesture.speed == evr::input::ThrowSettings{}.speed);
    CHECK(bad.settings.swing.speed == evr::input::SwingSettings{}.speed);
}

TEST_CASE("the hands-up jump is off unless turned on") {
    CHECK_FALSE(parse({}).settings.handsJump.enabled);
    const auto on = parse({{"ETERNALVR_HANDS_JUMP", "1"}});
    CHECK(on.issues.empty());
    CHECK(on.settings.handsJump.enabled);
    const auto bad = parse({{"ETERNALVR_HANDS_JUMP", "sometimes"}});
    CHECK(bad.issues.size() == 1);
    CHECK_FALSE(bad.settings.handsJump.enabled);
}

TEST_CASE("the punch speed and the button hold time can be set within their ranges") {
    const auto defaults = parse({});
    CHECK(defaults.settings.punchSpeed == evr::input::kDefaultPunchMetresPerSecond);
    CHECK(defaults.settings.holdSeconds == evr::input::kDefaultHoldSeconds);
    const auto set = parse({{"ETERNALVR_PUNCH_SPEED", "1.6"}, {"ETERNALVR_HOLD_SECONDS", "0.4"}});
    CHECK(set.issues.empty());
    CHECK(set.settings.punchSpeed == doctest::Approx(1.6f));
    CHECK(set.settings.holdSeconds == doctest::Approx(0.4f));
    const auto bad = parse({{"ETERNALVR_PUNCH_SPEED", "9"}, {"ETERNALVR_HOLD_SECONDS", "0.01"}});
    CHECK(bad.issues.size() == 2);
    CHECK(bad.settings.punchSpeed == evr::input::kDefaultPunchMetresPerSecond);
    CHECK(bad.settings.holdSeconds == evr::input::kDefaultHoldSeconds);
}

TEST_CASE("the off-hand arm's offsets are mirrored with the weapon in the left hand") {
    using evr::game::Handedness;
    using evr::input::kDefaultOffhandShoulder;
    const evr::game::WeaponOffset o{-0.08f, 0.035f, 0.01f, 5.0f, 10.0f, -20.0f};
    CHECK(evr::input::offhandOffsetFor(o, Handedness::Right) == o);
    for (const Handedness left : {Handedness::LeftButtonSwap, Handedness::LeftButtonAndStickSwap}) {
        const auto m = evr::input::offhandOffsetFor(o, left);
        CHECK(m == evr::game::WeaponOffset{-0.08f, -0.035f, 0.01f, 5.0f, -10.0f, 20.0f});
        CHECK(evr::input::offhandOffsetFor(kDefaultOffhandShoulder, left).left ==
              -kDefaultOffhandShoulder.left);
    }
}

TEST_CASE("the game's shoulder anchors the off arm only with the weapon in the right hand") {
    using evr::game::Handedness;
    using evr::input::ShoulderAnchor;
    CHECK(evr::input::shoulderAnchorFor(ShoulderAnchor::Model, Handedness::Right) == ShoulderAnchor::Model);
    CHECK(evr::input::shoulderAnchorFor(ShoulderAnchor::Head, Handedness::Right) == ShoulderAnchor::Head);
    CHECK(evr::input::shoulderAnchorFor(ShoulderAnchor::Model, Handedness::LeftButtonSwap) ==
          ShoulderAnchor::Head);
    CHECK(evr::input::shoulderAnchorFor(ShoulderAnchor::Model, Handedness::LeftButtonAndStickSwap) ==
          ShoulderAnchor::Head);
}

TEST_CASE("the weapon arm is posed by IK unless ETERNALVR_WEAPON_ARM says game") {
    using evr::input::WeaponArmMode;
    CHECK(parse({}).settings.weaponArm == WeaponArmMode::Ik);
    CHECK(parse({{"ETERNALVR_WEAPON_ARM", "game"}}).settings.weaponArm == WeaponArmMode::Game);
    CHECK(parse({{"ETERNALVR_WEAPON_ARM", " IK "}}).settings.weaponArm == WeaponArmMode::Ik);
    const auto bad = parse({{"ETERNALVR_WEAPON_ARM", "free"}});
    REQUIRE(bad.issues.size() == 1);
    CHECK(bad.issues[0].name == "ETERNALVR_WEAPON_ARM");
    CHECK(bad.settings.weaponArm == WeaponArmMode::Ik);
    // Whatever the off hand does.
    CHECK(parse({{"ETERNALVR_OFFHAND", "free"}}).settings.weaponArm == WeaponArmMode::Ik);
    CHECK(std::string(evr::input::weaponArmModeName(WeaponArmMode::Ik)) == "ik");
    CHECK(std::string(evr::input::weaponArmModeName(WeaponArmMode::Game)) == "game");
}

TEST_CASE("the weapon arm's shoulder and elbow are the off hand's on the weapon hand's side") {
    using evr::input::kDefaultOffhandElbow;
    using evr::input::kDefaultOffhandShoulder;
    using evr::input::weaponArmOffsetFor;
    // Weapon in the right hand: 18 cm to the right of the eyes, the elbow out to the right.
    const auto shoulder = weaponArmOffsetFor(kDefaultOffhandShoulder, Handedness::Right);
    CHECK(shoulder == evr::game::WeaponOffset{-0.08f, -0.18f, -0.24f, 0.0f, 0.0f, 0.0f});
    CHECK(weaponArmOffsetFor(kDefaultOffhandElbow, Handedness::Right) ==
          evr::game::WeaponOffset{-0.2f, -0.6f, -1.0f, 0.0f, 0.0f, 0.0f});
    // Weapon in the left hand (the model's right arm at the left controller): to the left, as given, the
    // off hand's mirrored to the right.
    for (const Handedness left : {Handedness::LeftButtonSwap, Handedness::LeftButtonAndStickSwap}) {
        CHECK(weaponArmOffsetFor(kDefaultOffhandShoulder, left) == kDefaultOffhandShoulder);
        CHECK(weaponArmOffsetFor(kDefaultOffhandElbow, left) == kDefaultOffhandElbow);
        CHECK(evr::input::offhandOffsetFor(kDefaultOffhandShoulder, left).left == -0.18f);
    }
}

TEST_CASE("the weapon arm's test shoulder is unset unless given, and a bad one is reported") {
    CHECK_FALSE(parse({}).settings.weaponArmTestShoulder.has_value());
    const auto set = parse({{"ETERNALVR_WEAPON_ARM_TEST_SHOULDER", "0,-0.5,0.3"}});
    CHECK(set.issues.empty());
    REQUIRE(set.settings.weaponArmTestShoulder.has_value());
    CHECK(*set.settings.weaponArmTestShoulder ==
          evr::game::WeaponOffset{0.0f, -0.5f, 0.3f, 0.0f, 0.0f, 0.0f});
    const auto bad = parse({{"ETERNALVR_WEAPON_ARM_TEST_SHOULDER", "right"}});
    REQUIRE(bad.issues.size() == 1);
    CHECK(bad.issues[0].name == "ETERNALVR_WEAPON_ARM_TEST_SHOULDER");
    CHECK_FALSE(bad.settings.weaponArmTestShoulder.has_value());
}
