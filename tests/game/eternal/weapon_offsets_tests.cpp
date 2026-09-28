#include "game/eternal/weapon_offsets.hpp"

#include <doctest/doctest.h>

#include <ostream>
#include <string>

using evr::game::builtinWeaponOffsets;
using evr::game::OffsetPosture;
using evr::game::parseOffsetList;
using evr::game::parseWeaponOffsets;
using evr::game::WeaponOffset;
using evr::game::WeaponOffsetTable;

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

TEST_CASE("the built-in table reads without issues and has a default for both postures") {
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    INFO(describe(table));
    CHECK(table.ok());
    CHECK(table.standing.contains("default"));
    CHECK(table.seated.contains("default"));
    // The seated placement is pulled back toward the body (T-074).
    CHECK(table.lookup("weapon/player/shotgun", OffsetPosture::Seated).forward <
          table.lookup("weapon/player/shotgun", OffsetPosture::Standing).forward);
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
