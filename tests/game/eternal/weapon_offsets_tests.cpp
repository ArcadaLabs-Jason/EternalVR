#include "game/eternal/weapon_offsets.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

using evr::game::builtinWeaponOffsets;
using evr::game::OffsetPosture;
using evr::game::parseOffsetList;
using evr::game::parseWeaponOffsets;
using evr::game::SightRow;
using evr::game::WeaponModDecls;
using evr::game::WeaponOffset;
using evr::game::WeaponOffsetTable;
using evr::game::WeaponSight;

namespace {

std::string describe(const WeaponOffsetTable& table) {
    std::string text;
    for (const std::string& issue : table.issues) {
        text += issue + "\n";
    }
    return text;
}

constexpr const char* kTable = R"(# comment
[standing]
"default" = [-0.25, 0.18, 0.22, 0, 0, 0]
"weapon/player/shotgun" = [-0.20, 0.15, 0.20]   # three numbers: no rotation
"weapon/player/shotgun_secondary" = [-0.21, 0.15, 0.20, 1, 2, 3]
"weapon/player/double_barrel" = [-0.30, 0.10, 0.25, 0, -5, 0]

[seated]
"weapon/player/double_barrel" = [-0.35, 0.10, 0.25, 0, -5, 0]
)";

} // namespace

TEST_CASE("the built-in table reads without issues, and seated play uses the standing rows") {
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    CHECK(table.ok());
    CHECK(table.standing.contains("default"));
    // The glove follows the real hand in either posture.
    CHECK(table.seated.empty());
    for (const char* weapon :
         {"weapon/player/shotgun", "weapon/player/chaingun", "weapon/player/fists", ""}) {
        INFO(weapon);
        CHECK(table.lookup(weapon, OffsetPosture::Seated) == table.lookup(weapon, OffsetPosture::Standing));
    }
}

TEST_CASE("the swim's fists have their own row; a melee's and the Berserk's fists keep the default") {
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    REQUIRE(table.ok());
    const WeaponOffset fallback = table.lookup("", OffsetPosture::Standing);
    const WeaponOffset swim = table.lookup("weapon/player/fists_swim", OffsetPosture::Standing);
    // Measured on the hands rig under water: the glove's wrist 26.4 cm (median) ahead of and right of the
    // real hand on the default row.
    CHECK(swim == WeaponOffset{-0.493f, 0.264f, 0.177f, 0.0f, 0.0f, 0.0f});
    CHECK_FALSE(swim == fallback);
    CHECK(table.lookup("weapon/player/fists_doom5melee", OffsetPosture::Standing) == fallback);
    CHECK(table.lookup("weapon/player/fists_berserk", OffsetPosture::Standing) == fallback);
}

TEST_CASE("the built-in sights cover every gun whose line is off the ray, and nothing else") {
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    REQUIRE(table.ok());
    for (const char* weapon :
         {"weapon/player/shotgun", "weapon/player/heavy_cannon", "weapon/player/chaingun",
          "weapon/player/double_barrel", "weapon/player/gauss_rifle", "weapon/player/plasma_rifle",
          "weapon/player/rocket_launcher"}) {
        INFO(weapon);
        CHECK(table.lookupSight(weapon).has_value());
    }
    // The BFG's and the Unmaykr's lines are within 0.06 degrees of the ray unturned
    // (docs/rig-findings/hands-rig.md, section 18).
    for (const char* other : {"weapon/player/bfg", "weapon/player/unmaykr", "weapon/player/fists",
                              "weapon/player/fists_doom5melee", "weapon/player/chainsaw",
                              "weapon/player/crucible", "weapon/player/hammer", ""}) {
        INFO(other);
        CHECK_FALSE(table.lookupSight(other).has_value());
    }
}

