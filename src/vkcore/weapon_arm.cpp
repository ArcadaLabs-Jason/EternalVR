// The weapon arm (weapon_arm.hpp).

#include "vkcore/weapon_arm.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/arm_solve.hpp"
#include "features/arm/arm_surfaces.hpp"
#include "vkcore/game_arm.hpp"
#include "vkcore/hands_surfaces.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/offhand_pose.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <numbers>
#include <optional>

namespace evr::vkcore::controllers::weapon_arm {

namespace {

constexpr const char* kTag = "weapon arm";
constexpr arm::ArmSide kSide = arm::ArmSide::Right;

// Type info (build 25216728): the right attach joint's index, stored by InitJointMods' second AddJointMod
// (checked at install, offhand_mods.hpp).
constexpr std::size_t kHandsRightAttachJoint = 0x28E6;

// No solved joint is further than this from the wrist (the arm is about 0.56 long): a read that runs away
// is not written.
constexpr float kMaxArmMetres = 1.5f;
constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

// Off-hand hook thread only.
input::ArmBlend g_blend;
input::ArmDecision g_lastDecision{false, input::ArmReason::ModeGame};
bool g_traced = false;
std::uint64_t g_traceLines = 0;
ULONGLONG g_lastPoseTrace = 0;
bool g_loggedBones = false;
std::atomic<std::uint64_t> g_rejected{0};
// The hands the arm was last posed on, and whether the one give-back after a multiplayer guard trip ran.
const std::byte* g_posedHands = nullptr;
bool g_tripReleased = false;

// The arm back to the game: the layer's modifiers to no change, the arm's surface as the weapon's kit has it.
void giveBack(const std::byte* hands) {
    game_arm::releaseArm(kSide, hands);
    hands_surfaces::update(kSide, hands, false);
}

void trace(const input::ArmDecision& d, float weight) {
    if ((g_traced && d.controller == g_lastDecision.controller && d.reason == g_lastDecision.reason) ||
        g_traceLines >= 400) {
        return;
    }
    g_traced = true;
    g_lastDecision = d;
    ++g_traceLines;
    EVR_LOG("%s: %s (%s), weight %.2f", kTag, d.controller ? "ik" : "game", input::armReasonName(d.reason),
            weight);
}

bool poseTraceDue(const input::ControllerSettings& cfg) {
    if (!cfg.offhandTrace) {
        return false;
    }
    const ULONGLONG ticks = GetTickCount64();
    if (ticks - g_lastPoseTrace < 1000) {
        return false;
    }
    g_lastPoseTrace = ticks;
    return true;
}

// The arm solved to the wrist and mixed with the game's by `weight`, written; false when it cannot be used
// this tick (nothing written).
bool poseArm(const std::byte* hands,
             const WorldHand& world,
             const ModelPlacement& model,
             const input::ControllerSettings& cfg,
             float weight) {
    using arm::ArmJoint;
    using arm::index;
    std::int16_t attachJoint = -1;
    if (!safeRead(hands + kHandsRightAttachJoint, attachJoint)) {
        return false;
    }
    const auto read = game_arm::readArm(kSide, hands, attachJoint, nullptr);
    if (!read) {
        return false;
    }
    const arm::ArmPoses& animated = read->animated;
    const float upm = world.unitsPerMetre;

    // The shoulder and the elbow's bend in the arms model's space; the wrist stays the game's.
    const Vec3 shoulder = xr_math::inModelSpace(model.pose, {world.weaponShoulder, model.pose.axis}).position;
    const Vec3 pole = arm::toLocal(model.pose.axis, world.weaponElbow);
    const auto ours = arm::solveArmToWrist(animated, shoulder, pole);
    if (!ours) {
        return false;
    }
    arm::ArmPoses mixed;
    for (std::size_t i = 0; i < arm::kArmJointCount; ++i) {
        mixed[i] = arm::blendPose(animated[i], ours->joints[i], weight);
    }
    const Vec3 wrist = animated[index(ArmJoint::Hand)].position;
    // How far the layer moves the arm from the game's pose: near 0 where the game's arm already reaches a
    // shoulder at the head (a flat-screen-like grip), which looks the same as ETERNALVR_WEAPON_ARM=game.
    float moved = 0.0f;
    for (const ArmJoint j : offhand_mods::kLayerJoints) {
        if (!(length(mixed[index(j)].position - wrist) <= kMaxArmMetres * upm)) {
            return false;
        }
        moved = std::max(moved, length(mixed[index(j)].position - animated[index(j)].position));
    }
    if (!mp_guard::allowsGameTouch() || !game_arm::writeArm(kSide, hands, mixed, read->scale, read->origin)) {
        return false;
    }
    g_posedHands = hands;
    // The weapon's mesh kit usually hides this arm (hands_surfaces.hpp): shown while the layer poses it.
    hands_surfaces::update(kSide, hands, true);

    const Vec3 solvedShoulder = ours->joints[index(ArmJoint::UpperArm)].position;
    if (!g_loggedBones) {
        g_loggedBones = true;
        EVR_LOG("%s: upper arm %.3f, forearm %.3f (animated, game units), model scale (%.3f %.3f %.3f); the "
                "first pose moves the arm up to %.2f from the game's%s",
                kTag, ours->upper, ours->lower, read->scale.x, read->scale.y, read->scale.z, moved,
                cfg.weaponArmTestShoulder ? " (test shoulder)" : "");
    }
    if (poseTraceDue(cfg)) {
        const Vec3 e = mixed[index(ArmJoint::ForeArm)].position;
        const Vec3 s = mixed[index(ArmJoint::UpperArm)].position;
        EVR_LOG(
            "%s: ik: wrist (%.2f %.2f %.2f), elbow (%.2f %.2f %.2f), shoulder (%.2f %.2f %.2f), %.2f from "
            "the head's point, reach %.2f, twist %.0f deg, weight %.2f, up to %.2f from the game's pose, "
            "read back within %.2f; %llu rejected; right arm surface %s",
            kTag, wrist.x, wrist.y, wrist.z, e.x, e.y, e.z, s.x, s.y, s.z, length(solvedShoulder - shoulder),
            ours->ik.reach, ours->twist * kDegreesPerRadian, weight, moved, read->landed,
            static_cast<unsigned long long>(g_rejected.load()),
            arm::surfaceStateName(hands_surfaces::state(kSide)));
    }
    return true;
}

} // namespace

void logSettings() {
    const input::ControllerSettings& cfg = settings();
    const game::WeaponOffset sh = cfg.weaponArmTestShoulder
                                      ? *cfg.weaponArmTestShoulder
                                      : input::weaponArmOffsetFor(cfg.offhandShoulderOffset, cfg.handedness);
    const game::WeaponOffset e = input::weaponArmOffsetFor(cfg.offhandElbow, cfg.handedness);
    EVR_LOG(
        "%s: ik on the arms model's right arm (righthandattach, idHands+0x%zX): the wrist stays under the "
        "gun, shoulder (%.2f %.2f %.2f) m from the eyes, elbow toward (%.2f %.2f %.2f), blend %.2f s, hold "
        "%.2f s%s%s%s",
        kTag, kHandsRightAttachJoint, sh.forward, sh.left, sh.up, e.forward, e.left, e.up,
        cfg.offhandBlendSeconds, cfg.offhandHoldSeconds,
        cfg.handedness != game::Handedness::Right ? ", for the left controller (weapon in the left hand)"
                                                  : "",
        cfg.offhandTrace ? ", trace on" : "",
        cfg.weaponArmTestShoulder ? ", TEST shoulder (ETERNALVR_WEAPON_ARM_TEST_SHOULDER)" : "");
}

void tick(const std::byte* hands,
          input::ArmSignals signals,
          const WorldHand& world,
          const ModelPlacement& model,
          float dt) {
    const input::ControllerSettings& cfg = settings();
    // The head and the weapon hand: the shoulder's point and the gun.
    signals.offHandTracked = world.valid && secondsSince(world.qpc) <= kWorldStaleSeconds;
    const input::ArmDecision decision = input::decideWeaponArm(signals, cfg.weaponArm);
    const float weight =
        g_blend.update(decision.controller, dt, cfg.offhandBlendSeconds, cfg.offhandHoldSeconds);
    if (cfg.offhandTrace) {
        trace(decision, weight);
    }
    if (weight <= 0.0f) {
        giveBack(hands);
        return;
    }
    if (!poseArm(hands, world, model, cfg, weight)) {
        g_rejected.fetch_add(1, std::memory_order_relaxed);
        giveBack(hands);
    }
}

void release(const std::byte* hands) {
    giveBack(hands);
}

void releaseOnTrip(const std::byte* hands) {
    if (g_tripReleased || (g_posedHands && hands != g_posedHands)) {
        return;
    }
    g_tripReleased = true;
    if (!g_posedHands) {
        return;
    }
    game_arm::releaseArm(kSide, hands);
    hands_surfaces::releaseAfterTrip(kSide, hands);
    EVR_LOG(
        "%s: the multiplayer guard tripped: the layer's arm modifiers set back to no change; the weapon arm "
        "is the game's",
        kTag);
}

} // namespace evr::vkcore::controllers::weapon_arm
