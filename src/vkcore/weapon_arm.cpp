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
// The arm's twist of the last tick it was solved (arm_solve.hpp), until the arm is given back.
std::optional<float> g_lastTwist;

// The glove on the grip (features/arm/grip_hand.hpp): the held item's [right_grip] row, the grip's weight
// (0 to 1, eased in and out over ETERNALVR_OFFHAND_BLEND as the held weapon has a row or not), the wrist's
// animated pose and the idle hand's fingers as the last clean read had them.
const std::byte* g_gripDecl = nullptr;
std::optional<game::RightGrip> g_grip;       // the held item's
std::optional<game::RightGrip> g_activeGrip; // the held item's, or the last one's while the grip eases out
std::string g_gripItem;
float g_gripRamp = 0.0f;
arm::WristTracker g_wrist;
std::array<xr_math::ModelPose, arm::kHandJointCount> g_idleHand{};
bool g_idleHandOk = false;
const std::byte* g_gloveHands = nullptr;
std::uint64_t g_gripLines = 0;
float g_gloveCurl = 0.0f; // the last tick's, for the trace
// The hands' modifier weights this tick, how the glove is written for them and its settle weight (tick()).
std::optional<arm::ModWeights> g_weights;
arm::GloveWeight g_gloveWeight;
arm::GloveSettle g_settle;
float g_settleWeight = 0.0f;
// Trace only: the glove's wrist read back against where it was meant to land the tick before (game units,
// the largest since the last trace line), and how many reads showed the write of 1, 2 or 3 ticks before.
std::optional<xr_math::ModelPose> g_lastWanted;
// Trace only: the gun's root joint as read the tick the last glove was written under it, and the per-tick
// lines logged.
std::optional<xr_math::ModelPose> g_lastAttach;
// The forearm's first joint where it was meant to land, in the wrist's frame.
xr_math::ModelPose g_lastSleeve{};
Vec3 g_lastShoulder{}; // the shoulder end where it was meant to land (model space)
std::uint64_t g_tickLines = 0;
constexpr std::uint64_t kMaxTickLines = 6000;
float g_landedMax = 0.0f;
std::array<unsigned, 3> g_readLags{};
// Trace only: ticks on one modifier node, the ticks its generation's low bit (the list SetJointMod writes)
// changed since the tick before, and the largest step.
std::optional<std::int32_t> g_lastGeneration;
unsigned g_generationTicks = 0;
unsigned g_generationSteps = 0;
std::int32_t g_generationJump = 0; // the largest move
const std::byte* g_generationNode = nullptr;

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
arm::GripPose g_gripPose = arm::heavyCannonGrip(); // the active grip's

