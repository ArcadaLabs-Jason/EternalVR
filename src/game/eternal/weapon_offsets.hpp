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
//
// Keys are the inventory decl names of the held item. A decl whose exact name has no entry uses the
// longest key it starts with followed by '_' ("weapon/player/shotgun_secondary_full_auto" uses
// "weapon/player/shotgun"), then "default", then no offset. Seated, the [seated] entry for the weapon
// wins, then the seated "default", then the standing rules. There is no file IO here.

#include <map>
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

enum class OffsetPosture : unsigned char {
    Standing,
    Seated,
};

struct WeaponOffsetTable {
    std::map<std::string, WeaponOffset, std::less<>> standing;
    std::map<std::string, WeaponOffset, std::less<>> seated;
    std::vector<std::string> issues; // one line per problem, with its line number

    [[nodiscard]] bool ok() const { return issues.empty(); }
    // The offset for the held item `declName` (rules above).
    [[nodiscard]] WeaponOffset lookup(std::string_view declName, OffsetPosture posture) const;
};

// Reads the table. Lines with problems are reported and skipped; the rest are kept. Values must be six
// finite numbers, translations within 2 m and angles within 180 degrees.
WeaponOffsetTable parseWeaponOffsets(std::string_view text);

// The built-in table's text (data/weapons/viewmodel_offsets.toml).
std::string_view builtinWeaponOffsets();

// "f,l,u" or "f,l,u,pitch,yaw,roll" (the ETERNALVR_VIEWMODEL_OFFSET form); false for anything else.
bool parseOffsetList(std::string_view text, WeaponOffset& out);

} // namespace evr::game
