// The weapon arm (weapon_arm.hpp).

#include "vkcore/weapon_arm.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/arm_solve.hpp"
#include "features/arm/arm_surfaces.hpp"
#include "features/arm/grip_hand.hpp"
#include "features/arm/mod_mix.hpp"
#include "features/arm/twist_fault.hpp"
#include "vkcore/arm_dump.hpp"
#include "vkcore/arm_solve_mode.hpp"
#include "vkcore/game_arm.hpp"
#include "vkcore/hands_surfaces.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/weapon_arm_state.hpp"
#include "xr_math/offhand_pose.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>

namespace evr::vkcore::controllers::weapon_arm {

using detail::ArmState;
using detail::poseTraceDue;
using detail::trace;
using detail::traceTick;

namespace {

// No solved joint is further than this from the wrist (the arm is about 0.56 long): a read that runs away
// is not written.
constexpr float kMaxArmMetres = 1.5f;
constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;

ArmState g_right{arm::ArmSide::Right, 0x28E6, "weapon arm"};
ArmState g_left{arm::ArmSide::Left, 0x28E4, "weapon arm (left glove)"};

ArmState& stateOf(arm::ArmSide side) {
    return side == arm::ArmSide::Right ? g_right : g_left;
}

float smooth(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// The arm back to the game: the layer's modifiers to no change, the arm's surface as the weapon's kit has it.
void giveBack(ArmState& s, const std::byte* hands) {
    s.lastTwist.reset();
    s.wrist.idle();
    s.lastWanted.reset();
    s.lastAttach.reset();
    s.lastShoulderRead.reset();
    game_arm::releaseArm(s.side, hands);
    hands_surfaces::update(s.side, hands, false);
}

// Where the arm's shoulder and elbow go (world, relative to the eye; the elbow's bend direction).
struct Shoulder {
    Vec3 point;
    Vec3 elbow;
};

// The game's grip of the held item from a read the layer's overrides had not reached: the wrist in the attach
// joint's frame, the hand's joints in their parents' frames, and the attach joint.
void noteCapture(ArmState& s, const std::byte* hands, const game_arm::ArmRead& read) {
    using arm::ArmJoint;
    using arm::index;
    game_arm::HandRead hand;
    if (!game_arm::readHand(s.side, hands, hand)) {
        return;
    }
    const xr_math::ModelPose& attach = read.read[index(ArmJoint::Attach)];
    s.captured = Capture{
        s.item, {arm::relative(attach, read.read[index(ArmJoint::Hand)]), hand.local}, attach, ++s.captures};
}

// The arm solved to the wrist and mixed with the game's by `weight`, written; false when it cannot be used
// this tick (nothing written).
bool poseArm(ArmState& s,
             const std::byte* hands,
             const WorldHand& world,
             const ModelPlacement& model,
             const Shoulder& where,
             const input::ControllerSettings& cfg,
             float weight) {
    using arm::ArmJoint;
    using arm::index;
    std::int16_t attachJoint = -1;
    if (!safeRead(hands + s.attachJoint, attachJoint)) {
        return false;
    }
    const auto read = game_arm::readArm(s.side, hands, attachJoint, nullptr);
    if (!read) {
        return false;
    }
    const float upm = world.unitsPerMetre;
    // The arm as the game animates it, and the arm the solve starts from: the same, but for a weapon whose
    // idle holds it by a handle turned away from a controller's grip (its [right_grip] row), where the
    // glove's wrist goes on that row's grip and the arm is carried rigidly there. While the glove's overrides
    // show, the wrist's animated pose is the tracker's (grip_hand.hpp) and the arm is carried with it.
    arm::ArmPoses animated = read->animated;
    arm::ArmPoses from = animated;
    std::optional<arm::GripOverrides> glove;
    const xr_math::ModelPose& attach = read->read[index(ArmJoint::Attach)];
    s.attachRead = attach;
    std::optional<xr_math::ModelPose> wristAnimated;
    if (offhand_mods::fingerCount(s.side) > 0) {
        if (hands != s.gloveHands) {
            s.gloveHands = hands;
            s.wrist.reset();
            s.idleHandOk = false;
            s.captured.reset();
        }
        const bool clean = read->landed < 0.0f;
        const xr_math::ModelPose& readWrist = read->read[index(ArmJoint::Hand)];
        const std::optional<float> settled =
            s.weights && s.gloveWeight.settled ? std::optional<float>(s.gloveWeight.write) : std::nullopt;
        if (cfg.offhandTrace) {
            const std::optional<std::int32_t> generation = offhand_mods::modGeneration(hands);
            const std::byte* node = offhand_mods::modNode(hands);
            if (generation && s.lastGeneration && node == s.generationNode) {
                ++s.generationTicks;
                s.generationSteps += ((*generation - *s.lastGeneration) & 1) != 0 ? 1u : 0u;
                s.generationJump = std::max(s.generationJump, *generation - *s.lastGeneration);
            }
            s.lastGeneration = generation;
            s.generationNode = node;
        }
        if (cfg.offhandTrace && settled) {
            if (const auto lag = s.wrist.readLag(attach, readWrist, *settled)) {
                ++s.readLags[static_cast<std::size_t>(std::clamp(lag->ticks, 1, 3) - 1)];
            }
        }
        if (s.lastWanted) {
            const float miss = length(arm::relative(attach, readWrist).position - s.lastWanted->position);
            s.landedMax = std::max(s.landedMax, miss);
            if (cfg.offhandTrace) {
                const xr_math::ModelPose sleeve =
                    arm::relative(readWrist, read->read[index(ArmJoint::Roll3)]);
                traceTick(s, attach, miss, length(sleeve.position - s.lastSleeve.position),
                          read->read[index(ArmJoint::UpperArm)].position);
            }
        }
        wristAnimated = s.wrist.update(clean, attach, readWrist, settled.has_value());
        game_arm::HandRead hand;
        if (clean && game_arm::readHand(s.side, hands, hand)) {
            s.idleHand = hand.local;
            s.idleHandOk = true;
            if (input::leftHandArms(cfg)) {
                noteCapture(s, hands, *read);
            }
        }
    }
    const float gripWeight = smooth(s.gripRamp) * s.settleWeight;
    // The glove's inputs this tick: the wrist's animated pose, the grip, the hands' modifier weight.
    struct GloveIn {
        xr_math::ModelPose wrist;
        const arm::GripPose* grip;
        float modWeight;
    };
    std::optional<GloveIn> gloveIn;
    if (s.wristTarget) {
        s.activeGrip.wrist = arm::relative(attach, *s.wristTarget);
    }
    if (s.hasActive && gripWeight > 0.0f && wristAnimated && s.idleHandOk && s.weights) {
        gloveIn = GloveIn{*wristAnimated, &s.activeGrip, s.gloveWeight.write};
        const arm::GloveArm arms = arm::gloveArm(read->animated, gloveIn->wrist, *gloveIn->grip, gripWeight);
        animated = arms.animated;
        from = arms.from;
    }

    // The shoulder and the elbow's bend in the arms model's space; the wrist stays the game's (or the
    // grip's).
    const Vec3 shoulder = xr_math::inModelSpace(model.pose, {where.point, model.pose.axis}).position;
    const Vec3 pole = arm::toLocal(model.pose.axis, where.elbow);
    const arm::SolveMode solve = armSolveMode();
    auto ours = arm::solveArmToWrist(from, shoulder, pole, s.lastTwist, solve.twistFrom);
    if (!ours) {
        return false;
    }
    s.lastTwist = ours->twist;
    if (cfg.testArmFault == input::ArmFault::Twist) {
        arm::plantTwistFault(*ours, animated);
    }
    arm::ArmPoses mixed;
    for (std::size_t i = 0; i < arm::kArmJointCount; ++i) {
        mixed[i] = arm::blendPose(animated[i], ours->joints[i], weight);
    }
    if (gloveIn) {
        // The glove: its wrist where the arm's blend puts it, the fingers closed as far.
        s.gloveCurl = weight * gripWeight;
        glove =
            arm::gripOverrides(attach, gloveIn->wrist, arm::relative(attach, mixed[index(ArmJoint::Hand)]),
                               s.idleHand, *gloveIn->grip, s.gloveCurl, gloveIn->modWeight);
        glove->modelSpace = s.modelSpace;
        glove->modelShoulder = solve.modelShoulder;
    }
    const arm::HandsOnlyArm handsOnly = game_arm::handsOnlyFor(mixed, cfg);
    mixed = handsOnly.joints;
    const Vec3 wrist = mixed[index(ArmJoint::Hand)].position;
    // How far the layer moves the arm from the game's pose: near 0 where the game's arm already reaches a
    // shoulder at the head (a flat-screen-like grip), which looks the same as ETERNALVR_WEAPON_ARM=game.
    float moved = 0.0f;
    for (const ArmJoint j : offhand_mods::kLayerJoints) {
        if (!(length(mixed[index(j)].position - wrist) <= kMaxArmMetres * upm)) {
            return false;
        }
        moved = std::max(moved, length(mixed[index(j)].position - animated[index(j)].position));
    }
    // The bind solve writes past the targets by the blend's mix (features/arm/mod_mix.hpp), from the wrist
    // the game reads (its attach joint's change is the game's, mixed in already).
    const std::optional<float> mix = pastWeight(solve, hands);
    arm::ArmPoses written = mix ? arm::modifiedFor(animated, mixed, *mix,
                                                   glove ? &glove->wrist : &animated[index(ArmJoint::Hand)])
                                : mixed;
    if (glove && !mix) {
        // The default solve: the forearm is not written past its targets but placed as wanted under the
        // glove's wrist as written (past its target), so it hangs from the wrist where the blend lands it.
        // The bind solve (`mix`) writes it past its targets instead (modifiedFor, forearmPast in
        // mod_mix.cpp), each joint placed from where the one before lands.
        for (std::size_t i = index(ArmJoint::Hand) + 1; i < arm::kArmJointCount; ++i) {
            written[i] = arm::compose(glove->wrist, arm::relative(mixed[index(ArmJoint::Hand)], mixed[i]));
        }
    }
    if (!mp_guard::allowsGameTouch() ||
        !game_arm::writeArm(s.side, hands, written, read->scale, read->origin, handsOnly.zeroScale, &mixed,
                            nullptr, glove ? &*glove : nullptr)) {
        return false;
    }
    if (glove) {
        s.wrist.wrote(attach, glove->wrist, s.gloveWeight.write);
        s.lastWanted = arm::relative(attach, mixed[index(ArmJoint::Hand)]);
        s.lastAttach = attach;
        s.lastSleeve = arm::relative(mixed[index(ArmJoint::Hand)], mixed[index(ArmJoint::Roll3)]);
        s.lastShoulder = mixed[index(ArmJoint::UpperArm)].position;
    } else {
        s.wrist.idle();
        s.lastWanted.reset();
        s.lastAttach.reset();
    }
    s.posedHands = hands;
    // The weapon's mesh kit usually hides this arm (hands_surfaces.hpp): shown while the layer poses it.
    hands_surfaces::update(s.side, hands, true);
    arm_dump::noteLayerArm(s.side, weight, ours->twist, *read, mixed, nullptr, handsOnly.zeroScale);

    const Vec3 solvedShoulder = ours->joints[index(ArmJoint::UpperArm)].position;
    if (!s.loggedBones) {
        s.loggedBones = true;
        EVR_LOG("%s: upper arm %.3f, forearm %.3f (animated, game units), model scale (%.3f %.3f %.3f); the "
                "first pose moves the arm up to %.2f from the game's%s",
                s.tag, ours->upper, ours->lower, read->scale.x, read->scale.y, read->scale.z, moved,
                cfg.weaponArmTestShoulder ? " (test shoulder)" : "");
    }
    if (poseTraceDue(s, cfg)) {
        const Vec3 e = mixed[index(ArmJoint::ForeArm)].position;
        const Vec3 sh = mixed[index(ArmJoint::UpperArm)].position;
        EVR_LOG(
            "%s: ik: wrist (%.2f %.2f %.2f), elbow (%.2f %.2f %.2f), shoulder (%.2f %.2f %.2f), %.2f from "
            "the head's point, reach %.2f, twist %.0f deg, weight %.2f, up to %.2f from the game's pose, "
            "read back within %.2f; %llu rejected; %s arm surface %s; glove %s (grip %.2f, fingers %.2f, "
            "weight %.3f %s, landed within %.1f mm, reads of the write 1/2/3 ticks before %u/%u/%u); the "
            "list "
            "switched in %u of %u ticks (generation steps up to %d)",
            s.tag, wrist.x, wrist.y, wrist.z, e.x, e.y, e.z, sh.x, sh.y, sh.z,
            length(solvedShoulder - shoulder), ours->ik.reach, ours->twist * kDegreesPerRadian, weight, moved,
            read->landed, static_cast<unsigned long long>(s.rejected.load()),
            s.side == arm::ArmSide::Right ? "right" : "left",
            arm::surfaceStateName(hands_surfaces::state(s.side)), glove ? "on the grip" : "the game's",
            gripWeight, glove ? s.gloveCurl : 0.0f,
            s.weights ? static_cast<double>(s.weights->current) : -1.0,
            s.gloveWeight.settled ? "settled" : "fading", static_cast<double>(s.landedMax) * 1000.0,
            s.readLags[0], s.readLags[1], s.readLags[2], s.generationSteps, s.generationTicks,
            s.generationJump);
        s.landedMax = 0.0f;
    }
    return true;
}

// On a tick the arm was given back (with the weapon in the left hand): reads it, and takes the game's grip
// when the read is clean. The give-back counted the tick already (game_arm::releaseArm).
void captureFrom(ArmState& s, const std::byte* hands) {
    std::int16_t attachJoint = -1;
    if (offhand_mods::fingerCount(s.side) == 0 || !safeRead(hands + s.attachJoint, attachJoint)) {
        return;
    }
    if (hands != s.gloveHands) {
        s.gloveHands = hands;
        s.wrist.reset();
        s.idleHandOk = false;
        s.captured.reset();
    }
    const auto read = game_arm::readArm(s.side, hands, attachJoint, nullptr, false);
    s.attachRead = read ? std::optional(read->read[arm::index(arm::ArmJoint::Attach)]) : std::nullopt;
    if (read && read->landed < 0.0f) {
        noteCapture(s, hands, *read);
    }
}

// One tick of an arm with its decision, the grip it holds (or none) and its shoulder.
void tickArm(ArmState& s,
             const std::byte* hands,
             const input::ArmDecision& decision,
             const WorldHand& world,
             const ModelPlacement& model,
             const Shoulder& where,
             float dt) {
    const input::ControllerSettings& cfg = settings();
    s.attachRead.reset();
    // A glove on a controller is the off hand (the right one with the weapon in the left hand): its blend.
    const float weight =
        s.modelSpace
            ? s.blend.updateOffHand(decision.controller, decision.reason == input::ArmReason::HandsHidden, dt,
                                    cfg.offhandBlendSeconds, cfg.offhandHoldSeconds)
            : s.blend.update(decision.controller, dt, cfg.offhandBlendSeconds, cfg.offhandHoldSeconds);
    if (cfg.offhandTrace) {
        trace(s, decision, weight);
    }
    arm_dump::noteGameArm(s.side, weight);
    if (s.held) {
        s.activeGrip = *s.held;
        s.hasActive = true;
    }
    const float step = cfg.offhandBlendSeconds > 0.0f ? dt / cfg.offhandBlendSeconds : 1.0f;
    s.gripRamp = std::clamp(s.gripRamp + (s.held ? step : -step), 0.0f, 1.0f);
    s.weights = offhand_mods::fingerCount(s.side) > 0 ? offhand_mods::modWeights(hands) : std::nullopt;
    s.gloveWeight = s.weights ? arm::gloveWeight(*s.weights) : arm::GloveWeight{};
    s.settleWeight = s.settle.update(s.weights && s.gloveWeight.settled, dt, cfg.offhandBlendSeconds);
    if (weight <= 0.0f) {
        giveBack(s, hands);
        if (input::leftHandArms(cfg)) {
            captureFrom(s, hands);
        }
        return;
    }
    if (!poseArm(s, hands, world, model, where, cfg, weight)) {
        s.rejected.fetch_add(1, std::memory_order_relaxed);
        giveBack(s, hands);
    }
}

void releaseAfterTrip(ArmState& s, const std::byte* hands) {
    if (s.tripReleased || (s.posedHands && hands != s.posedHands)) {
        return;
    }
    s.tripReleased = true;
    if (!s.posedHands) {
        return;
    }
    // Both lists at once, with no call into the game (game_arm::releaseArm); the overrides left read back.
    const bool released = game_arm::releaseArm(s.side, hands, true);
    const auto left = game_arm::overridesLeft(s.side, hands);
    hands_surfaces::releaseAfterTrip(s.side, hands);
    EVR_LOG(
        "%s: the multiplayer guard tripped: %s (overrides left per list %d/%d); the weapon arm is the game's",
        s.tag,
        released ? "the layer's arm modifiers set back to no change in both lists"
                 : "the layer's arm modifiers could not be set back",
        left[0], left[1]);
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
        g_right.tag, g_right.attachJoint, sh.forward, sh.left, sh.up, e.forward, e.left, e.up,
        cfg.offhandBlendSeconds, cfg.offhandHoldSeconds,
        cfg.handedness != game::Handedness::Right ? ", for the left controller (weapon in the left hand)"
                                                  : "",
        cfg.offhandTrace ? ", trace on" : "",
        cfg.weaponArmTestShoulder ? ", TEST shoulder (ETERNALVR_WEAPON_ARM_TEST_SHOULDER)" : "");
    if (cfg.testArmFault != input::ArmFault::None) {
        EVR_LOG("%s: planted %s fault ON (ETERNALVR_TEST_ARM_FAULT): %s, both arms (rig test)", g_right.tag,
                input::armFaultName(cfg.testArmFault),
                cfg.testArmFault == input::ArmFault::Twist     ? "the whole twist on the wrist"
                : cfg.testArmFault == input::ArmFault::Forearm ? "hands only with the forearm left drawn"
                                                               : "the off hand's fingers left the game's");
    }
}

void tick(const std::byte* hands,
          const ArmsTick& arms,
          input::ArmSignals signals,
          const WorldHand& world,
          const ModelPlacement& model,
          float dt) {
    const input::ControllerSettings& cfg = settings();
    g_right.item = arms.arms;
    g_right.held = rowGrip(arms.arms);
    g_right.modelSpace = false;
    tickArm(g_right, hands, input::decideWeaponArm(signals, cfg.weaponArm), world, model,
            {world.weaponShoulder, world.weaponElbow}, dt);
}

void holding(arm::ArmSide side,
             const std::byte* hands,
             const Hold& hold,
             const WorldHand& world,
             const ModelPlacement& model,
             float dt) {
    ArmState& s = stateOf(side);
    s.held = hold.grip ? std::optional(*hold.grip) : std::nullopt;
    s.modelSpace = hold.modelSpace;
    s.item = hold.item;
    s.wristTarget = hold.grip ? hold.wrist : std::nullopt;
    tickArm(s, hands, hold.decision, world, model, {hold.shoulder, hold.elbow}, dt);
}

std::optional<Capture> captured(arm::ArmSide side) {
    return stateOf(side).captured;
}

void forgetCapture(arm::ArmSide side) {
    stateOf(side).captured.reset();
}

std::optional<xr_math::ModelPose> attachRead(arm::ArmSide side) {
    return stateOf(side).attachRead;
}

void release(const std::byte* hands) {
    giveBack(g_right, hands);
}

void release(arm::ArmSide side, const std::byte* hands) {
    ArmState& s = stateOf(side);
    giveBack(s, hands);
    s.blend.reset(); // blended in again from the game's arm when it is taken back
    s.gripRamp = 0.0f;
}

void releaseOnTrip(const std::byte* hands) {
    releaseAfterTrip(g_right, hands);
    releaseAfterTrip(g_left, hands);
}

} // namespace evr::vkcore::controllers::weapon_arm
