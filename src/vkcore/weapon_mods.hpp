#pragma once

// The held weapon's active mods: the decl each fire mode's mod puts in idWeapon::weaponModOverrides, which
// names the mod ("weapon/player/heavy_cannon_bolt_action" while the Heavy Cannon's Precision Bolt is active,
// "weapon/player/heavy_cannon_burst_detonate" with its Micro Missiles). The held item's decl stays the base
// weapon's whichever mod is active. Read by the viewmodel hook for the weapon sights (a [sights] row keyed by
// a mod, game/eternal/weapon_offsets.hpp) and by the zoom look (zoom_look.cpp); the viewmodel hook notes what
// it applied for the hands rig's dump.
//
// Type info: idHands, idHandsItem, idInventoryItem, idWeapon and idWeapon::weaponModOverrides_t have the same
// size and every field the same offset in the type info tables of the Steam build 25216728 and the Game Pass
// build 1.0.56.0, so these are read on either build.

#include "game/eternal/weapon_offsets.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace evr::vkcore::controllers {

// idHands::rightItem (0x29B0) .item (idHandsItem 0x30, an idManagedClassPtr of 0x20 bytes): its first word is
// a handle and the object pointer the second (rig: that word's idInventoryItem names the held decl); the
// object is used only when its decl is the held one.
inline constexpr std::size_t kHandsItemObject = 0x29B0 + 0x30 + 0x8;
inline constexpr std::size_t kInventoryItemDecl = 0x38;       // idInventoryItem::decl
inline constexpr std::size_t kWeaponModOverrides = 0x3328;    // idWeapon::weaponModOverrides[2]
inline constexpr std::size_t kWeaponModOverridesSize = 0x3D8; // idWeapon::weaponModOverrides_t
inline constexpr std::size_t kModOverrideDecl = 0x8;          // weaponModOverrides_t::weaponDecl

// One decl per fire mode, null for none.
using ModDecls = std::array<const std::byte*, 2>;

// The right hand's item object of `hands` when its decl is `decl`, else null (no object, an unreadable one,
// or one naming another decl).
const std::byte* heldItemObject(const std::byte* hands, const std::byte* decl);

// The mod decls of the weapon object `weapon` (weaponModOverrides[i].weaponDecl); nulls for a null weapon or
// an unreadable field.
ModDecls weaponModDecls(const std::byte* weapon);

// The viewmodel hook, when the held item or its mods change: the [sights] row in force for the held item
// `name` with its active mods' decl names `mods` ("" for none; game::WeaponOffsetTable::lookupSight), and the
// weapon's own row (the one its offset row is measured with; arm::rowPlacement turns a mod's row about the
// glove's wrist); none with ETERNALVR_WEAPON_SIGHTS=0. Noted for the hands rig's dump.
struct SightRows {
    std::optional<game::SightRow> row;
    std::optional<game::SightRow> own;
};
SightRows sightRowsFor(const std::string& name, const std::array<std::string, 2>& mods);

// What sightRowsFor last found: the mods' decl names and the row's key ("" for none).
struct SightRowNote {
    std::array<std::string, 2> mods;
    std::string row;
};
// The last note (empty before the first).
SightRowNote sightRowNote();

} // namespace evr::vkcore::controllers
