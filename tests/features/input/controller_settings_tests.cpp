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
    CHECK(s.path == InputPath::Auto);
    CHECK(s.shotOrigin == ShotOrigin::Hand);
    CHECK(s.viewmodel);
    CHECK(s.weaponFov);
    CHECK_FALSE(s.seated);
    CHECK_FALSE(s.viewmodelOffset.has_value());
    CHECK(s.controllerDataPath.empty());
    CHECK(s.testInputPath.empty());
}

TEST_CASE("every setting reads its documented values") {
    const auto result = parse({{"ETERNALVR_CONTROLLERS", "0"},
                               {"ETERNALVR_AIM", "hand"},
                               {"ETERNALVR_LOCOMOTION", "hand"},
                               {"ETERNALVR_TURN", "snap"},
                               {"ETERNALVR_TURN_RATE", "300"},
                               {"ETERNALVR_SNAP_DEGREES", "30"},
                               {"ETERNALVR_HANDEDNESS", "left_mirror"},
                               {"ETERNALVR_DOSSIER", "tap"},
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
    CHECK(s.locomotion == LocomotionFrame::OffHand);
    CHECK(s.turn.mode == TurnMode::Snap);
    CHECK(s.turn.smoothDegreesPerSecond == 300.0f);
    CHECK(s.turn.snapDegrees == 30.0f);
    CHECK(s.handedness == Handedness::LeftButtonAndStickSwap);
    CHECK(s.dossier == evr::input::DossierPress::Tap);
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
