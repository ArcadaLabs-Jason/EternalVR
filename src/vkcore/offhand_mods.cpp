// The hands' joint-modifier lists for the arms the layer bends (offhand_mods.hpp).

#include "vkcore/offhand_mods.hpp"

#include "features/arm/grip_hand.hpp"
#include "features/arm/mod_mix.hpp"
#include "features/arm/mod_room.hpp"
#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seh_filter.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

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
// The weapon arm's attach joint: the second name lookup (mov rcx, [table]; lea rdx, [rsp+0x90]; mov r8,
// [name]) and the second AddJointMod's arguments (movzx edx, [rsp+0x90]; lea r9, [rbx+0x28E6]).
constexpr std::size_t kRightNameLookup = 0x164;
constexpr const char* kRightNameLookupBytes = "48 8B 0D ?? ?? ?? ?? 48 8D 94 24 90 00 00 00 4C 8B 05";
constexpr Expect kRightAttachExpected[] = {
    {0x1B9, "0F B7 94 24 90 00 00 00 4C 8D 8B E6 28 00 00"},
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

// The node's weight (offhand_mods.hpp): InitJointMods loads it into the create's parameters and hands the
// create idHands+0x2878, whose +0x10 is the owner (idHands+0x2888); the create copies the weight to the
// owner's current and target weight, GetWeight returns the current one.
constexpr std::size_t kOwnerWeight = 0x3C;
constexpr std::size_t kOwnerTargetWeight = 0x40;
constexpr std::size_t kInitCreateCall = 0x10A;
constexpr Expect kWeightInitExpected[] = {
    {0xB0, "F3 0F 10 05"},           // movss xmm0, [the constant]
    {0xBA, "F3 0F 11 44 24 4C"},     // movss [rsp+0x4C], xmm0: the parameters' +0x2C
    {0x103, "48 8D 8B 78 28 00 00"}, // lea rcx, [rbx+0x2878]: the create's object
};
constexpr Expect kWeightCreateExpected[] = {
    {0xF, "48 8B D9"},                             // mov rbx, rcx
    {0x5F, "48 8B 4B 10"},                         // mov rcx, [rbx+0x10]: the owner
    {0x76, "8B 47 2C 89 41 3C 8B 47 2C 89 41 40"}, // the weight to the owner's +0x3C and +0x40
};
constexpr std::ptrdiff_t kGetWeightFromCreate = -0x20;
constexpr const char* kGetWeightBytes = "48 8B 41 10 F3 0F 10 40 3C C3";
bool g_weightChecked = false;                 // install, then read-only
const std::byte* g_weightRefusedOn = nullptr; // the hands a refused weight was last logged for
bool g_weightUsedLogged = false;

using SetNumFn = void(__fastcall*)(void* node, std::int32_t num);
SetNumFn g_setNum = nullptr;
std::int32_t g_room = 0;  // the modifiers made room for: six per arm (install, then read-only)
bool g_wrist = false;     // the off hand has the wrist's change too (install, then read-only)
bool g_gloveMods = false; // the weapon arm has the glove's wrist and fingers too (install, then read-only)
bool g_leftGlove =
    false; // the left arm has them too, with the weapon in the left hand (install, then read-only)
std::atomic<std::uint32_t> g_roomLines{0};

// The weapon arm's attach joint: the joint InitJointMods stores at idHands+0x28E6 is the one named
// "righthandattach" (offhand_mods.hpp). False (logged) leaves the weapon arm the game's.
bool rightAttachChecks(const GameImage& image, const std::byte* init) {
    const std::byte* lookup = init + kRightNameLookup;
    bool ok = matchesAt(image, lookup, kRightNameLookupBytes);
    for (const Expect& e : kRightAttachExpected) {
        ok = ok && matchesAt(image, init + e.at, e.bytes);
    }
    const std::byte* nameSlot = ok ? ripTarget(image, lookup + 18, lookup + 22) : nullptr;
    const std::byte* name = nullptr;
    if (!nameSlot || !safeRead(nameSlot, name) || stringAt(image, name) != "righthandattach") {
        EVR_LOG("weapon arm: InitJointMods does not store the joint named righthandattach at idHands+0x28E6; "
                "the weapon arm stays the game's");
        return false;
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
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// Set when SetNum faulted: the game's lists may be half-resized, so it is not called again this session.
std::atomic<bool> g_setNumFaulted{false};

// A line per event, up to 64 (a map load or a respawn each).
bool roomLineDue() {
    return g_roomLines.fetch_add(1, std::memory_order_relaxed) < 64;
}

// InitJointMods, right after its third AddJointMod: room for the layer's modifiers in the new lists.
void onJointModsBuilt(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch() || g_setNumFaulted.load(std::memory_order_relaxed)) {
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
    const arm::RoomPlan plan = arm::planRoom(counts(lists[0]), counts(lists[1]), g_room);
    if (plan.step != arm::RoomPlan::Step::Grow) {
        if (roomLineDue()) {
            EVR_LOG("%s: arm: the hands' new joint modifier lists (%d of %d, %d of %d) %s", kTag,
                    lists[0].num, lists[0].size, lists[1].num, lists[1].size,
                    plan.step == arm::RoomPlan::Step::Enough ? "have room already"
                                                             : "do not check out; no room made");
        }
        return;
    }
    if (!callSetNum(node, plan.growTo) || !callSetNum(node, plan.num)) {
        g_setNumFaulted.store(true, std::memory_order_relaxed);
        EVR_LOG(
            "%s: arm: the game's SetNum faulted; no room is made for the rest of this session, the off hand "
            "stays the game's",
            kTag);
        return;
    }
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
    if (read && arm::roomMade(counts(lists[0]), counts(lists[1]), plan.num, g_room)) {
        EVR_LOG(
            "%s: arm: the game's SetNum made room for %d more joint modifiers in the hands' new lists (%d "
            "in use, room for %d and %d)",
            kTag, g_room, plan.num, lists[0].size, lists[1].size);
    } else {
        EVR_LOG("%s: arm: SetNum did not make room for %d more joint modifiers (%s; %d of %d, %d of %d)",
                kTag, g_room, !read ? "the lists cannot be read back" : "the lists did not grow",
                lists[0].num, lists[0].size, lists[1].num, lists[1].size);
    }
}

// Where the node's weight comes from (offhand_mods.hpp); false (logged) leaves the layer writing as before.
bool weightChecks(const GameImage& image, const std::byte* init) {
    bool ok = true;
    for (const Expect& e : kWeightInitExpected) {
        ok = ok && matchesAt(image, init + e.at, e.bytes);
    }
    const std::byte* create = ok ? callTarget(image, init + kInitCreateCall) : nullptr;
    ok = create && matchesAt(image, create + kGetWeightFromCreate, kGetWeightBytes);
    for (const Expect& e : kWeightCreateExpected) {
        ok = ok && matchesAt(image, create + e.at, e.bytes);
    }
    if (ok) {
        EVR_LOG("%s: arm: the hands' modifier weight is the owner's +0x%zX (InitJointMods +0x%zX, the node's "
                "create at RVA 0x%X)",
                kTag, kOwnerWeight, kWeightInitExpected[0].at, image.rva(create));
    } else {
        EVR_LOG("%s: arm: where the hands' modifier weight comes from is not as expected; the arms are "
                "written without allowing for it",
                kTag);
    }
    return ok;
}

} // namespace

ArmSet install(const GameImage& image, ArmSet wanted) {
    const char* stays = wanted.offHand && wanted.weapon ? "the off hand and the weapon arm stay the game's"
                        : wanted.weapon                 ? "the weapon arm stays the game's"
                                                        : "off hand stays the game's";
    const std::byte* init = findUnique(image, kTag, "InitJointMods", kInitJointModsSignature);
    const std::byte* setNum = findUnique(image, kTag, "the joint modifier node's SetNum", kSetNumSignature);
    if (!init || !setNum || !image.inText(init, kHookSite + 16) || !image.inText(setNum, 0x1B4)) {
        EVR_LOG("%s: arm: InitJointMods or SetNum not found; %s", kTag, stays);
        return {};
    }
    for (const Expect& e : kInitExpected) {
        if (!matchesAt(image, init + e.at, e.bytes)) {
            EVR_LOG("%s: arm: InitJointMods differs at +0x%zX; %s", kTag, e.at, stays);
            return {};
        }
    }
    for (const Expect& e : kSetNumExpected) {
        if (!matchesAt(image, setNum + e.at, e.bytes)) {
            EVR_LOG("%s: arm: SetNum differs at +0x%zX; %s", kTag, e.at, stays);
            return {};
        }
    }
    const std::byte* add = callTarget(image, init + kAddJointModCalls[0]);
    bool calls = add && callTarget(image, init + kInitSetNumCall) == setNum &&
                 image.inText(add, kAddSetNumCall + 5) &&
                 matchesAt(image, add + kAddSetNumBytesAt, kAddSetNumBytes) &&
                 callTarget(image, add + kAddSetNumCall) == setNum;
    for (const std::size_t at : kAddJointModCalls) {
        calls = calls && callTarget(image, init + at) == add;
    }
    if (!calls) {
        EVR_LOG("%s: arm: InitJointMods does not call AddJointMod and SetNum where expected; %s", kTag,
                stays);
        return {};
    }
    ArmSet ready = wanted;
    ready.weapon = wanted.weapon && rightAttachChecks(image, init);
    // The left arm only for the free off hand, or for the weapon in the left hand with a weapon arm to pair.
    ready.offHand = wanted.offHand && (ready.weapon || settings().offhand == input::OffhandMode::Free);
    const std::int32_t arms = (ready.offHand ? 1 : 0) + (ready.weapon ? 1 : 0);
    if (arms == 0) {
        return {};
    }
    // The off hand's wrist's change only for the bind solve, which writes past its targets. The weapon arm's
    // glove (its wrist and fingers) always, and the weight with it: the glove is written past its targets in
    // either solve.
    // With the weapon in the left hand both arms hold a glove on the gun (vkcore/left_hand_arms.hpp): the
    // left arm has the glove's modifiers too, and no wrist change.
    g_leftGlove = ready.offHand && ready.weapon && input::leftHandArms(settings());
    g_wrist = ready.offHand && settings().armSolve == input::ArmSolve::Bind && !g_leftGlove;
    g_gloveMods = ready.weapon;
    const std::int32_t glove = 1 + static_cast<std::int32_t>(arm::kFingerModCount);
    g_room =
        arm::modsForArms(arms) + (g_wrist ? 1 : 0) + (g_gloveMods ? glove : 0) + (g_leftGlove ? glove : 0);
    g_weightChecked =
        (settings().armSolve == input::ArmSolve::Bind || g_gloveMods) && weightChecks(image, init);
    g_setNum = reinterpret_cast<SetNumFn>(const_cast<std::byte*>(setNum));
    std::string error;
    std::byte* site = const_cast<std::byte*>(init + kHookSite);
    if (!installMidHook(site, &onJointModsBuilt, error)) {
        EVR_LOG("%s: arm: hook at RVA 0x%X failed: %s; %s", kTag, image.rva(site), error.c_str(), stays);
        return {};
    }
    EVR_LOG("%s: arm: InitJointMods at RVA 0x%X (AddJointMod 0x%X, SetNum 0x%X), hooked at RVA 0x%X to make "
            "room for the layer's joint modifiers (%d: %s%s)",
            kTag, image.rva(init), image.rva(add), image.rva(setNum), image.rva(site), g_room,
            ready.offHand && ready.weapon ? "the off hand and the weapon arm"
            : ready.weapon                ? "the weapon arm"
                                          : "the off hand",
            g_wrist       ? ", the off hand's wrist too"
            : g_leftGlove ? ", both gloves (the weapon in the left hand)"
                          : "");
    if (g_gloveMods) {
        EVR_LOG(
            "weapon arm: room for the glove's modifiers too (its wrist and %zu finger joints), to put it on "
            "a grip for a weapon whose idle holds it by a handle turned away from a controller's grip",
            arm::kFingerModCount);
    }
    return ready;
}

bool hasWrist(arm::ArmSide side) {
    return side == arm::ArmSide::Left ? g_wrist || g_leftGlove : g_gloveMods;
}

std::int32_t fingerCount(arm::ArmSide side) {
    const bool glove = side == arm::ArmSide::Right ? g_gloveMods : g_leftGlove;
    return glove ? static_cast<std::int32_t>(arm::kFingerModCount) : 0;
}

std::int32_t armCount(arm::ArmSide side) {
    return arm::layerModCount(hasWrist(side));
}

std::int32_t layerCount(arm::ArmSide side) {
    return armCount(side) + fingerCount(side);
}

arm::ArmJoint layerJoint(arm::ArmSide side, std::int32_t k) {
    return arm::layerModJoint(hasWrist(side), k);
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

std::optional<float> modWeight(const std::byte* hands) {
    const std::byte* owner = nullptr;
    float weight = -1.0f;
    float target = -1.0f;
    if (!g_weightChecked || !hands || !safeRead(hands + kHandsJointMods, owner) || !owner ||
        !safeRead(owner + kOwnerWeight, weight) || !safeRead(owner + kOwnerTargetWeight, target)) {
        return std::nullopt;
    }
    if (!arm::usableModWeight(weight) || !arm::usableModWeight(target)) {
        if (g_weightRefusedOn != hands) {
            g_weightRefusedOn = hands;
            EVR_LOG(
                "%s: arm: the hands' modifier weight reads %.3f (target %.3f), outside (0.5, 1]; the arms "
                "are written without allowing for it while it is",
                kTag, static_cast<double>(weight), static_cast<double>(target));
        }
        return std::nullopt;
    }
    if (!g_weightUsedLogged) {
        g_weightUsedLogged = true;
        EVR_LOG(
            "%s: arm: the hands' modifier weight %.3f (target %.3f): the arms' joints are written past their "
            "targets by it (the glove on the grip; with ETERNALVR_ARM_SOLVE=bind every joint)",
            kTag, static_cast<double>(weight), static_cast<double>(target));
    }
    return weight;
}

std::optional<std::int32_t> modGeneration(const std::byte* hands) {
    constexpr std::size_t kNodeGeneration = 0x28;
    const std::byte* node = modNode(hands);
    std::int32_t generation = 0;
    return node && safeRead(node + kNodeGeneration, generation) ? std::optional<std::int32_t>(generation)
                                                                : std::nullopt;
}

std::optional<arm::ModWeights> modWeights(const std::byte* hands) {
    const std::byte* owner = nullptr;
    arm::ModWeights w;
    if (!g_weightChecked || !hands || !safeRead(hands + kHandsJointMods, owner) || !owner ||
        !safeRead(owner + kOwnerWeight, w.current) || !safeRead(owner + kOwnerTargetWeight, w.target) ||
        !(w.current >= 0.0f && w.current <= 1.0f) || !(w.target >= 0.0f && w.target <= 1.0f)) {
        return std::nullopt;
    }
    return w;
}

bool clearLayerMods(const std::byte* node, std::int32_t base, std::span<const std::int16_t> ids) {
    ModList lists[2];
    if (!layerModsInPlace(node, base, ids) || !readList(node, 0, lists[0]) || !readList(node, 1, lists[1])) {
        return false;
    }
    bool ok = true;
    for (const arm::EntryWrite& w : arm::noChangeWrites(base, ids)) {
        ok = safeCopy(lists[w.list].data + w.offset, w.bytes.data(), w.bytes.size()) && ok;
    }
    return ok;
}

std::array<std::int32_t, 2>
overridesInLists(const std::byte* node, std::int32_t base, std::span<const std::int16_t> ids) {
    std::array<std::int32_t, 2> out{-1, -1};
    for (std::size_t which = 0; which < 2; ++which) {
        ModList list;
        if (!node || base < 0 || !readList(node, which, list) ||
            list.num < base + static_cast<std::int32_t>(ids.size())) {
            continue;
        }
        out[which] = 0;
        for (std::size_t k = 0; k < ids.size(); ++k) {
            std::uint16_t flags = 0;
            safeRead(list.data + (static_cast<std::size_t>(base) + k) * kModSize + kModJoint + 4, flags);
            out[which] += (flags & 0x20) != 0 ? 1 : 0;
        }
    }
    return out;
}

bool layerModsInPlace(const std::byte* node, std::int32_t base, std::span<const std::int16_t> ids) {
    if (!node || base < 0) {
        return false;
    }
    const auto count = static_cast<std::int32_t>(ids.size());
    for (std::size_t which = 0; which < 2; ++which) {
        ModList list;
        if (!readList(node, which, list) || list.num < base + count || list.num > list.size ||
            list.size > arm::kMaxJointMods) {
            return false;
        }
        for (std::int32_t k = 0; k < count; ++k) {
            if (jointAt(list, base + k) != ids[static_cast<std::size_t>(k)]) {
                return false;
            }
        }
    }
    return true;
}

std::int32_t
addLayerMods(const std::byte* node, arm::ArmSide side, std::span<const std::int16_t> ids, AddError& error) {
    ModList lists[2];
    if (!readList(node, 0, lists[0]) || !readList(node, 1, lists[1]) ||
        !arm::modListsAgree(counts(lists[0]), counts(lists[1]))) {
        error = {Problem::List, "the hands' joint modifier lists do not check out"};
        return -1;
    }
    const std::int32_t base = lists[0].num;
    const auto count = static_cast<std::int32_t>(ids.size());
    if (!arm::roomFor(counts(lists[0]), counts(lists[1]), count)) {
        char message[160];
        std::snprintf(
            message, sizeof(message),
            "no room for %d more joint modifiers (%d in use, room for %d and %d; none was made when "
            "the game built the lists)",
            count, base, lists[0].size, lists[1].size);
        error = {Problem::NoRoom, message};
        return -1;
    }
    for (const ModList& list : lists) {
        for (std::int32_t m = 0; m < base; ++m) {
            for (const std::int16_t joint : ids) {
                if (jointAt(list, m) == joint) {
                    error = {Problem::Taken, "the game already modifies joint " + std::to_string(joint) +
                                                 ", one of the layer's"};
                    return -1;
                }
            }
        }
    }
    for (const arm::EntryWrite& w : arm::noChangeWrites(base, ids)) {
        if (!safeCopy(lists[w.list].data + w.offset, w.bytes.data(), w.bytes.size())) {
            error = {Problem::List, "a joint modifier could not be written"};
            return -1;
        }
    }
    // The counts last, as SetNum does, so a reader never sees an entry before it is filled in.
    const std::int32_t num = base + count;
    auto* first = const_cast<std::byte*>(node + kNodeLists + kListNum);
    auto* second = const_cast<std::byte*>(node + kNodeLists + kListStride + kListNum);
    if (!safeCopy(first, &num, sizeof(num)) || !safeCopy(second, &num, sizeof(num))) {
        error = {Problem::List, "the joint modifier count could not be written"};
        return -1;
    }
    EVR_LOG("%s: %d joint modifiers added at %d..%d (the hands' list now holds %d, room for %d)",
            side == arm::ArmSide::Right ? "weapon arm" : "offhand: arm", count, base, base + count - 1, num,
            lists[0].size);
    return base;
}

} // namespace evr::vkcore::controllers::offhand_mods
