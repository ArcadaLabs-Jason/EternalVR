#include "features/input/controller_settings.hpp"
#include "game/eternal/quest_touch_bindings.hpp"
#include "game/eternal/weapon_offsets.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/weapon_pose.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

using evr::Vec3;
using evr::game::builtinWeaponOffsets;
using evr::game::Handedness;
using evr::game::OffsetPosture;
using evr::game::parseWeaponOffsets;
using evr::game::WeaponOffset;
using evr::game::WeaponOffsetTable;
using evr::game::WeaponSight;
using evr::xr_math::axisFromAngles;
using evr::xr_math::EyeRelativePose;
using evr::xr_math::IdAngles;
using evr::xr_math::IdViewAxis;
using evr::xr_math::placeViewmodel;
using evr::xr_math::translationKeepingWrist;

namespace {

constexpr Vec3 kWrist{evr::game::kRowsWrist.forward, evr::game::kRowsWrist.left, evr::game::kRowsWrist.up};

// Where the right glove holds each gun, in the arms model's frame as the game animates the weapon at idle:
// metres along the model's forward, left and up from its origin. Measured on the hands rig
// (docs/rig-findings/hands-rig.md, section 10), the median over five held controller poses (the idle sway
// moves it by under 1 cm): the right hand joint (the glove's wrist); for the chaingun and the ballista, whose
// idle parks the right glove 72 and 48 cm off the gun, where the weapon arm puts it on the gun's grip
// (features/arm/grip_hand.hpp). The Unmaykr and the Crucible: section 12 (the Crucible's row also turns the
// model).
struct Glove {
    const char* weapon;
    Vec3 wrist;
};
constexpr Glove kGloves[] = {
    {"weapon/player/shotgun", {0.104f, -0.169f, -0.223f}},
    {"weapon/player/heavy_cannon", {0.190f, -0.149f, -0.209f}},
    {"weapon/player/plasma_rifle", {-0.055f, -0.236f, -0.362f}},
    {"weapon/player/rocket_launcher", {0.264f, -0.183f, -0.215f}},
    {"weapon/player/double_barrel", {0.115f, -0.171f, -0.224f}},
    {"weapon/player/gauss_rifle", {0.246f, -0.210f, -0.385f}},
    {"weapon/player/chaingun", {0.535f, -0.201f, -0.324f}},
    {"weapon/player/bfg", {0.275f, -0.129f, -0.383f}},
    {"weapon/player/unmaykr", {0.165f, -0.229f, -0.356f}},
    {"weapon/player/crucible", {0.564f, -0.280f, -0.245f}},
};

// The glove's wrist joint axes (rows: the joint's x, y and z), measured on the hands rig at the neutral hold
// (docs/rig-findings/hands-rig.md, section 12): the Heavy Cannon's in the aim ray's frame, with its sights
// turn, which every gun's glove matches within 7 degrees; the Crucible's in the arms model's own frame.
constexpr Vec3 kGunGlove[3] = {
    {0.231f, 0.112f, -0.966f}, {-0.967f, -0.079f, -0.241f}, {-0.104f, 0.991f, 0.090f}};
constexpr Vec3 kCrucibleGlove[3] = {
    {-0.247f, 0.367f, -0.897f}, {-0.877f, -0.479f, 0.046f}, {-0.412f, 0.798f, 0.440f}};

// The largest angle, in degrees, between matching axes of two frames.
float largestAxisAngle(const Vec3 (&a)[3], const Vec3 (&b)[3]) {
    float largest = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float c = evr::dot(evr::normalize(a[i]), evr::normalize(b[i]));
        largest = std::max(largest, std::acos(std::clamp(c, -1.0f, 1.0f)) * 57.29578f);
    }
    return largest;
}

// How far the glove's wrist may sit from its target.
constexpr float kToleranceMetres = 0.01f;

Vec3 inFrame(const IdViewAxis& axis, Vec3 v) {
    return axis.forward * v.x + axis.left * v.y + axis.up * v.z;
}

// The glove's wrist, relative to the eye, where the layer places the model for a row: `hand` is the grip
// position with the aim ray's axis, `gripAxis` the grip's own orientation.
Vec3 placedWrist(const WeaponOffsetTable& table,
                 const Glove& glove,
                 const EyeRelativePose& hand,
                 const IdViewAxis& gripAxis,
                 IdViewAxis* modelAxis = nullptr) {
    const WeaponOffset o = table.lookup(glove.weapon, OffsetPosture::Standing);
    const std::optional<WeaponSight> row = table.lookupSight(glove.weapon);
    const std::optional<IdAngles> sight =
        row ? std::optional<IdAngles>(IdAngles{row->pitch, row->yaw, 0.0f}) : std::nullopt;
    const EyeRelativePose placed = placeViewmodel(hand, gripAxis, {o.forward, o.left, o.up},
                                                  {o.pitch, o.yaw, o.roll}, sight, kWrist, 1.0f);
    if (modelAxis) {
        *modelAxis = placed.axis;
    }
    return placed.offset + inFrame(placed.axis, glove.wrist);
}

} // namespace

