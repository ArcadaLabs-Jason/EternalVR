// The hands' joint-modifier lists for the free off hand's arm (offhand_mods.hpp).

#include "vkcore/offhand_mods.hpp"

#include "features/arm/mod_room.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace evr::vkcore::controllers::offhand_mods {

namespace {

constexpr const char* kTag = "offhand";

constexpr std::size_t kHandsJointMods = 0x2888; // the joint-modifier animator's node owner
constexpr std::size_t kJointModNode = 0x18;
// The modifier node (SetJointMod, SetNum).
constexpr std::size_t kNodeLists = 0x30;
constexpr std::size_t kListStride = 0x18;
constexpr std::size_t kListNum = 0x8;
constexpr std::size_t kListSize = 0xC;
constexpr std::size_t kModSize = 0x40;
constexpr std::size_t kModJoint = 0x30;

constexpr std::uint16_t kFlagsChange = 0x20B; // model space, rotation, translation, reference pose

constexpr const char* kInitJointModsSignature =
    "48 85 D2 0F 84 ?? ?? ?? ?? 53 48 83 EC 70 48 89 AC 24 80 00 00 00 48 8B D9";
constexpr const char* kSetNumSignature = "4C 8B DC 57 48 81 EC B0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 "
                                         "89 44 24 70 8B FA 89 54 24 24 3B 51 38";

struct Expect {
    std::size_t at;
    const char* bytes;
};
// idHands::InitJointMods (build 25216728); the hands are in rbx from +0x16 on.
constexpr Expect kInitExpected[] = {
    {0x3D, "48 8B 83 88 28 00 00"},                                // mov rax, [rbx+0x2888]
    {0x49, "48 8B 48 18"},                                         // mov rcx, [rax+0x18]   the old node
    {0x124, "48 8B 78 18 48 85 FF"},                               // mov rdi, [rax+0x18]   the new node
    {0x131, "FF 47 28"},                                           // inc [rdi+0x28]        the generation
    {0x1AB, "41 B8 0B 02 00 00 48 8B CB E8"},                      // AddJointMod(hands, ..., 0x20B, ...)
    {0x1C8, "41 B8 0B 02 00 00 89 83 D0 28 00 00 48 8B CB E8"},    // the second
    {0x1EB, "41 B8 0B 02 00 00 89 83 D4 28 00 00 48 8B CB E8"},    // the third
    {0x207, "89 83 D8 28 00 00 48 C7 83 74 29 00 00 00 00 80 3F"}, // its index stored, then the hook site
};
constexpr std::size_t kInitSetNumCall = 0x54;
constexpr std::size_t kAddJointModCalls[] = {0x1B4, 0x1D7, 0x1FA};
constexpr std::size_t kHookSite = 0x20D;
// AddJointMod: SetNum(node, [node+0x38] + 1).
constexpr std::size_t kAddSetNumCall = 0xA3;
constexpr const char* kAddSetNumBytes = "48 63 59 38 8D 53 01 E8";
constexpr std::size_t kAddSetNumBytesAt = 0x9C;
// SetNum: both lists at +0x30, 0x18 apart, each grown past its size with Resize.
constexpr Expect kSetNumExpected[] = {
    {0x33, "48 8D 59 30"},    // lea rbx, [rcx+0x30]
    {0x3B, "BE 02 00 00 00"}, // two lists
    {0x55, "3B 7B 0C 7E 14"}, // cmp edi, [rbx+0xC]: the size
    {0x5F, "E8"},             // call Resize
    {0x1AB, "44 89 4B 08"},   // mov [rbx+8], r9d: the count
    {0x1AF, "48 83 C3 18"},   // the next list
};

using SetNumFn = void(__fastcall*)(void* node, std::int32_t num);
SetNumFn g_setNum = nullptr;
std::atomic<std::uint32_t> g_roomLines{0};

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

const std::byte* callTarget(const GameImage& image, const std::byte* call) {
    return image.inText(call, 5) && static_cast<std::uint8_t>(call[0]) == 0xE8 ? call + 5 + readI32(call + 1)
                                                                               : nullptr;
}

struct ModList {
    std::byte* data = nullptr;
    std::int32_t num = 0;
    std::int32_t size = 0;
};

bool readList(const std::byte* node, std::size_t which, ModList& out) {
    const std::byte* list = node + kNodeLists + which * kListStride;
    return safeRead(list, out.data) && safeRead(list + kListNum, out.num) &&
           safeRead(list + kListSize, out.size) && out.data != nullptr;
}

arm::ModListCounts counts(const ModList& list) {
    return {list.num, list.size};
}

std::int16_t jointAt(const ModList& list, std::int32_t mod) {
    std::int16_t joint = -2;
    safeRead(list.data + static_cast<std::size_t>(mod) * kModSize + kModJoint, joint);
    return joint;
}

// The game's SetNum, apart from anything with a destructor (__try needs that). False when it faulted.
bool callSetNum(const std::byte* node, std::int32_t num) {
    __try {
        g_setNum(const_cast<std::byte*>(node), num);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A line per event, up to 64 (a map load or a respawn each).
bool roomLineDue() {
    return g_roomLines.fetch_add(1, std::memory_order_relaxed) < 64;
}

// InitJointMods, right after its third AddJointMod: room for the layer's six in the new lists.
void onJointModsBuilt(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const std::byte* node = modNode(reinterpret_cast<const std::byte*>(regs.rbx));
    ModList lists[2];
    if (!node || !readList(node, 0, lists[0]) || !readList(node, 1, lists[1])) {
        if (roomLineDue()) {
            EVR_LOG("%s: arm: the hands' new joint modifier lists cannot be read; no room made", kTag);
        }
        return;
    }
    const arm::RoomPlan plan = arm::planRoom(counts(lists[0]), counts(lists[1]), kLayerCount);
    if (plan.step != arm::RoomPlan::Step::Grow) {
        if (roomLineDue()) {
            EVR_LOG("%s: arm: the hands' new joint modifier lists (%d of %d, %d of %d) %s", kTag,
                    lists[0].num, lists[0].size, lists[1].num, lists[1].size,
                    plan.step == arm::RoomPlan::Step::Enough ? "have room already"
                                                             : "do not check out; no room made");
        }
        return;
    }
    const bool called = callSetNum(node, plan.growTo) && callSetNum(node, plan.num);
    bool read = readList(node, 0, lists[0]) && readList(node, 1, lists[1]);
    // SetNum lowers no count but the first list's when that one did not grow (mod_room.hpp).
    for (std::size_t which = 0; read && which < 2; ++which) {
        if (arm::restoreCount(counts(lists[which]), plan.num)) {
            auto* num = const_cast<std::byte*>(node + kNodeLists + which * kListStride + kListNum);
            read = safeCopy(num, &plan.num, sizeof(plan.num)) && readList(node, which, lists[which]);
        }
    }
    if (!roomLineDue()) {
        return;
    }
    if (called && read && arm::roomMade(counts(lists[0]), counts(lists[1]), plan.num, kLayerCount)) {
        EVR_LOG("%s: arm: the game's SetNum made room for 6 more joint modifiers in the hands' new lists (%d "
                "in use, room for %d and %d)",
                kTag, plan.num, lists[0].size, lists[1].size);
    } else {
        EVR_LOG("%s: arm: SetNum did not make room for 6 more joint modifiers (%s; %d of %d, %d of %d)", kTag,
                !called ? "it faulted"
                : !read ? "the lists cannot be read back"
                        : "the lists did not grow",
                lists[0].num, lists[0].size, lists[1].num, lists[1].size);
    }
}

} // namespace

bool install(const GameImage& image) {
    const std::byte* init = findUnique(image, kTag, "InitJointMods", kInitJointModsSignature);
    const std::byte* setNum = findUnique(image, kTag, "the joint modifier node's SetNum", kSetNumSignature);
    if (!init || !setNum || !image.inText(init, kHookSite + 16) || !image.inText(setNum, 0x1B4)) {
        EVR_LOG("%s: arm: InitJointMods or SetNum not found; off hand stays the game's", kTag);
        return false;
    }
    for (const Expect& e : kInitExpected) {
        if (!bytesMatch(init + e.at, e.bytes)) {
            EVR_LOG("%s: arm: InitJointMods differs at +0x%zX; off hand stays the game's", kTag, e.at);
            return false;
        }
    }
    for (const Expect& e : kSetNumExpected) {
        if (!bytesMatch(setNum + e.at, e.bytes)) {
            EVR_LOG("%s: arm: SetNum differs at +0x%zX; off hand stays the game's", kTag, e.at);
            return false;
        }
    }
    const std::byte* add = callTarget(image, init + kAddJointModCalls[0]);
    bool calls = add && callTarget(image, init + kInitSetNumCall) == setNum &&
                 image.inText(add, kAddSetNumCall + 5) &&
                 bytesMatch(add + kAddSetNumBytesAt, kAddSetNumBytes) &&
                 callTarget(image, add + kAddSetNumCall) == setNum;
    for (const std::size_t at : kAddJointModCalls) {
        calls = calls && callTarget(image, init + at) == add;
    }
    if (!calls) {
        EVR_LOG(
            "%s: arm: InitJointMods does not call AddJointMod and SetNum where expected; off hand stays the "
            "game's",
            kTag);
        return false;
    }
    g_setNum = reinterpret_cast<SetNumFn>(const_cast<std::byte*>(setNum));
    std::string error;
    std::byte* site = const_cast<std::byte*>(init + kHookSite);
    if (!installMidHook(site, &onJointModsBuilt, error)) {
        EVR_LOG("%s: arm: hook at RVA 0x%X failed: %s; off hand stays the game's", kTag, image.rva(site),
                error.c_str());
        return false;
    }
    EVR_LOG("%s: arm: InitJointMods at RVA 0x%X (AddJointMod 0x%X, SetNum 0x%X), hooked at RVA 0x%X to make "
            "room for the layer's joint modifiers",
            kTag, image.rva(init), image.rva(add), image.rva(setNum), image.rva(site));
    return true;
}

const std::byte* modNode(const std::byte* hands) {
    const std::byte* owner = nullptr;
    const std::byte* node = nullptr;
    if (!hands || !safeRead(hands + kHandsJointMods, owner) || !owner ||
        !safeRead(owner + kJointModNode, node)) {
        return nullptr;
    }
    return node;
}

bool layerModsInPlace(const std::byte* node, std::int32_t base, const arm::ArmJointIndices& joints) {
    if (!node || base < 0) {
        return false;
    }
    for (std::size_t which = 0; which < 2; ++which) {
        ModList list;
        if (!readList(node, which, list) || list.num < base + kLayerCount || list.num > list.size ||
            list.size > arm::kMaxJointMods) {
            return false;
        }
        for (std::int32_t k = 0; k < kLayerCount; ++k) {
            if (jointAt(list, base + k) != joints[arm::index(kLayerJoints[static_cast<std::size_t>(k)])]) {
                return false;
            }
        }
    }
    return true;
}

std::int32_t addLayerMods(const std::byte* node, const arm::ArmJointIndices& joints, AddError& error) {
    ModList lists[2];
    if (!readList(node, 0, lists[0]) || !readList(node, 1, lists[1]) ||
        !arm::modListsAgree(counts(lists[0]), counts(lists[1]))) {
        error = {Problem::List, "the hands' joint modifier lists do not check out"};
        return -1;
    }
    const std::int32_t base = lists[0].num;
    if (!arm::roomFor(counts(lists[0]), counts(lists[1]), kLayerCount)) {
        char message[160];
        std::snprintf(message, sizeof(message),
                      "no room for 6 more joint modifiers (%d in use, room for %d and %d; none was made when "
                      "the game built the lists)",
                      base, lists[0].size, lists[1].size);
        error = {Problem::NoRoom, message};
        return -1;
    }
    for (const ModList& list : lists) {
        for (std::int32_t m = 0; m < base; ++m) {
            for (const arm::ArmJoint j : kLayerJoints) {
                if (jointAt(list, m) == joints[arm::index(j)]) {
                    error = {Problem::Taken,
                             std::string("the game already modifies ") + arm::kArmJointNames[arm::index(j)]};
                    return -1;
                }
            }
        }
    }
    for (ModList& list : lists) {
        for (std::int32_t k = 0; k < kLayerCount; ++k) {
            std::byte entry[kModSize] = {};
            const float identity[12] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f};
            const std::int16_t joint = joints[arm::index(kLayerJoints[static_cast<std::size_t>(k)])];
            const std::int16_t noParent = -1;
            std::memcpy(entry, identity, sizeof(identity));
            std::memcpy(entry + kModJoint, &joint, sizeof(joint));
            std::memcpy(entry + kModJoint + 2, &noParent, sizeof(noParent));
            std::memcpy(entry + kModJoint + 4, &kFlagsChange, sizeof(kFlagsChange));
            if (!safeCopy(list.data + static_cast<std::size_t>(base + k) * kModSize, entry, sizeof(entry))) {
                error = {Problem::List, "a joint modifier could not be written"};
                return -1;
            }
        }
    }
    // The counts last, as SetNum does, so a reader never sees an entry before it is filled in.
    const std::int32_t num = base + kLayerCount;
    auto* first = const_cast<std::byte*>(node + kNodeLists + kListNum);
    auto* second = const_cast<std::byte*>(node + kNodeLists + kListStride + kListNum);
    if (!safeCopy(first, &num, sizeof(num)) || !safeCopy(second, &num, sizeof(num))) {
        error = {Problem::List, "the joint modifier count could not be written"};
        return -1;
    }
    EVR_LOG("%s: arm: 6 joint modifiers added at %d..%d (the hands' list now holds %d, room for %d)", kTag,
            base, base + kLayerCount - 1, num, lists[0].size);
    return base;
}

} // namespace evr::vkcore::controllers::offhand_mods
