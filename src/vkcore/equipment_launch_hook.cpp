// The equipment launcher's launch under ETERNALVR_EQUIPMENT_AIM (features/input/action_aim.hpp,
// docs/VR_CONTROLLERS.md "Melee and equipment aim").
//
// A frag grenade or an ice bomb never goes through idHands::FireWeapon. Static RE (Steam build 25216728):
// UseEquipmentItem 0x1466CE0 queues it, the launch animation's event (AnimEvent_LaunchEquipmentLauncher
// 0x1351A20) reaches idHandsItem::LaunchEquipmentLauncher 0x1389470, which takes the origin and axis from the
// shoulder launcher's muzzle joint (0x1389020 -> idEquipmentLauncher vslot 0xC8, 0xFDBD10, which also turns
// it by the decl's launchDirOffsetDegs*) and calls the launch core 0x1389910 with them. The view angles are
// not used. With the arms model at the weapon hand (viewmodel_hook.cpp) the grenade therefore follows the
// weapon hand whatever the view does.
//
// A mid hook on the LEA at RVA 0x13898A1, just before that call: rsi is the idHandsItem (its first int the
// hands slot: 5 or 6 for the shoulder launchers), r14 the idPlayer, the origin (idVec3) at [rbp-0x80] and the
// axis (idMat3, rows forward, left, up) at [rbp+0x98]. For the local player under hand aim the origin moves
// to the head's (or the off hand's) ray start and the axis to that ray, keeping the turn and arc the game's
// axis has from the weapon hand's ray (xr_math::carryAimOffset). Installed only when ETERNALVR_EQUIPMENT_AIM
// is set.
//
// The Flame Belch's visible plume takes its own axis, refreshed every frame (static RE, Steam build): the
// plume effect started by PrepareFire follows idEquipmentLauncher's
// idFXFlameBelchAxisUpdate (launcher+0x110, its axis at launcher+0x120), which the hands item's update
// 0x138AB20 sets from the launcher's muzzle joint (GetMuzzleTransform, vslot 0xC8) at 0x138B02D, the axis
// only (the plume's origin is the launcher model's). A mid hook on that store turns the muzzle axis at
// [rbp-9] (idMat3, rows forward, left, up) onto the head's or the off hand's ray before it is stored; the
// Belch's damage (the fire hook, aim_hooks.cpp) takes the same ray from the same launcher position.

#include "vkcore/controllers_impl.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/hand_aim.hpp"
#include "xr_math/weapon_pose.hpp"

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// LaunchEquipmentLauncher's call of the launch core: lea rax,[rbp+0x98]; movaps xmm3,xmm9; mov
// [rsp+0x30],rax; mov r8,rdi; lea rax,[rbp-0x80]; mov rdx,r14; mov [rsp+0x28],rax; mov rcx,rsi; mov
// rax,[rbp+0x170]; mov [rsp+0x20],rax; call <core>.
constexpr const char* kLaunchSignature =
    "48 8D 85 98 00 00 00 41 0F 28 D9 48 89 44 24 30 4C 8B C7 48 8D 45 80 49 8B D6 48 89 44 24 28 48 8B CE "
    "48 8B 85 70 01 00 00 48 89 44 24 20 E8";
constexpr std::size_t kLaunchCall = 0x2E; // the call's E8
// The launch core's start (0x1389910).
constexpr const char* kLaunchCoreSignature =
    "4D 85 C0 0F 84 ?? ?? ?? ?? 55 56 57 41 56 41 57 B8 F0 16 00 00 E8 ?? ?? ?? ?? 48 2B E0";
constexpr std::ptrdiff_t kOriginFromRbp = -0x80;
constexpr std::ptrdiff_t kAxisFromRbp = 0x98;
constexpr int kLeftShoulderSlot = 5;
constexpr int kRightShoulderSlot = 6;
constexpr std::size_t kPlayerFirstPersonOrigin = 0x16580; // idPlayer::firstPersonViewOrigin
// The farthest the launch may start from the eye, in metres (arm's reach plus the controller).
constexpr float kMaxReachMetres = 1.0f;

LogCap g_launchLines{12};

