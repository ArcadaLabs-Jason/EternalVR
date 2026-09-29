// Motion controllers, hand aim and shots (controllers.hpp, docs/rig-findings/input-aim.md section 2, T-055).
//
// - Hand aim reuses head aim's closed loop (presenter_head.cpp): each game frame it reads the game's own
//   angles back and moves them to body + target, where the target here is the weapon hand's aim ray
//   instead of the head. The render camera stays on the head.
// - Forced views: an entry hook on idPlayer::SetViewAngles (RVA 0x1454480) counts calls from anywhere but
//   the per-tick view update (idPlayer::UpdateViewAngles, whose call is found by its target); with the
//   player's view inhibit bits and cutscenes they feed the gate that makes hand aim yield.
// - Shots: a mid-hook in idHands::FireWeapon just after GetWeaponFireInfo returns (RVA 0x135D733): the
//   fire position [rsp+0x68] and fire axis [rbp+0x2B0] are replaced with the hand ray, so hitscan and
//   projectiles leave the gun, not the eye. The game adds spread and derives the muzzle offset afterwards.

#include "vkcore/controllers_impl.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/hand_aim.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// input-aim.md 1.3 (k) and (j).
constexpr const char* kSetViewAnglesSignature =
    "48 89 5C 24 10 48 89 6C 24 18 57 41 56 41 57 48 83 EC 20 41 0F B6 E8 4C 8B FA 48 8B F9";
constexpr const char* kUpdateViewAnglesSignature =
    "40 55 53 56 48 8B EC 48 83 EC 70 48 8B F1 48 8B 0D ?? ?? ?? ??";
// How far into UpdateViewAngles its SetViewAngles call is looked for (it is at +0x533 in build 25216728).
constexpr std::size_t kUpdateViewAnglesScan = 0x1000;

// input-aim.md section 2: FireWeapon's call of GetWeaponFireInfo, from `lea r9,[rsp+0x68]` (firePos).
constexpr const char* kFireSignature =
    "4C 8D 4C 24 68 48 8D 4C 24 78 49 8B D6 48 89 4C 24 28 48 8D 8D ?? ?? ?? ?? "
    "48 89 4C 24 20 49 8B CF E8 ?? ?? ?? ?? 0F B6 44 24 40";
constexpr std::size_t kFireAxisDisp = 21; // lea rcx,[rbp+fireAxis]
constexpr std::size_t kFireCall = 33;     // call GetWeaponFireInfo
constexpr std::size_t kFireHook = 38;     // the instruction after the call
constexpr std::int32_t kFireAxisFromRbp = 0x2B0;
constexpr std::size_t kFirePosFromRsp = 0x68;
constexpr const char* kGetWeaponFireInfoSignature =
    "4C 8B DC 55 53 56 57 41 55 41 56 41 57 49 8D AB ?? ?? ?? ?? 48 81 EC 20 02 00 00 48 8B 05 ?? ?? ?? ?? "
    "48 "
    "33 C4 48 89 85 ?? ?? ?? ?? 48 83 B9 ?? ?? ?? ?? 00 4D 8B F1 48 8B B5 ?? ?? ?? ??";

// Type info (build 25216728; the offsets are used only when PlayerAim confirmed the build).
constexpr std::size_t kHandsOwner = 0x358;                // idHands::owner
constexpr std::size_t kPlayerInhibitFlags = 0x87FC;       // idPlayer::inhibitFlags
constexpr std::size_t kPlayerFirstPersonOrigin = 0x16580; // idPlayer::firstPersonViewOrigin

// A hand that loses tracking keeps aiming where it last pointed for this long, then the head takes over.
constexpr double kHandHoldSeconds = 0.5;
// The farthest a shot may start from the eye, in metres (arm's reach plus the controller).
constexpr float kMaxReachMetres = 1.0f;
// A game fire position this far from the eye is the muzzle, not the view origin.
constexpr float kMuzzleEpsilonMetres = 0.02f;

