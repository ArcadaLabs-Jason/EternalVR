// The built-in [left_hand_wrist] rows against the game's own grips as the hands rig read them
// (data/weapons/viewmodel_offsets.toml; features/arm/left_hand.hpp).

#include "features/arm/arm_frames.hpp"
#include "features/arm/left_hand.hpp"
#include "game/eternal/weapon_offsets.hpp"

#include <doctest/doctest.h>

#include <array>
#include <optional>

using evr::Vec3;
using evr::arm::compose;
using evr::arm::GripPose;
using evr::arm::LeftWristOnGun;
using evr::arm::mirroredGrip;
using evr::arm::relative;
using evr::game::builtinWeaponOffsets;
using evr::game::parseWeaponOffsets;
using evr::game::WeaponOffsetTable;
using evr::xr_math::IdViewAxis;
using evr::xr_math::ModelPose;

namespace {

// A pose as the hands rig's arm dump writes it: the position, then the axis rows forward, left, up (model
// space; the rig's world scale gives 1 game unit per metre).
ModelPose dumped(const std::array<float, 12>& p) {
    return {{p[0], p[1], p[2]}, IdViewAxis{{p[3], p[4], p[5]}, {p[6], p[7], p[8]}, {p[9], p[10], p[11]}}};
}

struct Sample {
    const char* item;
    std::array<float, 12> root;  // the gun's root (righthandattach) as read
    std::array<float, 12> wrist; // the game's right glove's wrist (RightHand), its own pose of the idle
};

// One settled line per gun of a left-handed sweep with the game's off hand (the arms posed by the layer at
// full weight, the scripted input held), on a level's idle.
constexpr std::array<Sample, 3> kSamples{{
    {"weapon/player/shotgun",
     {0.27277f, -0.11481f, -0.13939f, 0.99931f, 0.03716f, 0.00069f, -0.03709f, 0.99826f, -0.04578f, -0.00239f,
      0.04572f, 0.99895f},
     {0.10413f, -0.16915f, -0.22306f, 0.28704f, 0.09342f, -0.95335f, -0.94663f, -0.12467f, -0.29723f,
      -0.14663f, 0.98779f, 0.05265f}},
    {"weapon/player/heavy_cannon",
     {0.36032f, -0.10763f, -0.13695f, 1.0f, 0.00081f, -0.00087f, -0.00082f, 0.99988f, -0.01544f, 0.00086f,
      0.01545f, 0.99988f},
     {0.1901f, -0.14855f, -0.20997f, 0.28873f, 0.11182f, -0.95086f, -0.95101f, -0.08114f, -0.29831f,
      -0.11051f, 0.99041f, 0.08292f}},
    {"weapon/player/bfg",
     {0.45183f, -0.08931f, -0.29091f, 1.0f, -0.00004f, -0.00056f, 0.00005f, 0.99991f, 0.01325f, 0.00056f,
      -0.01325f, 0.99991f},
     {0.27536f, -0.12772f, -0.38368f, 0.28913f, 0.13881f, -0.94717f, -0.95099f, -0.07172f, -0.3008f,
      -0.10968f, 0.98772f, 0.11127f}},
}};

} // namespace

TEST_CASE("each gun's left-hand wrist row is where the game's right grip, reflected, puts the left glove") {
    // The row stands in for the wrist the read grip gives until it is read (LeftWristOnGun), so the model
    // steps by their distance as the grip replaces it: within the idle's sway of a few millimetres.
    const WeaponOffsetTable table = parseWeaponOffsets(builtinWeaponOffsets());
    constexpr float kMetres = 1.0f;
    for (const Sample& s : kSamples) {
        INFO(s.item);
        const auto row = table.lookupLeftHandWrist(s.item);
        REQUIRE(row.has_value());
        const ModelPose root = dumped(s.root);
        GripPose right;
        right.wrist = relative(root, dumped(s.wrist));
        const GripPose left = mirroredGrip(right);
        LeftWristOnGun onGun;
        const Vec3 rowPoint{(*row)[0], (*row)[1], (*row)[2]};
        // Before the grip is read: the row.
        const std::optional<Vec3> before = onGun.update(&s, std::nullopt, std::nullopt, rowPoint, kMetres);
        REQUIRE(before.has_value());
        // Read: the grip's wrist on the gun.
        const std::optional<Vec3> read = onGun.update(&s, root, left, std::nullopt, kMetres);
        REQUIRE(read.has_value());
        CHECK(length(*read - compose(root, left.wrist).position) < 1e-6f);
        CHECK(length(*read - *before) < 0.003f);
    }
}