TEST_CASE("sight rows follow the name rules, with no default") {
    const WeaponOffsetTable table = parseWeaponOffsets(R"([standing]
"default" = [-0.25, 0.18, 0.22]
[sights]
"weapon/player/shotgun" = [0.2, 2.17]
"weapon/player/shotgun_full_auto" = [0.1, 1.0]
)");
    INFO(describe(table));
    REQUIRE(table.ok());
    CHECK(table.lookupSight("weapon/player/shotgun") == WeaponSight{0.2f, 2.17f});
    CHECK(table.lookupSight("weapon/player/shotgun_full_auto") == WeaponSight{0.1f, 1.0f});
    CHECK(table.lookupSight("weapon/player/shotgun_sticky") == WeaponSight{0.2f, 2.17f});
    CHECK_FALSE(table.lookupSight("weapon/player/shotgunner").has_value());
    CHECK_FALSE(table.lookupSight("weapon/player/plasma_rifle").has_value());
    // The sections around it read as before.
    CHECK(table.lookup("weapon/player/shotgun", OffsetPosture::Standing).forward == -0.25f);
}

TEST_CASE("a row keyed by the active mod's decl wins over the weapon's while that mod is active") {
    const WeaponOffsetTable table = parseWeaponOffsets(R"([sights]
"weapon/player/heavy_cannon" = [-3.45, 0.19]
"weapon/player/heavy_cannon_bolt_action" = [-0.15, 0.15]
"weapon/player/shotgun" = [0.21, 2.17]
"weapon/player/rifle_scope" = [0.5, 0.5]
)");
    INFO(describe(table));
    REQUIRE(table.ok());
    constexpr std::string_view kCannon = "weapon/player/heavy_cannon";
    constexpr std::string_view kBolt = "weapon/player/heavy_cannon_bolt_action";
    const SightRow irons{{-3.45f, 0.19f}, kCannon};
    const SightRow scope{{-0.15f, 0.15f}, kBolt};
    // The Precision Bolt active (its decl in either fire mode's override): the scope's row.
    CHECK(table.lookupSight(kCannon, {"", kBolt}) == scope);
    CHECK(table.lookupSight(kCannon, {kBolt, ""}) == scope);
    // A longer decl of the mod (a mastered one) takes the mod's row by the name rules.
    CHECK(table.lookupSight(kCannon, {"", "weapon/player/heavy_cannon_bolt_action_mastery"}) == scope);
    // The other mod, its mastered decl, or none: the weapon's own row.
    for (const WeaponModDecls& mods :
         {WeaponModDecls{"", "weapon/player/heavy_cannon_burst_detonate"},
          WeaponModDecls{"", "weapon/player/heavy_cannon_burst_detonate_mastery"}, WeaponModDecls{"", ""}}) {
        INFO(mods[1]);
        CHECK(table.lookupSight(kCannon, mods) == irons);
    }
    // A weapon with one row keeps it whatever its mods.
    const SightRow shotgun{{0.21f, 2.17f}, "weapon/player/shotgun"};
    CHECK(table.lookupSight("weapon/player/shotgun", {"", "weapon/player/shotgun_secondary_pop_rockets"}) ==
          shotgun);
    // Only the held weapon's own mods count: another weapon's mod decl is not looked at.
    CHECK(table.lookupSight("weapon/player/shotgun", {kBolt, ""}) == shotgun);
    // No row for the weapon nor its mod: not turned.
    CHECK_FALSE(
        table.lookupSight("weapon/player/plasma_rifle", {"", "weapon/player/plasma_rifle_secondary_aoe"})
            .has_value());
    // A mod's row on a weapon without one of its own applies only while that mod is active.
    CHECK(table.lookupSight("weapon/player/rifle", {"weapon/player/rifle_scope", ""}) ==
          SightRow{{0.5f, 0.5f}, "weapon/player/rifle_scope"});
    CHECK_FALSE(table.lookupSight("weapon/player/rifle", {"", ""}).has_value());
    // The held decl's own rules are the plain lookup's.
    CHECK(table.lookupSight("weapon/player/shotgun_sticky", {"", ""}) == shotgun);
}

