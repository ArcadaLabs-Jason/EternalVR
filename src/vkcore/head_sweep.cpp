// The head sweep through the engine's collision query (head_sweep.hpp, docs/VR_ROOMSCALE.md).
//
// Facts (build 25216728, docs/VR_ROOMSCALE.md "Collision query"):
// - idHavokCollision::Translation (RVA 0x4FC0B0) casts a shape from start to end and fills a trace_t
//   synchronously when given a result pointer; the game calls it the same way on the game-frame path
//   (third-person and spring cameras, the weapon's view trace).
// - The collision world is the map instance + a displacement that map-instance vslot 0x370 returns
//   (`lea rax, [rcx+disp32]; ret`); the map instance is a global read by the third-person camera's trace
//   (RVA 0x145378B) and the weapon's view trace (RVA 0x135F875).
// - The world keeps ready-made shapes; +0x170 is `clip16sphere`, a 0.16 m sphere.
// - trace_t: fraction at +0 (1.0 when nothing was hit), 0x80 bytes.
// - The player's spawn id (the entity the sweep ignores) is idHavokPhysics_Player +0x30.

#include "vkcore/head_sweep.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/player_aim.hpp"
#include "vkcore/seh_filter.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "room";

constexpr const char* kTranslationSignature =
    "48 8B C4 48 89 58 20 55 56 57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 00 02 00 00 0F 29 "
    "70 "
    "B8 0F 29 78 A8 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 ?? ?? ?? ?? 4C 8B B5 ?? ?? ?? ?? 4C 8B E2 4C 8B "
    "AD "
    "?? ?? ?? ?? 49 8B D8 4C 8B BD ?? ?? ?? ?? 48 8B F1 48 89 54 24 30 48 8B 95 ?? ?? ?? ??";
// Both begin with `mov rcx, [rip+disp32]`: the map-instance global.
constexpr const char* kThirdPersonSiteSignature =
    "48 8B 0D ?? ?? ?? ?? 8B 18 48 8B 01 FF 90 ?? ?? ?? ?? 45 33 E4 48 8D 0D ?? ?? ?? ?? 44 89 64 24 60 4C "
    "8D "
    "4D 80 4C 89 64 24 58 4C 8D 45 40 48 89 4C 24 50 48 8D 55 A8 44 89 64 24 48 48 8D 0D ?? ?? ?? ?? 89 5C "
    "24 "
    "40 89 74 24 38 48 89 4C 24 30 48 8D 4D 90 4C 89 74 24 28 48 89 4C 24 20 48 8B C8 E8 ?? ?? ?? ??";
constexpr const char* kWeaponSiteSignature = "48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 44 89 64 24 60 "
                                             "4C 8D 4D A8 4C 89 64 24 58 4C 8D 45 F0 48 "
                                             "8B C8 48 8D 05 ?? ?? ?? ?? 48 89 44 24 50 8B 05 ?? ?? ?? ?? 44 "
                                             "89 64 24 48 89 44 24 40 48 8D 45 70 C7 44 "
                                             "24 38 01 00 14 00 48 89 44 24 30 48 8B 05 ?? ?? ?? ?? 48 8B 90 "
                                             "?? ?? ?? ?? 48 8D 45 88 48 89 54 24 28 48 "
                                             "8D 54 24 78 48 89 44 24 20 E8 ?? ?? ?? ??";

constexpr std::size_t kCollisionWorldSlot = 0x370; // map-instance vtable slot returning the world
constexpr std::size_t kSphereShape = 0x170;        // idHavokCollision: clip16sphere (0.16 m)
constexpr std::size_t kPlayerPhysics = 0x8A50;     // idPlayer::physicsObjHavok
constexpr std::size_t kPhysicsSpawnId = 0x30;      // idHavokPhysics_Player: the owner's spawn id
constexpr std::int32_t kContents = 0x100009;       // MASK_PLAYERDEADSOLID: world and player clip, no monsters
constexpr std::size_t kTraceSize = 0x80;
constexpr std::int32_t kNoSpawnId = 0x1FFFFFE;

using TranslationFn = void(__fastcall*)(void* collision,
                                        std::uint64_t* queryId,
                                        void* result,
                                        const float* start,
                                        const float* end,
                                        void* shape,
                                        const float* axis,
                                        std::int32_t contents,
                                        std::int32_t passSpawnId,
                                        std::uint32_t group,
                                        const char* name,
                                        void* collector,
                                        std::int32_t unused);

GameImage g_image;
TranslationFn g_translation = nullptr;
void* const* g_mapInstanceGlobal = nullptr;
PlayerAim g_player;
std::atomic<bool> g_available{false};
std::once_flag g_installOnce;

// Camera hook only.
const std::byte* g_cachedMapInstance = nullptr;
std::byte* g_cachedCollision = nullptr;
std::uint64_t g_calls = 0;
std::uint64_t g_startSolid = 0;

