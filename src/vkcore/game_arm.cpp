// The game's first-person arms bent by the layer (game_arm.hpp).

#include "vkcore/game_arm.hpp"

#include "features/arm/arm_frames.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_arm_joints.hpp"
#include "vkcore/log.hpp"
#include "vkcore/seh_filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers::game_arm {

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
constexpr std::size_t kAnimatorScale = 0x500;

constexpr std::uint16_t kFlagsChange = 0x20B;   // model space, rotation, translation, reference pose
constexpr std::uint16_t kFlagsOverride = 0x22B; // the same, as the joint's whole pose
// How long after the last override the read may still show it (the blend runs a frame or two behind).
constexpr ULONGLONG kOverrideShowsMs = 150;
// An override read back further than this from where it was written (game units; the arm is about 0.55
// long, and a frame's lag moves a hand a few centimetres) for this many ticks in a row stops the arm.
constexpr float kLandingTolerance = 0.1f;
constexpr std::int32_t kLandingTicks = 30;

struct Expect {
    std::size_t at;
    const char* bytes;
};
// UpdateWeaponLagJointMods: the call of the animator getter.
constexpr std::size_t kAnimatorCall = 0x94;
constexpr const char* kAnimatorGetter = "48 8B 81 70 03 00 00 48 8B 80 E0 04 00 00 C3";
constexpr Expect kGetJointsExpected[] = {
    {0x24, "48 8B 49 08"},                      // mov rcx, [rcx+8]           the model
    {0x36, "B8 78 03 00 00 41 B8 80 03 00 00"}, // 0x378 / 0x380              the pose by space
    {0x57, "48 8B 81 80 00 00 00"},             // mov rax, [rcx+0x80]
    {0x61, "48 8B 88 10 03 00 00"},             // mov rcx, [rax+0x310]       the skeleton
    {0x68, "48 8B 41 60 44 0F B7 68 02"},       // [rcx+0x60], its u16 at +2  the joint count
    {0xE1, "F3 41 0F 59 87 00 05 00 00"},       // mulss xmm0, [r15+0x500]    the scale
};
constexpr Expect kSetJointModExpected[] = {
    {0x1A, "48 8B 89 88 28 00 00"},                                  // [hands+0x2888]
    {0x2A, "48 8B 49 18"},                                           // +0x18: the node
    {0x37, "8B 41 28 83 E0 01 48 83 C0 02 48 8D 04 40 48 8B 14 C1"}, // lists at +0x30 + (gen & 1) * 0x18
    {0x5A, "48 C1 E1 06"},                                           // 0x40 per modifier
};

using GetJointTransformsFn = bool(__fastcall*)(void* animator,
                                               std::int32_t space,
                                               const std::int16_t* joints,
                                               std::int32_t count,
                                               float* positions,
                                               float* axes);
using SetJointModFn = void(__fastcall*)(void* hands,
                                        std::int32_t mod,
                                        std::int16_t joint,
                                        const float* translation,
                                        const float* rotation,
                                        std::uint16_t flags);

GetJointTransformsFn g_getJoints = nullptr;
SetJointModFn g_setJointMod = nullptr;

// One per arm, game thread only.
struct Cache {
    const std::byte* skeletonData = nullptr;
    std::int16_t attach = -1;
    arm::ArmJointIndices joints{};
    bool jointsOk = false;
    const std::byte* hands = nullptr;
    const std::byte* node = nullptr;
    std::int32_t base = -1; // the first of the layer's modifiers in both lists
    // The layer's joints relative to the wrist, from the last read the layer's overrides had not reached.
    arm::ArmPoses fromHand{};
    bool fromHandOk = false;
    ULONGLONG lastOverride = 0; // GetTickCount64 of the last override written
    arm::ArmPoses written{};    // the poses last written
    std::int32_t offTicks = 0;  // ticks in a row the read put an override away from where it was written
    bool offForGood = false;
    std::uint32_t loggedReasons = 0;
};
Cache g_cache[2];

Cache& cacheOf(arm::ArmSide side) {
    return g_cache[side == arm::ArmSide::Right ? 1 : 0];
}

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

void callFaulted(arm::ArmSide side, const char* call) {
    g_gameCallFaulted = true;
    cacheOf(side).offForGood = true;
    const std::string message = std::string("the game's ") + call + " faulted; off for this session";
    refuse(side, Refusal::Fault, message.c_str());
}

