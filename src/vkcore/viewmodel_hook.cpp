// Motion controllers, the viewmodel (controllers.hpp, docs/rig-findings/input-aim.md section 3, T-054).
//
// - endGameView (camera hook): the weapon hand's aim and grip poses relative to the game's eye, for this
//   file's hook and the shot hook; the view yaw the mapper rotates movement into; and the weapon FOV:
//   renderView_t's weaponFOVX/Y and customFOV2X/Y get the headset FOV the camera hook wrote into fov_x/y,
//   so the arms and weapon are drawn with the world's projection.
// - A mid-hook in idHands::UpdatePosition just before the render model's stores (RVA 0x13807EA): the final
//   origin [rsp+0x48] and axis [rbp-0x30] are replaced with the controller's grip, turned to its aim ray,
//   plus the held weapon's offset from the table (data/weapons/viewmodel_offsets.toml). The game's own
//   stores then carry it into both the deferred and the current render-model pose.

#include "vkcore/controllers_impl.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/room_scale.hpp"
#include "xr_math/hand_aim.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <numbers>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// input-aim.md section 3, extended to the origin load that follows (movsd xmm0,[rsp+0x48]).
constexpr const char* kViewmodelSignature =
    "0F B6 86 ?? ?? ?? ?? 0F 10 45 D0 24 0C 3C 0C 74 54 0F 11 86 ?? ?? ?? ?? 0F 10 4D E0 0F 11 8E ?? ?? ?? "
    "?? 8B "
    "45 F0 89 86 ?? ?? ?? ?? 0F 10 45 D0 0F 11 86 ?? ?? ?? ?? 0F 10 4D E0 0F 11 8E ?? ?? ?? ?? 8B 45 F0 89 "
    "86 "
    "?? ?? ?? ?? F2 0F 10 44 24 48 F2 0F 11 86 ?? ?? ?? ?? 8B 44 24 50";
// The render model's fields the stores name: flags, g.axis (three parts), deferredAxis (three parts),
// g.origin.
struct Disp {
    std::size_t at;
    std::int32_t value;
};
constexpr Disp kViewmodelDisps[] = {{3, 0xB0},     {0x14, 0x164}, {0x1F, 0x174}, {0x28, 0x184},
                                    {0x33, 0x104}, {0x3E, 0x114}, {0x47, 0x124}, {0x55, 0x158}};
constexpr std::size_t kOriginFromRsp = 0x48;
constexpr std::intptr_t kAxisFromRbp = -0x30;

// Type info (build 25216728).
constexpr std::size_t kHandsOwner = 0x358;                 // idHands::owner
constexpr std::size_t kHandsRightItemDecl = 0x29B0 + 0x8;  // idHands::rightItem.itemDecl
constexpr std::size_t kDeclName = 0x8;                     // idResource::name (idAtomicString, a char*)
constexpr std::size_t kPlayerFirstPersonOrigin = 0x16580;  // idPlayer::firstPersonViewOrigin
constexpr std::size_t kPlayerViewAngles = 0x8A50 + 0x3F10; // idHavokPhysics_Player::viewAngles
constexpr std::size_t kPlayerViewYaw = kPlayerViewAngles + 4;
// renderView_t
constexpr std::size_t kFovX = 0x28;
constexpr std::size_t kWeaponFovX = 0x30;
constexpr std::size_t kCustomFov2X = 0x38;

constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

// A melee or a Blood Punch puts the fists in the hands for the punch (weapon/player/fists_doom5melee), and
// the punch's hit follows the fists on the hands model, not the view angles. Under Melee aim with Head or Off
// hand the view is turned onto that source before the press reaches the game (action_aim_hook.cpp), so the
// hands are left where the game puts them from its view while the fists are held: the punch goes where the
// view looks (issue #15: with the arms kept on the weapon hand the punch went along the controller).
constexpr std::string_view kFistsPrefix = "weapon/player/fists";

// The held item's decl and its offset (viewmodel hook thread only).
const std::byte* g_cachedDecl = nullptr;
bool g_cachedSeated = false;
game::WeaponOffset g_cachedOffset;
bool g_cachedFists = false; // the held item is the fists of a melee or a Blood Punch
std::atomic<std::uint64_t> g_loggedFists{0};
std::atomic<std::uint64_t> g_loggedWeapons{0};
std::atomic<std::uint64_t> g_loggedPlacements{0};
std::atomic<bool> g_loggedEyeMismatch{false};

