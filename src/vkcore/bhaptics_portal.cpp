// bHaptics suits and sleeves: going through a portal (bhaptics_game.cpp, docs/BHAPTICS.md).
//
// Nothing the camera hook reads tells a portal from a respawn, a checkpoint or the recovery after a fall
// (idPlayer::teleportCount rises for all of them), so hooks on the game's own paths note the portals:
// - idTrigger_Teleporter::TriggerStuff_Impl (RVA 0xDB2320 in Steam build 25216728, its name from its own
//   assert): every trigger teleport, pads and in-map portals, and the _Fade kind (the Slayer Gates, the
//   Fortress's portals), which calls it at the moment of the teleport after its fade-out. rcx = the trigger,
//   rdx = the activator. The same classes put the player back after a fall, so a _Fade trigger with a damage
//   decl (+0xE20) or the falling stinger for its fade-out sound (fadeOut.fadeSound, +0xDD0 + 0x18, an
//   idSoundEvent with its name at +0x8) is not a portal (bhaptics::teleportKindOf), except in the Fortress
//   of Doom, whose teleporters all fade out to the falling stinger with no damage decl (the Ripatorium's way
//   back and the others, on the rig); the map is the last one mp_guard saw load. The entity's name is the
//   idStr at +0x40 (its text at +0x8). The mid hook sits after the activator's null check (+9), where rcx and
//   rdx still hold both.
// - idTarget_LevelTransition's activate (RVA 0xD720D0): the level exits and the Fortress's mission launches,
//   which are not teleports. It sets `activated` (+0xC28); only a call that finds it clear counts.
// - The player's teleport (RVA 0x13F1AB0, under idPlayer::Teleport, vtable slot 284), for fast travel from
//   the Dossier's map, which goes through none of the above: the map's choice starts the fast travel (an
//   event handler, RVA 0x13E2E30), which sets idPlayer::fastTravelDestinationWarpPointName (an idStr at
//   +0x4D098: text +0x8, length +0x10) and posts the "teleport" event half a second later; that teleport
//   finds the name set (the game's own test there plays the warp's loop sound and posts postFastTravel), and
//   endFastTravel clears it. Every other teleport of the player (pads, respawns, checkpoints, loads) finds it
//   empty. rcx = the player at the function's entry.
// The field offsets are from the type-info tables of that build (tmp-vr research bhaptics-crystal-portal.md),
// so the hooks go in only with bHaptics on, which needs the build PlayerAim confirmed. Each only notes.

#include "vkcore/controllers_impl.hpp"

#include "features/bhaptics/body_haptics.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "bhaptics";

// TriggerStuff_Impl: test rdx, rdx; jz; mov r11, rsp; push rbp; push rsi; push r14; lea rbp, [r11-0x218];
// sub rsp, 0x300. Its override in the _Fade class starts the same way but differs after the jz.
constexpr const char* kTeleportSignature =
    "48 85 D2 0F 84 ?? ?? ?? ?? 4C 8B DC 55 56 41 56 49 8D AB E8 FD FF FF 48 81 EC 00 03 00 00";
constexpr std::size_t kTeleportHook = 9; // mov r11, rsp: after the null check, before the pushes
// idTarget_LevelTransition's activate: its prologue, the stack cookie, then cmp byte [rcx+0xC28], 0.
constexpr const char* kLevelExitSignature =
    "40 55 56 41 55 48 8D AC 24 60 F6 FF FF 48 81 EC A0 0A 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 50 "
    "09 00 00 80 B9 28 0C 00 00 00";
// The player's teleport: its prologue, the stack cookie, then the new origin stored at +0x2F68C.
constexpr const char* kPlayerTeleportSignature =
    "40 53 55 56 57 41 56 48 83 EC 70 0F 29 74 24 60 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 44 24 58 F2 0F 10 "
    "02 4D 8B F1 F2 0F 11 81 8C F6 02 00";

constexpr std::size_t kEntityName = 0x40;
constexpr std::size_t kFadeSound = 0xDD0 + 0x18;
constexpr std::size_t kFadeDamageDecl = 0xE20;
constexpr std::size_t kLevelExitActivated = 0xC28;
constexpr std::size_t kPlayerFastTravelText = 0x4D098 + 0x8;
constexpr std::size_t kPlayerFastTravelLength = 0x4D098 + 0x10;
// The player's teleport tests the fast travel's name itself, 0x283 bytes in: cmp [rsi+0x4D0A8], ebx (both
// builds). The hook goes in only when that test is there, so the name's offsets above hold.
constexpr std::size_t kFastTravelTestAt = 0x283;
constexpr unsigned char kFastTravelTest[] = {0x39, 0x9E, 0xA8, 0xD0, 0x04, 0x00};
constexpr int kLogged = 40;
constexpr int kFastTravelLogged = 20;

