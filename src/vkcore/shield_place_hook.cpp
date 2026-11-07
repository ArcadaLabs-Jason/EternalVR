// The Chaingun's Energy Shield in front of the gun in the hand (shield_place.hpp).

#include "vkcore/shield_place.hpp"

#include "features/input/gun_placement.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/shield_pose.hpp"
#include "xr_math/weapon_pose.hpp"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers::shield_place {

namespace {

constexpr const char* kTag = "controllers";

// idEnergyShell's think, its last call: lea rdx,[rbp+0x80]; mov rcx,rsi; the axis copied to [rbp+0xA0];
// mov rax,[rsi]; call [rax+0x4E0] (the entity's effect transform).
constexpr const char* kSiteSignature =
    "48 8D 95 80 00 00 00 48 8B CE 0F 10 00 0F 11 85 A0 00 00 00 0F 10 48 10 0F 11 8D B0 00 00 00 8B 40 20 "
    "89 85 C0 00 00 00 48 8B 06 FF 90 E0 04 00 00";
constexpr std::size_t kSiteHook = 0x28; // mov rax, [rsi]
constexpr std::size_t kEffectOrigin = 0x8;
constexpr std::size_t kEffectAxis = 0x20;
// idEntity's effect scale (vslot 0xB80): the scale for the entity's effect group at +0xB30.
constexpr const char* kScaleSignature =
    "40 53 48 83 EC 30 48 8B D9 0F 29 74 24 20 48 8B 0D ?? ?? ?? ?? 0F 28 F1 E8 ?? ?? ?? ?? 48 85 C0 74 1C "
    "48 83 BB 30 0B 00 00 FF 74 12 48 8B 93 30 0B 00 00 0F 28 D6 48 8B C8 E8";
// Where an effect action that tracks its entity (FX_ORG_TRACK_POS) finds it: the effect's owner (vslot 0x18,
// 0xBD7370) asks the entity's vslot 0x3A0, which for the shield is idAnimatedEntity's (0xB5B850, shared by
// many classes): the bind master's, the animator's or the physics object's origin and axis, each path ending
// in its own return. At each (the stack's release) the entity is in rdi, the origin out at rsi, the axis out
// at rbp.
constexpr const char* kEffectPoseSignature = "40 55 56 57 48 83 EC 30 48 8B 01 49 8B E8 48 8B F2 48 8B F9 FF "
                                             "90 98 06 00 00 48 8B 17 48 8B CF 48 85 C0 "
                                             "74 55 FF 92 78 0E 00 00 48 8B C8 48 8B 10 FF 92 70 06 00 00 48 "
                                             "8B CF F2 0F 10 00 F2 0F 11 06 8B 40 08 89 "
                                             "46 08 48 8B 07 FF 90 78 0E 00 00 48 8B C8 48 8B 10 FF 92 78 06 "
                                             "00 00 0F 10 00 0F 11 45 00 0F 10 48 10 0F "
                                             "11 4D 10 8B 40 20 89 45 20 48 83 C4 30 5F 5E 5D C3";
struct EffectPoseExit {
    std::size_t at;
    const char* code;
};
constexpr EffectPoseExit kEffectPoseExits[] = {
    {0x72, "48 83 C4 30 5F 5E 5D C3"},                 // the bind master's pose
    {0x18C, "0F 28 74 24 20 48 83 C4 30 5F 5E 5D C3"}, // the animator's
    {0x2CC, "0F 28 74 24 20 48 83 C4 30 5F 5E 5D C3"}, // the physics object's
};
// The effect's own pose queries, the only callers whose answer is moved (the owner's vslot 0x18, 0xBD7370,
// tail-jumps to the entity's vslot 0x3A0, so the return address at the exits is the query's): the tracking
// action's update (0x4DB440, its call at 0x4DB890) and the action's start (0x4D66D0, its call at 0x4D6923).
// Each signature runs up to the call; the return address is `ret` bytes in. At every exit the return address
// is at [rsp+0x48] (the stack's release of 0x30, then three pops).
struct EffectQuery {
    const char* name;
    const char* code;
    std::size_t ret;
};
constexpr EffectQuery kEffectQueries[] = {
    {"effect action update's pose query",
     "4D 85 F6 74 32 49 8B 06 4C 8D 85 A8 00 00 00 48 8D 55 C0 49 8B CE FF 50 18 8B 8D C8 00 00 00", 0x19},
    {"effect action start's pose query",
     "74 18 48 8B 01 4C 8D 45 08 48 8D 55 88 FF 50 18 8B 4D 90 F2 0F 10 55 88 EB", 0x10},
};
constexpr std::size_t kReturnFromRsp = 0x48;
constexpr std::string_view kShieldClass = ".?AVidEnergyShell@@";
constexpr std::size_t kPlayerFirstPersonOrigin = 0x16580;
// The game puts the effect at the eye; anything farther from it (metres) is not the transform this hook
// knows, and stays the game's.
constexpr float kEffectAtEyeMetres = 0.05f;

using SetEffectScale = void (*)(void* entity, float scale);
SetEffectScale g_setScale = nullptr;

std::mutex g_scaleMutex;
xr_math::ShieldScale g_scale; // under g_scaleMutex

LogCap g_lines{6, 2000};
std::atomic<bool> g_loggedNotAtEye{false};
std::atomic<bool> g_loggedDrawn{false};

GameImage g_image;
// The shield's vtable, once one shield's RTTI name was checked (any thread); an entity with another vtable
// is left as the game's.
std::atomic<const std::byte*> g_shieldVtable{nullptr};
std::atomic<bool> g_loggedClass{false};

// The shield the layer places and where (written by the think, read by the owner's callback): the move from
// the game's origin, and the axis. The effect's actions read their entity's render model, which the game
// puts at its eye, every frame, so the callback moves what they read.
struct Drawn {
    Vec3 move;
    float axis[9] = {};
    LONGLONG qpc = 0;                 // the think that placed it (or held it)
    const std::byte* owner = nullptr; // the player it was placed for
};
std::atomic<const std::byte*> g_drawnShell{nullptr};
std::mutex g_drawnMutex;
Drawn g_drawn; // under g_drawnMutex

// Set once every hook is in; until then the think's hook does nothing (all or none).
std::atomic<bool> g_live{false};
std::uintptr_t g_effectReturns[std::size(kEffectQueries)] = {}; // written by install, then read-only

// The exits' cost on the hot path (they run for every animated entity's tracked effect): calls counted per
// thread and added up every 1024, and one call in 1024 timed in cycles from the callback's start to its end.
std::atomic<std::uint64_t> g_poseCalls{0};
std::atomic<std::uint64_t> g_poseSampleCycles{0};
std::atomic<std::uint64_t> g_poseSamples{0};
std::atomic<std::uint64_t> g_poseMoved{0};
std::atomic<std::uint64_t> g_lastStatsMs{0};
thread_local std::uint64_t t_poseCalls = 0;
// Return addresses refused for the shield, each logged once (the first 8).
std::mutex g_refusedMutex;
std::uintptr_t g_refused[8] = {}; // under g_refusedMutex
std::atomic<bool> g_refusedFull{false};

bool readFloats(const std::byte* at, float* out, std::size_t count) {
    if (!safeCopy(out, at, count * sizeof(float))) {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(out[i])) {
            return false;
        }
    }
    return true;
}