TEST_CASE("the rows' wrist is the off hand's wrist point mirrored for the right glove") {
    // Its place only: the off hand's turn toward the little finger (T-179) is its own, the gun glove's comes
    // with each weapon's row.
    const WeaponOffset mirrored =
        evr::input::weaponArmOffsetFor(evr::input::kDefaultOffhandOffset, Handedness::Right);
    CHECK(mirrored.forward == evr::game::kRowsWrist.forward);
    CHECK(mirrored.left == evr::game::kRowsWrist.left);
    CHECK(mirrored.up == evr::game::kRowsWrist.up);
    CHECK(kWrist.y < 0.0f); // toward the back of the right hand
}

TEST_CASE("each weapon's row puts the glove's wrist where a wrist sits on the controller grip") {
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    REQUIRE(table.ok());
    const EyeRelativePose grip{}; // the grip at the eye, on the aim ray, its axis the identity
    for (const Glove& glove : kGloves) {
        const Vec3 got = placedWrist(table, glove, grip, grip.axis);
        INFO(std::string(glove.weapon), ": wrist (", got.x, " ", got.y, " ", got.z, ")");
        CHECK(evr::length(got - kWrist) <= kToleranceMetres);
    }
}

TEST_CASE(
    "with the grip turned from the aim ray the wrist stays in the grip's frame and the gun on the ray") {
    // No controller's aim-to-grip turn is recorded in the repository or the layer: a 40 degree pitch (with
    // a little yaw and roll) stands in for a headset's, on an aim ray that is itself turned.
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    REQUIRE(table.ok());
    const IdViewAxis aim = axisFromAngles({10.0f, 30.0f, 5.0f});
    const IdViewAxis turn = axisFromAngles({-40.0f, 4.0f, -6.0f});
    const IdViewAxis gripAxis{inFrame(aim, turn.forward), inFrame(aim, turn.left), inFrame(aim, turn.up)};
    const EyeRelativePose hand{{0.4f, -0.2f, -0.3f}, aim};
    for (const Glove& glove : kGloves) {
        IdViewAxis model;
        const Vec3 got = placedWrist(table, glove, hand, gripAxis, &model);
        IdViewAxis onAim;
        placedWrist(table, glove, hand, aim, &onAim);
        INFO(std::string(glove.weapon), ": wrist (", got.x, " ", got.y, " ", got.z, ")");
        CHECK(evr::length(got - (hand.offset + inFrame(gripAxis, kWrist))) <= kToleranceMetres);
        // The model is moved, not turned: its axis is the one the aim ray gives.
        CHECK(evr::length(model.forward - onAim.forward) <= 1e-6f);
        CHECK(evr::length(model.up - onAim.up) <= 1e-6f);
    }
}

TEST_CASE("the Crucible's row turns the model so the glove holds the hilt as a gun's grip is held") {
    // Its idle holds the hilt in the right fist turned 37 degrees from every gun's grip; nothing on it aims
    // (the swing hands the arm back to the game), so the row turns the model until the glove sits as on the
    // guns, and the hilt stays in the fist as the game holds it.
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    REQUIRE(table.ok());
    const EyeRelativePose grip{};
    IdViewAxis model;
    placedWrist(table, {"weapon/player/crucible", {}}, grip, grip.axis, &model);
    const Vec3 glove[3] = {inFrame(model, kCrucibleGlove[0]), inFrame(model, kCrucibleGlove[1]),
                           inFrame(model, kCrucibleGlove[2])};
    const float turned = largestAxisAngle(glove, kGunGlove);
    INFO("the Crucible's glove from the guns' grip: ", turned, " deg");
    CHECK(turned <= 3.0f);
}