bool bytesMatch(const std::byte* at, std::string_view hex) {
    std::size_t i = 0;
    for (std::size_t p = 0; p + 1 < hex.size(); p += 3, ++i) {
        const auto nibble = [](char c) {
            return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
        };
        if (static_cast<std::uint8_t>(at[i]) !=
            static_cast<std::uint8_t>(nibble(hex[p]) * 16 + nibble(hex[p + 1]))) {
            return false;
        }
    }
    return true;
}

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

bool callSetJointMod(
    void* hands, std::int32_t mod, std::int16_t joint, const float* t, const float* r, std::uint16_t f) {
    __try {
        g_setJointMod(hands, mod, joint, t, r, f);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
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
    c.jointsOk = true;
    return true;
}

bool isIdentity(const xr_math::IdViewAxis& a) {
    return a.forward == Vec3{1.0f, 0.0f, 0.0f} && a.left == Vec3{0.0f, 1.0f, 0.0f} &&
           a.up == Vec3{0.0f, 0.0f, 1.0f};
}

bool closeTo(Vec3 a, Vec3 b, float eps) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

const char* stays(offhand_mods::ArmSet wanted) {
    return wanted.offHand && wanted.weapon ? "the off hand and the weapon arm stay the game's"
           : wanted.weapon                 ? "the weapon arm stays the game's"
                                           : "off hand stays the game's";
}

} // namespace

