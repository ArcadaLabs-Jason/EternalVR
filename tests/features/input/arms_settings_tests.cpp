// The arms' settings (ETERNALVR_ARMS, ETERNALVR_ARM_MODEL, ETERNALVR_TEST_ARM_FAULT): hands only as a third
// value beside shown and hidden, with the arm settings it needs, the rig's model switch without the base
// model (DECISIONS T-148), and the hands rig's planted faults.

#include "features/input/controller_settings.hpp"

#include <doctest/doctest.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

using evr::input::ArmFault;
using evr::input::ArmModel;
using evr::input::ArmSolve;
using evr::input::ControllerSettingsResult;
using evr::input::OffhandMode;
using evr::input::parseControllerSettings;
using evr::input::WeaponArmMode;

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

bool reported(const ControllerSettingsResult& r, std::string_view name) {
    for (const auto& issue : r.issues) {
        if (issue.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("ETERNALVR_ARMS: shown, hidden or hands only, and a mistyped value names all three") {
    const auto shown = parse({});
    CHECK_FALSE(shown.settings.armsHidden);
    CHECK_FALSE(shown.settings.armsHandsOnly);
    const auto hands = parse({{"ETERNALVR_ARMS", " Hands "}});
    CHECK(hands.issues.empty());
    CHECK(hands.settings.armsHandsOnly);
    CHECK_FALSE(hands.settings.armsHidden);
    const auto hidden = parse({{"ETERNALVR_ARMS", "hidden"}});
    CHECK(hidden.settings.armsHidden);
    CHECK_FALSE(hidden.settings.armsHandsOnly);
    const auto bad = parse({{"ETERNALVR_ARMS", "gloves"}});
    REQUIRE(bad.issues.size() == 1);
    CHECK(bad.issues[0].name == "ETERNALVR_ARMS");
    CHECK(bad.issues[0].message.find("shown, hidden, hands") != std::string::npos);
    CHECK_FALSE(bad.settings.armsHidden);
    CHECK_FALSE(bad.settings.armsHandsOnly);
}

TEST_CASE("hands only takes the free off hand, the weapon arm's ik and the bind solve for the session") {
    // As the launcher sets it: nothing else given, nothing reported.
    const auto hands = parse({{"ETERNALVR_ARMS", "hands"}});
    CHECK(hands.issues.empty());
    CHECK(hands.settings.offhand == OffhandMode::Free);
    CHECK(hands.settings.weaponArm == WeaponArmMode::Ik);
    CHECK(hands.settings.armSolve == ArmSolve::Bind);
    // Variables that already agree are not reported.
    const auto agree = parse({{"ETERNALVR_ARMS", "hands"},
                              {"ETERNALVR_OFFHAND", "free"},
                              {"ETERNALVR_WEAPON_ARM", "ik"},
                              {"ETERNALVR_ARM_SOLVE", "bind"}});
    CHECK(agree.issues.empty());
    // Ones that say otherwise are reported and overridden.
    const auto other = parse({{"ETERNALVR_ARMS", "hands"},
                              {"ETERNALVR_OFFHAND", "game"},
                              {"ETERNALVR_WEAPON_ARM", "game"},
                              {"ETERNALVR_ARM_SOLVE", "animated"}});
    CHECK(other.issues.size() == 3);
    CHECK(reported(other, "ETERNALVR_OFFHAND"));
    CHECK(reported(other, "ETERNALVR_WEAPON_ARM"));
    CHECK(reported(other, "ETERNALVR_ARM_SOLVE"));
    CHECK(other.settings.offhand == OffhandMode::Free);
    CHECK(other.settings.weaponArm == WeaponArmMode::Ik);
    CHECK(other.settings.armSolve == ArmSolve::Bind);
    const auto probe = parse({{"ETERNALVR_ARMS", "hands"}, {"ETERNALVR_OFFHAND", "probe"}});
    CHECK(probe.settings.offhand == OffhandMode::Free);
    CHECK(reported(probe, "ETERNALVR_OFFHAND"));
}

TEST_CASE(
    "shown and hidden take the arms as the variables say: the free off hand and the bind solve by default") {
    for (const char* arms : {"shown", "hidden"}) {
        CAPTURE(arms);
        const auto r = parse({{"ETERNALVR_ARMS", arms}});
        CHECK(r.issues.empty());
        CHECK(r.settings.offhand == OffhandMode::Free);
        CHECK(r.settings.weaponArm == WeaponArmMode::Ik);
        CHECK(r.settings.armSolve == ArmSolve::Bind);
        const auto gameArm = parse({{"ETERNALVR_ARMS", arms}, {"ETERNALVR_WEAPON_ARM", "game"}});
        CHECK(gameArm.issues.empty());
        CHECK(gameArm.settings.weaponArm == WeaponArmMode::Game);
        // A developer's old arms: the game's off hand and the animated solve, kept as asked outside hands
        // only.
        const auto old = parse(
            {{"ETERNALVR_ARMS", arms}, {"ETERNALVR_OFFHAND", "game"}, {"ETERNALVR_ARM_SOLVE", "animated"}});
        CHECK(old.issues.empty());
        CHECK(old.settings.offhand == OffhandMode::Game);
        CHECK(old.settings.armSolve == ArmSolve::Animated);
    }
    const auto bind = parse({{"ETERNALVR_ARM_SOLVE", "bind"}, {"ETERNALVR_OFFHAND", "free"}});
    CHECK(bind.settings.armSolve == ArmSolve::Bind);
    CHECK_FALSE(bind.settings.armsHandsOnly);
}

TEST_CASE("the rig's arms model is the game's or a skin set; the base model is no longer taken") {
    CHECK(parse({}).settings.armModel == ArmModel::Game);
    CHECK(parse({{"ETERNALVR_ARM_MODEL", "skin:3"}}).settings.armModel == ArmModel::Skin);
    const auto base = parse({{"ETERNALVR_ARM_MODEL", "base"}});
    REQUIRE(base.issues.size() == 1);
    CHECK(base.issues[0].name == "ETERNALVR_ARM_MODEL");
    CHECK(base.issues[0].message.find("base") == std::string::npos);
    CHECK(base.issues[0].message.find("game, skin") != std::string::npos);
    CHECK(base.settings.armModel == ArmModel::Game);
    CHECK(std::string(evr::input::armModelName(ArmModel::Skin)) == "skin");
    CHECK(std::string(evr::input::armModelName(ArmModel::Game)) == "game");
}

TEST_CASE("the hands rig's planted faults: none, twist or forearm") {
    CHECK(parse({}).settings.testArmFault == ArmFault::None);
    CHECK(parse({{"ETERNALVR_TEST_ARM_FAULT", "twist"}}).settings.testArmFault == ArmFault::Twist);
    const auto forearm = parse({{"ETERNALVR_TEST_ARM_FAULT", "forearm"}, {"ETERNALVR_ARMS", "hands"}});
    CHECK(forearm.issues.empty());
    CHECK(forearm.settings.testArmFault == ArmFault::Forearm);
    CHECK(forearm.settings.armsHandsOnly);
    CHECK(std::string(evr::input::armFaultName(ArmFault::None)) == "none");
    CHECK(std::string(evr::input::armFaultName(ArmFault::Twist)) == "twist");
    CHECK(std::string(evr::input::armFaultName(ArmFault::Forearm)) == "forearm");
    CHECK(parse({{"ETERNALVR_TEST_ARM_FAULT", "fingers"}}).settings.testArmFault == ArmFault::Fingers);
    CHECK(std::string(evr::input::armFaultName(ArmFault::Fingers)) == "fingers");
    const auto bad = parse({{"ETERNALVR_TEST_ARM_FAULT", "sleeve"}});
    REQUIRE(bad.issues.size() == 1);
    CHECK(bad.settings.testArmFault == ArmFault::None);
}
