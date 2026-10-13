#pragma once

// Per-weapon viewmodel offsets (T-054): where the game's arms-and-weapon model sits relative to the
// tracked controller.
//
// The game authors each viewmodel relative to the eye: the model's origin is the camera, and the gun sits
// ahead, right and below it. Placed at the controller, the model's origin must therefore be moved back by
// that authored offset, which differs per weapon. An offset is given in the weapon frame, the controller's
// aim frame in id Tech axes (forward, left, up; metres), plus a rotation (pitch, yaw, roll; degrees,
// id Tech convention: pitch positive down, yaw counter-clockwise).
//
// The table is data (data/weapons/viewmodel_offsets.toml, built in; a player's copy reads the same way):
//
//   [standing]
//   "default" = [-0.25, 0.18, 0.22, 0, 0, 0]
//   "weapon/player/shotgun" = [-0.24, 0.17, 0.21, 0, 0, 0]
//   [seated]
//   "default" = [...]          desk-safe entries (T-074)
//   [sights]
//   "weapon/player/shotgun" = [0.21, 2.17]   the sight line in the model's frame (pitch, yaw; degrees)
//
// Keys are the inventory decl names of the held item. That is the base weapon's decl: a weapon mod does not
// change it, so one row covers every mod of a weapon. A decl whose exact name has no entry uses the longest
// key it starts with followed by '_' ("weapon/player/fists_doom5melee" uses "weapon/player/fists"), then
// "default", then no offset. Seated, the [seated] entry for the weapon wins, then the seated "default",
// then the standing rules.
//
// [sights] (ETERNALVR_WEAPON_SIGHTS): the direction of the weapon's sight line in the arms model's frame
// as the game animates it at idle (pitch positive down, yaw counter-clockwise, degrees). The layer turns the
// model so that line runs parallel to the aim ray. Same key rules, but no "default": a weapon without a row
// is not turned. There is no file IO here.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evr::game {

struct WeaponOffset {
    float forward = 0.0f;
    float left = 0.0f;
    float up = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;

    friend bool operator==(const WeaponOffset&, const WeaponOffset&) = default;
};

// A weapon's sight line in the arms model's frame (degrees, id Tech convention: pitch positive down).
struct WeaponSight {
    float pitch = 0.0f;
    float yaw = 0.0f;

    friend bool operator==(const WeaponSight&, const WeaponSight&) = default;
};

enum class OffsetPosture : unsigned char {
    Standing,
    Seated,
};

struct WeaponOffsetTable {
    std::map<std::string, WeaponOffset, std::less<>> standing;
    std::map<std::string, WeaponOffset, std::less<>> seated;
    std::map<std::string, WeaponSight, std::less<>> sights;
    std::vector<std::string> issues; // one line per problem, with its line number

    [[nodiscard]] bool ok() const { return issues.empty(); }
    // The offset for the held item `declName` (rules above).
    [[nodiscard]] WeaponOffset lookup(std::string_view declName, OffsetPosture posture) const;
    // The sight line of the held item `declName`, or nullopt for a weapon without a row (rules above).
    [[nodiscard]] std::optional<WeaponSight> lookupSight(std::string_view declName) const;
};

// Reads the table. Lines with problems are reported and skipped; the rest are kept. Offsets must be three
// or six finite numbers, translations within 2 m and angles within 180 degrees; sights two angles within
// 45 degrees.
WeaponOffsetTable parseWeaponOffsets(std::string_view text);

// The built-in table's text (data/weapons/viewmodel_offsets.toml).
std::string_view builtinWeaponOffsets();

// "f,l,u" or "f,l,u,pitch,yaw,roll" (the ETERNALVR_VIEWMODEL_OFFSET form); false for anything else.
bool parseOffsetList(std::string_view text, WeaponOffset& out);

} // namespace evr::game