offhand_mods::ArmSet install(const GameImage& image,
                             const std::byte* lagModsStart,
                             const std::byte* getJointTransforms,
                             const std::byte* setJointMod,
                             offhand_mods::ArmSet wanted) {
    const std::byte* call = lagModsStart + kAnimatorCall;
    const std::byte* getter = image.inText(call, 5) && static_cast<std::uint8_t>(call[0]) == 0xE8
                                  ? call + 5 + readI32(call + 1)
                                  : nullptr;
    if (!getter || !image.inText(getter, 15) || !bytesMatch(getter, kAnimatorGetter)) {
        EVR_LOG("%s: arm: the animator getter is not where expected; %s", kTag, stays(wanted));
        return {};
    }
    for (const Expect& e : kGetJointsExpected) {
        if (!image.inText(getJointTransforms + e.at, 16) || !bytesMatch(getJointTransforms + e.at, e.bytes)) {
            EVR_LOG("%s: arm: GetJointTransforms differs at +0x%zX; %s", kTag, e.at, stays(wanted));
            return {};
        }
    }
    for (const Expect& e : kSetJointModExpected) {
        if (!image.inText(setJointMod + e.at, 24) || !bytesMatch(setJointMod + e.at, e.bytes)) {
            EVR_LOG("%s: arm: SetJointMod differs at +0x%zX; %s", kTag, e.at, stays(wanted));
            return {};
        }
    }
    findNameTable(image, kTag);
    const offhand_mods::ArmSet ready = offhand_mods::install(image, wanted);
    if (!ready.offHand && !ready.weapon) {
        return {};
    }
    g_getJoints = reinterpret_cast<GetJointTransformsFn>(const_cast<std::byte*>(getJointTransforms));
    g_setJointMod = reinterpret_cast<SetJointModFn>(const_cast<std::byte*>(setJointMod));
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
                               const xr_math::ModelPose* attachAnimated) {
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
    float scale[3] = {};
    if (!safeCopy(scale, animator + kAnimatorScale, sizeof(scale))) {
        refuse(side, Refusal::Scale, "the model scale cannot be read");
        return std::nullopt;
    }
    for (const float s : scale) {
        if (!std::isfinite(s) || s < 0.5f || s > 2.0f) {
            char message[96];
            std::snprintf(message, sizeof(message), "the model scale (%.3f %.3f %.3f) is out of range",
                          static_cast<double>(scale[0]), static_cast<double>(scale[1]),
                          static_cast<double>(scale[2]));
            refuse(side, Refusal::Scale, message);
            return std::nullopt;
        }
    }
    out.scale = {scale[0], scale[1], scale[2]};
    if (!resolveJoints(side, path, attachJoint)) {
        return std::nullopt;
    }
    const arm::ArmJointIndices& joints = c.joints;

    const std::byte* node = offhand_mods::modNode(hands);
    if (!node) {
        refuse(side, Refusal::List, "the hands' joint modifier node cannot be reached");
        return std::nullopt;
    }
    if (c.hands != hands || c.node != node || !offhand_mods::layerModsInPlace(node, c.base, joints)) {
        c.hands = hands;
        c.node = node;
        offhand_mods::AddError error;
        c.base = offhand_mods::addLayerMods(node, side, joints, error);
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
    for (std::size_t i = 0; i < arm::kArmJointCount; ++i) {
        const float* p = positions + i * 3;
        const float* a = axes + i * 9;
        out.animated[i] = {{p[0], p[1], p[2]}, {{a[0], a[1], a[2]}, {a[3], a[4], a[5]}, {a[6], a[7], a[8]}}};
        // GetJointTransforms hands out the identity for an axis it could not use; no arm joint has it.
        if (!arm::plausiblePose(out.animated[i]) || (i != 0 && isIdentity(out.animated[i].axis))) {
            const std::string message =
                std::string("the animated pose of ") + arm::armJointNames(side)[i] + " is not usable";
            refuse(side, Refusal::Read, message.c_str());
            return std::nullopt;
        }
    }
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
    // The read includes the layer's overrides of the last frames: while they may still show, the layer's
    // joints are carried by the wrist as they were in the last read without them.
    const xr_math::ModelPose& hand = out.animated[arm::index(arm::ArmJoint::Hand)];
    if (GetTickCount64() - c.lastOverride > kOverrideShowsMs) {
        for (const arm::ArmJoint j : offhand_mods::kLayerJoints) {
            c.fromHand[arm::index(j)] = arm::relative(hand, out.animated[arm::index(j)]);
        }
        c.fromHandOk = true;
    } else if (!c.fromHandOk) {
        refuse(side, Refusal::Read, "no animated pose of the arm without the layer's overrides yet");
        return std::nullopt;
    } else {
        // Where the overrides landed: where they were written, a frame behind at most.
        float off = 0.0f;
        for (const arm::ArmJoint j : offhand_mods::kLayerJoints) {
            off = std::max(off,
                           length(out.animated[arm::index(j)].position - c.written[arm::index(j)].position));
            out.animated[arm::index(j)] = arm::compose(hand, c.fromHand[arm::index(j)]);
        }
        out.landed = off;
        c.offTicks = off > kLandingTolerance ? c.offTicks + 1 : 0;
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

bool writeArm(
    arm::ArmSide side, const std::byte* hands, const arm::ArmPoses& poses, Vec3 scale, Vec3 origin) {
    Cache& c = cacheOf(side);
    if (g_gameCallFaulted || hands != c.hands || !offhand_mods::layerModsInPlace(c.node, c.base, c.joints)) {
        return false;
    }
    for (std::int32_t k = 0; k < offhand_mods::kLayerCount; ++k) {
        const arm::ArmJoint joint = offhand_mods::kLayerJoints[static_cast<std::size_t>(k)];
        const xr_math::ModelPose& pose = poses[arm::index(joint)];
        const float translation[3] = {(pose.position.x - origin.x) / scale.x,
                                      (pose.position.y - origin.y) / scale.y,
                                      (pose.position.z - origin.z) / scale.z};
        const xr_math::Mat3Rows rotation = xr_math::toRows(pose.axis);
        if (!callSetJointMod(const_cast<std::byte*>(hands), c.base + k, c.joints[arm::index(joint)],
                             translation, rotation.data(), kFlagsOverride)) {
            callFaulted(side, "SetJointMod");
            return false;
        }
        c.lastOverride = GetTickCount64();
    }
    c.written = poses;
    return true;
}

bool releaseArm(arm::ArmSide side, const std::byte* hands) {
    Cache& c = cacheOf(side);
    if (g_gameCallFaulted || !hands || hands != c.hands ||
        !offhand_mods::layerModsInPlace(c.node, c.base, c.joints)) {
        return false;
    }
    const float zero[3] = {};
    const xr_math::Mat3Rows identity{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (std::int32_t k = 0; k < offhand_mods::kLayerCount; ++k) {
        const arm::ArmJoint joint = offhand_mods::kLayerJoints[static_cast<std::size_t>(k)];
        if (!callSetJointMod(const_cast<std::byte*>(hands), c.base + k, c.joints[arm::index(joint)], zero,
                             identity.data(), kFlagsChange)) {
            callFaulted(side, "SetJointMod");
            return false;
        }
    }
    return true;
}

const std::byte* armHands(arm::ArmSide side) {
    return cacheOf(side).hands;
}

} // namespace evr::vkcore::controllers::game_arm
