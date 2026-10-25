// The arms model the hands are given (arm_model.hpp).

#include "vkcore/arm_model.hpp"

#include "vkcore/controllers_impl.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/seh_filter.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace evr::vkcore::controllers::arm_model {

namespace {

constexpr const char* kTag = "arm model";

// The call of SetModel in idPlayer::InitHands: mov rdx, [rbx+0x4E70]; lea rcx, [rbx+0xD2C8]; call; and the
// call in the cosmetic skin's apply: mov rdx, [rdi]; test rdx, rdx; je; mov rcx, rbx; call.
constexpr const char* kInitHandsCallSignature =
    "48 8B 93 70 4E 00 00 48 8D 8B C8 D2 00 00 E8 ?? ?? ?? ?? 48 8D 54 24 30 48 C7 44 24 30 00 00 00 00";
constexpr std::size_t kInitHandsCall = 0xE;
constexpr const char* kSkinCallSignature =
    "48 8B 17 48 85 D2 74 08 48 8B CB E8 ?? ?? ?? ?? 48 8B B3 70 03 00 00";
constexpr std::size_t kSkinCall = 0xB;
// idHands::SetModel: returns early when the render model's decl (+0x4D0) is the one asked for.
constexpr const char* kSetModelSignature = "40 56 41 56 48 81 EC D8 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 "
                                           "48 89 84 24 C0 00 00 00 48 8B 81 70 03 00 "
                                           "00 4C 8B F2 48 8B F1 48 39 90 D0 04 00 00";
// The resource list of a decl class by its name (walks the lists, comparing each one's class name); the
// signature as reference/idtech7/typeinfo/advancedoptions-Sigs-rev3.h has it (idResourceListForClassName).
constexpr const char* kListForClassSignature =
    "48 89 5C 24 08 57 48 83 EC 20 48 8B 1D ?? ?? ?? ?? 48 8B F9 48 85 DB 74 24";
// In idDeclInfo::FindWithInheritance: mov rcx, [r15+8]; xor r8d, r8d; mov rdx, rdi; ...; call: the list's
// find by name, before anything is loaded.
constexpr const char* kFindCallSignature = "49 8B 4F 08 45 33 C0 48 8B D7 4C 89 AC 24 A8 02 00 00 E8";
constexpr std::size_t kFindCall = 0x12;
constexpr const char* kFindPrologue = "4C 8B DC 55 53 41 56 49 8D AB 18 FC FF FF";

// The md6 decl (build 25216728): the vtable, the name (a C string) at +8, the decl it inherits from.
constexpr std::size_t kDeclName = 0x8;
constexpr std::size_t kDeclParent = 0xB8;
// The resource list node: its list at +8.
constexpr std::size_t kListNodeList = 0x8;

using ListForClassFn = const std::byte*(__fastcall*)(const char* className);
using FindFn = const std::byte*(__fastcall*)(const std::byte* list, const char* name, bool flag);

ListForClassFn g_listForClass = nullptr;
FindFn g_find = nullptr;
input::ArmModel g_model = input::ArmModel::Game;
int g_skin = 3;

// Game thread (SetModel's callers) only: the candidate found for each game decl.
struct Choice {
    const std::byte* game = nullptr;
    const std::byte* candidate = nullptr;
};
std::array<Choice, 8> g_choices{};
std::size_t g_choiceCount = 0;
std::atomic<std::uint32_t> g_swaps{0};

// Set once both calls are hooked: until then (and for good when the second hook failed) the first does
// nothing, so the switch is all or nothing.
std::atomic<bool> g_live{false};

const std::byte* callTarget(const GameImage& image, const std::byte* call) {
    return image.inText(call, 5) && static_cast<std::uint8_t>(call[0]) == 0xE8 ? call + 5 + readI32(call + 1)
                                                                               : nullptr;
}

// A C string the game holds, read safely; empty when it cannot be read.
std::string cString(const std::byte* at, std::size_t max = 160) {
    std::string out;
    for (std::size_t i = 0; at && i < max; ++i) {
        char c = 0;
        if (!safeRead(at + i, c) || c == 0) {
            break;
        }
        out.push_back(c);
    }
    return out;
}

std::string declName(const std::byte* decl) {
    const std::byte* name = nullptr;
    return decl && safeRead(decl + kDeclName, name) ? cString(name) : std::string{};
}

const void* vtableOf(const std::byte* object) {
    const void* vtable = nullptr;
    return object && safeRead(object, vtable) ? vtable : nullptr;
}

// The game's lookups, apart from anything with a destructor (__try needs that). False when they faulted.
bool callListForClass(const char* className, const std::byte*& out) {
    __try {
        out = g_listForClass(className);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

bool callFind(const std::byte* list, const char* name, const std::byte*& out) {
    __try {
        out = g_find(list, name, false);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

const std::byte* skinFor(std::string& why) {
    // The decls' own names carry their type's folder (the hands' is md6def/player/human/base/praetor.md6 on
    // the rig); the name without it is tried as well.
    char text[112];
    std::snprintf(text, sizeof(text), "md6def/customization/player/human/set%d/base/praetor_set%d.md6",
                  g_skin, g_skin);
    const std::byte* node = nullptr;
    const std::byte* list = nullptr;
    if (!callListForClass("idDeclMD6", node) || !node || !safeRead(node + kListNodeList, list) || !list) {
        why = "the md6 decl list (idDeclMD6) was not found";
        return nullptr;
    }
    for (const char* name : {text, text + 7}) {
        const std::byte* found = nullptr;
        if (!callFind(list, name, found)) {
            why = "the game's decl lookup faulted";
            return nullptr;
        }
        if (found) {
            return found;
        }
    }
    why = std::string("no md6 decl named ") + text + " (or without md6def/) is loaded";
    return nullptr;
}

const std::byte* candidateFor(const std::byte* game) {
    for (std::size_t i = 0; i < g_choiceCount; ++i) {
        if (g_choices[i].game == game) {
            return g_choices[i].candidate;
        }
    }
    std::string why;
    const std::byte* candidate = skinFor(why);
    if (candidate && vtableOf(candidate) != vtableOf(game)) {
        why = "the candidate " + declName(candidate) + " is not a decl of the game's class";
        candidate = nullptr;
    }
    const std::string gameName = declName(game);
    if (candidate) {
        const std::byte* parent = nullptr;
        safeRead(game + kDeclParent, parent);
        EVR_LOG("%s: the hands' model %s (decl %p, parent %s) becomes %s (decl %p)", kTag, gameName.c_str(),
                static_cast<const void*>(game), declName(parent).c_str(), declName(candidate).c_str(),
                static_cast<const void*>(candidate));
    } else {
        EVR_LOG("%s: the hands' model %s (decl %p) stays the game's: %s", kTag, gameName.c_str(),
                static_cast<const void*>(game), why.c_str());
    }
    if (g_choiceCount < g_choices.size()) {
        g_choices[g_choiceCount++] = {game, candidate};
    }
    return candidate;
}

// Before either call of SetModel: the hands in rcx, the game's decl in rdx.
void onSetModel(HookRegisters& regs) {
    if (!g_live.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch() || regs.rdx == 0) {
        return;
    }
    const auto* game = reinterpret_cast<const std::byte*>(regs.rdx);
    const std::byte* candidate = candidateFor(game);
    if (!candidate || candidate == game) {
        return;
    }
    regs.rdx = reinterpret_cast<std::uintptr_t>(candidate);
    const std::uint32_t n = g_swaps.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 16) {
        EVR_LOG("%s: SetModel %u for hands %p: %s instead of %s", kTag, n, reinterpret_cast<void*>(regs.rcx),
                declName(candidate).c_str(), declName(game).c_str());
    }
}

} // namespace

bool install() {
    const input::ControllerSettings& cfg = settings();
    g_model = cfg.armModel;
    g_skin = cfg.armModelSkin;
    if (g_model == input::ArmModel::Game) {
        return false;
    }
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* initCall =
        findUnique(image, kTag, "InitHands' call of SetModel", kInitHandsCallSignature);
    const std::byte* skinCall =
        findUnique(image, kTag, "the skin apply's call of SetModel", kSkinCallSignature);
    const std::byte* setModel = findUnique(image, kTag, "idHands::SetModel", kSetModelSignature);
    if (!initCall || !skinCall || !setModel || callTarget(image, initCall + kInitHandsCall) != setModel ||
        callTarget(image, skinCall + kSkinCall) != setModel) {
        EVR_LOG("%s: the calls of idHands::SetModel are not where expected; the arms model stays the game's",
                kTag);
        return false;
    }
    const std::byte* listForClass =
        findUnique(image, kTag, "the resource list by class", kListForClassSignature);
    const std::byte* findCall = findUnique(image, kTag, "FindWithInheritance's find", kFindCallSignature);
    const std::byte* find = findCall ? callTarget(image, findCall + kFindCall) : nullptr;
    if (!listForClass || !find || !matchesAt(image, find, kFindPrologue)) {
        EVR_LOG("%s: the decl lookup by name is not where expected; the arms model stays the game's", kTag);
        return false;
    }
    g_listForClass = reinterpret_cast<ListForClassFn>(const_cast<std::byte*>(listForClass));
    g_find = reinterpret_cast<FindFn>(const_cast<std::byte*>(find));
    std::string error;
    for (const std::byte* call : {initCall + kInitHandsCall, skinCall + kSkinCall}) {
        if (!installMidHookEdit(const_cast<std::byte*>(call), &onSetModel, error)) {
            EVR_LOG(
                "%s: hook at RVA 0x%X failed: %s; the arms model stays the game's (a hook already in place "
                "does nothing)",
                kTag, image.rva(call), error.c_str());
            return false;
        }
    }
    g_live.store(true, std::memory_order_release);
    EVR_LOG(
        "%s: ETERNALVR_ARM_MODEL=%s:%d (rig tests): idHands::SetModel at RVA 0x%X, its calls in InitHands "
        "(RVA 0x%X) and the skin apply (RVA 0x%X) hooked",
        kTag, input::armModelName(g_model), g_skin, image.rva(setModel), image.rva(initCall + kInitHandsCall),
        image.rva(skinCall + kSkinCall));
    return true;
}

} // namespace evr::vkcore::controllers::arm_model
