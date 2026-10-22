// The held weapon's active mods (weapon_mods.hpp).

#include "vkcore/weapon_mods.hpp"

#include "vkcore/controllers_impl.hpp"

#include <mutex>
#include <string_view>

namespace evr::vkcore::controllers {

namespace {

std::mutex g_noteMutex;
SightRowNote g_note; // under g_noteMutex

} // namespace

const std::byte* heldItemObject(const std::byte* hands, const std::byte* decl) {
    const std::byte* object = nullptr;
    const std::byte* objectDecl = nullptr;
    if (!hands || !decl || !safeRead(hands + kHandsItemObject, object) || !object ||
        !safeRead(object + kInventoryItemDecl, objectDecl) || objectDecl != decl) {
        return nullptr;
    }
    return object;
}

ModDecls weaponModDecls(const std::byte* weapon) {
    ModDecls decls{};
    if (!weapon) {
        return decls;
    }
    for (std::size_t i = 0; i < decls.size(); ++i) {
        const std::byte* decl = nullptr;
        if (safeRead(weapon + kWeaponModOverrides + i * kWeaponModOverridesSize + kModOverrideDecl, decl)) {
            decls[i] = decl;
        }
    }
    return decls;
}

SightRows sightRowsFor(const std::string& name, const std::array<std::string, 2>& mods) {
    const game::WeaponOffsetTable& table = state().offsets;
    SightRows rows;
    if (settings().weaponSights) {
        rows.row = table.lookupSight(name, {mods[0], mods[1]});
        rows.own = table.lookupSight(name, {});
    }
    std::lock_guard lock(g_noteMutex);
    g_note.mods = mods;
    g_note.row = rows.row ? std::string(rows.row->key) : std::string{};
    return rows;
}

SightRowNote sightRowNote() {
    std::lock_guard lock(g_noteMutex);
    return g_note;
}

} // namespace evr::vkcore::controllers