// True for an entity of the shield's class: its vtable's RTTI name checked once, then the vtable compared.
bool isShield(const std::byte* entity) {
    const std::byte* vtable = nullptr;
    if (!safeRead(entity, vtable) || !vtable) {
        return false;
    }
    const std::byte* known = g_shieldVtable.load(std::memory_order_acquire);
    if (known) {
        return vtable == known;
    }
    const std::string_view name = rttiName(g_image, vtable);
    if (name != kShieldClass) {
        if (!g_loggedClass.exchange(true)) {
            EVR_LOG("%s: energy shield: the think's entity is '%.*s', not %.*s: left as the game's", kTag,
                    static_cast<int>(name.size()), name.data(), static_cast<int>(kShieldClass.size()),
                    kShieldClass.data());
        }
        return false;
    }
    g_shieldVtable.store(vtable, std::memory_order_release);
    return true;
}

struct Placed {
    xr_math::ShieldEffect game;
    xr_math::ShieldEffect moved;
    Vec3 eye;
    Vec3 rayStart;
    Vec3 ray;
    Vec3 muzzle;
    bool hand = false;
    float unitsPerMetre = 1.0f;
};

std::optional<Placed> place(const std::byte* owner, const std::byte* effect) {
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    if (!cfg.viewmodel || !s.attached.load(std::memory_order_acquire) || s.yielding.load()) {
        return std::nullopt; // forced views keep the game's hands, and its shield
    }
    float eye[3];
    float origin[3];
    float axis[9];
    if (!readFloats(owner + kPlayerFirstPersonOrigin, eye, 3) ||
        !readFloats(effect + kEffectOrigin, origin, 3) || !readFloats(effect + kEffectAxis, axis, 9)) {
        return std::nullopt;
    }
    const TickView view = tickView();
    if (!view.worldCurrent) {
        return std::nullopt;
    }
    const WorldHand& world = view.world;
    Placed p;
    p.eye = {eye[0], eye[1], eye[2]};
    p.unitsPerMetre = world.unitsPerMetre;
    p.game.origin = Vec3{origin[0], origin[1], origin[2]} - p.eye;
    p.game.axis = {{axis[0], axis[1], axis[2]}, {axis[3], axis[4], axis[5]}, {axis[6], axis[7], axis[8]}};
    if (length(p.game.origin) > kEffectAtEyeMetres * p.unitsPerMetre) {
        if (!g_loggedNotAtEye.exchange(true)) {
            EVR_LOG(
                "%s: energy shield: the game's effect is %.3f from the eye, not at it: left as the game's",
                kTag, length(p.game.origin) / p.unitsPerMetre);
        }
        return std::nullopt;
    }
    xr_math::EyeRelativePose arms;
    switch (input::gunPlacement(cfg.aim, false, cfg.actionAim.melee)) {
    case input::GunPlacement::Hand:
        if (!view.modelCurrent) {
            return std::nullopt; // the gun was not placed at the hand this tick
        }
        p.hand = true;
        p.rayStart = world.aim.offset;
        p.ray = world.aim.axis.forward;
        arms = view.model.pose;
        break;
    case input::GunPlacement::Head: {
        // The game's arms model at the rendered head along the view (head_gun.hpp).
        p.rayStart = world.head.offset;
        p.ray = xr_math::shieldEffectForward(p.game.axis);
        const std::optional<xr_math::IdViewAxis> along = xr_math::axisFromDirection(p.ray);
        if (!along) {
            return std::nullopt;
        }
        arms = {p.rayStart, *along};
        break;
    }
    case input::GunPlacement::GameView:
        return std::nullopt;
    }
    p.muzzle = xr_math::chaingunMuzzle(arms, p.unitsPerMetre);
    const std::optional<xr_math::ShieldEffect> moved =
        xr_math::shieldInFrontOfGun(p.game, p.rayStart, p.ray, p.muzzle, p.unitsPerMetre);
    if (!moved) {
        return std::nullopt;
    }
    p.moved = *moved;
    return p;
}