// The held item's grip row, looked up when the held item changes.
void updateGrip(const std::byte* hands) {
    const std::byte* decl = heldItemDecl(hands);
    if (decl == g_gripDecl) {
        return;
    }
    g_gripDecl = decl;
    g_gripItem = decl ? itemDeclName(decl) : std::string{};
    g_grip = state().offsets.lookupRightGrip(g_gripItem);
    if (g_grip) {
        g_activeGrip = g_grip;
        g_gripPose = gripPose(*g_grip);
    }
    if (g_grip && g_gripLines < 20) {
        ++g_gripLines;
        EVR_LOG("%s: '%s' holds its gun by a handle turned away from a controller's grip: the glove goes "
                "on a grip in the Heavy Cannon's idle grip while the layer poses the arm (the gun stays the "
                "game's)",
                kTag, g_gripItem.c_str());
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
void traceTick(const xr_math::ModelPose& attach, float miss, float sleeve, float shoulder) {
    if (!g_lastAttach || !g_lastWanted || g_tickLines >= kMaxTickLines) {
        return;
    }
    ++g_tickLines;
    const Vec3 then = arm::compose(*g_lastAttach, *g_lastWanted).position;
    const Vec3 now = arm::compose(attach, *g_lastWanted).position;
    const Quat q = conjugate(arm::toQuat(g_lastAttach->axis)) * arm::toQuat(attach.axis);
    const float turn = 2.0f * std::atan2(length(Vec3{q.x, q.y, q.z}), std::fabs(q.w)) * kDegreesPerRadian;
    EVR_LOG(
        "%s: glove tick: wrist %.1f mm off in the gun's frame, the sleeve's end %.1f mm off the glove, the "
        "shoulder end %.1f mm off; "
        "the gun's root moved %.1f mm at the grip (%.1f mm at the root, turned %.2f deg) from the read the "
        "write was placed under",
        kTag, static_cast<double>(miss) * 1000.0, static_cast<double>(sleeve) * 1000.0,
        static_cast<double>(shoulder) * 1000.0, static_cast<double>(length(now - then)) * 1000.0,
        static_cast<double>(length(attach.position - g_lastAttach->position)) * 1000.0,
        static_cast<double>(turn));
}

// The arm back to the game: the layer's modifiers to no change, the arm's surface as the weapon's kit has it.
void giveBack(const std::byte* hands) {
    g_lastTwist.reset();
    g_wrist.idle();
    g_lastWanted.reset();
    g_lastAttach.reset();
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
    const float upm = world.unitsPerMetre;
    // The arm as the game animates it, and the arm the solve starts from: the same, but for a weapon whose
    // idle holds it by a handle turned away from a controller's grip (its [right_grip] row), where the
    // glove's wrist goes on that row's grip and the arm is carried rigidly there. While the glove's overrides
    // show, the wrist's animated pose is the tracker's (grip_hand.hpp) and the arm is carried with it.
    arm::ArmPoses animated = read->animated;
    arm::ArmPoses from = animated;
    std::optional<arm::GripOverrides> glove;
    const xr_math::ModelPose& attach = read->read[index(ArmJoint::Attach)];
    std::optional<xr_math::ModelPose> wristAnimated;
    if (offhand_mods::fingerCount(kSide) > 0) {
        if (hands != g_gloveHands) {
            g_gloveHands = hands;
            g_wrist.reset();
            g_idleHandOk = false;
        }
        const bool clean = read->landed < 0.0f;
        const xr_math::ModelPose& readWrist = read->read[index(ArmJoint::Hand)];
        const std::optional<float> settled =
            g_weights && g_gloveWeight.settled ? std::optional<float>(g_gloveWeight.write) : std::nullopt;
        if (cfg.offhandTrace) {
            const std::optional<std::int32_t> generation = offhand_mods::modGeneration(hands);
            const std::byte* node = offhand_mods::modNode(hands);
            if (generation && g_lastGeneration && node == g_generationNode) {
                ++g_generationTicks;
                g_generationSteps += ((*generation - *g_lastGeneration) & 1) != 0 ? 1u : 0u;
                g_generationJump = std::max(g_generationJump, *generation - *g_lastGeneration);
            }
            g_lastGeneration = generation;
            g_generationNode = node;
        }
        if (cfg.offhandTrace && settled) {
            if (const auto lag = g_wrist.readLag(attach, readWrist, *settled)) {
                ++g_readLags[static_cast<std::size_t>(std::clamp(lag->ticks, 1, 3) - 1)];
            }
        }
        if (g_lastWanted) {
            const float miss = length(arm::relative(attach, readWrist).position - g_lastWanted->position);
            g_landedMax = std::max(g_landedMax, miss);
            if (cfg.offhandTrace) {
                const xr_math::ModelPose sleeve =
                    arm::relative(readWrist, read->read[index(ArmJoint::Roll3)]);
                traceTick(attach, miss, length(sleeve.position - g_lastSleeve.position),
                          length(read->read[index(ArmJoint::UpperArm)].position - g_lastShoulder));
            }
        }
        wristAnimated = g_wrist.update(clean, attach, readWrist, settled.has_value());
        game_arm::HandRead hand;
        if (clean && game_arm::readHand(kSide, hands, hand)) {
            g_idleHand = hand.local;
            g_idleHandOk = true;
        }
    }
    const float gripWeight = smooth(g_gripRamp) * g_settleWeight;
    // The glove's inputs this tick: the wrist's animated pose, the grip, the hands' modifier weight.
    struct GloveIn {
        xr_math::ModelPose wrist;
        const arm::GripPose* grip;
        float modWeight;
    };
    std::optional<GloveIn> gloveIn;
    if (g_activeGrip && gripWeight > 0.0f && wristAnimated && g_idleHandOk && g_weights) {
        gloveIn = GloveIn{*wristAnimated, &g_gripPose, g_gloveWeight.write};
        const arm::GloveArm arms = arm::gloveArm(read->animated, gloveIn->wrist, *gloveIn->grip, gripWeight);
        animated = arms.animated;
        from = arms.from;
    }

    // The shoulder and the elbow's bend in the arms model's space; the wrist stays the game's (or the
    // grip's).
    const Vec3 shoulder = xr_math::inModelSpace(model.pose, {world.weaponShoulder, model.pose.axis}).position;
    const Vec3 pole = arm::toLocal(model.pose.axis, world.weaponElbow);
    auto ours = arm::solveArmToWrist(from, shoulder, pole, g_lastTwist,
                                     cfg.armSolve == input::ArmSolve::Bind ? arm::TwistFrom::Bind
                                                                           : arm::TwistFrom::Animated);
    if (!ours) {
        return false;
    }
    g_lastTwist = ours->twist;
    if (cfg.testArmTwistFault) {
        arm::plantTwistFault(*ours, animated);
    }
    arm::ArmPoses mixed;
    for (std::size_t i = 0; i < arm::kArmJointCount; ++i) {
        mixed[i] = arm::blendPose(animated[i], ours->joints[i], weight);
    }
    if (gloveIn) {
        // The glove: its wrist where the arm's blend puts it, the fingers closed as far.
        g_gloveCurl = weight * gripWeight;
        glove =
            arm::gripOverrides(attach, gloveIn->wrist, arm::relative(attach, mixed[index(ArmJoint::Hand)]),
                               g_idleHand, *gloveIn->grip, g_gloveCurl, gloveIn->modWeight);
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
    if (!mp_guard::allowsGameTouch() || !game_arm::writeArm(kSide, hands, written, read->scale, read->origin,
                                                            cfg.armsHandsOnly ? handsOnly.zeroScale : 0u,
                                                            &mixed, nullptr, glove ? &*glove : nullptr)) {
        return false;
    }
    if (glove) {
        g_wrist.wrote(attach, glove->wrist, g_gloveWeight.write);
        g_lastWanted = arm::relative(attach, mixed[index(ArmJoint::Hand)]);
        g_lastAttach = attach;
        g_lastSleeve = arm::relative(mixed[index(ArmJoint::Hand)], mixed[index(ArmJoint::Roll3)]);
        g_lastShoulder = mixed[index(ArmJoint::UpperArm)].position;
    } else {
        g_wrist.idle();
        g_lastWanted.reset();
        g_lastAttach.reset();
    }
    g_posedHands = hands;
    // The weapon's mesh kit usually hides this arm (hands_surfaces.hpp): shown while the layer poses it.
    hands_surfaces::update(kSide, hands, true);
    arm_dump::noteLayerArm(kSide, weight, ours->twist, *read, mixed, nullptr);

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
            "read back within %.2f; %llu rejected; right arm surface %s; glove %s (grip %.2f, fingers %.2f, "
            "weight %.3f %s, landed within %.1f mm, reads of the write 1/2/3 ticks before %u/%u/%u); the "
            "list "
            "switched in %u of %u ticks (generation steps up to %d)",
            kTag, wrist.x, wrist.y, wrist.z, e.x, e.y, e.z, s.x, s.y, s.z, length(solvedShoulder - shoulder),
            ours->ik.reach, ours->twist * kDegreesPerRadian, weight, moved, read->landed,
            static_cast<unsigned long long>(g_rejected.load()),
            arm::surfaceStateName(hands_surfaces::state(kSide)), glove ? "on the grip" : "the game's",
            gripWeight, glove ? g_gloveCurl : 0.0f,
            g_weights ? static_cast<double>(g_weights->current) : -1.0,
            g_gloveWeight.settled ? "settled" : "fading", static_cast<double>(g_landedMax) * 1000.0,
            g_readLags[0], g_readLags[1], g_readLags[2], g_generationSteps, g_generationTicks,
            g_generationJump);
        g_landedMax = 0.0f;
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
    if (cfg.testArmTwistFault) {
        EVR_LOG("%s: planted twist fault ON (ETERNALVR_TEST_ARM_FAULT): the whole twist on the wrist, both "
                "arms (rig test)",
                kTag);
    }
}

void tick(const std::byte* hands,
          input::ArmSignals signals,
          const WorldHand& world,
          const ModelPlacement& model,
          float dt) {
    const input::ControllerSettings& cfg = settings();
    const input::ArmDecision decision = input::decideWeaponArm(signals, cfg.weaponArm);
    const float weight =
        g_blend.update(decision.controller, dt, cfg.offhandBlendSeconds, cfg.offhandHoldSeconds);
    if (cfg.offhandTrace) {
        trace(decision, weight);
    }
    arm_dump::noteGameArm(kSide, weight);
    updateGrip(hands);
    const float step = cfg.offhandBlendSeconds > 0.0f ? dt / cfg.offhandBlendSeconds : 1.0f;
    g_gripRamp = std::clamp(g_gripRamp + (g_grip ? step : -step), 0.0f, 1.0f);
    g_weights = offhand_mods::fingerCount(kSide) > 0 ? offhand_mods::modWeights(hands) : std::nullopt;
    g_gloveWeight = g_weights ? arm::gloveWeight(*g_weights) : arm::GloveWeight{};
    g_settleWeight = g_settle.update(g_weights && g_gloveWeight.settled, dt, cfg.offhandBlendSeconds);
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
    // Both lists at once, with no call into the game (game_arm::releaseArm); the overrides left read back.
    const bool released = game_arm::releaseArm(kSide, hands, true);
    const auto left = game_arm::overridesLeft(kSide, hands);
    hands_surfaces::releaseAfterTrip(kSide, hands);
    EVR_LOG(
        "%s: the multiplayer guard tripped: %s (overrides left per list %d/%d); the weapon arm is the game's",
        kTag,
        released ? "the layer's arm modifiers set back to no change in both lists"
                 : "the layer's arm modifiers could not be set back",
        left[0], left[1]);
}

} // namespace evr::vkcore::controllers::weapon_arm