GameImage g_image;
std::atomic<int> g_logged{0};
std::atomic<int> g_fastTravelLogged{0}; // a count of its own: the teleports before it never use it up

bool logNext() {
    return g_logged.fetch_add(1, std::memory_order_relaxed) < kLogged;
}

void onTeleport(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto* trigger = reinterpret_cast<const std::byte*>(regs.rcx);
    if (!isPlayerSafe(reinterpret_cast<const std::byte*>(regs.rdx))) {
        return; // a demon or an object
    }
    const std::byte* vtable = nullptr;
    const std::string_view type = safeRead(trigger, vtable) ? rttiName(g_image, vtable) : std::string_view{};
    const bool fade = type.find("Teleporter_Fade") != std::string_view::npos;
    const std::string name = itemDeclName(trigger + kEntityName);
    std::string sound;
    bool damage = false;
    if (fade) {
        const std::byte* event = nullptr;
        const std::byte* decl = nullptr;
        if (safeRead(trigger + kFadeSound, event) && event) {
            sound = itemDeclName(event);
        }
        damage = safeRead(trigger + kFadeDamageDecl, decl) && decl != nullptr;
    }
    const bool fortress = bhaptics::isFortressMap(mp_guard::lastMapLoaded());
    const bhaptics::TeleportKind kind = bhaptics::teleportKindOf(fade, damage, sound, name, fortress);
    if (kind == bhaptics::TeleportKind::Portal) {
        noteBhapticsPortal();
    }
    if (logNext()) {
        EVR_LOG("%s: teleport '%s' (%s, fade sound '%s', damage %d%s): %s", kTag, name.c_str(),
                fade ? "fade" : "plain", sound.c_str(), damage ? 1 : 0, fortress ? ", the Fortress" : "",
                bhaptics::teleportKindName(kind));
    }
}

void onLevelExit(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto* target = reinterpret_cast<const std::byte*>(regs.rcx);
    std::uint8_t activated = 1;
    if (!safeRead(target + kLevelExitActivated, activated) || activated != 0) {
        return;
    }
    noteBhapticsPortal();
    if (logNext()) {
        EVR_LOG("%s: level exit '%s': portal", kTag, itemDeclName(target + kEntityName).c_str());
    }
}

void onPlayerTeleport(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const auto* player = reinterpret_cast<const std::byte*>(regs.rcx);
    std::int32_t length = 0;
    if (!isPlayerSafe(player) || !safeRead(player + kPlayerFastTravelLength, length) || length <= 0) {
        return; // not a fast travel's teleport
    }
    noteBhapticsPortal();
    if (g_fastTravelLogged.fetch_add(1, std::memory_order_relaxed) < kFastTravelLogged) {
        const char* text = nullptr;
        char warp[96] = {};
        if (safeRead(player + kPlayerFastTravelText, text) && text) {
            safeCopy(warp, text, static_cast<std::size_t>(std::min<std::int32_t>(length, sizeof(warp) - 1)));
        }
        EVR_LOG("%s: fast travel to '%s': portal", kTag, warp);
    }
}

bool install(const char* what,
             const char* signature,
             std::size_t offset,
             MidHookCallback callback,
             std::span<const unsigned char> anchor = {},
             std::size_t anchorAt = 0) {
    const std::byte* site = findUnique(g_image, kTag, what, signature);
    if (!site) {
        return false;
    }
    if (!anchor.empty() && std::memcmp(site + anchorAt, anchor.data(), anchor.size()) != 0) {
        EVR_LOG("%s: %s: the code at RVA 0x%X is not the one expected; not hooked", kTag, what,
                g_image.rva(site + anchorAt));
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site + offset), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, what, g_image.rva(site + offset), error.c_str());
        return false;
    }
    EVR_LOG("%s: %s hook at RVA 0x%X", kTag, what, g_image.rva(site + offset));
    return true;
}

} // namespace

bool installBhapticsPortalHooks() {
    if (!locateGameImage(g_image, kTag)) {
        return false;
    }
    const bool teleport = install("teleport", kTeleportSignature, kTeleportHook, &onTeleport);
    const bool levelExit = install("level exit", kLevelExitSignature, 0, &onLevelExit);
    if (!install("fast travel", kPlayerTeleportSignature, 0, &onPlayerTeleport, kFastTravelTest,
                 kFastTravelTestAt)) {
        EVR_LOG("%s: no fast travel hook: fast travel from the map plays nothing", kTag);
    }
    return teleport || levelExit;
}

} // namespace evr::vkcore::controllers