float degreesBetween(Vec3 a, Vec3 b) {
    return std::atan2(length(cross(a, b)), dot(a, b)) * 180.0f / std::numbers::pi_v<float>;
}

void logPlaced(const Placed& p, float scale) {
    std::uint64_t skipped = 0;
    if (!g_lines.due(GetTickCount64(), skipped)) {
        return;
    }
    const float upm = p.unitsPerMetre;
    const Vec3 forward = xr_math::shieldEffectForward(p.moved.axis);
    const Vec3 centre = p.moved.origin + forward * (xr_math::kShieldEffectReach * upm);
    const Vec3 gameForward = xr_math::shieldEffectForward(p.game.axis);
    // The axis convention (shield_pose.hpp) against the game's own: the game's axis rebuilt from its forward.
    float convention = -1.0f;
    if (const std::optional<xr_math::IdViewAxis> view = xr_math::axisFromDirection(gameForward)) {
        const xr_math::IdViewAxis rebuilt = xr_math::shieldEffectAxis(*view);
        const Vec3 rows[3][2] = {{rebuilt.forward, p.game.axis.forward},
                                 {rebuilt.left, p.game.axis.left},
                                 {rebuilt.up, p.game.axis.up}};
        convention = 0.0f;
        for (const auto& r : rows) {
            convention = std::fmax(convention, length(r[0] - r[1]));
        }
    }
    EVR_LOG(
        "%s: energy shield: effect (%.2f %.2f %.2f) along (%.3f %.3f %.3f) from the eye -> (%.2f %.2f %.2f) "
        "along (%.3f %.3f %.3f), scale %.2f; its centre %.2f along the %s ray from (%.2f %.2f %.2f), the "
        "muzzle %.2f along it; the game's shield %.1f deg from the ray, its axis %.4f from the one the "
        "convention rebuilds; eye (%.2f %.2f %.2f) (%llu not "
        "logged)",
        kTag, p.game.origin.x / upm, p.game.origin.y / upm, p.game.origin.z / upm, gameForward.x,
        gameForward.y, gameForward.z, p.moved.origin.x / upm, p.moved.origin.y / upm, p.moved.origin.z / upm,
        forward.x, forward.y, forward.z, scale, dot(centre - p.rayStart, p.ray) / upm,
        p.hand ? "hand's" : "head's", p.rayStart.x / upm, p.rayStart.y / upm, p.rayStart.z / upm,
        dot(p.muzzle - p.rayStart, p.ray) / upm, degreesBetween(gameForward, p.ray), convention, p.eye.x,
        p.eye.y, p.eye.z, static_cast<unsigned long long>(skipped));
}

