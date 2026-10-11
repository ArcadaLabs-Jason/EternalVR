// bHaptics suits and sleeves: ammo picked up (bhaptics_game.cpp, docs/BHAPTICS.md, public issue #12).
//
// The player's ammo counts change with every shot and weapon change, and the damage feedback's pickUpBuffer
// (the pickup sounds, idDamageFeedbackComponent +0x7E8) is written only for the other players of a match:
// its AddPickUp (RVA 0x1269020 in Steam build 25216728) plays the sound at once for the local player and
// keeps the buffer for the network (none of its lines in single-player rig runs with health, armor and ammo
// pickups). So two mid hooks on idProp2's own paths note what the player picks up, both at the entry of the
// function that plays a picked-up prop's sounds through its use component (rcx = the prop, rdx = its user):
// - RVA 0xD08C10, from the prop's use (RVA 0xD06950, after its use component's Use returned true at
//   0xD06AB5), which the touch handlers (RVA 0xD05B90, 0xD06020) and the end of the prop's magnetization
//   (0xD059BF in RVA 0xD055E0) call: a pickup walked over (the shells, health and armor in Atlantica on the
//   rig) and, by that path, drops that fly to the player;
// - RVA 0xD08D80, from idProp2's vtable slots 279 (RVA 0xD0C060) and 289 (RVA 0xD0C4B0), which player code
//   calls (RVA 0x1432B60, 0x13F0820): an explicit interact. Not seen on the rig yet.
// What the prop gives tells ammo (bhaptics::isAmmoPickup): idProp2::useableComponentDecl (+0x1BE8) is an
// idDeclProp_ItemPickupComponent whose inventoryDecl (+0x130) is an idDeclAmmo (on the rig:
// 'propitem/ammo/shotgun_10' giving 'ammo/sharedammopool/shells'), or an idDeclProp_UniversalAmmoComponent.
// Health and armor props are idDeclProp_HealthComponent. Field offsets from the type-info tables of that
// build; the hooks go in only with bHaptics on, which needs the build PlayerAim confirmed. Each only notes.

#include "vkcore/controllers_impl.hpp"

#include "features/bhaptics/ammo.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "bhaptics";

// Both copies: push rbp; push rsi|rdi; sub rsp, 0x38; cmp qword [rcx+0x1BF0], 0 (the use component); ...
constexpr const char* kUsedSignature =
    "40 55 56 48 83 EC 38 48 83 B9 F0 1B 00 00 00 48 8B EA 48 8B F1 0F 84 ?? ?? ?? ?? 48 89 5C 24 58 48 8B "
    "CA 48 89 7C 24 30 E8";
constexpr const char* kInteractSignature =
    "40 55 57 48 83 EC 38 48 83 B9 F0 1B 00 00 00 48 8B EA 48 8B F9 0F 84 ?? ?? ?? ?? 48 89 5C 24 58 48 8B "
    "CA 48 89 74 24 30 E8";

constexpr std::size_t kEntityName = 0x40;
constexpr std::size_t kPropUseComponentDecl = 0x1BE8;
constexpr std::size_t kItemPickupInventoryDecl = 0x130;
constexpr std::string_view kItemPickupClass = ".?AVidDeclProp_ItemPickupComponent@@";
constexpr int kLogged = 60;

GameImage g_image;
std::atomic<int> g_logged{0};

// The MSVC RTTI name of a game object's class (".?AVidDeclAmmo@@"), or empty.
std::string_view className(const std::byte* object) {
    const std::byte* vtable = nullptr;
    return object && safeRead(object, vtable) ? rttiName(g_image, vtable) : std::string_view{};
}

void onPickedUp(const HookRegisters& regs, const char* how) {
    if (!mp_guard::allowsGameTouch() || !isPlayerSafe(reinterpret_cast<const std::byte*>(regs.rdx))) {
        return; // a demon, or nobody
    }
    const auto* prop = reinterpret_cast<const std::byte*>(regs.rcx);
    const std::byte* component = nullptr;
    if (!safeRead(prop + kPropUseComponentDecl, component)) {
        component = nullptr;
    }
    const std::string_view componentClass = className(component);
    const std::byte* item = nullptr;
    if (componentClass != kItemPickupClass || !safeRead(component + kItemPickupInventoryDecl, item)) {
        item = nullptr;
    }
    const std::string_view itemClass = className(item);
    const bool ammo = bhaptics::isAmmoPickup(componentClass, itemClass);
    if (ammo) {
        noteBhapticsAmmo();
    }
    if (g_logged.fetch_add(1, std::memory_order_relaxed) < kLogged) {
        const std::string componentName = component ? itemDeclName(component) : std::string{};
        const std::string itemName = item ? itemDeclName(item) : std::string{};
        EVR_LOG("%s: picked up (%s) '%s': %.*s '%s', gives %.*s '%s': %s", kTag, how,
                itemDeclName(prop + kEntityName).c_str(), static_cast<int>(componentClass.size()),
                componentClass.data(), componentName.c_str(), static_cast<int>(itemClass.size()),
                itemClass.data(), itemName.c_str(), ammo ? "ammo" : "not ammo");
    }
}

void onUsed(const HookRegisters& regs) {
    onPickedUp(regs, "used");
}

void onInteract(const HookRegisters& regs) {
    onPickedUp(regs, "used (interact)");
}

bool install(const char* what, const char* signature, MidHookCallback callback) {
    const std::byte* site = findUnique(g_image, kTag, what, signature);
    if (!site) {
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, what, g_image.rva(site), error.c_str());
        return false;
    }
    EVR_LOG("%s: %s hook at RVA 0x%X", kTag, what, g_image.rva(site));
    return true;
}

} // namespace

bool installBhapticsAmmoHooks() {
    if (!locateGameImage(g_image, kTag)) {
        return false;
    }
    const bool used = install("prop used", kUsedSignature, &onUsed);
    const bool interact = install("prop interact", kInteractSignature, &onInteract);
    return used || interact;
}

} // namespace evr::vkcore::controllers
