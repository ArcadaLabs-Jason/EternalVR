// The weapon arm (weapon_arm.hpp).

#include "vkcore/weapon_arm.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/arm_solve.hpp"
#include "features/arm/arm_surfaces.hpp"
#include "features/arm/grip_hand.hpp"
#include "features/arm/mod_mix.hpp"
#include "features/arm/twist_fault.hpp"
#include "vkcore/arm_dump.hpp"
#include "vkcore/game_arm.hpp"
#include "vkcore/hands_surfaces.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "xr_math/offhand_pose.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>

namespace evr::vkcore::controllers::weapon_arm {

namespace {

// No solved joint is further than this from the wrist (the arm is about 0.56 long): a read that runs away
// is not written.
constexpr float kMaxArmMetres = 1.5f;
constexpr float kDegreesPerRadian = 180.0f / std::numbers::pi_v<float>;
constexpr std::uint64_t kMaxTickLines = 6000;

// One arm holding a glove (off-hand hook thread only): the weapon arm, and with the weapon in the left hand
// the left arm too (holding()).
struct ArmState {
    arm::ArmSide side;
    // Type info (build 25216728): the arm's attach joint's index, stored by InitJointMods' AddJointMod (the
    // right one's checked at install, offhand_mods.hpp).
    std::size_t attachJoint;
    const char* tag;
    input::ArmBlend blend;
    input::ArmDecision lastDecision{false, input::ArmReason::ModeGame};
    bool traced = false;
    std::uint64_t traceLines = 0;
    ULONGLONG lastPoseTrace = 0;
    bool loggedBones = false;
    std::atomic<std::uint64_t> rejected{0};
    // The hands the arm was last posed on, and whether the one give-back after a multiplayer guard trip ran.
    const std::byte* posedHands = nullptr;
    bool tripReleased = false;
    // The arm's twist of the last tick it was solved (arm_solve.hpp), until the arm is given back.
    std::optional<float> lastTwist;

