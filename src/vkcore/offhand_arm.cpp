// The free off hand's arm (offhand_arm.hpp).

#include "vkcore/offhand_arm.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/arm_solve.hpp"
#include "features/arm/arm_surfaces.hpp"
#include "features/arm/hand_offset.hpp"
#include "features/arm/mod_mix.hpp"
#include "features/arm/twist_fault.hpp"
#include "vkcore/arm_dump.hpp"
#include "vkcore/arm_solve_mode.hpp"
#include "vkcore/game_arm.hpp"
#include "vkcore/hands_surfaces.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/offhand_fingers.hpp"

#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>

namespace evr::vkcore::controllers::offhand_arm {

namespace {

constexpr const char* kTag = "offhand";

// Type info (build 25216728): idHands' left attach joint's index (the off-hand hook's byte check).
constexpr std::size_t kHandsLeftAttachJoint = 0x28E4;
// The attach joint sits about half a metre from the wrist, so turning the hand swings it further.
constexpr float kMaxAttachMetres = 3.0f;
constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

// Hook thread only.
ULONGLONG g_lastPoseTrace = 0;
bool g_loggedBones = false;
// The left arm's twist of the last tick it was solved (arm_solve.hpp), until the arm is given back.
std::optional<float> g_lastTwist;
// The attach joint's change the game was handed last tick, the layer's or its own (internal units), and the
// hands it was handed for, whose read of this tick has mixed it in (a solve that writes past, mod_mix.hpp).
std::optional<arm::HandedMod> g_lastMod;

Vec3 scaled(Vec3 v, Vec3 s) {
    return {v.x * s.x, v.y * s.y, v.z * s.z};
}

Vec3 unscaled(Vec3 v, Vec3 s) {
    return {v.x / s.x, v.y / s.y, v.z / s.z};
}

} // namespace

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

std::optional<xr_math::JointMod> pose(const std::byte* hands,
                                      const xr_math::ModelPose& animatedAttach,
                                      const xr_math::JointMod& game,
                                      const WorldHand& world,
                                      const ModelPlacement& model,
                                      const input::ControllerSettings& cfg,
                                      float weight,
                                      std::uint64_t rejected) {
    using arm::ArmJoint;
    using arm::index;
    std::int16_t attachJoint = -1;
    if (!safeRead(hands + kHandsLeftAttachJoint, attachJoint)) {
        return std::nullopt;
    }
    const auto read = game_arm::readArm(arm::ArmSide::Left, hands, attachJoint, &animatedAttach);
    if (!read) {
        return std::nullopt;
    }
    const arm::ArmPoses& animated = read->animated;
    const float upm = world.unitsPerMetre;
    const arm::SolveMode solve = armSolveMode();
    // A solve that writes past allows for the blend's mix: the read holds last tick's change of the attach
    // joint, mixed in, so the arm without modifiers is taken back from it, and everything is written past its
    // target.
    const std::optional<float> mix = pastWeight(solve, hands);
    // Other hands' (a new level's) read has not mixed it in: dropped.
    const std::optional<xr_math::JointMod> handed = arm::handedTo(g_lastMod, hands);
    g_lastMod = handed ? g_lastMod : std::nullopt;
    xr_math::JointMod last = handed.value_or(xr_math::JointMod{});
    last.translation = scaled(last.translation, read->scale);
    const arm::ArmPoses pre = mix && handed ? arm::preModifierArm(animated, last, *mix) : animated;

    // The targets in the arms model's space.
    const game::WeaponOffset o = input::offhandOffsetFor(cfg.offhandOffset, cfg.handedness);
    const arm::HandOffset offset{Vec3{o.forward, o.left, o.up} * upm, {o.pitch, o.yaw, o.roll}};
    arm::ArmTargets targets;
    targets.hand = arm::wristFromGrip(xr_math::inModelSpace(model.pose, world.offGrip), offset);
    targets.shoulder =
        input::shoulderAnchorFor(cfg.offhandShoulder, cfg.handedness) == input::ShoulderAnchor::Model
            ? animated[index(ArmJoint::UpperArm)].position
            : xr_math::inModelSpace(model.pose, {world.offShoulder, model.pose.axis}).position;
    targets.pole = arm::toLocal(model.pose.axis, world.offElbow);
    targets.lastTwist = g_lastTwist;
    targets.twistFrom = solve.twistFrom;
    targets.limitSwing = solve.limitSwing;
    // Hands only draws the glove alone: on its target wherever it is, turned as the controller is.
    targets = cfg.armsHandsOnly ? arm::handsOnlyTargets(targets) : targets;
    auto ours = arm::solveArm(animated, targets);
    if (!ours) {
        return std::nullopt;
    }
    g_lastTwist = ours->twist;
    if (cfg.testArmFault == input::ArmFault::Twist) {
        arm::plantTwistFault(*ours, animated);
    }

    // The game's arm: its modifier moves the attach joint and the rest follow. Mixed at the wrist, and the
    // attach joint placed to carry the mixed wrist.
    xr_math::JointMod gameInUnits = game;
    gameInUnits.translation = scaled(game.translation, read->scale);
    const arm::ArmPoses gameArm =
        mix ? arm::gameArmMixed(pre, gameInUnits, *mix)
            : arm::followAttach(animated,
                                xr_math::applyJointMod(animated[index(ArmJoint::Attach)], gameInUnits));
    arm::ArmPoses mixed;
    for (std::size_t i = 0; i < arm::kArmJointCount; ++i) {
        mixed[i] = arm::blendPose(gameArm[i], ours->joints[i], weight);
    }
    mixed[index(ArmJoint::Attach)] =
        arm::parentFor(mixed[index(ArmJoint::Hand)],
                       arm::relative(animated[index(ArmJoint::Attach)], animated[index(ArmJoint::Hand)]));
    const arm::HandsOnlyArm gloves = game_arm::handsOnlyFor(mixed, cfg);
    mixed = gloves.joints;

    // Written past, the wrist takes the rest of a turn the attach joint cannot make (mod_mix.hpp).
    const arm::WristArm out = mix ? arm::modifiedWithWrist(pre, mixed, *mix) : arm::WristArm{mixed, false};
    const arm::ArmPoses& written = out.modified;
    xr_math::JointMod wrist = arm::wristChange(pre, out);
    wrist.translation = unscaled(wrist.translation, read->scale);
    xr_math::JointMod mod =
        xr_math::jointModToward(pre[index(ArmJoint::Attach)], written[index(ArmJoint::Attach)]);
    mod.translation = unscaled(mod.translation, read->scale);
    const float wristMove =
        length(mixed[index(ArmJoint::Hand)].position - animated[index(ArmJoint::Hand)].position);
    // The fingers from the controller (hand poses, the bind solve only; offhand_fingers.hpp).
    const offhand_fingers::FingerWrites* fingers =
        offhand_fingers::pose(hands, read->landed < 0.0f, weight, cfg);
    // Hands only: a far controller still gets the glove, never the game's whole arm (arm::reachLimits).
    const arm::ReachLimits limit =
        arm::reachLimits(cfg.armsHandsOnly, wristMove, kMaxReachMetres * upm, kMaxAttachMetres * upm);
    if (!(wristMove <= limit.wrist) || !xr_math::plausibleJointMod(mod, limit.attach) ||
        !xr_math::plausibleJointMod(wrist, limit.wrist) || !mp_guard::allowsGameTouch() ||
        !game_arm::writeArm(arm::ArmSide::Left, hands, written, read->scale, read->origin, gloves.zeroScale,
                            &mixed, &wrist, nullptr, fingers)) {
        return std::nullopt;
    }

    if (cfg.testArmModWeight) {
        game_arm::holdModWeight(hands, *cfg.testArmModWeight);
    }
    // The weapon's mesh kit may hide this arm (HideAll weapons): shown while the layer poses it.
    hands_surfaces::update(arm::ArmSide::Left, hands, true);
    arm_dump::noteLayerArm(arm::ArmSide::Left, weight, ours->twist, *read, mixed, &targets.hand,
                           gloves.zeroScale);

    if (!g_loggedBones) {
        g_loggedBones = true;
        EVR_LOG("%s: arm: upper arm %.3f, forearm %.3f (animated, game units), model scale (%.3f %.3f %.3f), "
                "shoulder %s, wrist offset (%.3f %.3f %.3f) m (%.1f %.1f %.1f) deg",
                kTag, ours->upper, ours->lower, read->scale.x, read->scale.y, read->scale.z,
                input::shoulderAnchorName(input::shoulderAnchorFor(cfg.offhandShoulder, cfg.handedness)),
                o.forward, o.left, o.up, o.pitch, o.yaw, o.roll);
    }
    if (poseTraceDue(cfg)) {
        const Vec3 w = mixed[index(ArmJoint::Hand)].position;
        const Vec3 t = targets.hand.position;
        const Vec3 e = mixed[index(ArmJoint::ForeArm)].position;
        const Vec3 s = mixed[index(ArmJoint::UpperArm)].position;
        EVR_LOG(
            "%s: ik: wrist (%.2f %.2f %.2f) target (%.2f %.2f %.2f), reach %.2f%s, elbow (%.2f %.2f %.2f), "
            "shoulder (%.2f %.2f %.2f), twist %.0f deg (forearm %.0f, upper arm %.0f), swing %.0f/%.0f "
            "deg%s, weight %.2f; %llu rejected; left arm surface %s",
            kTag, w.x, w.y, w.z, t.x, t.y, t.z, ours->ik.reach, ours->ik.clamped ? " (clamped)" : "", e.x,
            e.y, e.z, s.x, s.y, s.z, ours->twist * kDegreesPerRadian, ours->split.forearm * kDegreesPerRadian,
            ours->split.upper * kDegreesPerRadian, ours->swing.flex * kDegreesPerRadian,
            ours->swing.deviation * kDegreesPerRadian, ours->swing.clamped ? " (limited)" : "", weight,
            static_cast<unsigned long long>(rejected),
            arm::surfaceStateName(hands_surfaces::state(arm::ArmSide::Left)));
        offhand_fingers::trace(hands, kTag);
    }
    return mod;
}

void giveBack(const std::byte* hands) {
    g_lastTwist.reset();
    offhand_fingers::giveBack();
    if (settings().testArmModWeight) {
        game_arm::releaseModWeight(hands);
    }
    game_arm::releaseArm(arm::ArmSide::Left, hands);
    hands_surfaces::update(arm::ArmSide::Left, hands, false);
}

void noteAttachMod(const void* hands, const std::optional<xr_math::JointMod>& mod) {
    g_lastMod = mod ? std::optional(arm::HandedMod{hands, *mod}) : std::nullopt;
}

} // namespace evr::vkcore::controllers::offhand_arm