void onShieldEffect(const HookRegisters& regs) {
    if (!g_live.load(std::memory_order_acquire)) {
        return;
    }
    if (!mp_guard::allowsGameTouch()) {
        // Nothing of the game's is touched; a world scale already out is taken back once the guard allows.
        g_drawnShell.store(nullptr, std::memory_order_release);
        std::lock_guard lock(g_scaleMutex);
        g_scale.refused();
        return;
    }
    auto* shell = reinterpret_cast<std::byte*>(regs.rsi);
    auto* effect = reinterpret_cast<std::byte*>(regs.rdx);
    const auto* owner = reinterpret_cast<const std::byte*>(regs.r13);
    if (!shell || !effect || !isPlayerSafe(owner) || !isShield(shell)) {
        return; // not the local player's shield
    }
    const std::optional<Placed> placed = place(owner, effect);
    xr_math::ShieldScale::Step step;
    {
        std::lock_guard lock(g_scaleMutex);
        step = g_scale.next(placed.has_value());
    }
    {
        std::lock_guard lock(g_drawnMutex);
        if (owner != g_drawn.owner || shell != g_drawnShell.load(std::memory_order_relaxed)) {
            // Another player (a load) or another shield: nothing of the last one is held for it.
            if (!placed) {
                step.draw = false;
            }
            g_drawn.owner = owner;
        }
        if (placed) {
            const xr_math::IdViewAxis& a = placed->moved.axis;
            const float axis[9] = {a.forward.x, a.forward.y, a.forward.z, a.left.x, a.left.y,
                                   a.left.z,    a.up.x,      a.up.y,      a.up.z};
            g_drawn.move = placed->moved.origin - placed->game.origin;
            std::memcpy(g_drawn.axis, axis, sizeof(axis));
        }
        if (step.draw) {
            g_drawn.qpc = nowQpc(); // placed now, or the last placement held
            g_drawnShell.store(shell, std::memory_order_release);
        } else {
            g_drawnShell.store(nullptr, std::memory_order_release);
        }
    }
    if (!mp_guard::allowsGameTouch()) {
        g_drawnShell.store(nullptr, std::memory_order_release);
        std::lock_guard lock(g_scaleMutex);
        g_scale.notSent(step);
        return;
    }
    const std::optional<float> scale = step.send;
    if (placed) {
        const Vec3 at = placed->eye + placed->moved.origin;
        const float origin[3] = {at.x, at.y, at.z};
        const xr_math::IdViewAxis& a = placed->moved.axis;
        const float axis[9] = {a.forward.x, a.forward.y, a.forward.z, a.left.x, a.left.y,
                               a.left.z,    a.up.x,      a.up.y,      a.up.z};
        safeCopy(effect + kEffectOrigin, origin, sizeof(origin));
        safeCopy(effect + kEffectAxis, axis, sizeof(axis));
    }
    if (scale) {
        g_setScale(shell, *scale);
        if (!placed) {
            EVR_LOG("%s: energy shield: back to the game's (scale 1, after %d thinks without a placement)",
                    kTag, xr_math::ShieldScale::kHoldThinks);
        }
    }
    if (placed) {
        logPlaced(*placed, xr_math::kShieldWorldScale);
    }
}

