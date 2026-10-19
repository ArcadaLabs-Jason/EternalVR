// Finding an arm's joints in the game's loaded arms skeleton (game_arm_joints.hpp).

#include "vkcore/game_arm_joints.hpp"

#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/seh_filter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace evr::vkcore::controllers::game_arm {

namespace {

constexpr std::size_t kMaxSkeletonBytes = 0x10000;

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

// The global name table's string-to-handle call, as idHands::InitJointMods (0x138B080) makes it for the
// attach joints: mov rcx, [table]; lea rdx, [out]; mov r8, [name]; mov rax, [rcx]; call [rax+0x38].
using NameHandleFn = void*(__fastcall*)(void* table, std::int16_t* out, const char* name);
constexpr const char* kNameHandleCall =
    "48 8B 0D ?? ?? ?? ?? 48 8D 94 24 88 00 00 00 4C 8B 05 ?? ?? ?? ?? 48 8B 01 FF 50 38";
constexpr std::size_t kNameHandleSlot = 0x38;

const std::byte* g_nameTableGlobal = nullptr; // holds the name table's address
const std::byte* g_textBegin = nullptr;
const std::byte* g_textEnd = nullptr;

constexpr SideText kSideText[2] = {
    {"offhand", "offhand: arm", "the off hand", "left"},
    {"weapon arm", "weapon arm", "the weapon arm", "right"},
};

std::size_t slot(arm::ArmSide side) {
    return side == arm::ArmSide::Right ? 1 : 0;
}

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

// The game's name handle for `name` (the call InitJointMods makes for "lefthandattach"); false when the
// call faults. Apart from anything with a destructor (__try needs that).
bool callNameHandle(void* table, NameHandleFn fn, std::int16_t& out, const char* name) {
    __try {
        fn(table, &out, name);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// Game thread only: the name lookup faulted once, so it is never called again this session.
bool g_nameLookupFaulted = false;

// The handles of `names`, or nullopt when the name table cannot be used.
template <std::size_t N>
std::optional<std::array<std::int16_t, N>> nameHandles(const std::array<const char*, N>& names) {
    if (g_nameLookupFaulted) {
        return std::nullopt;
    }
    const std::byte* table = nullptr;
    const std::byte* vtable = nullptr;
    const std::byte* fn = nullptr;
    if (!g_nameTableGlobal || !safeRead(g_nameTableGlobal, table) || !table || !safeRead(table, vtable) ||
        !vtable || !safeRead(vtable + kNameHandleSlot, fn) || !fn || fn < g_textBegin || fn >= g_textEnd) {
        return std::nullopt;
    }
    std::array<std::int16_t, N> out{};
    for (std::size_t j = 0; j < N; ++j) {
        out[j] = -1;
        if (!callNameHandle(const_cast<std::byte*>(table),
                            reinterpret_cast<NameHandleFn>(const_cast<std::byte*>(fn)), out[j], names[j])) {
            g_nameLookupFaulted = true;
            return std::nullopt;
        }
    }
    return out;
}

} // namespace

std::optional<Vec3> readModelScale(const std::byte* animator, std::string& error) {
    constexpr std::size_t kAnimatorScale = 0x500;
    float scale[3] = {};
    if (!safeCopy(scale, animator + kAnimatorScale, sizeof(scale))) {
        error = "the model scale cannot be read";
        return std::nullopt;
    }
    for (const float s : scale) {
        if (!std::isfinite(s) || s < 0.5f || s > 2.0f) {
            char message[96];
            std::snprintf(message, sizeof(message), "the model scale (%.3f %.3f %.3f) is out of range",
                          static_cast<double>(scale[0]), static_cast<double>(scale[1]),
                          static_cast<double>(scale[2]));
            error = message;
            return std::nullopt;
        }
    }
    return Vec3{scale[0], scale[1], scale[2]};
}

bool isIdentity(const xr_math::IdViewAxis& a) {
    return a.forward == Vec3{1.0f, 0.0f, 0.0f} && a.left == Vec3{0.0f, 1.0f, 0.0f} &&
           a.up == Vec3{0.0f, 0.0f, 1.0f};
}

bool closeTo(Vec3 a, Vec3 b, float eps) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
}

const std::byte* checkArmCalls(const GameImage& image,
                               const std::byte* lagModsStart,
                               const std::byte* getJointTransforms,
                               const std::byte* setJointMod,
                               const char* stays) {
    constexpr const char* kTag = "offhand";
    const std::byte* call = lagModsStart + kAnimatorCall;
    const std::byte* getter = image.inText(call, 5) && static_cast<std::uint8_t>(call[0]) == 0xE8
                                  ? call + 5 + readI32(call + 1)
                                  : nullptr;
    if (!getter || !matchesAt(image, getter, kAnimatorGetter)) {
        EVR_LOG("%s: arm: the animator getter is not where expected; %s", kTag, stays);
        return nullptr;
    }
    for (const Expect& e : kGetJointsExpected) {
        if (!matchesAt(image, getJointTransforms + e.at, e.bytes)) {
            EVR_LOG("%s: arm: GetJointTransforms differs at +0x%zX; %s", kTag, e.at, stays);
            return nullptr;
        }
    }
    for (const Expect& e : kSetJointModExpected) {
        if (!matchesAt(image, setJointMod + e.at, e.bytes)) {
            EVR_LOG("%s: arm: SetJointMod differs at +0x%zX; %s", kTag, e.at, stays);
            return nullptr;
        }
    }
    return getter;
}

const SideText& sideText(arm::ArmSide side) {
    return kSideText[slot(side)];
}

void findNameTable(const GameImage& image, const char* tag) {
    g_textBegin = image.text.data();
    g_textEnd = image.text.data() + image.text.size();
    if (const std::byte* lookup = findUnique(image, tag, "the attach joints' name lookup", kNameHandleCall)) {
        const std::byte* global = ripTarget(image, lookup + 3, lookup + 7);
        const std::byte* nameSlot = ripTarget(image, lookup + 18, lookup + 22);
        const std::byte* name = nullptr;
        if (global && nameSlot && safeRead(nameSlot, name) && stringAt(image, name) == "lefthandattach") {
            g_nameTableGlobal = global;
        }
    }
    if (!g_nameTableGlobal) {
        EVR_LOG("%s: arm: the game's name table was not found; the arm's joint names will not be checked",
                tag);
    }
}

std::uint32_t nameTableRva(const GameImage& image) {
    return g_nameTableGlobal ? image.rva(g_nameTableGlobal) : 0u;
}

void dumpSkeleton(arm::ArmSide side, const SkeletonPath& p, std::int16_t attach) {
    static bool dumped[2] = {};
    if (dumped[slot(side)]) {
        return;
    }
    dumped[slot(side)] = true;
    const char* prefix = sideText(side).prefix;
    EVR_LOG("%s: skeleton path: hands %p, model %p, animator %p, animator+8 %p, +0x80 %p, +0x310 %p, "
            "+0x60 (data) %p",
            prefix, static_cast<const void*>(p.hands), static_cast<const void*>(p.model),
            static_cast<const void*>(p.animator), static_cast<const void*>(p.animModel),
            static_cast<const void*>(p.decl), static_cast<const void*>(p.skeleton),
            static_cast<const void*>(p.data));
    if (!p.data) {
        return;
    }
    EVR_LOG("%s: skeleton data +0x0: %s", prefix, hexDump(p.data, 0x80).c_str());
    std::uint16_t end = 0;
    std::uint16_t handles = 0;
    if (safeCopy(&end, p.data + arm::kSkeletonEndAt, sizeof(end)) &&
        safeCopy(&handles, p.data + arm::kSkeletonNameHandlesAt, sizeof(handles))) {
        EVR_LOG("%s: skeleton data +0x%X (the file's names): %s", prefix, end,
                hexDump(p.data + end, 0x40).c_str());
        EVR_LOG("%s: skeleton data +0x%X (the name handles): %s", prefix, handles,
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
        EVR_LOG("%s: skeleton parents (%u joints at +0x%X): unreadable", prefix, count, parentsAt);
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
    EVR_LOG("%s: skeleton parents (%u joints at +0x%X, the first %zu): %s", prefix, count, parentsAt, shown,
            table.c_str());
    EVR_LOG("%s: the game's %s attach joint %d has children: %s", prefix, sideText(side).side, attach,
            children.empty() ? "none" : children.c_str());
}

std::optional<arm::ArmJointIndices> findJoints(arm::ArmSide side,
                                               const SkeletonPath& path,
                                               std::int16_t attach,
                                               JointsProblem& problem,
                                               std::string& error) {
    const std::byte* data = path.data;
    const arm::SkeletonRead read = [data](std::size_t offset, void* out, std::size_t size) {
        return offset + size <= kMaxSkeletonBytes && safeCopy(out, data + offset, size);
    };
    const auto skeleton = arm::readSkeleton(read, error);
    const auto joints = skeleton ? arm::findArmJoints(*skeleton, attach, error) : std::nullopt;
    if (!joints) {
        problem = skeleton ? JointsProblem::Joints : JointsProblem::Skeleton;
        dumpSkeleton(side, path, attach);
        return std::nullopt;
    }
    const auto handles = nameHandles(arm::armJointNames(side));
    if (g_nameLookupFaulted) {
        problem = JointsProblem::Fault;
        error = "the game's name lookup faulted; off for this session";
        return std::nullopt;
    }
    // The weapon arm is on by default, so its names must check out; the off hand (asked for) may go by
    // shape alone, as before.
    if (!handles && side == arm::ArmSide::Right) {
        problem = JointsProblem::Names;
        error = "the game's name table is not usable, so the joints' names cannot be checked";
        return std::nullopt;
    }
    if (handles && !arm::namesMatch(*skeleton, side, *joints, *handles, error)) {
        problem = JointsProblem::Names;
        dumpSkeleton(side, path, attach);
        return std::nullopt;
    }
    const auto& j = *joints;
    const arm::ArmJointNames& n = arm::armJointNames(side);
    EVR_LOG("%s: arm joints (skeleton of %zu joints, %s): %s %d, %s %d, %s %d, %s %d, %s %d, %s %d, %s %d, "
            "%s %d",
            sideText(side).tag, skeleton->parents.size(),
            handles ? "found by shape, names checked with the game's name handles"
                    : "found by shape; names NOT checked, the game's name table was not usable",
            n[0], j[0], n[1], j[1], n[2], j[2], n[3], j[3], n[4], j[4], n[5], j[5], n[6], j[6], n[7], j[7]);
    return joints;
}

std::optional<arm::HandJointIndices>
findHandJoints(const SkeletonPath& path, std::int16_t wrist, std::string& error) {
    const std::byte* data = path.data;
    const arm::SkeletonRead read = [data](std::size_t offset, void* out, std::size_t size) {
        return offset + size <= kMaxSkeletonBytes && safeCopy(out, data + offset, size);
    };
    const auto skeleton = arm::readSkeleton(read, error);
    const auto handles = skeleton ? nameHandles(arm::kRightHandJointNames) : std::nullopt;
    if (!skeleton) {
        return std::nullopt;
    }
    if (!handles) {
        error = "the game's name table is not usable, so the glove's finger joints cannot be found";
        return std::nullopt;
    }
    const auto hand = arm::findHandJoints(*skeleton, wrist, *handles, error);
    if (hand) {
        EVR_LOG("weapon arm: the glove's joints (names checked with the game's name handles): %s %d .. %s %d",
                arm::kRightHandJointNames.front(), hand->front(), arm::kRightHandJointNames.back(),
                hand->back());
    }
    return hand;
}

} // namespace evr::vkcore::controllers::game_arm