const char kQueryName[] = "EternalVR head";
const float kIdentityAxis[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

bool copyGuarded(void* destination, const void* source, std::size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// The call itself, apart from anything with a destructor (__try needs that). False when it faulted.
bool callTranslation(
    void* collision, void* result, const float* start, const float* end, void* shape, std::int32_t spawnId) {
    __try {
        std::uint64_t queryId = 0;
        g_translation(collision, &queryId, result, start, end, shape, kIdentityAxis, kContents, spawnId, 0u,
                      kQueryName, nullptr, 0);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// The collision world of `mapInstance`, checked through its vtable: the slot must be `lea rax,
// [rcx+disp32]; ret` inside the game's code.
std::byte* collisionWorldOf(const std::byte* mapInstance) {
    if (mapInstance == g_cachedMapInstance) {
        return g_cachedCollision;
    }
    g_cachedMapInstance = mapInstance;
    g_cachedCollision = nullptr;
    const std::byte* vtable = nullptr;
    const std::byte* slot = nullptr;
    std::uint8_t code[8] = {};
    if (!mapInstance || !copyGuarded(&vtable, mapInstance, sizeof(vtable)) || !g_image.contains(vtable) ||
        !copyGuarded(&slot, vtable + kCollisionWorldSlot, sizeof(slot)) ||
        !g_image.inText(slot, sizeof(code)) || !copyGuarded(code, slot, sizeof(code))) {
        return nullptr;
    }
    if (code[0] != 0x48 || code[1] != 0x8D || code[2] != 0x81 || code[7] != 0xC3) {
        EVR_LOG("%s: map-instance vslot 0x%zX is not `lea rax, [rcx+disp]; ret`; head sweep off", kTag,
                kCollisionWorldSlot);
        g_available.store(false);
        return nullptr;
    }
    std::int32_t disp = 0;
    std::memcpy(&disp, code + 3, sizeof(disp));
    g_cachedCollision = const_cast<std::byte*>(mapInstance) + disp;
    EVR_LOG("%s: collision world at map instance %p + 0x%X", kTag, static_cast<const void*>(mapInstance),
            disp);
    return g_cachedCollision;
}

void install() {
    if (!locateGameImage(g_image, kTag)) {
        return;
    }
    if (!g_player.init()) {
        EVR_LOG("%s: unknown game build; no head sweep (the lean cap still applies)", kTag);
        return;
    }
    const std::byte* translation = findUnique(g_image, kTag, "collision Translation", kTranslationSignature);
    const std::byte* third = findUnique(g_image, kTag, "third-person trace site", kThirdPersonSiteSignature);
    const std::byte* weapon = findUnique(g_image, kTag, "weapon view trace site", kWeaponSiteSignature);
    if (!translation || !third || !weapon) {
        EVR_LOG("%s: collision query not found; no head sweep (the lean cap still applies)", kTag);
        return;
    }
    const std::byte* global = ripTarget(g_image, third + 3, third + 7);
    const std::byte* global2 = ripTarget(g_image, weapon + 3, weapon + 7);
    if (!global || global != global2) {
        EVR_LOG("%s: the two trace sites read different map-instance globals; no head sweep", kTag);
        return;
    }
    g_translation = reinterpret_cast<TranslationFn>(const_cast<std::byte*>(translation));
    g_mapInstanceGlobal = reinterpret_cast<void* const*>(global);
    g_available.store(true, std::memory_order_release);
    EVR_LOG("%s: head sweep through the collision query at RVA 0x%X (map instance global RVA 0x%X)", kTag,
            g_image.rva(translation), g_image.rva(global));
}

} // namespace

bool installHeadSweep() {
    if (!mp_guard::allowsGameTouch()) {
        return false;
    }
    std::call_once(g_installOnce, &install);
    return g_available.load(std::memory_order_acquire);
}

bool headSweepAvailable() {
    return g_available.load(std::memory_order_acquire);
}

std::optional<float> sweepHead(const std::byte* player, Vec3 from, Vec3 to, float /*radius*/) {
    if (!g_available.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch() || !player ||
        !g_player.isPlayer(player)) {
        return std::nullopt;
    }
    const std::byte* mapInstance = nullptr;
    if (!copyGuarded(&mapInstance, g_mapInstanceGlobal, sizeof(mapInstance))) {
        return std::nullopt;
    }
    std::byte* collision = collisionWorldOf(mapInstance);
    void* shape = nullptr;
    void* world = nullptr;
    std::int32_t spawnId = kNoSpawnId;
    if (!collision || !copyGuarded(&world, collision, sizeof(world)) || !world ||
        !copyGuarded(&shape, collision + kSphereShape, sizeof(shape)) || !shape ||
        !copyGuarded(&spawnId, player + kPlayerPhysics + kPhysicsSpawnId, sizeof(spawnId))) {
        return std::nullopt;
    }
    alignas(16) std::byte result[kTraceSize] = {};
    const float start[3] = {from.x, from.y, from.z};
    const float end[3] = {to.x, to.y, to.z};
    if (!callTranslation(collision, result, start, end, shape, spawnId)) {
        g_available.store(false);
        EVR_LOG("%s: the collision query faulted; head sweep off for this session", kTag);
        return std::nullopt;
    }
    float fraction = 1.0f;
    std::memcpy(&fraction, result, sizeof(fraction));
    if (++g_calls == 1) {
        EVR_LOG("%s: first head sweep: fraction %.3f, player spawn id 0x%X", kTag, fraction,
                static_cast<unsigned>(spawnId));
    }
    if (!std::isfinite(fraction) || fraction >= 1.0f) {
        return std::nullopt;
    }
    if (fraction <= 0.0f) {
        // The game's eye is itself in contact (a scripted camera, a tight crouch): not a head in a wall.
        if (++g_startSolid <= 5) {
            EVR_LOG("%s: head sweep starts in contact; ignored", kTag);
        }
        return std::nullopt;
    }
    return fraction;
}

} // namespace evr::vkcore