TEST_CASE("the built-in table gives the Heavy Cannon one sight row for every mod: the front ring's line") {
    // The front sight is a ring, and the line through its centre from the eye point of the game's own zoom
    // pose is the one the Precision Bolt's scope tube runs on: every mod looks along it, so a mod switch does
    // not turn the gun. The line from the rear sight to the ring's top bar (3.5 degrees above the bore) is
    // not one an eye can sit on: about 18 cm behind the rear sight it runs into the top of the receiver.
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    REQUIRE(table.ok());
    constexpr std::string_view kCannon = "weapon/player/heavy_cannon";
    const std::optional<SightRow> own = table.lookupSight(kCannon, {"", ""});
    REQUIRE(own.has_value());
    CHECK(own->key == kCannon);
    for (const WeaponModDecls& mods :
         {WeaponModDecls{"", "weapon/player/heavy_cannon_bolt_action"},
          WeaponModDecls{"", "weapon/player/heavy_cannon_burst_detonate"},
          WeaponModDecls{"", "weapon/player/heavy_cannon_burst_detonate_mastery"},
          WeaponModDecls{"", "weapon/player/heavy_cannon_burst_detonate_faster_recharge"}}) {
        INFO(mods[1]);
        CHECK(table.lookupSight(kCannon, mods) == own);
    }
    // The ring's line is 0.2 degrees above the bore and the idle pose holds the gun level: a small turn.
    CHECK(std::fabs(own->sight.pitch) < 0.5f);
    CHECK(std::fabs(own->sight.yaw) < 0.5f);
}

TEST_CASE("bad sight rows are reported and skipped") {
    const WeaponOffsetTable table = parseWeaponOffsets(R"([sights]
"default" = [0, 0]
"a" = [1]
"b" = [1, 2, 3]
"c" = [0, 50]
"d" = [nan, 0]
"e" = 1, 2
"f" = [1, 2]
"f" = [3, 4]
[standing]
"g" = [1, 2]
)");
    INFO(describe(table));
    CHECK(table.issues.size() == 8);
    CHECK(table.issues.front().starts_with("line 2:"));
    CHECK(table.sights.size() == 1);
    CHECK(table.lookupSight("f") == WeaponSight{1.0f, 2.0f});
    CHECK(table.standing.empty());
}

TEST_CASE("an exact name wins, then the longest key the name extends, then the default") {
    const WeaponOffsetTable table = parseWeaponOffsets(kTable);
    INFO(describe(table));
    REQUIRE(table.ok());
    CHECK(table.lookup("weapon/player/shotgun", OffsetPosture::Standing) ==
          WeaponOffset{-0.20f, 0.15f, 0.20f, 0.0f, 0.0f, 0.0f});
    CHECK(table.lookup("weapon/player/shotgun_secondary_full_auto", OffsetPosture::Standing) ==
          WeaponOffset{-0.21f, 0.15f, 0.20f, 1.0f, 2.0f, 3.0f});
    CHECK(table.lookup("weapon/player/shotgun_other", OffsetPosture::Standing).forward == -0.20f);
    // A prefix must end at a '_': "shotgunner" is not a shotgun variant.
    CHECK(table.lookup("weapon/player/shotgunner", OffsetPosture::Standing).forward == -0.25f);
    CHECK(table.lookup("weapon/player/plasma_rifle", OffsetPosture::Standing).forward == -0.25f);
}

TEST_CASE("seated uses its own entry, then the standing rules when it has neither entry nor default") {
    const WeaponOffsetTable table = parseWeaponOffsets(kTable);
    REQUIRE(table.ok());
    CHECK(table.lookup("weapon/player/double_barrel", OffsetPosture::Seated).forward == -0.35f);
    CHECK(table.lookup("weapon/player/double_barrel_meat_hook", OffsetPosture::Seated).forward == -0.35f);
    CHECK(table.lookup("weapon/player/shotgun", OffsetPosture::Seated).forward == -0.20f);

    const WeaponOffsetTable withDefault =
        parseWeaponOffsets(std::string(kTable) + "\"default\" = [-0.4, 0, 0]\n");
    REQUIRE(withDefault.ok());
    CHECK(withDefault.lookup("weapon/player/shotgun", OffsetPosture::Seated).forward == -0.4f);
    CHECK(withDefault.lookup("weapon/player/double_barrel", OffsetPosture::Seated).forward == -0.35f);
}