std::atomic<const std::byte*> g_updateViewAnglesReturn{nullptr};
// The idPlayer the camera hook last saw (the forced-view hook counts only its calls).
std::atomic<const std::byte*> g_viewPlayer{nullptr};
const std::byte* g_imageBase = nullptr;

// Distinct foreign SetViewAngles callers seen, for the log (the live check of section 2's table).
std::mutex g_callersMutex;
std::array<std::uintptr_t, 16> g_callers{};
std::size_t g_callerCount = 0;

std::atomic<std::uint64_t> g_loggedShots{0};
std::atomic<ULONGLONG> g_lastShotStats{0};
std::atomic<float> g_maxAimError{0.0f};

void noteCaller(const std::byte* returnAddress) {
    const auto rva = static_cast<std::uintptr_t>(returnAddress - g_imageBase);
    std::lock_guard lock(g_callersMutex);
    for (std::size_t i = 0; i < g_callerCount; ++i) {
        if (g_callers[i] == rva) {
            return;
        }
    }
    if (g_callerCount < g_callers.size()) {
        g_callers[g_callerCount++] = rva;
        EVR_LOG("%s: SetViewAngles called from RVA 0x%llX (a forced view; hand aim yields)", kTag,
                static_cast<unsigned long long>(rva));
    }
}

void onSetViewAngles(const HookRegisters& regs) {
    const std::byte* returnAddress = nullptr;
    if (!safeRead(reinterpret_cast<const std::byte*>(regs.rsp), returnAddress)) {
        return;
    }
    // Only calls for the local player count (rcx is the idPlayer the view is set on).
    if (returnAddress != g_updateViewAnglesReturn.load(std::memory_order_relaxed) &&
        reinterpret_cast<const std::byte*>(regs.rcx) == g_viewPlayer.load(std::memory_order_relaxed)) {
        state().foreignSetViewAngles.fetch_add(1, std::memory_order_relaxed);
        noteCaller(returnAddress);
    }
}

bool readVec3(const std::byte* at, Vec3& out) {
    float v[3];
    if (!safeCopy(v, at, sizeof(v)) || !std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) {
        return false;
    }
    out = {v[0], v[1], v[2]};
    return true;
}

void logShotStats(State& s) {
    const ULONGLONG ticks = GetTickCount64();
    ULONGLONG last = g_lastShotStats.load();
    if (ticks - last < 10000 || !g_lastShotStats.compare_exchange_strong(last, ticks)) {
        return;
    }
    EVR_LOG("%s: %llu shot(s), %llu from the hand; view-to-hand error over 0.5 deg in %llu, max %.2f deg",
            kTag, static_cast<unsigned long long>(s.shots.load()),
            static_cast<unsigned long long>(s.shotsRewritten.load()),
            static_cast<unsigned long long>(s.shotsOverHalfDegree.load()), g_maxAimError.load());
}

