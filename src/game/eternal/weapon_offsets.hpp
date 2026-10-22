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
//   "default" = [...]          rows used while seated (the built-in table has none)
//   [sights]
//   "weapon/player/shotgun" = [0.21, 2.17]   the sight line in the model's frame (pitch, yaw; degrees)
//
// Keys are the inventory decl names of the held item. That is the base weapon's decl: a weapon mod does not
// change it, so one row covers every mod of a weapon (a [sights] row may name a mod, below). A decl whose
// exact name has no entry uses the longest key it starts with followed by '_'
// ("weapon/player/fists_doom5melee" uses "weapon/player/fists"), then "default", then no offset. Seated, the
// [seated] entry for the weapon wins, then the seated "default", then the standing rules.
//
// [sights] (on by default, T-135; ETERNALVR_WEAPON_SIGHTS=0 turns them off on the rig): the direction of
// the weapon's sight line in the arms model's frame as the game animates it at idle (pitch positive down,
// yaw counter-clockwise, degrees). The layer turns the model so that line runs parallel to the aim ray. Same
// key rules, but no "default": a weapon without a row is not turned. A weapon mod that changes the sight
// line (the Heavy Cannon's Precision Bolt draws a scope in place of the iron sights) has a row keyed by the
// mod's own weapon decl, which the held weapon names while that mod is active (idWeapon's mod overrides):
// "weapon/player/heavy_cannon_bolt_action". Such a row wins over the weapon's while its mod is active.
//
// [right_grip]: the weapons whose idle animations hold them by a handle turned well away from how a hand
// holds a controller (the chaingun's and the ballista's rear top handle), and the game grip the weapon arm
// puts the glove in instead ("heavy_cannon": the Heavy Cannon's idle hand, which the chaingun's zoom and
// reload use too), as `"name"` or, with the grip's wrist moved on this gun, `["name", forward, left, up]`.
// Same key rules, no "default": a weapon without a row keeps the glove where the game animates it.
//
// [left_handed_off_hand]: with the weapon in the left hand and the game's off hand (ETERNALVR_OFFHAND=game),
// the right glove holds the gun in the game's left grip reflected across the gun's middle; on a weapon whose
// left grip holds a part on one side only (the chaingun's front handle) the reflected fist closes on nothing.
// `"controller"`: on that weapon the right glove follows the right controller instead, as with the free off
// hand. Same key rules, no "default". There is no file IO here.

#include <array>
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

// The held weapon's active mod decls: the decl names of idWeapon::weaponModOverrides[0] and [1] (one per fire
// mode), "" for none.
using WeaponModDecls = std::array<std::string_view, 2>;

// The [sights] row in force and the key it was found under.
struct SightRow {
    WeaponSight sight;
    std::string_view key;

    friend bool operator==(const SightRow&, const SightRow&) = default;
};

// The game grips the right glove can be put in (features/arm/grip_hand.hpp).
enum class GameGrip : unsigned char {
    HeavyCannon,
};

// A weapon's [right_grip] row: the game grip, and where its wrist sits on this gun (metres, in the weapon's
// root joint's frame: forward, left, up) when the gun's grip is not where the grip's own gun has it.
struct RightGrip {
    GameGrip grip = GameGrip::HeavyCannon;
    std::optional<std::array<float, 3>> wrist;

    friend bool operator==(const RightGrip&, const RightGrip&) = default;
};

// A weapon's [left_handed_off_hand] row: what the right glove does instead of the reflected left grip.
enum class LeftOffHand : unsigned char {
    Controller, // it follows the right controller
};

enum class OffsetPosture : unsigned char {
    Standing,
    Seated,
};

// The right glove's wrist that the built-in rows put at the controller (metres, the grip's forward, left,
// up): the off hand's default wrist offset (input::kDefaultOffhandOffset) mirrored for the right hand, 8 cm
// behind the grip's centre and 3.5 cm toward the back of the hand. The layer keeps it fixed in the grip's
// frame.
inline constexpr WeaponOffset kRowsWrist{-0.08f, -0.035f, 0.0f, 0.0f, 0.0f, 0.0f};

struct WeaponOffsetTable {
    std::map<std::string, WeaponOffset, std::less<>> standing;
    std::map<std::string, WeaponOffset, std::less<>> seated;
    std::map<std::string, WeaponSight, std::less<>> sights;
    std::map<std::string, RightGrip, std::less<>> rightGrips;
    std::map<std::string, LeftOffHand, std::less<>> leftOffHands;
    std::vector<std::string> issues; // one line per problem, with its line number

    [[nodiscard]] bool ok() const { return issues.empty(); }
    // The offset for the held item `declName` (rules above).
    [[nodiscard]] WeaponOffset lookup(std::string_view declName, OffsetPosture posture) const;
    // The sight line of the held item `declName`, or nullopt for a weapon without a row (rules above).
    [[nodiscard]] std::optional<WeaponSight> lookupSight(std::string_view declName) const;
    // The sight row of the held item `declName` with its active mod decls `mods`: a row keyed by an active
    // mod's decl (a key longer than `declName` that the mod's name matches, the mod's name being the held
    // decl's followed by '_') wins over the held decl's own row; the longest such key when both mods have
    // one. nullopt when neither has a row.
    [[nodiscard]] std::optional<SightRow> lookupSight(std::string_view declName,
                                                      const WeaponModDecls& mods) const;
    // The grip the right glove is put in for `declName`, or nullopt for a weapon without a row (rules above).
    [[nodiscard]] std::optional<RightGrip> lookupRightGrip(std::string_view declName) const;
    // The [left_handed_off_hand] row for `declName`, or nullopt for a weapon without one (rules above).
    [[nodiscard]] std::optional<LeftOffHand> lookupLeftOffHand(std::string_view declName) const;
};

// Reads the table. Lines with problems are reported and skipped; the rest are kept. Offsets must be three
// or six finite numbers, translations within 2 m and angles within 180 degrees; sights two angles within
// 45 degrees; right grips a quoted grip name ("heavy_cannon"), alone or first in a list with three numbers
// within 1 m; left-handed off hands "controller".
WeaponOffsetTable parseWeaponOffsets(std::string_view text);

// The built-in table's text (data/weapons/viewmodel_offsets.toml).
std::string_view builtinWeaponOffsets();

// "f,l,u" or "f,l,u,pitch,yaw,roll" (the ETERNALVR_VIEWMODEL_OFFSET form); false for anything else.
bool parseOffsetList(std::string_view text, WeaponOffset& out);

} // namespace evr::game