TEST_CASE("a mod's sight row turns the gun about the glove's wrist, not the grip") {
    // The layer's placement for a mod's row (arm::rowPlacement, the right hand): the weapon's offset row,
    // its translation moved so the gun turns about the glove's wrist from the weapon's own sight turn to the
    // mod's. No built-in row is keyed by a mod now, so the Heavy Cannon's rows as they were before its two
    // mods shared one line stand in (DECISIONS T-206).
    const WeaponOffsetTable table = parseWeaponOffsets(R"([standing]
"weapon/player/heavy_cannon" = [-0.270, 0.114, 0.204]
[sights]
"weapon/player/heavy_cannon" = [-3.45, 0.19]
"weapon/player/heavy_cannon_bolt_action" = [-0.15, 0.15]
)");
    REQUIRE(table.ok());
    const Glove& cannon = kGloves[1];
    REQUIRE(std::string(cannon.weapon) == "weapon/player/heavy_cannon");
    const std::optional<evr::game::SightRow> own = table.lookupSight(cannon.weapon, {"", ""});
    const std::optional<evr::game::SightRow> mod =
        table.lookupSight(cannon.weapon, {"", "weapon/player/heavy_cannon_bolt_action"});
    REQUIRE(own.has_value());
    REQUIRE(mod.has_value());
    REQUIRE(mod->key != own->key);
    const IdAngles ownTurn{own->sight.pitch, own->sight.yaw, 0.0f};
    const IdAngles modTurn{mod->sight.pitch, mod->sight.yaw, 0.0f};
    const WeaponOffset o = table.lookup(cannon.weapon, OffsetPosture::Standing);
    const Vec3 rowTranslation{o.forward, o.left, o.up};
    const IdAngles rotation{o.pitch, o.yaw, o.roll};
    const EyeRelativePose grip{};
    const auto wristWith = [&](Vec3 translation, const IdAngles& turn) {
        const EyeRelativePose placed =
            placeViewmodel(grip, grip.axis, translation, rotation, turn, kWrist, 1.0f);
        return placed.offset + inFrame(placed.axis, cannon.wrist);
    };
    const Vec3 withOwn = wristWith(rowTranslation, ownTurn);
    const Vec3 moved = translationKeepingWrist(rowTranslation, rotation, ownTurn, modTurn, kWrist);
    const Vec3 withMod = wristWith(moved, modTurn);
    INFO("wrist with the mod's row (", withMod.x, " ", withMod.y, " ", withMod.z, ")");
    CHECK(evr::length(withMod - withOwn) <= 0.001f);
    // Turned about the grip with the row's own translation, it would sit about 5 mm off.
    CHECK(evr::length(wristWith(rowTranslation, modTurn) - withOwn) > 0.003f);
}

TEST_CASE("the Heavy Cannon sits where the Precision Bolt's rig-checked placement put it, with every mod") {
    // Before the two mods shared one line the Bolt's scope row turned the gun about the glove's wrist from
    // the irons' turn: that placement was checked on the hands rig (docs/rig-findings/hands-rig.md, section
    // 18: the scope 0.17 degrees from the ray, the glove's wrist 0.1 / 0.3 cm from its target). With no mod
    // and with the Micro Missiles the gun now sits there too: the same turn, the glove within a millimetre.
    const IdAngles ironsTurn{-3.45f, 0.19f, 0.0f};
    const IdAngles scopeTurn{-0.15f, 0.15f, 0.0f};
    const IdAngles noRotation{};
    const EyeRelativePose grip{};
    const Glove& cannon = kGloves[1];
    REQUIRE(std::string(cannon.weapon) == "weapon/player/heavy_cannon");
    const Vec3 boltTranslation =
        translationKeepingWrist({-0.270f, 0.114f, 0.204f}, noRotation, ironsTurn, scopeTurn, kWrist);
    const EyeRelativePose bolt =
        placeViewmodel(grip, grip.axis, boltTranslation, noRotation, scopeTurn, kWrist, 1.0f);
    const Vec3 boltWrist = bolt.offset + inFrame(bolt.axis, cannon.wrist);

    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    REQUIRE(table.ok());
    const WeaponOffset o = table.lookup(cannon.weapon, OffsetPosture::Standing);
    const std::optional<evr::game::SightRow> own = table.lookupSight(cannon.weapon, {"", ""});
    REQUIRE(own.has_value());
    const IdAngles ownTurn{own->sight.pitch, own->sight.yaw, 0.0f};
    for (const evr::game::WeaponModDecls& mods :
         {evr::game::WeaponModDecls{"", ""},
          evr::game::WeaponModDecls{"", "weapon/player/heavy_cannon_bolt_action"},
          evr::game::WeaponModDecls{"", "weapon/player/heavy_cannon_burst_detonate"},
          evr::game::WeaponModDecls{"", "weapon/player/heavy_cannon_burst_detonate_mastery"}}) {
        INFO(mods[1]);
        const std::optional<evr::game::SightRow> row = table.lookupSight(cannon.weapon, mods);
        REQUIRE(row.has_value());
        const IdAngles turn{row->sight.pitch, row->sight.yaw, 0.0f};
        const IdAngles rotation{o.pitch, o.yaw, o.roll};
        const Vec3 translation =
            translationKeepingWrist({o.forward, o.left, o.up}, rotation, ownTurn, turn, kWrist);
        const EyeRelativePose placed =
            placeViewmodel(grip, grip.axis, translation, rotation, turn, kWrist, 1.0f);
        const Vec3 wrist = placed.offset + inFrame(placed.axis, cannon.wrist);
        INFO("forward (", placed.axis.forward.x, " ", placed.axis.forward.y, " ", placed.axis.forward.z, ")");
        CHECK(evr::length(placed.axis.forward - bolt.axis.forward) <= 1e-4f);
        CHECK(evr::length(placed.axis.up - bolt.axis.up) <= 1e-4f);
        CHECK(evr::length(wrist - boltWrist) <= 0.001f);
    }
}