// The hands item's update storing the launcher's muzzle axis: lea r9,[rbp-9]; mov rdx,[r14+0x78]; lea
// r8,[rbp-0x39]; mov rcx,rax; call [r10+0xC8]; test al,al; je; movups xmm0,[rbp-9]; movups [rbx+0x120],xmm0;
// movups xmm1,[rbp+7]; movups [rbx+0x130],xmm1; mov eax,[rbp+0x17]; mov [rbx+0x140],eax.
constexpr const char* kBelchAxisSignature = "4C 8D 4D F7 49 8B 56 78 4C 8D 45 C7 48 8B C8 41 FF 92 C8 00 00 "
                                            "00 84 C0 74 1F 0F 10 45 F7 0F 11 83 20 01 "
                                            "00 00 0F 10 4D 07 0F 11 8B 30 01 00 00 8B 45 17 89 83 40 01";
constexpr std::size_t kBelchAxisStore = 0x1A; // movups xmm0,[rbp-9]
constexpr std::ptrdiff_t kMuzzleAxisFromRbp = -9;
constexpr std::size_t kHandsOwner = 0x358; // idHands::owner

LogCap g_belchLines{6, 2000};

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

void onLaunch(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    const auto* item = reinterpret_cast<const std::byte*>(regs.rsi);
    const auto* player = reinterpret_cast<const std::byte*>(regs.r14);
    int slot = -1;
    if (!safeRead(item, slot) || (slot != kLeftShoulderSlot && slot != kRightShoulderSlot) ||
        !isPlayerSafe(player)) {
        return;
    }
    auto* origin = reinterpret_cast<std::byte*>(regs.rbp + kOriginFromRbp);
    auto* axis = reinterpret_cast<std::byte*>(regs.rbp + kAxisFromRbp);
    float gameOrigin[3];
    float gameAxis[9];
    float eye[3];
    if (!readFloats(origin, gameOrigin, 3) || !readFloats(axis, gameAxis, 9) ||
        !readFloats(player + kPlayerFirstPersonOrigin, eye, 3)) {
        return;
    }
    WorldHand world;
    {
        std::lock_guard lock(s.viewMutex);
        world = s.world;
    }
    const bool usable = cfg.aim == input::AimSource::Hand && s.attached.load(std::memory_order_acquire) &&
                        world.valid && secondsSince(world.qpc) <= kWorldStaleSeconds && !s.yielding.load();
    std::uint64_t skipped = 0;
    const bool log = g_launchLines.due(GetTickCount64(), skipped);
    if (!usable || cfg.actionAim.equipment == input::ActionAimSource::Same) {
        if (log) {
            EVR_LOG(
                "%s: equipment launch (slot %d): origin (%.2f %.2f %.2f) dir (%.3f %.3f %.3f), the game's",
                kTag, slot, gameOrigin[0], gameOrigin[1], gameOrigin[2], gameAxis[0], gameAxis[1],
                gameAxis[2]);
        }
        return;
    }
    const bool offHand = cfg.actionAim.equipment == input::ActionAimSource::OffHand && world.offAimValid;
    const xr_math::EyeRelativePose& ray = offHand ? world.offAim : world.head;
    const Vec3 eyeAt{eye[0], eye[1], eye[2]};
    const std::optional<Vec3> direction = xr_math::carryAimOffset({gameAxis[0], gameAxis[1], gameAxis[2]},
                                                                  world.aim.axis.forward, ray.axis.forward);
    if (!direction) {
        return;
    }
    const std::optional<xr_math::Shot> shot =
        xr_math::shotFromHand(eyeAt, ray.offset, *direction, kMaxReachMetres * world.unitsPerMetre, 0.0f);
    if (!shot || !mp_guard::allowsGameTouch()) {
        return;
    }
    const float newOrigin[3] = {shot->origin.x, shot->origin.y, shot->origin.z};
    const float newAxis[9] = {shot->axis.forward.x, shot->axis.forward.y, shot->axis.forward.z,
                              shot->axis.left.x,    shot->axis.left.y,    shot->axis.left.z,
                              shot->axis.up.x,      shot->axis.up.y,      shot->axis.up.z};
    safeCopy(origin, newOrigin, sizeof(newOrigin));
    safeCopy(axis, newAxis, sizeof(newAxis));
    if (log) {
        const Vec3 hand = world.aim.axis.forward;
        EVR_LOG("%s: equipment launch (slot %d): origin (%.2f %.2f %.2f) dir (%.3f %.3f %.3f) "
                "-> (%.2f %.2f %.2f) dir (%.3f %.3f %.3f) along the %s; weapon hand (%.3f %.3f %.3f), "
                "eye (%.2f %.2f %.2f)",
                kTag, slot, gameOrigin[0], gameOrigin[1], gameOrigin[2], gameAxis[0], gameAxis[1],
                gameAxis[2], newOrigin[0], newOrigin[1], newOrigin[2], newAxis[0], newAxis[1], newAxis[2],
                offHand ? "off hand" : "head", hand.x, hand.y, hand.z, eye[0], eye[1], eye[2]);
    }
}

