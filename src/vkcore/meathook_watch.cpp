// The Super Shotgun's Meat Hook in the log (docs/VR_CONTROLLERS.md, "The Meat Hook"): what the game takes as
// the hook's target and when, for player logs and the rig's meathook scenarios. Read only.
//
// Type info (Steam build 25216728): the held item's object (weapon_mods.hpp) is an idDoubleBarrelShotgun
// while the held decl is the Super Shotgun's. Its meatHookIsAvailable (+0x3D96), mostRecentTarget (+0x3DD0,
// an idManagedClassPtr: the spawn id first, 0x1FFFFFE for none, the object at +8), hookedAI (+0x3C08, the
// same kind) and meatHookDecl (+0x3E18, the decl's meatHook_t: targetHorizontalFov +0x3A0, targetVerticalFov
// +0x3A4, maxTargetDistance +0x3A8). The game picks the target from its own view (the player's view angles;
// hand aim writes them from the weapon hand) and draws the hook's lock-on mark on the target through the
// HUD's world-attached markers (marker_hooks.cpp). The target's place comes from its physics
// (idEntity::physics +0x1D0, idHavokPhysics_Base::bodyOrigin +0xB0: the feet). Every read is guarded and only
// logged. Steam build only: the Game Pass build's layouts of these classes were not compared, so nothing is
// read or logged there.

#include "vkcore/controllers_impl.hpp"

#include "vkcore/game_build.hpp"
#include "vkcore/log.hpp"
#include "vkcore/player_aim.hpp"
#include "vkcore/weapon_mods.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers {

namespace {

constexpr std::string_view kSuperShotgun = "weapon/player/double_barrel";
constexpr std::size_t kAvailable = 0x3D96;
constexpr std::size_t kTarget = 0x3DD0;
constexpr std::size_t kHookedAi = 0x3C08;
constexpr std::size_t kManagedObject = 0x8;
constexpr std::size_t kMeatHookDecl = 0x3E18;
constexpr std::size_t kTargetFovH = 0x3A0;
constexpr std::size_t kTargetFovV = 0x3A4;
constexpr std::size_t kTargetRange = 0x3A8;
constexpr std::uint32_t kNoSpawnId = 0x1FFFFFE;
constexpr int kMaxLines = 60;
constexpr std::size_t kPlayerFirstPersonOrigin = 0x16580; // idPlayer::firstPersonViewOrigin
constexpr std::size_t kEntityPhysics = 0x1D0;             // idEntity::physics
constexpr std::size_t kPhysicsBodyOrigin = 0xB0;          // idHavokPhysics_Base::bodyOrigin
constexpr float kDegreesPerRadian = 57.2957795f;

// The viewmodel hook's thread (under its cache lock).
const std::byte* g_decl = nullptr;
bool g_superShotgun = false;
bool g_declLogged = false;
std::uint32_t g_target = kNoSpawnId;
std::uint32_t g_hooked = kNoSpawnId;
bool g_available = false;
LONGLONG g_since = 0;
int g_lines = 0;

std::string entityName(const std::byte* managed) {
    const std::byte* entity = nullptr;
    const std::byte* def = nullptr;
    if (!safeRead(managed + kManagedObject, entity) || !entity || !safeRead(entity + kEntityDef, def) ||
        !def) {
        return "?";
    }
    const std::string name = itemDeclName(def);
    return name.empty() ? std::string("?") : name;
}

// Where the target stands seen from the player's eye: " (its origin at yaw Y pitch P, D m)", or empty when
// its physics cannot be read. Yaw and pitch as the game's view angles (pitch positive down).
std::string targetBearing(const std::byte* managed, const std::byte* player) {
    const std::byte* entity = nullptr;
    const std::byte* physics = nullptr;
    float origin[3] = {};
    float eye[3] = {};
    if (!safeRead(managed + kManagedObject, entity) || !entity ||
        !safeRead(entity + kEntityPhysics, physics) || !physics ||
        !safeCopy(origin, physics + kPhysicsBodyOrigin, sizeof(origin)) ||
        !safeCopy(eye, player + kPlayerFirstPersonOrigin, sizeof(eye))) {
        return {};
    }
    const float dx = origin[0] - eye[0];
    const float dy = origin[1] - eye[1];
    const float dz = origin[2] - eye[2];
    const float flat = std::sqrt(dx * dx + dy * dy);
    const float distance = std::sqrt(flat * flat + dz * dz);
    if (!std::isfinite(distance) || distance <= 0.0f || distance > 1000.0f) {
        return {};
    }
    char text[96];
    std::snprintf(text, sizeof(text), " (its origin at yaw %.1f pitch %.1f, %.1f m)",
                  std::atan2(dy, dx) * kDegreesPerRadian, -std::atan2(dz, flat) * kDegreesPerRadian,
                  distance);
    return text;
}

// The decl's cone and range read as a meatHook_t's: finite, each angle in (0, 360] degrees and the range in
// (0, 1000) m. Anything else is not logged (another layout, or the decl not loaded yet).
bool plausibleDecl(const float (&values)[3]) {
    for (const float v : values) {
        if (!std::isfinite(v)) {
            return false;
        }
    }
    return values[0] > 0.0f && values[0] <= 360.0f && values[1] > 0.0f && values[1] <= 360.0f &&
           values[2] > 0.0f && values[2] < 1000.0f;
}

} // namespace