// The pose an entity gives its effect, for the placed shield only: its own pose (the game's eye), moved and
// turned as the think placed it.
void logPoseStats() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_lastStatsMs.load(std::memory_order_relaxed);
    if (last == 0) {
        g_lastStatsMs.compare_exchange_strong(last, now, std::memory_order_relaxed);
        return;
    }
    if (now - last < 10'000 || !g_lastStatsMs.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
        return;
    }
    const std::uint64_t calls = g_poseCalls.exchange(0, std::memory_order_relaxed);
    const std::uint64_t samples = g_poseSamples.exchange(0, std::memory_order_relaxed);
    const std::uint64_t cycles = g_poseSampleCycles.exchange(0, std::memory_order_relaxed);
    const std::uint64_t moved = g_poseMoved.exchange(0, std::memory_order_relaxed);
    EVR_LOG("%s: energy shield: effect pose returns: about %llu calls in %.1f s (%.0f/s), the callback %.0f "
            "cycles on average over %llu sampled calls, %llu answers moved",
            kTag, static_cast<unsigned long long>(calls), (now - last) / 1000.0,
            calls * 1000.0 / static_cast<double>(now - last),
            samples ? static_cast<double>(cycles) / static_cast<double>(samples) : 0.0,
            static_cast<unsigned long long>(samples), static_cast<unsigned long long>(moved));
}

void noteRefused(std::uintptr_t returnAddress) {
    if (g_refusedFull.load(std::memory_order_relaxed)) {
        return;
    }
    std::lock_guard lock(g_refusedMutex);
    for (std::uintptr_t& slot : g_refused) {
        if (slot == returnAddress) {
            return;
        }
        if (slot == 0) {
            slot = returnAddress;
            const auto* at = reinterpret_cast<const std::byte*>(returnAddress);
            EVR_LOG("%s: energy shield: its pose asked from RVA 0x%X (not the effect's query): left as the "
                    "game's",
                    kTag, g_image.contains(at) ? g_image.rva(at) : 0u);
            return;
        }
    }
    g_refusedFull.store(true, std::memory_order_relaxed);
}

// The pose an entity gives when asked, moved and turned as the think placed it, for the placed shield only
// and only when the effect's own query asks (xr_math::movesShieldPose).
void movePose(const HookRegisters& regs, int exit) {
    const std::byte* shell = g_drawnShell.load(std::memory_order_acquire);
    const auto* entity = reinterpret_cast<const std::byte*>(regs.rdi);
    if (!shell || entity != shell) {
        return; // the hot path: any other entity, or no shield up
    }
    std::uintptr_t returnAddress = 0;
    safeRead(reinterpret_cast<const std::byte*>(regs.rsp + kReturnFromRsp), returnAddress);
    Drawn drawn;
    {
        std::lock_guard lock(g_drawnMutex);
        drawn = g_drawn;
    }
    xr_math::ShieldPoseAsk ask;
    ask.placedShield = true;
    ask.shieldClass = isShield(entity);
    ask.sincePlaced = secondsSince(drawn.qpc);
    for (const std::uintptr_t allowed : g_effectReturns) {
        ask.fromEffect = ask.fromEffect || (allowed != 0 && returnAddress == allowed);
    }
    if (!xr_math::movesShieldPose(ask)) {
        if (ask.shieldClass && !ask.fromEffect) {
            noteRefused(returnAddress);
        }
        return;
    }
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    auto* originAt = reinterpret_cast<std::byte*>(regs.rsi);
    auto* axisAt = reinterpret_cast<std::byte*>(regs.rbp);
    float origin[3];
    if (!readFloats(originAt, origin, 3)) {
        return;
    }
    const float moved[3] = {origin[0] + drawn.move.x, origin[1] + drawn.move.y, origin[2] + drawn.move.z};
    safeCopy(originAt, moved, sizeof(moved));
    safeCopy(axisAt, drawn.axis, sizeof(drawn.axis));
    g_poseMoved.fetch_add(1, std::memory_order_relaxed);
    if (!g_loggedDrawn.exchange(true)) {
        EVR_LOG(
            "%s: energy shield: its effect asked for the shield's pose (return %d, asked from RVA 0x%X: %.2f "
            "%.2f %.2f), given (%.2f %.2f %.2f)",
            kTag, exit, g_image.rva(reinterpret_cast<const std::byte*>(returnAddress)), origin[0], origin[1],
            origin[2], moved[0], moved[1], moved[2]);
    }
}