void onFire(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    if (cfg.aim != input::AimSource::Hand || !s.attached.load(std::memory_order_acquire)) {
        return;
    }
    const auto* hands = reinterpret_cast<const std::byte*>(regs.r15);
    const std::byte* owner = nullptr;
    if (!safeRead(hands + kHandsOwner, owner) || !isPlayerSafe(owner)) {
        return; // not the local player's hands
    }
    s.shots.fetch_add(1, std::memory_order_relaxed);
    WorldHand world;
    {
        std::lock_guard lock(s.viewMutex);
        world = s.world;
    }
    Vec3 eye;
    auto* firePos = reinterpret_cast<std::byte*>(regs.rsp + kFirePosFromRsp);
    auto* fireAxis = reinterpret_cast<std::byte*>(regs.rbp + kFireAxisFromRbp);
    Vec3 gamePos;
    float axis[9];
    if (!world.valid || secondsSince(world.qpc) > kWorldStaleSeconds || s.yielding.load() ||
        !readVec3(owner + kPlayerFirstPersonOrigin, eye) || !readVec3(firePos, gamePos) ||
        !safeCopy(axis, fireAxis, sizeof(axis))) {
        logShotStats(s);
        return;
    }
    const Vec3 direction = world.aim.axis.forward;
    const auto gameAngles = xr_math::anglesOfDirection({axis[0], axis[1], axis[2]});
    const auto handAngles = xr_math::anglesOfDirection(direction);
    float error = -1.0f;
    if (gameAngles && handAngles) {
        error = xr_math::aimErrorDegrees(*gameAngles, *handAngles);
        if (error > 0.5f) {
            s.shotsOverHalfDegree.fetch_add(1, std::memory_order_relaxed);
        }
        float max = g_maxAimError.load();
        while (error > max && !g_maxAimError.compare_exchange_weak(max, error)) {
        }
    }
    std::optional<xr_math::Shot> shot;
    // Weapons that fire from the muzzle (the Heavy Cannon, projectiles) already start at the muzzle tag of
    // the viewmodel, which the viewmodel hook has put at the hand (seen on the rig): only their direction
    // changes. Hitscan starts at the eye; it moves to the hand.
    const Vec3 fromEye = gamePos - eye;
    const bool fromMuzzle = length(fromEye) > kMuzzleEpsilonMetres * world.unitsPerMetre;
    if (cfg.shotOrigin == input::ShotOrigin::Hand && !fromMuzzle) {
        shot = xr_math::shotFromHand(eye, world.aim.offset, direction, kMaxReachMetres * world.unitsPerMetre,
                                     0.0f);
    } else if (const auto only = xr_math::axisFromDirection(direction)) {
        shot = xr_math::Shot{gamePos, *only};
    }
    if (!shot || !mp_guard::allowsGameTouch()) {
        return;
    }
    const float pos[3] = {shot->origin.x, shot->origin.y, shot->origin.z};
    const float newAxis[9] = {shot->axis.forward.x, shot->axis.forward.y, shot->axis.forward.z,
                              shot->axis.left.x,    shot->axis.left.y,    shot->axis.left.z,
                              shot->axis.up.x,      shot->axis.up.y,      shot->axis.up.z};
    safeCopy(firePos, pos, sizeof(pos));
    safeCopy(fireAxis, newAxis, sizeof(newAxis));
    s.shotsRewritten.fetch_add(1, std::memory_order_relaxed);
    if (g_loggedShots.fetch_add(1) < 10) {
        EVR_LOG(
            "%s: shot: game start (%.2f %.2f %.2f) dir (%.3f %.3f %.3f) -> hand start (%.2f %.2f %.2f) dir "
            "(%.3f %.3f %.3f); eye (%.2f %.2f %.2f); view-to-hand error %.2f deg",
            kTag, gamePos.x, gamePos.y, gamePos.z, axis[0], axis[1], axis[2], pos[0], pos[1], pos[2],
            newAxis[0], newAxis[1], newAxis[2], eye.x, eye.y, eye.z, error);
    }
    logShotStats(s);
}

bool hookAt(const GameImage& image, const char* name, const std::byte* at, MidHookCallback callback) {
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(at), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, name, image.rva(at), error.c_str());
        return false;
    }
    EVR_LOG("%s: %s hook at RVA 0x%X", kTag, name, image.rva(at));
    return true;
}

// The address after UpdateViewAngles' call of SetViewAngles: the one caller that is not a forced view.
const std::byte* findUpdateReturn(const GameImage& image, const std::byte* setViewAngles) {
    const std::byte* update = findUnique(image, kTag, "UpdateViewAngles", kUpdateViewAnglesSignature);
    if (!update) {
        return nullptr;
    }
    const std::byte* found = nullptr;
    int count = 0;
    for (std::size_t i = 0; i + 5 <= kUpdateViewAnglesScan && image.inText(update + i, 5); ++i) {
        if (update[i] != std::byte{0xE8}) {
            continue;
        }
        const std::byte* next = update + i + 5;
        if (next + readI32(update + i + 1) == setViewAngles) {
            found = next;
            ++count;
        }
    }
    if (count != 1) {
        EVR_LOG("%s: UpdateViewAngles calls SetViewAngles %d time(s) in its first 0x%zX bytes, expected 1",
                kTag, count, kUpdateViewAnglesScan);
        return nullptr;
    }
    return found;
}