// The decl name of the item in the right hand, or empty.
std::string declName(const std::byte* decl) {
    const char* name = nullptr;
    if (!safeRead(decl + kDeclName, name) || !name) {
        return {};
    }
    char buffer[128] = {};
    if (!safeCopy(buffer, name, sizeof(buffer) - 1)) {
        // The string may end near the end of its page; read it a byte at a time.
        for (std::size_t i = 0; i + 1 < sizeof(buffer); ++i) {
            if (!safeCopy(buffer + i, name + i, 1) || buffer[i] == '\0') {
                break;
            }
        }
    }
    buffer[sizeof(buffer) - 1] = '\0';
    const std::string_view text(buffer);
    for (const char c : text) {
        if (c < 0x20 || c > 0x7E) {
            return {};
        }
    }
    return std::string(text);
}

game::WeaponOffset offsetFor(const std::byte* hands) {
    const input::ControllerSettings& cfg = settings();
    if (cfg.viewmodelOffset) {
        return *cfg.viewmodelOffset;
    }
    const std::byte* decl = nullptr;
    safeRead(hands + kHandsRightItemDecl, decl);
    // Seated offsets when ETERNALVR_SEATED says so or the room's posture is seated (T-074).
    const bool seated = cfg.seated || roomPosture() == posture::Posture::Seated;
    if (decl != g_cachedDecl || seated != g_cachedSeated) {
        g_cachedDecl = decl;
        g_cachedSeated = seated;
        const std::string name = decl ? declName(decl) : std::string{};
        g_cachedFists = name.starts_with(kFistsPrefix);
        g_cachedOffset = state().offsets.lookup(name, seated ? game::OffsetPosture::Seated
                                                             : game::OffsetPosture::Standing);
        if (g_loggedWeapons.fetch_add(1) < 40) {
            EVR_LOG("%s: held item '%s': viewmodel offset (%.2f %.2f %.2f) m, (%.1f %.1f %.1f) deg", kTag,
                    name.c_str(), g_cachedOffset.forward, g_cachedOffset.left, g_cachedOffset.up,
                    g_cachedOffset.pitch, g_cachedOffset.yaw, g_cachedOffset.roll);
        }
    }
    return g_cachedOffset;
}

void onViewmodel(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    if (!cfg.viewmodel || !s.attached.load(std::memory_order_acquire) || s.yielding.load()) {
        return; // forced views (glory kills, cutscenes) keep the game's own hands animation
    }
    const auto* hands = reinterpret_cast<const std::byte*>(regs.rdi);
    const std::byte* owner = nullptr;
    if (!safeRead(hands + kHandsOwner, owner) || !isPlayerSafe(owner)) {
        return;
    }
    WorldHand world;
    {
        std::lock_guard lock(s.viewMutex);
        world = s.world;
    }
    float eye[3];
    if (!world.valid || secondsSince(world.qpc) > kWorldStaleSeconds ||
        !safeCopy(eye, owner + kPlayerFirstPersonOrigin, sizeof(eye)) || !std::isfinite(eye[0]) ||
        !std::isfinite(eye[1]) || !std::isfinite(eye[2])) {
        return;
    }
    const game::WeaponOffset offset = offsetFor(hands);
    if (g_cachedFists && cfg.aim == input::AimSource::Hand &&
        cfg.actionAim.melee != input::ActionAimSource::Same) {
        if (g_loggedFists.fetch_add(1) == 0) {
            EVR_LOG("%s: viewmodel: the fists follow the game's view, on the melee aim's %s", kTag,
                    input::actionAimSourceName(cfg.actionAim.melee));
        }
        return;
    }
    const xr_math::EyeRelativePose placed =
        xr_math::applyLocalOffset(world.grip, {offset.forward, offset.left, offset.up},
                                  {offset.pitch, offset.yaw, offset.roll}, world.unitsPerMetre);
    const Vec3 origin = xr_math::atEye({eye[0], eye[1], eye[2]}, placed);
    const float newOrigin[3] = {origin.x, origin.y, origin.z};
    const float newAxis[9] = {placed.axis.forward.x, placed.axis.forward.y, placed.axis.forward.z,
                              placed.axis.left.x,    placed.axis.left.y,    placed.axis.left.z,
                              placed.axis.up.x,      placed.axis.up.y,      placed.axis.up.z};
    auto* originAt = reinterpret_cast<std::byte*>(regs.rsp + kOriginFromRsp);
    auto* axisAt = reinterpret_cast<std::byte*>(static_cast<std::intptr_t>(regs.rbp) + kAxisFromRbp);
    float gameOrigin[3] = {};
    safeCopy(gameOrigin, originAt, sizeof(gameOrigin));
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    safeCopy(originAt, newOrigin, sizeof(newOrigin));
    safeCopy(axisAt, newAxis, sizeof(newAxis));
    s.viewmodelWrites.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lock(s.viewMutex);
        s.model = {true, nowQpc(), placed}; // the off hand's joint is placed relative to this
    }
    if (g_loggedPlacements.fetch_add(1) < 5) {
        EVR_LOG("%s: viewmodel: game origin (%.2f %.2f %.2f) -> hand (%.2f %.2f %.2f); eye (%.2f %.2f %.2f)",
                kTag, gameOrigin[0], gameOrigin[1], gameOrigin[2], newOrigin[0], newOrigin[1], newOrigin[2],
                eye[0], eye[1], eye[2]);
    }
}