void onEffectPose(const HookRegisters& regs, int exit) {
    const bool sampled = (++t_poseCalls & 1023) == 0;
    if (!sampled) {
        movePose(regs, exit);
        return;
    }
    const std::uint64_t start = __rdtsc();
    movePose(regs, exit);
    g_poseSampleCycles.fetch_add(__rdtsc() - start, std::memory_order_relaxed);
    g_poseSamples.fetch_add(1, std::memory_order_relaxed);
    g_poseCalls.fetch_add(1024, std::memory_order_relaxed);
    logPoseStats();
}

void onEffectPose0(const HookRegisters& regs) {
    onEffectPose(regs, 0);
}
void onEffectPose1(const HookRegisters& regs) {
    onEffectPose(regs, 1);
}
void onEffectPose2(const HookRegisters& regs) {
    onEffectPose(regs, 2);
}

} // namespace

bool install() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* site = findUnique(image, kTag, "energy shield effect transform", kSiteSignature);
    const std::byte* scale = findUnique(image, kTag, "entity effect scale", kScaleSignature);
    const std::byte* effectPose = findUnique(image, kTag, "entity effect pose", kEffectPoseSignature);
    if (!site || !scale || !effectPose) {
        return false;
    }
    for (std::size_t i = 0; i < std::size(kEffectQueries); ++i) {
        const std::byte* query = findUnique(image, kTag, kEffectQueries[i].name, kEffectQueries[i].code);
        if (!query) {
            return false;
        }
        g_effectReturns[i] = reinterpret_cast<std::uintptr_t>(query + kEffectQueries[i].ret);
    }
    for (const EffectPoseExit& exit : kEffectPoseExits) {
        if (!matchesAt(image, effectPose + exit.at, exit.code)) {
            EVR_LOG("%s: energy shield: the entity effect pose's return at +0x%zX is not the one known: off",
                    kTag, exit.at);
            return false;
        }
    }
    g_image = image;
    g_setScale = reinterpret_cast<SetEffectScale>(const_cast<std::byte*>(scale));
    std::string error;
    // The think's hook first: it does nothing until every hook is in (g_live), so a failed exit hook leaves
    // it inert, and a failed think hook leaves no exit hooks.
    if (!installMidHook(const_cast<std::byte*>(site + kSiteHook), &onShieldEffect, error)) {
        EVR_LOG("%s: energy shield hook at RVA 0x%X failed: %s", kTag, image.rva(site + kSiteHook),
                error.c_str());
        return false;
    }
    const MidHookCallback exits[] = {&onEffectPose0, &onEffectPose1, &onEffectPose2};
    for (std::size_t i = 0; i < std::size(kEffectPoseExits); ++i) {
        if (!installMidHook(const_cast<std::byte*>(effectPose + kEffectPoseExits[i].at), exits[i], error)) {
            EVR_LOG("%s: energy shield effect pose hook at RVA 0x%X failed: %s", kTag,
                    image.rva(effectPose + kEffectPoseExits[i].at), error.c_str());
            return false;
        }
    }
    g_live.store(true, std::memory_order_release);
    EVR_LOG("%s: energy shield hook at RVA 0x%X (effect scale at RVA 0x%X, effect pose returns at RVA 0x%X, "
            "0x%X, 0x%X)",
            kTag, image.rva(site + kSiteHook), image.rva(scale),
            image.rva(effectPose + kEffectPoseExits[0].at), image.rva(effectPose + kEffectPoseExits[1].at),
            image.rva(effectPose + kEffectPoseExits[2].at));
    return true;
}

} // namespace evr::vkcore::controllers::shield_place