bool installFireHook(const GameImage& image) {
    const std::byte* site = findUnique(image, kTag, "fire info call", kFireSignature);
    if (!site) {
        return false;
    }
    const std::byte* callee = site + kFireCall + 5 + readI32(site + kFireCall + 1);
    const std::byte* expected = findUnique(image, kTag, "GetWeaponFireInfo", kGetWeaponFireInfoSignature);
    if (readI32(site + kFireAxisDisp) != kFireAxisFromRbp || !expected || callee != expected) {
        EVR_LOG("%s: FireWeapon's call does not match GetWeaponFireInfo and its fire axis at rbp+0x%X; shot "
                "hook off",
                kTag, static_cast<unsigned>(kFireAxisFromRbp));
        return false;
    }
    return hookAt(image, "fire", site + kFireHook, &onFire);
}

} // namespace

void updateForcedView(const std::byte* player, bool cutscene, bool cameraAnimation) {
    State& s = state();
    input::ForcedAngleSignals signals;
    signals.foreignSetViewAngles = s.foreignSetViewAngles.exchange(0, std::memory_order_relaxed) > 0;
    signals.cutscene = cutscene;
    signals.cameraAnimation = cameraAnimation;
    if (isPlayerSafe(player)) {
        g_viewPlayer.store(player, std::memory_order_relaxed);
        safeRead(player + kPlayerInhibitFlags, signals.inhibitFlags);
    }
    const bool yield = s.gate.update(signals);
    s.yielding.store(yield);
    if (s.gate.reason() != s.lastReason) {
        if (s.gate.episodes() <= 50) {
            EVR_LOG("%s: forced view %s (%s; inhibit 0x%X): hand aim, shots and viewmodel %s", kTag,
                    yield ? "starts" : "ends", input::forcedReasonName(s.gate.reason()), signals.inhibitFlags,
                    yield ? "leave the game alone" : "resume");
        }
        s.lastReason = s.gate.reason();
    }
}

bool forcedView() {
    State& s = state();
    return s.attached.load(std::memory_order_acquire) && s.yielding.load();
}

std::optional<xr_math::IdAngles> aimAngles(const xr_math::IdAngles& head) {
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    if (cfg.aim != input::AimSource::Hand || !s.attached.load(std::memory_order_acquire)) {
        return head;
    }
    if (s.yielding.load()) {
        return std::nullopt;
    }
    return aimAnglesUnforced(head);
}

xr_math::IdAngles aimAnglesUnforced(const xr_math::IdAngles& head) {
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    if (cfg.aim != input::AimSource::Hand || !s.attached.load(std::memory_order_acquire)) {
        return head;
    }
    bool valid = false;
    Pose aim;
    {
        std::lock_guard lock(s.viewMutex);
        const auto hand = static_cast<std::size_t>(weaponHand());
        valid = s.poses.valid && s.poses.aimValid[hand];
        aim = s.poses.aim[hand];
    }
    if (valid) {
        s.lastHandAngles = xr_math::handAimAngles(aim.orientation);
        s.lastHandQpc = nowQpc();
        return s.lastHandAngles;
    }
    if (s.lastHandQpc != 0 && secondsSince(s.lastHandQpc) < kHandHoldSeconds) {
        return s.lastHandAngles;
    }
    return head;
}

bool installAimHooks(bool& fireInstalled) {
    fireInstalled = false;
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    g_imageBase = image.base;
    bool forced = false;
    if (const std::byte* setView = findUnique(image, kTag, "SetViewAngles", kSetViewAnglesSignature)) {
        if (const std::byte* ret = findUpdateReturn(image, setView)) {
            g_updateViewAnglesReturn.store(ret);
            EVR_LOG("%s: the per-tick view update returns to RVA 0x%X", kTag, image.rva(ret));
            forced = hookAt(image, "forced view", setView, &onSetViewAngles);
        }
    }
    if (!forced) {
        EVR_LOG("%s: no forced-view hook; hand aim yields only to cutscenes and the view inhibit bits", kTag);
    }
    fireInstalled = installFireHook(image);
    return forced;
}

} // namespace evr::vkcore::controllers