TEST_CASE("an empty table gives no offset") {
    const WeaponOffsetTable table = parseWeaponOffsets("");
    CHECK(table.ok());
    CHECK(table.lookup("weapon/player/shotgun", OffsetPosture::Standing) == WeaponOffset{});
    CHECK(table.lookup("weapon/player/shotgun", OffsetPosture::Seated) == WeaponOffset{});
}

TEST_CASE("bad lines are reported with their line numbers and skipped") {
    const WeaponOffsetTable table = parseWeaponOffsets(R"([standing]
"a" = [1, 2]
"b" = [1, 2, 3, 4]
"c" = [1, 2, nan]
"d" = [3, 0, 0]
"e" = [0, 0, 0, 0, 200, 0]
f = [0, 0, 0]
"g" = 0, 0, 0
"h" = [0, 0, 0]
"h" = [1, 1, 1]
[other]
"i" = [0, 0, 0]
)");
    INFO(describe(table));
    CHECK(table.issues.size() == 9);
    CHECK(table.issues.front().starts_with("line 2:"));
    CHECK(table.standing.size() == 1);
    CHECK(table.standing.at("h") == WeaponOffset{});
}

TEST_CASE("a byte order mark and CRLF line ends are accepted") {
    const WeaponOffsetTable table =
        parseWeaponOffsets("\xEF\xBB\xBF[standing]\r\n\"default\" = [0.1, 0.2, 0.3]\r\n");
    INFO(describe(table));
    CHECK(table.ok());
    CHECK(table.lookup("x", OffsetPosture::Standing) == WeaponOffset{0.1f, 0.2f, 0.3f, 0.0f, 0.0f, 0.0f});
}

TEST_CASE("the tuning override reads three or six numbers") {
    WeaponOffset offset;
    CHECK(parseOffsetList("-0.2,0.1,0.15", offset));
    CHECK(offset == WeaponOffset{-0.2f, 0.1f, 0.15f, 0.0f, 0.0f, 0.0f});
    CHECK(parseOffsetList(" -0.2, 0.1, 0.15, 5, -10, 0 ", offset));
    CHECK(offset.yaw == -10.0f);
    CHECK_FALSE(parseOffsetList("", offset));
    CHECK_FALSE(parseOffsetList("1,2", offset));
    CHECK_FALSE(parseOffsetList("a,b,c", offset));
    CHECK_FALSE(parseOffsetList("0,0,0,0,0,0,0", offset));
}

TEST_CASE("the built-in right grips cover the weapons whose idle parks the glove and no others") {
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    REQUIRE(table.ok());
    for (const char* weapon : {"weapon/player/chaingun", "weapon/player/gauss_rifle"}) {
        INFO(weapon);
        REQUIRE(table.lookupRightGrip(weapon).has_value());
        CHECK(table.lookupRightGrip(weapon)->grip == evr::game::GameGrip::HeavyCannon);
    }
    for (const char* other :
         {"weapon/player/shotgun", "weapon/player/heavy_cannon", "weapon/player/plasma_rifle",
          "weapon/player/rocket_launcher", "weapon/player/double_barrel", "weapon/player/bfg",
          "weapon/player/unmaykr", "weapon/player/fists", "weapon/player/chainsaw", ""}) {
        INFO(other);
        CHECK_FALSE(table.lookupRightGrip(other).has_value());
    }
}