void onBelchAxis(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    const auto* hands = reinterpret_cast<const std::byte*>(regs.r13);
    const std::byte* owner = nullptr;
    if (cfg.aim != input::AimSource::Hand || cfg.actionAim.equipment == input::ActionAimSource::Same ||
        !safeRead(hands + kHandsOwner, owner) || !isPlayerSafe(owner)) {
        return;
    }
    WorldHand world;
    {
        std::lock_guard lock(s.viewMutex);
        world = s.world;
    }
    if (!s.attached.load(std::memory_order_acquire) || !world.valid ||
        secondsSince(world.qpc) > kWorldStaleSeconds || s.yielding.load()) {
        return;
    }
    auto* axis = reinterpret_cast<std::byte*>(regs.rbp + kMuzzleAxisFromRbp);
    float gameAxis[9];
    if (!readFloats(axis, gameAxis, 9)) {
        return;
    }
    const bool offHand = cfg.actionAim.equipment == input::ActionAimSource::OffHand && world.offAimValid;
    const xr_math::EyeRelativePose& ray = offHand ? world.offAim : world.head;
    const std::optional<xr_math::IdViewAxis> turned = xr_math::axisFromDirection(ray.axis.forward);
    if (!turned || !mp_guard::allowsGameTouch()) {
        return;
    }
    const float newAxis[9] = {turned->forward.x, turned->forward.y, turned->forward.z,
                              turned->left.x,    turned->left.y,    turned->left.z,
                              turned->up.x,      turned->up.y,      turned->up.z};
    safeCopy(axis, newAxis, sizeof(newAxis));
    std::uint64_t skipped = 0;
    if (game::contains(heldActions(), game::GameAction::FlameBelch) &&
        g_belchLines.due(GetTickCount64(), skipped)) {
        const Vec3 hand = world.aim.axis.forward;
        EVR_LOG("%s: Flame Belch plume axis (%.3f %.3f %.3f) -> (%.3f %.3f %.3f) along the %s; weapon hand "
                "(%.3f %.3f %.3f)",
                kTag, gameAxis[0], gameAxis[1], gameAxis[2], newAxis[0], newAxis[1], newAxis[2],
                offHand ? "off hand" : "head", hand.x, hand.y, hand.z);
    }
}

} // namespace

bool installBelchAxisHook() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* site = findUnique(image, kTag, "Flame Belch axis store", kBelchAxisSignature);
    if (!site) {
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site + kBelchAxisStore), &onBelchAxis, error)) {
        EVR_LOG("%s: Flame Belch axis hook at RVA 0x%X failed: %s", kTag, image.rva(site + kBelchAxisStore),
                error.c_str());
        return false;
    }
    EVR_LOG("%s: Flame Belch axis hook at RVA 0x%X", kTag, image.rva(site + kBelchAxisStore));
    return true;
}

bool installEquipmentLaunchHook() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* site = findUnique(image, kTag, "equipment launch call", kLaunchSignature);
    const std::byte* core = findUnique(image, kTag, "equipment launch core", kLaunchCoreSignature);
    if (!site || !core) {
        return false;
    }
    if (site + kLaunchCall + 5 + readI32(site + kLaunchCall + 1) != core) {
        EVR_LOG("%s: the equipment launch call does not reach the launch core; equipment aim hook off", kTag);
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site), &onLaunch, error)) {
        EVR_LOG("%s: equipment launch hook at RVA 0x%X failed: %s", kTag, image.rva(site), error.c_str());
        return false;
    }
    EVR_LOG("%s: equipment launch hook at RVA 0x%X", kTag, image.rva(site));
    return true;
}

} // namespace evr::vkcore::controllers