void noteMeatHook(const std::byte* hands, const std::byte* player, const std::byte* decl) {
    const GameBuild* build = currentGameBuild();
    if (!build || build->kind != GameBuildKind::Steam || !hands || !player) {
        return;
    }
    if (decl != g_decl) {
        g_decl = decl;
        g_superShotgun = decl && itemDeclName(decl) == kSuperShotgun;
    }
    const std::byte* weapon = g_superShotgun ? heldItemObject(hands, decl) : nullptr;
    if (!weapon) {
        return;
    }
    if (!g_declLogged) {
        const std::byte* hook = nullptr;
        float fov[3] = {};
        if (safeRead(weapon + kMeatHookDecl, hook) && hook && safeRead(hook + kTargetFovH, fov[0]) &&
            safeRead(hook + kTargetFovV, fov[1]) && safeRead(hook + kTargetRange, fov[2]) &&
            plausibleDecl(fov)) {
            // Logged once; a read that does not check out is tried again on the next tick.
            g_declLogged = true;
            EVR_LOG(
                "meat hook: the Super Shotgun's hook takes targets within %.1f x %.1f deg of the game's view "
                "(horizontal x vertical) and %.1f m",
                fov[0], fov[1], fov[2]);
        }
    }
    std::uint8_t available = 0;
    std::uint32_t target = kNoSpawnId;
    std::uint32_t hooked = kNoSpawnId;
    if (!safeRead(weapon + kAvailable, available) || !safeRead(weapon + kTarget, target) ||
        !safeRead(weapon + kHookedAi, hooked)) {
        return;
    }
    const bool nowAvailable = available != 0;
    if (target == g_target && hooked == g_hooked && nowAvailable == g_available) {
        return;
    }
    const LONGLONG now = nowQpc();
    const double held = g_since ? qpcToSeconds(now) - qpcToSeconds(g_since) : 0.0;
    const bool targetChanged = target != g_target;
    g_target = target;
    g_hooked = hooked;
    g_available = nowAvailable;
    g_since = now;
    if (g_lines >= kMaxLines) {
        return;
    }
    ++g_lines;
    float view[3] = {};
    safeCopy(view, player + kPlayerViewAngles, sizeof(view));
    const std::string bearing =
        target == kNoSpawnId ? std::string{} : targetBearing(weapon + kTarget, player);
    EVR_LOG("meat hook: target %s%s%s, hooked %s, available %s (the last state held %.2f s); the game's view "
            "pitch %.1f yaw %.1f%s",
            target == kNoSpawnId ? "none" : entityName(weapon + kTarget).c_str(), bearing.c_str(),
            targetChanged ? " (new)" : "",
            hooked == kNoSpawnId ? "none" : entityName(weapon + kHookedAi).c_str(),
            nowAvailable ? "yes" : "no", held, view[0], view[1],
            g_lines == kMaxLines ? "; no more meat hook lines" : "");
}

} // namespace evr::vkcore::controllers