TEST_CASE("right grip rows follow the name rules; bad ones are reported and skipped") {
    const WeaponOffsetTable table = parseWeaponOffsets(R"([right_grip]
"weapon/player/chaingun" = "heavy_cannon"
"weapon/player/gauss_rifle" = ["heavy_cannon", -0.3, -0.04, -0.17]
"default" = "heavy_cannon"
"a" = "shotgun"
"b" = heavy_cannon
"c" = ["heavy_cannon", 0, 0]
"d" = ["heavy_cannon", 0, 0, 2]
"e" = ["shotgun", 0, 0, 0]
"weapon/player/chaingun" = "heavy_cannon"
[sights]
"weapon/player/chaingun" = [0.32, -0.01]
)");
    INFO(describe(table));
    CHECK(table.issues.size() == 7);
    CHECK(table.issues.front().starts_with("line 4:"));
    CHECK(table.rightGrips.size() == 2);
    CHECK(table.lookupRightGrip("weapon/player/chaingun_turret") == evr::game::RightGrip{});
    const evr::game::RightGrip moved{evr::game::GameGrip::HeavyCannon,
                                     std::array<float, 3>{-0.3f, -0.04f, -0.17f}};
    CHECK(table.lookupRightGrip("weapon/player/gauss_rifle") == moved);
    CHECK_FALSE(table.lookupRightGrip("weapon/player/chaingunner").has_value());
    // The section after it reads as before.
    CHECK(table.lookupSight("weapon/player/chaingun") == WeaponSight{0.32f, -0.01f});
}

TEST_CASE("the built-in left-handed off-hand rows cover the chaingun only") {
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    REQUIRE(table.ok());
    CHECK(table.lookupLeftOffHand("weapon/player/chaingun") == evr::game::LeftOffHand::Controller);
    for (const char* other : {"weapon/player/shotgun", "weapon/player/heavy_cannon",
                              "weapon/player/plasma_rifle", "weapon/player/rocket_launcher",
                              "weapon/player/double_barrel", "weapon/player/gauss_rifle", "weapon/player/bfg",
                              "weapon/player/unmaykr", "weapon/player/crucible", "weapon/player/fists", ""}) {
        INFO(other);
        CHECK_FALSE(table.lookupLeftOffHand(other).has_value());
    }
}

TEST_CASE("left-handed off-hand rows follow the name rules; bad ones are reported and skipped") {
    const WeaponOffsetTable table = parseWeaponOffsets(R"([left_handed_off_hand]
"weapon/player/chaingun" = "controller"
"default" = "controller"
"a" = controller
"b" = "game"
"weapon/player/chaingun" = "controller"
[sights]
"weapon/player/chaingun" = [0.32, -0.01]
)");
    INFO(describe(table));
    CHECK(table.issues.size() == 4);
    CHECK(table.issues.front().starts_with("line 3:"));
    CHECK(table.leftOffHands.size() == 1);
    CHECK(table.lookupLeftOffHand("weapon/player/chaingun_turret") == evr::game::LeftOffHand::Controller);
    CHECK_FALSE(table.lookupLeftOffHand("weapon/player/chaingunner").has_value());
    CHECK(table.lookupSight("weapon/player/chaingun") == WeaponSight{0.32f, -0.01f});
}

TEST_CASE("the built-in left-hand wrist rows cover every item with a row but the swim's fists") {
    // With the weapon in the left hand an item without one is placed for the right glove until its grip is
    // read on its first raise of a session, then moves into the left hand (by 0.8 to 2.4 cm on the guns).
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    REQUIRE(table.ok());
    int rows = 0;
    for (const auto& [name, offset] : table.standing) {
        if (name == "default" || name == "weapon/player/fists_swim") {
            continue;
        }
        INFO(name);
        const auto wrist = table.lookupLeftHandWrist(name);
        REQUIRE(wrist.has_value());
        ++rows;
        // To the model's right of its middle (the left glove holds the right glove's grip reflected across
        // the gun's root, which hangs right of the middle) and below the eye, within the model's reach.
        CHECK((*wrist)[1] < 0.0f);
        CHECK((*wrist)[2] < 0.0f);
        CHECK(std::hypot((*wrist)[0], (*wrist)[1], (*wrist)[2]) < 1.0f);
    }
    CHECK(rows == 10);
    const auto crucible = table.lookupLeftHandWrist("weapon/player/crucible");
    REQUIRE(crucible.has_value());
    // In front of the model's origin, where the hilt sits at idle.
    CHECK((*crucible)[0] > 0.5f);
    // The swim's fists have no idle to measure (the stroke moves the wrist by 6 to 11 cm), and the fists of a
    // melee take no grip.
    for (const char* other : {"weapon/player/fists_swim", "weapon/player/fists", ""}) {
        INFO(other);
        CHECK_FALSE(table.lookupLeftHandWrist(other).has_value());
    }
}

