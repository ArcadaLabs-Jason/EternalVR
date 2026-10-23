// Motion controllers, the game's view (controllers.hpp; the viewmodel hook in viewmodel_hook.cpp):
// endGameView (camera hook): the weapon hand's aim and grip poses relative to the game's eye, for the
// viewmodel hook and the shot hook; the view yaw the mapper rotates movement into; and the weapon FOV:
// renderView_t's weaponFOVX/Y and customFOV2X/Y get the headset FOV the camera hook wrote into fov_x/y, so
// the arms and weapon are drawn with the world's projection (the game's own in a cutscene around the player
// with the arms hidden).

#include "vkcore/controllers_impl.hpp"

#include "features/input/gun_placement.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/hands_surfaces.hpp"
#include "vkcore/head_gun.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/room_scale.hpp"
#include "xr_math/hand_aim.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <numbers>
#include <optional>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// Type info (build 25216728).
constexpr std::size_t kPlayerFirstPersonOrigin = 0x16580;  // idPlayer::firstPersonViewOrigin
constexpr std::size_t kPlayerViewAngles = 0x8A50 + 0x3F10; // idHavokPhysics_Player::viewAngles
constexpr std::size_t kPlayerViewYaw = kPlayerViewAngles + 4;
// renderView_t
constexpr std::size_t kFovX = 0x28;
constexpr std::size_t kWeaponFovX = 0x30;
constexpr std::size_t kCustomFov2X = 0x38;

constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

std::atomic<bool> g_loggedEyeMismatch{false};
// A cutscene shown around the player with the arms hidden (noteImmersiveCutscene; camera hook writes).
std::atomic<bool> g_cutsceneArms{false};

// The controller's grip orientation against its aim ray, logged when it first shows and when it turns by more
// than kGripLogDegrees from the last line (camera hook thread, under the view lock), with how far the glove
// moves to keep its wrist in the grip's frame (xr_math::placeViewmodel). The simulator gives both one pose.
constexpr float kGripLogDegrees = 5.0f;
// The off hand's aim ray as last tracked and when (camera hook thread, under the view lock): a tracking blip
// keeps it for kHandHoldSeconds, as the weapon hand's aim, so the equipment's ray (an off-hand Flame Belch or
// launch) does not flip to the head and back.
xr_math::EyeRelativePose g_offAim;
LONGLONG g_offAimQpc = 0;
std::optional<xr_math::IdAngles> g_loggedGrip;
int g_gripLines = 0;

void logGripAgainstAim(const xr_math::IdViewAxis& aim, const xr_math::IdViewAxis& grip) {
    const auto inAim = [&aim](Vec3 v) {
        return Vec3{dot(aim.forward, v), dot(aim.left, v), dot(aim.up, v)};
    };
    const xr_math::IdAngles rel =
        xr_math::anglesFromAxis({inAim(grip.forward), inAim(grip.left), inAim(grip.up)});
    if (g_gripLines >= 6 || (g_loggedGrip && std::fabs(rel.pitch - g_loggedGrip->pitch) <= kGripLogDegrees &&
                             std::fabs(rel.yaw - g_loggedGrip->yaw) <= kGripLogDegrees &&
                             std::fabs(rel.roll - g_loggedGrip->roll) <= kGripLogDegrees)) {
        return;
    }
    g_loggedGrip = rel;
    ++g_gripLines;
    const game::WeaponOffset& w = game::kRowsWrist;
    const Vec3 shift =
        inAim(grip.forward * w.forward + grip.left * w.left + grip.up * w.up) - Vec3{w.forward, w.left, w.up};
    EVR_LOG(
        "%s: the weapon hand's grip against its aim ray: (%.1f %.1f %.1f) deg; the glove moves (%.1f %.1f "
        "%.1f) cm (aim frame) to keep its wrist in the grip's frame",
        kTag, rel.pitch, rel.yaw, rel.roll, shift.x * 100.0f, shift.y * 100.0f, shift.z * 100.0f);
}

} // namespace

void noteImmersiveCutscene(bool playing) {
    // Only where the arms can be hidden: the controllers on and the hands hook's surface code found. With
    // ETERNALVR_ARMS=hidden the arms are hidden anyway and nothing changes (the weapon FOV stays the
    // headset's).
    const input::ControllerSettings& cfg = settings();
    const bool hidden = playing && cfg.cutsceneArmsHidden && !cfg.armsHidden &&
                        state().attached.load(std::memory_order_acquire) && hands_surfaces::installed();
    if (g_cutsceneArms.exchange(hidden, std::memory_order_relaxed) != hidden) {
        static std::atomic<int> logged{0};
        if (logged.fetch_add(1) < 20) {
            EVR_LOG("%s: cutscene around the player %s: the arms %s, the weapon FOV %s", kTag,
                    hidden ? "starts" : "ends", hidden ? "hidden" : "as in play",
                    hidden ? "the game's" : "the headset's");
        }
    }
}

bool cutsceneArmsHidden() {
    return g_cutsceneArms.load(std::memory_order_relaxed);
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
    head_gun::noteView(headOffset); // head aim's gun follows the rendered head (head_gun.hpp)
    // In a cutscene around the player the game's hands model keeps the game's own FOV (the arms are hidden).
    if (cfg.weaponFov && !g_cutsceneArms.load(std::memory_order_relaxed)) {
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
    if (p.gripValid[hand]) {
        logGripAgainstAim(world.aim.axis, world.grip.axis);
    }
    world.gripAxis = world.grip.axis;
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
    world.leftShoulder = headOffset + inYaw(cfg.offhandShoulderOffset) * unitsPerMetre;
    world.leftElbow = inYaw(cfg.offhandElbow);
    const game::Handedness right = game::Handedness::Right;
    world.rightShoulder =
        headOffset + inYaw(input::weaponArmOffsetFor(cfg.offhandShoulderOffset, right)) * unitsPerMetre;
    world.rightElbow = inYaw(input::weaponArmOffsetFor(cfg.offhandElbow, right));
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
    if (p.aimValid[off]) {
        g_offAim = xr_math::controllerRelativeToEye(body, headOffset, p.head, p.aim[off], unitsPerMetre);
        g_offAimQpc = nowQpc();
    }
    world.offAimValid = g_offAimQpc != 0 && (p.aimValid[off] || secondsSince(g_offAimQpc) < kHandHoldSeconds);
    if (world.offAimValid) {
        world.offAim = g_offAim;
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

} // namespace evr::vkcore::controllers