bool viewmodelSiteChecks(const std::byte* site) {
    for (const Disp& d : kViewmodelDisps) {
        if (readI32(site + d.at) != d.value) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string itemDeclName(const std::byte* decl) {
    return declName(decl);
}

const std::byte* heldItemDecl(const std::byte* hands) {
    const std::byte* decl = nullptr;
    return hands && safeRead(hands + kHandsRightItemDecl, decl) ? decl : nullptr;
}

std::optional<WorldRay> weaponRayInWorld(Vec3 eye) {
    State& s = state();
    if (!s.attached.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    std::lock_guard lock(s.viewMutex);
    if (!s.world.valid) {
        return std::nullopt;
    }
    return WorldRay{eye + s.world.aim.offset, s.world.aim.axis.forward};
}

void endGameView(std::byte* renderView,
                 const std::byte* player,
                 const xr_math::IdViewAxis& body,
                 Vec3 eye,
                 Vec3 headOffset,
                 float unitsPerMetre) {
    State& s = state();
    if (!s.attached.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    const input::ControllerSettings& cfg = settings();
    if (cfg.weaponFov) {
        float fov[2];
        std::memcpy(fov, renderView + kFovX, sizeof(fov));
        std::memcpy(renderView + kWeaponFovX, fov, sizeof(fov));
        std::memcpy(renderView + kCustomFov2X, fov, sizeof(fov));
    }

    // Piloting a demon (demon_view.hpp): the game built this view from the demon's own camera, which is the
    // frame its movement follows. The view is built through the idPlayer, whose own view yaw stays where the
    // Slayer stood, so it must not set the view's yaw below.
    const bool demon = isPilotedDemon(player, s.player);
    notePilotedDemon(demon);
    const bool piloting = demon && pilotingDemon();
    if (piloting) {
        // With demon aim the view is the body plus the head's yaw, the same as the Slayer's under head aim;
        // without it, the demon's camera.
        float yaw = 0.0f;
        if (const std::optional<PilotAim> aim = pilotAim()) {
            yaw = xr_math::normalize180(aim->aimYaw - aim->bodyYaw);
        }
        s.viewYawTracking.store(yaw * kRadiansPerDegree, std::memory_order_relaxed);
    }

    // The view's yaw in tracking space: the game's view yaw less the body's.
    float viewYaw = 0.0f;
    if (!piloting && s.player.isPlayer(player) && safeRead(player + kPlayerViewYaw, viewYaw) &&
        std::isfinite(viewYaw)) {
        const float bodyYaw = std::atan2(body.forward.y, body.forward.x) / kRadiansPerDegree;
        s.viewYawTracking.store(xr_math::normalize180(viewYaw - bodyYaw) * kRadiansPerDegree,
                                std::memory_order_relaxed);
        // The fire and viewmodel hooks add the hand to the player's first-person origin; it should be the
        // eye this view was built from.
        float fp[3];
        if (!g_loggedEyeMismatch.load() && safeCopy(fp, player + kPlayerFirstPersonOrigin, sizeof(fp))) {
            const float d = std::sqrt((fp[0] - eye.x) * (fp[0] - eye.x) + (fp[1] - eye.y) * (fp[1] - eye.y) +
                                      (fp[2] - eye.z) * (fp[2] - eye.z));
            if (d > 0.05f * unitsPerMetre && !g_loggedEyeMismatch.exchange(true)) {
                EVR_LOG("%s: the player's first-person origin (%.2f %.2f %.2f) is %.2f from the view origin "
                        "(%.2f %.2f %.2f)",
                        kTag, fp[0], fp[1], fp[2], d, eye.x, eye.y, eye.z);
            }
        }
    }

    const std::size_t hand = static_cast<std::size_t>(weaponHand());
    std::lock_guard lock(s.viewMutex);
    const GameViewPoses& p = s.poses;
    if (!p.valid || !p.aimValid[hand]) {
        s.world.valid = false;
        return;
    }
    WorldHand world;
    world.aim = xr_math::controllerRelativeToEye(body, headOffset, p.head, p.aim[hand], unitsPerMetre);
    // The grip gives the position; the barrel follows the aim ray, so shots and the gun agree.
    world.grip = p.gripValid[hand]
                     ? xr_math::controllerRelativeToEye(body, headOffset, p.head, p.grip[hand], unitsPerMetre)
                     : world.aim;
    world.grip.axis = world.aim.axis;
    // The head's yaw frame in the world: the arms' shoulders and their elbows' bend follow the head's
    // heading.
    const xr_math::IdViewAxis headAxis =
        xr_math::composeHeadAxis(body, xr_math::openXrToIdTech(normalize(p.head.orientation)));
    const xr_math::IdViewAxis yaw = xr_math::yawOnly(headAxis).value_or(body);
    const auto inYaw = [&yaw](const game::WeaponOffset& o) {
        return yaw.forward * o.forward + yaw.left * o.left + yaw.up * o.up;
    };
    // The weapon arm (weapon_arm.cpp): the off hand's shoulder and elbow on the weapon hand's side.
    world.weaponShoulder =
        headOffset + inYaw(cfg.weaponArmTestShoulder
                               ? *cfg.weaponArmTestShoulder
                               : input::weaponArmOffsetFor(cfg.offhandShoulderOffset, cfg.handedness)) *
                         unitsPerMetre;
    world.weaponElbow = inYaw(input::weaponArmOffsetFor(cfg.offhandElbow, cfg.handedness));
    // The off hand (the left arm, offhand_hook.cpp): its grip with its own orientation.
    const std::size_t off = 1 - hand;
    world.offValid = p.gripValid[off] || p.aimValid[off];
    if (world.offValid) {
        world.offGrip = xr_math::controllerRelativeToEye(
            body, headOffset, p.head, p.gripValid[off] ? p.grip[off] : p.aim[off], unitsPerMetre);
        // Given for the left hand; mirrored with the weapon in the left hand.
        world.offShoulder =
            headOffset +
            inYaw(input::offhandOffsetFor(cfg.offhandShoulderOffset, cfg.handedness)) * unitsPerMetre;
        world.offElbow = inYaw(input::offhandOffsetFor(cfg.offhandElbow, cfg.handedness));
    }
    // The off hand's aim ray and the head, for a Flame Belch shot under ETERNALVR_EQUIPMENT_AIM.
    world.offAimValid = p.aimValid[off];
    if (world.offAimValid) {
        world.offAim = xr_math::controllerRelativeToEye(body, headOffset, p.head, p.aim[off], unitsPerMetre);
    }
    world.head = xr_math::controllerRelativeToEye(body, headOffset, p.head, p.head, unitsPerMetre);
    world.unitsPerMetre = unitsPerMetre;
    world.qpc = nowQpc();
    world.valid = true;
    s.world = world;
    noteActionPressView(s, player, body);
    if (cfg.trace) {
        static ULONGLONG lastTrace = 0;
        const ULONGLONG ticks = GetTickCount64();
        float view[3] = {};
        if (ticks - lastTrace >= 250 && safeCopy(view, player + kPlayerViewAngles, sizeof(view))) {
            lastTrace = ticks;
            const auto handAngles =
                xr_math::anglesOfDirection(world.aim.axis.forward).value_or(xr_math::IdAngles{});
            EVR_LOG(
                "%s: trace: eye (%.2f %.2f %.2f) view (%.2f %.2f) body %.2f hand (%.2f %.2f) offset (%.2f "
                "%.2f %.2f)%s",
                kTag, eye.x, eye.y, eye.z, view[0], view[1],
                std::atan2(body.forward.y, body.forward.x) / kRadiansPerDegree, handAngles.pitch,
                handAngles.yaw, world.grip.offset.x, world.grip.offset.y, world.grip.offset.z,
                s.yielding.load() ? " forced" : "");
        }
    }
}

bool installViewmodelHook() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* site = findUnique(image, kTag, "viewmodel store", kViewmodelSignature);
    if (!site) {
        return false;
    }
    if (!viewmodelSiteChecks(site)) {
        EVR_LOG("%s: the viewmodel stores do not name the render model's pose fields; viewmodel hook off",
                kTag);
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site), &onViewmodel, error)) {
        EVR_LOG("%s: viewmodel hook at RVA 0x%X failed: %s", kTag, image.rva(site), error.c_str());
        return false;
    }
    EVR_LOG("%s: viewmodel hook at RVA 0x%X", kTag, image.rva(site));
    return true;
}

} // namespace evr::vkcore::controllers
