// The whole left arm for the free off hand (offhand_arm.hpp).

#include "vkcore/offhand_arm.hpp"

#include "features/arm/arm_frames.hpp"
#include "features/arm/arm_skeleton.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/offhand_mods.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers::offhand_arm {

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
constexpr std::size_t kMaxSkeletonBytes = 0x10000;
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

// The global name table's string-to-handle call, as idHands::InitJointMods (0x138B080) makes it for the
// attach joints: mov rcx, [table]; lea rdx, [out]; mov r8, [name]; mov rax, [rcx]; call [rax+0x38].
using NameHandleFn = void*(__fastcall*)(void* table, std::int16_t* out, const char* name);
constexpr const char* kNameHandleCall =
    "48 8B 0D ?? ?? ?? ?? 48 8D 94 24 88 00 00 00 4C 8B 05 ?? ?? ?? ?? 48 8B 01 FF 50 38";
constexpr std::size_t kNameHandleSlot = 0x38;

GetJointTransformsFn g_getJoints = nullptr;
const std::byte* g_nameTableGlobal = nullptr; // holds the name table's address
const std::byte* g_textBegin = nullptr;
const std::byte* g_textEnd = nullptr;
SetJointModFn g_setJointMod = nullptr;

// Game thread only.
struct Cache {
    const std::byte* skeletonData = nullptr;
    std::int16_t attach = -1;
    arm::ArmJointIndices joints{};
    bool jointsOk = false;
    const std::byte* hands = nullptr;
    const std::byte* node = nullptr;
    std::int32_t base = -1; // the first of the layer's modifiers in both lists
    // The layer's joints relative to LeftHand, from the last read the layer's overrides had not reached.
    arm::ArmPoses fromHand{};
    bool fromHandOk = false;
    ULONGLONG lastOverride = 0; // GetTickCount64 of the last override written
    arm::ArmPoses written{};    // the poses last written
    std::int32_t offTicks = 0;  // ticks in a row the read put an override away from where it was written
    bool offForGood = false;
};
Cache g_cache;
std::uint32_t g_loggedReasons = 0;

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
};

