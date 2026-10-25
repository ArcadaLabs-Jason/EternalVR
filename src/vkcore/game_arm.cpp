// The game's first-person arms bent by the layer: install and the read (game_arm.hpp; the writes in
// game_arm_write.cpp).

#include "vkcore/game_arm.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/parent_frames.hpp"
#include "features/input/controller_settings.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_arm_cache.hpp"
#include "vkcore/game_arm_joints.hpp"
#include "vkcore/log.hpp"
#include "vkcore/seh_filter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <string>

namespace evr::vkcore::controllers::game_arm {

using detail::Cache;
using detail::cacheOf;
using detail::callFaulted;
using detail::idsOf;
using detail::kTicksCap;

namespace {

constexpr const char* kTag = "offhand";

// idHands and its render model (build 25216728).
constexpr std::size_t kHandsRenderModel = 0x370; // idHands::renderModel
constexpr std::size_t kModelAnimator = 0x4E0;    // the render model's idJointAnimator
// idJointAnimator (GetJointTransforms).
constexpr std::size_t kAnimatorModel = 0x8;
constexpr std::size_t kModelDecl = 0x80;
constexpr std::size_t kDeclSkeleton = 0x310;
constexpr std::size_t kSkeletonData = 0x60;

// How long after the last override the read may still show it (the blend runs a frame or two behind).
constexpr ULONGLONG kOverrideShowsMs = 150;
// Also this many ticks since the last write, for every arm, so a hitch longer than kOverrideShowsMs does not
// pass a read the overrides still show in as clean (the arms keep posing through a hitch, view_poses.cpp).
constexpr std::int32_t kOverrideShowsTicks = 3;
// The wrist is read live once the wrist's change has been written as no change this many ticks in a row
// (the read is a frame or two behind the writes).
constexpr std::int32_t kWristQuietTicks = 3;
// An override read back further than this from where it was written (game units; the arm is about 0.55
// long, and a frame's lag moves a hand a few centimetres) for this many ticks in a row stops the arm.
constexpr float kLandingTolerance = 0.1f;
constexpr std::int32_t kLandingTicks = 30;

using GetJointTransformsFn = bool(__fastcall*)(void* animator,
                                               std::int32_t space,
                                               const std::int16_t* joints,
                                               std::int32_t count,
                                               float* positions,
                                               float* axes);

GetJointTransformsFn g_getJoints = nullptr;
detail::SetJointModFn g_setJointMod = nullptr;

// One per arm.
Cache g_cache[2];

enum class Refusal : std::uint8_t {
    Animator,
    Scale,
    Skeleton,
    Joints,
    Names,
    List,
    NoRoom,
    Taken,
    Read,
    Mismatch,
    Landing,
    Fault,
};

// Logs `message` the first time `reason` comes up for the arm.
void refuse(arm::ArmSide side, Refusal reason, const char* message) {
    const std::uint32_t bit = 1u << static_cast<unsigned>(reason);
    Cache& c = cacheOf(side);
    if ((c.loggedReasons & bit) != 0) {
        return;
    }
    c.loggedReasons |= bit;
    EVR_LOG("%s: %s; %s stays the game's", sideText(side).prefix, message, sideText(side).subject);
}

// Game thread only: a call into the game faulted, so its data may be half-updated; no call into either arm
// is made again this session.
bool g_gameCallFaulted = false;

// The game's calls, apart from anything with a destructor (__try needs that). False when they faulted.
bool callGetJoints(void* animator,
                   const std::int16_t* joints,
                   std::int32_t count,
                   float* positions,
                   float* axes,
                   bool& result) {
    __try {
        result = g_getJoints(animator, 1, joints, count, positions, axes);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// With the off hand's wrist change (the bind solve): the offsets taken from a read without the layer's
// modifiers belong to the hands, node and skeleton they were read on; new ones need a new clean read.
// (Without it, and on the weapon arm, whose glove carries its own estimate (weapon_arm.cpp), the layer keeps
// its old behaviour.)
void forgetOffsets(arm::ArmSide side) {
    Cache& c = cacheOf(side);
    if (side == arm::ArmSide::Left && offhand_mods::hasWrist(side)) {
        c.fromHandOk = false;
        c.wristQuietTicks = 0;
    }
}

// The arm's joints in the skeleton at `path.data`, from the game's attach joint, cached per skeleton.
bool resolveJoints(arm::ArmSide side, const SkeletonPath& path, std::int16_t attach) {
    Cache& c = cacheOf(side);
    if (path.data == c.skeletonData && attach == c.attach) {
        return c.jointsOk;
    }
    c.skeletonData = path.data;
    c.attach = attach;
    c.jointsOk = false;
    forgetOffsets(side);
    JointsProblem problem = JointsProblem::Skeleton;
    std::string error;
    const auto joints = findJoints(side, path, attach, problem, error);
    if (!joints) {
        if (problem == JointsProblem::Fault) {
            callFaulted(side, "name lookup");
            return false;
        }
        refuse(side,
               problem == JointsProblem::Names    ? Refusal::Names
               : problem == JointsProblem::Joints ? Refusal::Joints
                                                  : Refusal::Skeleton,
               error.c_str());
        return false;
    }
    c.joints = *joints;
    if (offhand_mods::fingerCount(side) > 0) {
        const auto hand = findHandJoints(side, path, c.joints[arm::index(arm::ArmJoint::Hand)], error);
        if (!hand) {
            refuse(side, Refusal::Joints, error.c_str());
            return false;
        }
        c.hand = *hand;
    }
    const std::int32_t own = offhand_mods::armCount(side);
    for (std::int32_t k = 0; k < offhand_mods::layerCount(side); ++k) {
        c.ids[static_cast<std::size_t>(k)] =
            k < own ? c.joints[arm::index(offhand_mods::layerJoint(side, k))]
                    : c.hand[arm::kFingerMods[static_cast<std::size_t>(k - own)]];
    }
    c.jointsOk = true;
    return true;
}

} // namespace

namespace detail {

Cache& cacheOf(arm::ArmSide side) {
    return g_cache[side == arm::ArmSide::Right ? 1 : 0];
}

std::span<const std::int16_t> idsOf(const Cache& c, arm::ArmSide side) {
    return {c.ids.data(), static_cast<std::size_t>(offhand_mods::layerCount(side))};
}

SetJointModFn setJointMod() {
    return g_setJointMod;
}

bool faulted() {
    return g_gameCallFaulted;
}

void callFaulted(arm::ArmSide side, const char* call) {
    g_gameCallFaulted = true;
    cacheOf(side).offForGood = true;
    const std::string message = std::string("the game's ") + call + " faulted; off for this session";
    refuse(side, Refusal::Fault, message.c_str());
}

} // namespace detail

offhand_mods::ArmSet install(const GameImage& image,
                             const std::byte* lagModsStart,
                             const std::byte* getJointTransforms,
                             const std::byte* setJointMod,
                             offhand_mods::ArmSet wanted) {
    const std::byte* getter =
        checkArmCalls(image, lagModsStart, getJointTransforms, setJointMod, staysText(wanted));
    if (!getter) {
        return {};
    }
    findNameTable(image, kTag);
    const offhand_mods::ArmSet ready = offhand_mods::install(image, wanted);
    if (!ready.offHand && !ready.weapon) {
        return {};
    }
    g_getJoints = reinterpret_cast<GetJointTransformsFn>(const_cast<std::byte*>(getJointTransforms));
    g_setJointMod = reinterpret_cast<detail::SetJointModFn>(const_cast<std::byte*>(setJointMod));
    EVR_LOG("%s: arm: animator getter at RVA 0x%X, name table at RVA 0x%X; the forearm, elbow and shoulder "
            "follow the hand (two-bone IK)%s",
            kTag, image.rva(getter), nameTableRva(image),
            ready.offHand && ready.weapon ? " on both arms"
            : ready.weapon                ? " on the weapon arm"
                                          : "");
    return ready;
}

std::optional<ArmRead> readArm(arm::ArmSide side,
                               const std::byte* hands,
                               std::int16_t attachJoint,
                               const xr_math::ModelPose* attachAnimated,
                               bool countTick) {
    Cache& c = cacheOf(side);
    if (!g_getJoints || !g_setJointMod || c.offForGood) {
        return std::nullopt;
    }
    SkeletonPath path;
    path.hands = hands;
    if (!safeRead(hands + kHandsRenderModel, path.model) || !path.model ||
        !safeRead(path.model + kModelAnimator, path.animator) || !path.animator ||
        !safeRead(path.animator + kAnimatorModel, path.animModel) || !path.animModel ||
        !safeRead(path.animModel + kModelDecl, path.decl) || !path.decl ||
        !safeRead(path.decl + kDeclSkeleton, path.skeleton) || !path.skeleton ||
        !safeRead(path.skeleton + kSkeletonData, path.data) || !path.data) {
        refuse(side, Refusal::Animator, "the arms' animator or skeleton cannot be reached");
        dumpSkeleton(side, path, attachJoint);
        return std::nullopt;
    }
    const std::byte* animator = path.animator;
    ArmRead out;
    std::string scaleError;
    const std::optional<Vec3> scale = readModelScale(animator, scaleError);
    if (!scale) {
        refuse(side, Refusal::Scale, scaleError.c_str());
        return std::nullopt;
    }
    out.scale = *scale;
    c.animator = nullptr;
    if (!resolveJoints(side, path, attachJoint)) {
        return std::nullopt;
    }
    const arm::ArmJointIndices& joints = c.joints;

    const std::byte* node = offhand_mods::modNode(hands);
    if (!node) {
        refuse(side, Refusal::List, "the hands' joint modifier node cannot be reached");
        return std::nullopt;
    }
    if (c.hands != hands || c.node != node || !offhand_mods::layerModsInPlace(node, c.base, idsOf(c, side))) {
        // New hands forget the offsets. A new modifier node on the same hands does not: the game makes
        // the hands' lists again on every weapon switch, and the offsets are the skeleton's (resolveJoints
        // forgets them for another). Forgotten there, the free off hand, which stays on its controller
        // through a switch (offhand_policy.hpp, decideOffHand), went to the game's arm for the 150 ms a
        // clean read needs.
        if (c.hands != hands) {
            forgetOffsets(side);
        }
        c.hands = hands;
        c.node = node;
        offhand_mods::AddError error;
        c.base = offhand_mods::addLayerMods(node, side, idsOf(c, side), error);
        if (c.base < 0) {
            refuse(side,
                   error.problem == offhand_mods::Problem::NoRoom  ? Refusal::NoRoom
                   : error.problem == offhand_mods::Problem::Taken ? Refusal::Taken
                                                                   : Refusal::List,
                   error.message.c_str());
            return std::nullopt;
        }
    }

    float positions[arm::kArmJointCount * 3] = {};
    float axes[arm::kArmJointCount * 9] = {};
    bool read = false;
    if (!callGetJoints(const_cast<std::byte*>(animator), joints.data(),
                       static_cast<std::int32_t>(arm::kArmJointCount), positions, axes, read)) {
        callFaulted(side, "GetJointTransforms");
        return std::nullopt;
    }
    if (!read) {
        refuse(side, Refusal::Read, "GetJointTransforms failed for the arm's joints");
        return std::nullopt;
    }
    // Hands only: the layer's joints read back scaled to nothing while its overrides show; they are replaced
    // below by where the last read without them put them, so only the attach joint and the wrist are checked.
    const bool wrist = offhand_mods::hasWrist(side);
    c.ticksSinceOverride = std::min(c.ticksSinceOverride + (countTick ? 1 : 0), kTicksCap);
    const bool overrideShows =
        GetTickCount64() - c.lastOverride <= kOverrideShowsMs || c.ticksSinceOverride < kOverrideShowsTicks;
    for (std::size_t i = 0; i < arm::kArmJointCount; ++i) {
        const float* p = positions + i * 3;
        const float* a = axes + i * 9;
        out.animated[i] = {{p[0], p[1], p[2]}, {{a[0], a[1], a[2]}, {a[3], a[4], a[5]}, {a[6], a[7], a[8]}}};
        if (c.collapsed && overrideShows && i >= arm::index(arm::ArmJoint::Roll3)) {
            continue;
        }
        // GetJointTransforms hands out the identity for an axis it could not use; no arm joint has it.
        if (!arm::plausiblePose(out.animated[i]) || (i != 0 && isIdentity(out.animated[i].axis))) {
            const std::string message =
                std::string("the animated pose of ") + arm::armJointNames(side)[i] + " is not usable";
            refuse(side, Refusal::Read, message.c_str());
            return std::nullopt;
        }
    }
    out.read = out.animated;
    c.animator = animator;
    const xr_math::ModelPose& attach = out.animated[arm::index(arm::ArmJoint::Attach)];
    if (attachAnimated && (!closeTo(attach.position, attachAnimated->position, 1e-3f) ||
                           !closeTo(attach.axis.forward, attachAnimated->axis.forward, 1e-3f) ||
                           !closeTo(attach.axis.up, attachAnimated->axis.up, 1e-3f))) {
        refuse(side, Refusal::Mismatch, "the attach joint read back differs from the game's");
        return std::nullopt;
    }
    // The origin (the skeleton's root, joint 0): an override's translation is taken from it.
    const std::int16_t root = 0;
    float rootPosition[3] = {};
    float rootAxes[9] = {};
    if (!callGetJoints(const_cast<std::byte*>(animator), &root, 1, rootPosition, rootAxes, read)) {
        callFaulted(side, "GetJointTransforms");
        return std::nullopt;
    }
    if (!read || !std::isfinite(rootPosition[0]) || !std::isfinite(rootPosition[1]) ||
        !std::isfinite(rootPosition[2])) {
        refuse(side, Refusal::Read, "GetJointTransforms failed for the skeleton's root");
        return std::nullopt;
    }
    out.origin = {rootPosition[0], rootPosition[1], rootPosition[2]};
    out.root = {out.origin,
                {{rootAxes[0], rootAxes[1], rootAxes[2]},
                 {rootAxes[3], rootAxes[4], rootAxes[5]},
                 {rootAxes[6], rootAxes[7], rootAxes[8]}}};
    // The read includes the layer's overrides of the last frames: while they may still show, the layer's
    // joints are carried by the wrist as they were in the last read without them. The wrist, when the layer
    // has a change of its own on it (the off hand with the bind solve), is read live while that change has
    // been no change for kWristQuietTicks (then only the attach joint carries it, and its offset from the
    // attach joint is taken again), and after a change is carried by the attach joint at that offset.
    const std::size_t h = arm::index(arm::ArmJoint::Hand);
    arm::ArmPoses landing =
        c.writtenUnder ? arm::carriedBy(*c.writtenUnder, out.animated[0], c.written) : c.written;
    if (c.modelShoulder) { // in model space: it lands where written, wherever the gun went
        landing[arm::index(arm::ArmJoint::UpperArm)] = c.written[arm::index(arm::ArmJoint::UpperArm)];
    }
    // An alias of out.animated[h]: the live read until the wrist is rebuilt below, the rebuilt wrist after.
    const xr_math::ModelPose& hand = out.animated[h];
    if (!overrideShows) {
        for (const arm::ArmJoint j : offhand_mods::kLayerJoints) {
            c.fromHand[arm::index(j)] = arm::relative(hand, out.animated[arm::index(j)]);
        }
        c.handFromAttach = arm::relative(out.animated[0], hand);
        c.fromHandOk = true;
    } else if (!c.fromHandOk) {
        refuse(side, Refusal::Read, "no animated pose of the arm without the layer's overrides yet");
        return std::nullopt;
    } else {
        // Where the overrides landed: where they were written, a frame behind at most. The wrist's landing is
        // measured from the live read, before `hand` is rebuilt; it counts in `landed` but not toward the
        // landing guard below, so a wrist offset of the game's own cannot stop the arm for the session.
        const float wristOff = wrist ? length(hand.position - landing[h].position) : 0.0f;
        if (wrist && c.wristQuietTicks >= kWristQuietTicks) {
            c.handFromAttach = arm::relative(out.animated[0], hand);
        } else if (wrist) {
            out.animated[h] = arm::compose(out.animated[0], c.handFromAttach);
        }
        float off = 0.0f;
        for (const arm::ArmJoint j : offhand_mods::kLayerJoints) {
            off =
                std::max(off, length(out.animated[arm::index(j)].position - landing[arm::index(j)].position));
            out.animated[arm::index(j)] = arm::compose(hand, c.fromHand[arm::index(j)]);
        }
        out.landed = std::max(off, wristOff);
        // A joint scaled to nothing does not read back where it was written (hands only): not counted.
        c.offTicks = off > kLandingTolerance && !c.collapsed ? c.offTicks + 1 : 0;
        if (c.offTicks >= kLandingTicks) {
            c.offForGood = true;
            char message[128];
            std::snprintf(message, sizeof(message),
                          "the layer's overrides do not land where written (%.3f away for %d ticks)",
                          static_cast<double>(off), c.offTicks);
            refuse(side, Refusal::Landing, message);
            return std::nullopt;
        }
    }
    return out;
}

bool readHand(arm::ArmSide side, const std::byte* hands, HandRead& out) {
    Cache& c = cacheOf(side);
    if (g_gameCallFaulted || offhand_mods::fingerCount(side) == 0 || !c.animator || hands != c.hands) {
        return false;
    }
    std::array<std::int16_t, 1 + arm::kHandJointCount> joints{};
    joints[0] = c.joints[arm::index(arm::ArmJoint::Hand)];
    std::copy(c.hand.begin(), c.hand.end(), joints.begin() + 1);
    float positions[joints.size() * 3] = {};
    float axes[joints.size() * 9] = {};
    bool read = false;
    if (!callGetJoints(const_cast<std::byte*>(c.animator), joints.data(),
                       static_cast<std::int32_t>(joints.size()), positions, axes, read)) {
        callFaulted(side, "GetJointTransforms");
        return false;
    }
    if (!read) {
        return false;
    }
    std::array<xr_math::ModelPose, joints.size()> model{};
    for (std::size_t i = 0; i < joints.size(); ++i) {
        const float* p = positions + i * 3;
        const float* a = axes + i * 9;
        model[i] = {{p[0], p[1], p[2]}, {{a[0], a[1], a[2]}, {a[3], a[4], a[5]}, {a[6], a[7], a[8]}}};
        if (!arm::plausiblePose(model[i])) {
            return false;
        }
    }
    out.local = arm::handLocal(model);
    return true;
}

} // namespace evr::vkcore::controllers::game_arm