TEST_CASE("left-hand wrist rows follow the name rules; bad ones are reported and skipped") {
    const WeaponOffsetTable table = parseWeaponOffsets(R"([left_hand_wrist]
"weapon/player/crucible" = [0.681, -0.240, -0.266]
"default" = [0.5, 0, 0]
"a" = [0.5, 0]
"b" = [0.5, 0, 0, 0, 0, 0]
"c" = [2.5, 0, 0]
"d" = 0.5, 0, 0
"weapon/player/crucible" = [0.1, 0.1, 0.1]
[sights]
"weapon/player/chaingun" = [0.32, -0.01]
)");
    INFO(describe(table));
    CHECK(table.issues.size() == 6);
    CHECK(table.issues.front().starts_with("line 3:"));
    CHECK(table.leftHandWrists.size() == 1);
    const std::array<float, 3> crucible{0.681f, -0.240f, -0.266f};
    CHECK(table.lookupLeftHandWrist("weapon/player/crucible") == crucible);
    CHECK(table.lookupLeftHandWrist("weapon/player/crucible_x") == crucible);
    CHECK_FALSE(table.lookupLeftHandWrist("weapon/player/cruciblex").has_value());
    CHECK(table.lookupSight("weapon/player/chaingun") == WeaponSight{0.32f, -0.01f});
}

TEST_CASE("the built-in trigger finger rows cover the nine guns and none of the other held items") {
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    CHECK(table.ok());
    CHECK(table.triggerLifts.size() == 9);
    for (const char* gun :
         {"weapon/player/shotgun", "weapon/player/heavy_cannon", "weapon/player/plasma_rifle",
          "weapon/player/rocket_launcher", "weapon/player/double_barrel", "weapon/player/gauss_rifle",
          "weapon/player/chaingun", "weapon/player/bfg", "weapon/player/unmaykr"}) {
        INFO(gun);
        const auto lift = table.lookupTriggerLift(gun);
        REQUIRE(lift.has_value());
        CHECK(*lift > 0.0f);
        CHECK(*lift <= 1.0f);
    }
    for (const char* item : {"weapon/player/crucible", "weapon/player/fists", "weapon/player/fists_swim",
                             "weapon/player/chainsaw", "weapon/player/sentinel_hammer", "default"}) {
        INFO(item);
        CHECK_FALSE(table.lookupTriggerLift(item).has_value());
    }
}

TEST_CASE("trigger finger rows follow the name rules; bad ones are reported and skipped") {
    const WeaponOffsetTable table = parseWeaponOffsets(R"([trigger_finger]
"weapon/player/shotgun" = 0.6
"default" = 0.5
"a" = 1.5
"b" = -0.1
"c" = [0.5]
"d" = nan
"weapon/player/shotgun" = 0.9
"weapon/player/bfg" = 1
[sights]
"weapon/player/chaingun" = [0.32, -0.01]
)");
    INFO(describe(table));
    CHECK(table.issues.size() == 6);
    CHECK(table.issues.front().starts_with("line 3:"));
    CHECK(table.triggerLifts.size() == 2);
    CHECK(table.lookupTriggerLift("weapon/player/shotgun") == 0.6f);
    CHECK(table.lookupTriggerLift("weapon/player/shotgun_secondary_full_auto") == 0.6f);
    CHECK_FALSE(table.lookupTriggerLift("weapon/player/shotgunx").has_value());
    CHECK(table.lookupTriggerLift("weapon/player/bfg") == 1.0f);
    CHECK(table.lookupSight("weapon/player/chaingun") == WeaponSight{0.32f, -0.01f});
}