// Logs `message` the first time `reason` comes up.
void refuse(Refusal reason, const char* message) {
    const std::uint32_t bit = 1u << static_cast<unsigned>(reason);
    if ((g_loggedReasons & bit) != 0) {
        return;
    }
    g_loggedReasons |= bit;
    EVR_LOG("%s: arm: %s; the off hand stays the game's", kTag, message);
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool callSetJointMod(
    void* hands, std::int32_t mod, std::int16_t joint, const float* t, const float* r, std::uint16_t f) {
    __try {
        g_setJointMod(hands, mod, joint, t, r, f);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The pointers readArm followed to the skeleton, for the diagnostic dump.
struct SkeletonPath {
    const std::byte* hands = nullptr;
    const std::byte* model = nullptr;     // hands +0x370
    const std::byte* animator = nullptr;  // model +0x4E0
    const std::byte* animModel = nullptr; // animator +0x8
    const std::byte* decl = nullptr;      // +0x80
    const std::byte* skeleton = nullptr;  // +0x310
    const std::byte* data = nullptr;      // +0x60
};

// Up to 0x80 bytes at `at` as hex, read safely ("unreadable" when they cannot be).
std::string hexDump(const std::byte* at, std::size_t size) {
    std::uint8_t bytes[0x80] = {};
    size = std::min(size, sizeof(bytes));
    if (!at || !safeCopy(bytes, at, size)) {
        return "unreadable";
    }
    std::string out;
    char hex[4];
    for (std::size_t i = 0; i < size; ++i) {
        std::snprintf(hex, sizeof(hex), i % 16 == 15 ? "%02X|" : "%02X ", bytes[i]);
        out += hex;
    }
    return out;
}

// Once, on the first failure to read the skeleton: the pointers followed and the bytes found, so a rig log
// shows the real layout.
void dumpSkeleton(const SkeletonPath& p, std::int16_t attach) {
    static bool dumped = false;
    if (dumped) {
        return;
    }
    dumped = true;
    EVR_LOG("%s: arm: skeleton path: hands %p, model %p, animator %p, animator+8 %p, +0x80 %p, +0x310 %p, "
            "+0x60 (data) %p",
            kTag, static_cast<const void*>(p.hands), static_cast<const void*>(p.model),
            static_cast<const void*>(p.animator), static_cast<const void*>(p.animModel),
            static_cast<const void*>(p.decl), static_cast<const void*>(p.skeleton),
            static_cast<const void*>(p.data));
    if (!p.data) {
        return;
    }
    EVR_LOG("%s: arm: skeleton data +0x0: %s", kTag, hexDump(p.data, 0x80).c_str());
    std::uint16_t end = 0;
    std::uint16_t handles = 0;
    if (safeCopy(&end, p.data + arm::kSkeletonEndAt, sizeof(end)) &&
        safeCopy(&handles, p.data + arm::kSkeletonNameHandlesAt, sizeof(handles))) {
        EVR_LOG("%s: arm: skeleton data +0x%X (the file's names): %s", kTag, end,
                hexDump(p.data + end, 0x40).c_str());
        EVR_LOG("%s: arm: skeleton data +0x%X (the name handles): %s", kTag, handles,
                hexDump(p.data + handles, 0x40).c_str());
    }
    // The parent table (up to 100 entries) and the attach joint's children.
    std::uint16_t count = 0;
    std::uint16_t parentsAt = 0;
    std::int16_t parents[100] = {};
    if (!safeCopy(&count, p.data + arm::kSkeletonCountAt, sizeof(count)) ||
        !safeCopy(&parentsAt, p.data + arm::kSkeletonParentsAt, sizeof(parentsAt))) {
        return;
    }
    const std::size_t shown = std::min<std::size_t>(count, 100);
    if (!safeCopy(parents, p.data + parentsAt, shown * sizeof(std::int16_t))) {
        EVR_LOG("%s: arm: skeleton parents (%u joints at +0x%X): unreadable", kTag, count, parentsAt);
        return;
    }
    std::string table;
    std::string children;
    for (std::size_t i = 0; i < shown; ++i) {
        table += (i ? " " : "") + std::to_string(parents[i]);
        if (parents[i] == attach && attach >= 0) {
            children += (children.empty() ? "" : " ") + std::to_string(i);
        }
    }
    EVR_LOG("%s: arm: skeleton parents (%u joints at +0x%X, the first %zu): %s", kTag, count, parentsAt,
            shown, table.c_str());
    EVR_LOG("%s: arm: the game's left attach joint %d has children: %s", kTag, attach,
            children.empty() ? "none" : children.c_str());
}

// The game's name handle for `name` (the call InitJointMods makes for "lefthandattach"); false when the
// call faults.
bool callNameHandle(void* table, NameHandleFn fn, std::int16_t& out, const char* name) {
    __try {
        fn(table, &out, name);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The handles of kArmJointNames, or nullopt when the name table cannot be used.
std::optional<std::array<std::int16_t, arm::kArmJointCount>> nameHandles() {
    const std::byte* table = nullptr;
    const std::byte* vtable = nullptr;
    const std::byte* fn = nullptr;
    if (!g_nameTableGlobal || !safeRead(g_nameTableGlobal, table) || !table || !safeRead(table, vtable) ||
        !vtable || !safeRead(vtable + kNameHandleSlot, fn) || !fn || fn < g_textBegin || fn >= g_textEnd) {
        return std::nullopt;
    }
    std::array<std::int16_t, arm::kArmJointCount> out{};
    for (std::size_t j = 0; j < arm::kArmJointCount; ++j) {
        out[j] = -1;
        if (!callNameHandle(const_cast<std::byte*>(table),
                            reinterpret_cast<NameHandleFn>(const_cast<std::byte*>(fn)), out[j],
                            arm::kArmJointNames[j])) {
            return std::nullopt;
        }
    }
    return out;
}

// The arm's joints in the skeleton at `path.data`, from the game's attach joint, cached per skeleton.
bool resolveJoints(const SkeletonPath& path, std::int16_t attach) {
    const std::byte* data = path.data;
    if (data == g_cache.skeletonData && attach == g_cache.attach) {
        return g_cache.jointsOk;
    }
    g_cache.skeletonData = data;
    g_cache.attach = attach;
    g_cache.jointsOk = false;
    const arm::SkeletonRead read = [data](std::size_t offset, void* out, std::size_t size) {
        return offset + size <= kMaxSkeletonBytes && safeCopy(out, data + offset, size);
    };
    std::string error;
    const auto skeleton = arm::readSkeleton(read, error);
    const auto joints = skeleton ? arm::findArmJoints(*skeleton, attach, error) : std::nullopt;
    if (!joints) {
        refuse(skeleton ? Refusal::Joints : Refusal::Skeleton, error.c_str());
        dumpSkeleton(path, attach);
        return false;
    }
    const auto handles = nameHandles();
    if (handles && !arm::namesMatch(*skeleton, *joints, *handles, error)) {
        refuse(Refusal::Names, error.c_str());
        dumpSkeleton(path, attach);
        return false;
    }
    g_cache.joints = *joints;
    g_cache.jointsOk = true;
    const auto& j = *joints;
    EVR_LOG("%s: arm joints (skeleton of %zu joints, %s): %s %d, %s %d, %s %d, %s %d, %s %d, %s %d, %s %d, "
            "%s %d",
            kTag, skeleton->parents.size(),
            handles ? "found by shape, names checked with the game's name handles"
                    : "found by shape; names NOT checked, the game's name table was not usable",
            arm::kArmJointNames[0], j[0], arm::kArmJointNames[1], j[1], arm::kArmJointNames[2], j[2],
            arm::kArmJointNames[3], j[3], arm::kArmJointNames[4], j[4], arm::kArmJointNames[5], j[5],
            arm::kArmJointNames[6], j[6], arm::kArmJointNames[7], j[7]);
    return true;
}

bool isIdentity(const xr_math::IdViewAxis& a) {
    return a.forward == Vec3{1.0f, 0.0f, 0.0f} && a.left == Vec3{0.0f, 1.0f, 0.0f} &&
           a.up == Vec3{0.0f, 0.0f, 1.0f};
}

bool closeTo(Vec3 a, Vec3 b, float eps) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

} // namespace

bool install(const GameImage& image,
             const std::byte* lagModsStart,
             const std::byte* getJointTransforms,
             const std::byte* setJointMod) {
    const std::byte* call = lagModsStart + kAnimatorCall;
    const std::byte* getter = image.inText(call, 5) && static_cast<std::uint8_t>(call[0]) == 0xE8
                                  ? call + 5 + readI32(call + 1)
                                  : nullptr;
    if (!getter || !image.inText(getter, 15) || !bytesMatch(getter, kAnimatorGetter)) {
        EVR_LOG("%s: arm: the animator getter is not where expected; off hand stays the game's", kTag);
        return false;
    }
    for (const Expect& e : kGetJointsExpected) {
        if (!image.inText(getJointTransforms + e.at, 16) || !bytesMatch(getJointTransforms + e.at, e.bytes)) {
            EVR_LOG("%s: arm: GetJointTransforms differs at +0x%zX; off hand stays the game's", kTag, e.at);
            return false;
        }
    }
    for (const Expect& e : kSetJointModExpected) {
        if (!image.inText(setJointMod + e.at, 24) || !bytesMatch(setJointMod + e.at, e.bytes)) {
            EVR_LOG("%s: arm: SetJointMod differs at +0x%zX; off hand stays the game's", kTag, e.at);
            return false;
        }
    }
    // The name table, found where InitJointMods asks it for "lefthandattach". Without it the joints are
    // still found by the skeleton's shape, but their names go unchecked (logged).
    g_textBegin = image.text.data();
    g_textEnd = image.text.data() + image.text.size();
    if (const std::byte* lookup =
            findUnique(image, kTag, "the attach joints' name lookup", kNameHandleCall)) {
        const std::byte* global = ripTarget(image, lookup + 3, lookup + 7);
        const std::byte* nameSlot = ripTarget(image, lookup + 18, lookup + 22);
        const std::byte* name = nullptr;
        if (global && nameSlot && safeRead(nameSlot, name) && stringAt(image, name) == "lefthandattach") {
            g_nameTableGlobal = global;
        }
    }
    if (!g_nameTableGlobal) {
        EVR_LOG("%s: arm: the game's name table was not found; the arm's joint names will not be checked",
                kTag);
    }
    if (!offhand_mods::install(image)) {
        return false;
    }
    g_getJoints = reinterpret_cast<GetJointTransformsFn>(const_cast<std::byte*>(getJointTransforms));
    g_setJointMod = reinterpret_cast<SetJointModFn>(const_cast<std::byte*>(setJointMod));
    EVR_LOG("%s: arm: animator getter at RVA 0x%X, name table at RVA 0x%X; the forearm, elbow and shoulder "
            "follow the hand (two-bone IK)",
            kTag, image.rva(getter), g_nameTableGlobal ? image.rva(g_nameTableGlobal) : 0u);
    return true;
}

std::optional<ArmRead>
readArm(const std::byte* hands, std::int16_t attachJoint, const xr_math::ModelPose& attachAnimated) {
    if (!g_getJoints || !g_setJointMod || g_cache.offForGood) {
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
        refuse(Refusal::Animator, "the arms' animator or skeleton cannot be reached");
        dumpSkeleton(path, attachJoint);
        return std::nullopt;
    }
    const std::byte* animator = path.animator;
    ArmRead out;
    float scale[3] = {};
    if (!safeCopy(scale, animator + kAnimatorScale, sizeof(scale))) {
        refuse(Refusal::Scale, "the model scale cannot be read");
        return std::nullopt;
    }
    for (const float s : scale) {
        if (!std::isfinite(s) || s < 0.5f || s > 2.0f) {
            char message[96];
            std::snprintf(message, sizeof(message), "the model scale (%.3f %.3f %.3f) is out of range",
                          static_cast<double>(scale[0]), static_cast<double>(scale[1]),
                          static_cast<double>(scale[2]));
            refuse(Refusal::Scale, message);
            return std::nullopt;
        }
    }
    out.scale = {scale[0], scale[1], scale[2]};
    if (!resolveJoints(path, attachJoint)) {
        return std::nullopt;
    }
    const arm::ArmJointIndices& joints = g_cache.joints;

    const std::byte* node = offhand_mods::modNode(hands);
    if (!node) {
        refuse(Refusal::List, "the hands' joint modifier node cannot be reached");
        return std::nullopt;
    }
    if (g_cache.hands != hands || g_cache.node != node ||
        !offhand_mods::layerModsInPlace(node, g_cache.base, joints)) {
        g_cache.hands = hands;
        g_cache.node = node;
        offhand_mods::AddError error;
        g_cache.base = offhand_mods::addLayerMods(node, joints, error);
        if (g_cache.base < 0) {
            refuse(error.problem == offhand_mods::Problem::NoRoom  ? Refusal::NoRoom
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
                       static_cast<std::int32_t>(arm::kArmJointCount), positions, axes, read) ||
        !read) {
        refuse(Refusal::Read, "GetJointTransforms failed for the arm's joints");
        return std::nullopt;
    }
    for (std::size_t i = 0; i < arm::kArmJointCount; ++i) {
        const float* p = positions + i * 3;
        const float* a = axes + i * 9;
        out.animated[i] = {{p[0], p[1], p[2]}, {{a[0], a[1], a[2]}, {a[3], a[4], a[5]}, {a[6], a[7], a[8]}}};
        // GetJointTransforms hands out the identity for an axis it could not use; no arm joint has it.
        if (!arm::plausiblePose(out.animated[i]) || (i != 0 && isIdentity(out.animated[i].axis))) {
            const std::string message =
                std::string("the animated pose of ") + arm::kArmJointNames[i] + " is not usable";
            refuse(Refusal::Read, message.c_str());
            return std::nullopt;
        }
    }
    const xr_math::ModelPose& attach = out.animated[arm::index(arm::ArmJoint::Attach)];
    if (!closeTo(attach.position, attachAnimated.position, 1e-3f) ||
        !closeTo(attach.axis.forward, attachAnimated.axis.forward, 1e-3f) ||
        !closeTo(attach.axis.up, attachAnimated.axis.up, 1e-3f)) {
        refuse(Refusal::Mismatch, "the attach joint read back differs from the game's");
        return std::nullopt;
    }
    // The origin (the skeleton's root, joint 0): an override's translation is taken from it.
    const std::int16_t root = 0;
    float rootPosition[3] = {};
    float rootAxes[9] = {};
    if (!callGetJoints(const_cast<std::byte*>(animator), &root, 1, rootPosition, rootAxes, read) || !read ||
        !std::isfinite(rootPosition[0]) || !std::isfinite(rootPosition[1]) ||
        !std::isfinite(rootPosition[2])) {
        refuse(Refusal::Read, "GetJointTransforms failed for the skeleton's root");
        return std::nullopt;
    }
    out.origin = {rootPosition[0], rootPosition[1], rootPosition[2]};
    // The read includes the layer's overrides of the last frames: while they may still show, the layer's
    // joints are carried by the wrist as they were in the last read without them.
    const xr_math::ModelPose& hand = out.animated[arm::index(arm::ArmJoint::Hand)];
    if (GetTickCount64() - g_cache.lastOverride > kOverrideShowsMs) {
        for (const arm::ArmJoint j : offhand_mods::kLayerJoints) {
            g_cache.fromHand[arm::index(j)] = arm::relative(hand, out.animated[arm::index(j)]);
        }
        g_cache.fromHandOk = true;
    } else if (!g_cache.fromHandOk) {
        refuse(Refusal::Read, "no animated pose of the arm without the layer's overrides yet");
        return std::nullopt;
    } else {
        // Where the overrides landed: where they were written, a frame behind at most.
        float off = 0.0f;
        for (const arm::ArmJoint j : offhand_mods::kLayerJoints) {
            off = std::max(
                off, length(out.animated[arm::index(j)].position - g_cache.written[arm::index(j)].position));
            out.animated[arm::index(j)] = arm::compose(hand, g_cache.fromHand[arm::index(j)]);
        }
        g_cache.offTicks = off > kLandingTolerance ? g_cache.offTicks + 1 : 0;
        if (g_cache.offTicks >= kLandingTicks) {
            g_cache.offForGood = true;
            char message[128];
            std::snprintf(message, sizeof(message),
                          "the layer's overrides do not land where written (%.3f away for %d ticks)",
                          static_cast<double>(off), g_cache.offTicks);
            refuse(Refusal::Landing, message);
            return std::nullopt;
        }
    }
    return out;
}

bool writeArm(const std::byte* hands, const arm::ArmPoses& poses, Vec3 scale, Vec3 origin) {
    if (hands != g_cache.hands ||
        !offhand_mods::layerModsInPlace(g_cache.node, g_cache.base, g_cache.joints)) {
        return false;
    }
    for (std::int32_t k = 0; k < offhand_mods::kLayerCount; ++k) {
        const arm::ArmJoint joint = offhand_mods::kLayerJoints[static_cast<std::size_t>(k)];
        const xr_math::ModelPose& pose = poses[arm::index(joint)];
        const float translation[3] = {(pose.position.x - origin.x) / scale.x,
                                      (pose.position.y - origin.y) / scale.y,
                                      (pose.position.z - origin.z) / scale.z};
        const xr_math::Mat3Rows rotation = xr_math::toRows(pose.axis);
        if (!callSetJointMod(const_cast<std::byte*>(hands), g_cache.base + k,
                             g_cache.joints[arm::index(joint)], translation, rotation.data(),
                             kFlagsOverride)) {
            return false;
        }
        g_cache.lastOverride = GetTickCount64();
    }
    g_cache.written = poses;
    return true;
}

void releaseArm(const std::byte* hands) {
    if (!hands || hands != g_cache.hands ||
        !offhand_mods::layerModsInPlace(g_cache.node, g_cache.base, g_cache.joints)) {
        return;
    }
    const float zero[3] = {};
    const xr_math::Mat3Rows identity{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (std::int32_t k = 0; k < offhand_mods::kLayerCount; ++k) {
        const arm::ArmJoint joint = offhand_mods::kLayerJoints[static_cast<std::size_t>(k)];
        callSetJointMod(const_cast<std::byte*>(hands), g_cache.base + k, g_cache.joints[arm::index(joint)],
                        zero, identity.data(), kFlagsChange);
    }
}

} // namespace evr::vkcore::controllers::offhand_arm