    // The glove on the grip (features/arm/grip_hand.hpp): the grip held this tick (the held item's
    // [right_grip] row's), the last one held while the grip eases out, the grip's weight (0 to 1, eased in
    // and out over ETERNALVR_OFFHAND_BLEND as there is a grip or not), the wrist's animated pose and the idle
    // hand's fingers as the last clean read had them.
    std::optional<arm::GripPose> held;
    arm::GripPose activeGrip = arm::heavyCannonGrip();
    bool hasActive = false;
    bool modelSpace = false; // the glove written in model space (holding(): a glove on a controller)
    std::optional<xr_math::ModelPose> wristTarget; // the glove's wrist there (model space), placed each tick
    std::optional<xr_math::ModelPose> attachRead;  // the arm's attach joint as read this tick
    float gripRamp = 0.0f;
    arm::WristTracker wrist;
    std::array<xr_math::ModelPose, arm::kHandJointCount> idleHand{};
    bool idleHandOk = false;
    const std::byte* gloveHands = nullptr;
    float gloveCurl = 0.0f; // the last tick's, for the trace
    // The game's own grip of the held item as the last clean read had it (capture()).
    std::optional<Capture> captured;
    std::uint32_t captures = 0;
    // The hands' modifier weights this tick, how the glove is written for them and its settle weight
    // (tick()).
    std::optional<arm::ModWeights> weights;
    arm::GloveWeight gloveWeight;
    arm::GloveSettle settle;
    float settleWeight = 0.0f;
    // Trace only: the glove's wrist read back against where it was meant to land the tick before (game
    // units, the largest since the last trace line), and how many reads showed the write of 1, 2 or 3 ticks
    // before.
    std::optional<xr_math::ModelPose> lastWanted;
    // Trace only: the gun's root joint as read the tick the last glove was written under it.
    std::optional<xr_math::ModelPose> lastAttach;
    // The forearm's first joint where it was meant to land, in the wrist's frame.
    xr_math::ModelPose lastSleeve{};
    Vec3 lastShoulder{}; // the shoulder end where it was meant to land (model space)
    std::uint64_t tickLines = 0;
    float landedMax = 0.0f;
    std::array<unsigned, 3> readLags{};
    // Trace only: ticks on one modifier node, the ticks its generation's low bit (the list SetJointMod
    // writes) changed since the tick before, and the largest step.
    std::optional<std::int32_t> lastGeneration;
    unsigned generationTicks = 0;
    unsigned generationSteps = 0;
    std::int32_t generationJump = 0; // the largest move
    const std::byte* generationNode = nullptr;
};

ArmState g_right{arm::ArmSide::Right, 0x28E6, "weapon arm"};
ArmState g_left{arm::ArmSide::Left, 0x28E4, "weapon arm (left glove)"};

ArmState& stateOf(arm::ArmSide side) {
    return side == arm::ArmSide::Right ? g_right : g_left;
}

// The held item's [right_grip] row (the weapon arm's), looked up when the held item changes.
const std::byte* g_gripDecl = nullptr;
std::optional<game::RightGrip> g_grip;
std::string g_gripItem;
std::uint64_t g_gripLines = 0;

// The glove's grip for a [right_grip] row: the game grip, its wrist moved where the row says.
arm::GripPose gripPose(const game::RightGrip& row) {
    arm::GripPose grip = arm::heavyCannonGrip();
    switch (row.grip) {
    case game::GameGrip::HeavyCannon:
        break;
    }
    if (row.wrist) {
        grip.wrist.position = {(*row.wrist)[0], (*row.wrist)[1], (*row.wrist)[2]};
    }
    return grip;
}

void updateGrip(const std::byte* hands) {
    const std::byte* decl = heldItemDecl(hands);
    if (decl == g_gripDecl) {
        return;
    }
    g_gripDecl = decl;
    g_gripItem = decl ? itemDeclName(decl) : std::string{};
    g_grip = state().offsets.lookupRightGrip(g_gripItem);
    if (g_grip && g_gripLines < 20) {
        ++g_gripLines;
        EVR_LOG("%s: '%s' holds its gun by a handle turned away from a controller's grip: the glove goes "
                "on a grip in the Heavy Cannon's idle grip while the layer poses the arm (the gun stays the "
                "game's)",
                g_right.tag, g_gripItem.c_str());
    }
}

float smooth(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Trace only, every tick the glove was written the tick before: its wrist as read back (`miss`, game units,
// in the gun's root frame as read now) against where it was meant to land, and how far the root moved
// between the read the write was placed under and the frame the game drew it in (this read): at the grip,
// at the root, and its turn; and the forearm's first joint (the sleeve's end at the glove) as read back in
// the wrist's frame against where it was meant to land (`sleeve`, game units), and the shoulder end as read
// back against where it was meant to land in model space (`shoulder`).
void traceTick(ArmState& s, const xr_math::ModelPose& attach, float miss, float sleeve, float shoulder) {
    if (!s.lastAttach || !s.lastWanted || s.tickLines >= kMaxTickLines) {
        return;
    }
    ++s.tickLines;
    const Vec3 then = arm::compose(*s.lastAttach, *s.lastWanted).position;
    const Vec3 now = arm::compose(attach, *s.lastWanted).position;
    const Quat q = conjugate(arm::toQuat(s.lastAttach->axis)) * arm::toQuat(attach.axis);
    const float turn = 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegreesPerRadian;
    EVR_LOG(
        "%s: glove tick: wrist %.1f mm off in the gun's frame, the sleeve's end %.1f mm off the glove, the "
        "shoulder end %.1f mm off; "
        "the gun's root moved %.1f mm at the grip (%.1f mm at the root, turned %.2f deg) from the read the "
        "write was placed under",
        s.tag, static_cast<double>(miss) * 1000.0, static_cast<double>(sleeve) * 1000.0,
        static_cast<double>(shoulder) * 1000.0, static_cast<double>(length(now - then)) * 1000.0,
        static_cast<double>(length(attach.position - s.lastAttach->position)) * 1000.0,
        static_cast<double>(turn));
}

// The arm back to the game: the layer's modifiers to no change, the arm's surface as the weapon's kit has it.
void giveBack(ArmState& s, const std::byte* hands) {
    s.lastTwist.reset();
    s.wrist.idle();
    s.lastWanted.reset();
    s.lastAttach.reset();
    game_arm::releaseArm(s.side, hands);
    hands_surfaces::update(s.side, hands, false);
}

void trace(ArmState& s, const input::ArmDecision& d, float weight) {
    if ((s.traced && d.controller == s.lastDecision.controller && d.reason == s.lastDecision.reason) ||
        s.traceLines >= 400) {
        return;
    }
    s.traced = true;
    s.lastDecision = d;
    ++s.traceLines;
    EVR_LOG("%s: %s (%s), weight %.2f", s.tag, d.controller ? "ik" : "game", input::armReasonName(d.reason),
            weight);
}

bool poseTraceDue(ArmState& s, const input::ControllerSettings& cfg) {
    if (!cfg.offhandTrace) {
        return false;
    }
    const ULONGLONG ticks = GetTickCount64();
    if (ticks - s.lastPoseTrace < 1000) {
        return false;
    }
    s.lastPoseTrace = ticks;
    return true;
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
    s.captured = Capture{heldItemDecl(hands),
                         {arm::relative(attach, read.read[index(ArmJoint::Hand)]), hand.local},
                         attach,
                         ++s.captures};
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
                          length(read->read[index(ArmJoint::UpperArm)].position - s.lastShoulder));
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
    auto ours = arm::solveArmToWrist(from, shoulder, pole, s.lastTwist,
                                     cfg.armSolve == input::ArmSolve::Bind ? arm::TwistFrom::Bind
                                                                           : arm::TwistFrom::Animated);
    if (!ours) {
        return false;
    }
    s.lastTwist = ours->twist;
    if (cfg.testArmTwistFault) {
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
    }
    const arm::HandsOnlyArm handsOnly = arm::handsOnly(mixed, game_arm::cuffOf(cfg.handsCuff));
    mixed = cfg.armsHandsOnly ? handsOnly.joints : mixed;
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
    const std::optional<float> mix =
        cfg.armSolve == input::ArmSolve::Bind ? offhand_mods::modWeight(hands) : std::nullopt;
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
    if (!mp_guard::allowsGameTouch() || !game_arm::writeArm(s.side, hands, written, read->scale, read->origin,
                                                            cfg.armsHandsOnly ? handsOnly.zeroScale : 0u,
                                                            &mixed, nullptr, glove ? &*glove : nullptr)) {
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
    arm_dump::noteLayerArm(s.side, weight, ours->twist, *read, mixed, nullptr);

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
    const float weight =
        s.blend.update(decision.controller, dt, cfg.offhandBlendSeconds, cfg.offhandHoldSeconds);
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
    if (cfg.testArmTwistFault) {
        EVR_LOG("%s: planted twist fault ON (ETERNALVR_TEST_ARM_FAULT): the whole twist on the wrist, both "
                "arms (rig test)",
                g_right.tag);
    }
}

std::optional<arm::GripPose> rowGrip(const std::byte* hands) {
    updateGrip(hands);
    return g_grip ? std::optional(gripPose(*g_grip)) : std::nullopt;
}

void tick(const std::byte* hands,
          input::ArmSignals signals,
          const WorldHand& world,
          const ModelPlacement& model,
          float dt) {
    const input::ControllerSettings& cfg = settings();
    updateGrip(hands);
    g_right.held = g_grip ? std::optional(gripPose(*g_grip)) : std::nullopt;
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
