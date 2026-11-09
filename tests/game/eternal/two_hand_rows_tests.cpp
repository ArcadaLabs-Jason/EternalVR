#include "game/eternal/weapon_offsets.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <string>

using evr::game::builtinWeaponOffsets;
using evr::game::parseWeaponOffsets;
using evr::game::WeaponOffsetTable;

namespace {

std::string describe(const WeaponOffsetTable& table) {
    std::string text;
    for (const std::string& issue : table.issues) {
        text += issue + "\n";
    }
    return text;
}

} // namespace

TEST_CASE("the built-in two-hand rows: the guns held with both hands, not the chaingun or the melee items") {
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    REQUIRE(table.ok());
    for (const char* gun :
         {"weapon/player/shotgun", "weapon/player/double_barrel", "weapon/player/heavy_cannon",
          "weapon/player/plasma_rifle", "weapon/player/rocket_launcher", "weapon/player/gauss_rifle",
          "weapon/player/bfg", "weapon/player/unmaykr"}) {
        INFO(gun);
        const auto row = table.lookupTwoHand(gun);
        REQUIRE(row.has_value());
        // Ahead of the weapon hand along the barrel: 15 to 80 cm forward, within 2 cm of the gun's middle,
        // within 10 cm above or below the grip.
        CHECK((*row)[0] > 0.15f);
        CHECK((*row)[0] < 0.80f);
        CHECK(std::fabs((*row)[1]) < 0.02f);
        CHECK(std::fabs((*row)[2]) < 0.10f);
    }
    // A mod's decl uses its weapon's row.
    CHECK(table.lookupTwoHand("weapon/player/heavy_cannon_bolt_action") ==
          table.lookupTwoHand("weapon/player/heavy_cannon"));
    for (const char* other : {"weapon/player/chaingun", "weapon/player/crucible", "weapon/player/fists",
                              "weapon/player/fists_swim", "weapon/player/fists_berserk",
                              "weapon/player/chainsaw", "weapon/player/sentinel_hammer", ""}) {
        INFO(other);
        CHECK_FALSE(table.lookupTwoHand(other).has_value());
    }
    CHECK(table.twoHand.size() == 8);
}

TEST_CASE("two-hand rows follow the name rules; bad ones are reported and skipped") {
    const WeaponOffsetTable table = parseWeaponOffsets(R"([two_hand]
"weapon/player/shotgun" = [0.296, 0.006, 0.057]
"default" = [0.3, 0, 0]
"a" = [0.3, 0]
"b" = [0.3, 0, 0, 0, 0, 0]
"c" = [2.5, 0, 0]
"weapon/player/shotgun" = [0.1, 0.1, 0.1]
[left_hand_wrist]
"weapon/player/crucible" = [0.681, -0.240, -0.266]
)");
    INFO(describe(table));
    CHECK(table.issues.size() == 5);
    CHECK(table.issues.front().starts_with("line 3:"));
    CHECK(table.twoHand.size() == 1);
    CHECK(table.leftHandWrists.size() == 1);
    const std::array<float, 3> shotgun{0.296f, 0.006f, 0.057f};
    CHECK(table.lookupTwoHand("weapon/player/shotgun") == shotgun);
    CHECK(table.lookupTwoHand("weapon/player/shotgun_secondary") == shotgun);
    CHECK_FALSE(table.lookupTwoHand("weapon/player/shotgunx").has_value());
    CHECK_FALSE(table.lookupTwoHand("weapon/player/crucible").has_value());
}
