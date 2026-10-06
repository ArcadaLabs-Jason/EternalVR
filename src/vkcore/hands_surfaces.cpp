// The arms the layer poses, made visible on the hands model (hands_surfaces.hpp).

#include "vkcore/hands_surfaces.hpp"

#include "vkcore/controllers_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace evr::vkcore::controllers::hands_surfaces {

namespace {

constexpr const char* kTag = "arm surfaces";

// idHands' show/hide apply: the cvar and hands-flag gates, then `mov rcx, [rcx+0x370]` (renderModel) and
// a jump to the model's apply.
constexpr const char* kHandsApplySignature =
    "48 8B 05 ?? ?? ?? ?? 83 78 08 00 75 ?? 80 B9 A5 8D 00 00 00 7C ?? 48 8B 89 70 03 00 00 48 85 C9 "
    "0F 85";
constexpr std::size_t kHandsApplyJump = 0x20; // jne rel32 to the model's apply
// Show / Hide: the whole function (the bit at +0x518, then `or byte [rcx+0x57C], 0x10`).
constexpr const char* kShowSignature =
    "48 63 C2 83 E2 1F 48 C1 E8 05 4C 8D 04 81 41 8B 80 18 05 00 00 0F AB D0 41 89 80 18 05 00 00 80 "
    "89 7C 05 00 00 10 C3";
constexpr const char* kHideSignature =
    "48 63 C2 83 E2 1F 48 C1 E8 05 4C 8D 04 81 41 8B 80 18 05 00 00 0F B3 D0 41 89 80 18 05 00 00 80 "
    "89 7C 05 00 00 10 C3";
// FindSurfaces: [rcx+0x4D8], its count at +0x80 and pointer array at +0x78, then the name compare.
constexpr const char* kFindSurfacesSignature =
    "48 89 5C 24 18 48 89 6C 24 20 41 56 48 83 EC 20 48 8B 81 D8 04 00 00 49 8B D8 4C 8B F2 48 8B E9 "
    "48 85 C0 74 ?? 48 89 7C 24 38 33 FF 39 B8 80 00 00 00 7E ?? 48 89 74 24 30 8B F7 0F 1F 44 00 00 "
    "48 8B 40 78 49 8B D6 48 8B 0C 06 E8";
constexpr std::size_t kFindSurfacesNameCall = 0x4B;
constexpr std::size_t kSurfaceNameRead = 0x2E; // in the name compare: mov rdi, [rcx+8]
constexpr const char* kSurfaceNameReadBytes = "48 8B 79 08";
// SetMeshKit: [rcx+0x4D0], the group's count at +0x3C0 and kits at +0x3B8 (0x18 per group), a kit's
// surface count at +0x38 and indices at +0x30.
constexpr const char* kSetMeshKitSignature =
    "4C 89 44 24 18 53 55 56 57 41 54 41 57 48 83 EC 28 4C 8B 89 D0 04 00 00 33 FF 4C 63 E2 48 8B D9 "
    "BE FF FF FF FF 4C 89 64 24 60 8B EF 4B 8D 04 64 4C 8D 3C C5 C0 03 00 00 43 39 3C 0F 0F 8E ?? ?? "
    "?? ?? 4C 89 6C 24 68 4B 8D 04 64 4C 8B 64 24 70 4C 8D 2C C5 00 00 00 00 4C 89 74 24 20 44 8B F7 "
    "4B 8B 8C 29 B8 03 00 00 44 8B D7 49 03 CE 39 79 38 7E ?? 4C 8B CF 66 66 0F 1F 84 00 00 00 00 00 "
    "48 8B 41 30";
constexpr std::size_t kSetMeshKitNameCall = 0xBF;
constexpr std::size_t kSetMeshKitStride = 0xD2;
constexpr const char* kSetMeshKitStrideBytes = "49 83 C6 48"; // add r14, 0x48
constexpr const char* kKitNameReadBytes = "4C 8B 51 08";      // the kit name compare: mov r10, [rcx+8]

struct Expect {
    std::size_t at;
    const char* bytes;
};
// The model's apply (0x19C8590): SetMeshKit for group 5, then the per-surface calls.
constexpr Expect kModelApplyExpected[] = {
    {0x00, "40 55 56 41 55 41 57 48 8B EC"},
    {0x29, "4C 8B 42 30"},       // mov r8, [rdx+0x30]   the decl's bodyKit
    {0x3C, "BA 05 00 00 00 E8"}, // mov edx, 5; call SetMeshKit
};
constexpr std::size_t kApplySetMeshKitCall = 0x41;
constexpr std::size_t kApplyFindSurfacesCall = 0x8F;
constexpr std::size_t kApplyHideCall = 0xC4;
constexpr std::size_t kApplyShowCall = 0x174;
constexpr std::int32_t kBodyKitGroup = 5;

// The model (build 25216728).
constexpr std::size_t kHandsRenderModel = 0x370;
constexpr std::size_t kModelVisible = 0x518;
constexpr std::size_t kModelSurfaces = 0x4D8;
constexpr std::size_t kSurfacesArray = 0x78;
constexpr std::size_t kSurfacesCount = 0x80;
constexpr std::size_t kSurfaceName = 0x8;
constexpr std::size_t kModelKits = 0x4D0;
constexpr std::size_t kKitGroupList = 0x3B8;
constexpr std::size_t kKitGroupCount = 0x3C0;
constexpr std::size_t kKitGroupStride = 0x18;
constexpr std::size_t kKitSize = 0x48;
constexpr std::size_t kKitName = 0x8;
constexpr std::size_t kKitSurfaces = 0x30;
constexpr std::size_t kKitSurfaceCount = 0x38;
constexpr std::int32_t kMaxKits = 64;
constexpr std::size_t kMaxArmSurfaces = 4;
constexpr std::size_t kMaxName = 96;

using SurfaceFn = void(__fastcall*)(void* model, std::int32_t surface);

// Install time, then read-only.
SurfaceFn g_show = nullptr;
SurfaceFn g_hide = nullptr;
bool g_byName = false;
bool g_byKit = false;
// Both arms hidden (ETERNALVR_ARMS=hidden): every surface of theirs hidden each tick, whatever the posing.
bool g_armsHidden = false;
// Hook thread only: a call faulted, nothing is called again.
bool g_faulted = false;

struct Surface {
    std::int32_t index = -1;
    bool ours = false;
};
struct Side {
    const std::byte* hands = nullptr;
    const std::byte* model = nullptr; // the model the surfaces were looked up on
    std::array<Surface, kMaxArmSurfaces> surfaces{};
    std::size_t count = 0;
    arm::SurfaceState state = arm::SurfaceState::Off;
    bool loggedFound = false;
    bool loggedMissing = false;
    bool loggedShown = false;
    bool loggedRestored = false;
    bool loggedHidden = false;
    bool tripDone = false;
};
Side g_sides[2];

Side& sideOf(arm::ArmSide side) {
    return g_sides[side == arm::ArmSide::Right ? 1 : 0];
}

const char* prefix(arm::ArmSide side) {
    return side == arm::ArmSide::Right ? "weapon arm" : "offhand: arm";
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

const std::byte* callTarget(const GameImage& image, const std::byte* call) {
    return image.inText(call, 5) && static_cast<std::uint8_t>(call[0]) == 0xE8 ? call + 5 + readI32(call + 1)
                                                                               : nullptr;
}

bool checked(const GameImage& image, const std::byte* at, const char* bytes) {
    return at && image.inText(at, 16) && bytesMatch(at, bytes);
}

int accessViolationOnly(unsigned long code) {
    return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

// The game's Show / Hide, apart from anything with a destructor (__try needs that). False when it faulted.
bool callSurfaceFn(SurfaceFn fn, const std::byte* model, std::int32_t surface) {
    __try {
        fn(const_cast<std::byte*>(model), surface);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

// The NUL-terminated name at `at` (game memory), up to kMaxName characters; empty when unreadable.
std::string readName(const std::byte* at) {
    std::string out;
    for (std::size_t i = 0; at && i < kMaxName; ++i) {
        char c = 0;
        if (!safeRead(at + i, c) || c == 0) {
            break;
        }
        out.push_back(c);
    }
    return out;
}

bool visibleBit(const std::byte* model, std::int32_t surface, bool& visible) {
    const auto bit = arm::surfaceBit(surface);
    std::uint32_t word = 0;
    if (!bit || !safeRead(model + kModelVisible + bit->word * 4, word)) {
        return false;
    }
    visible = (word & bit->mask) != 0;
    return true;
}

// The arm's surfaces among the model's, by name; `seen` lists the names for a log line.
std::size_t
findByName(arm::ArmSide side, const std::byte* model, Side& s, std::int32_t& total, std::string& seen) {
    const std::byte* list = nullptr;
    const std::byte* array = nullptr;
    total = -1;
    if (!g_byName || !safeRead(model + kModelSurfaces, list) || !list ||
        !safeRead(list + kSurfacesCount, total) || total <= 0 || total > arm::kMaxSurfaces ||
        !safeRead(list + kSurfacesArray, array) || !array) {
        return 0;
    }
    const auto wanted = arm::armSurfaceNames(side);
    std::size_t found = 0;
    for (std::int32_t i = 0; i < total; ++i) {
        const std::byte* surface = nullptr;
        const std::byte* namePtr = nullptr;
        if (!safeRead(array + static_cast<std::size_t>(i) * sizeof(void*), surface) || !surface ||
            !safeRead(surface + kSurfaceName, namePtr)) {
            continue;
        }
        const std::string name = readName(namePtr);
        if (seen.size() < 400) {
            seen += (seen.empty() ? "" : ", ") + std::to_string(i) + " " + name;
        }
        for (const std::string_view w : wanted) {
            if (found < kMaxArmSurfaces && arm::surfaceNameIs(name, w)) {
                s.surfaces[found++] = {i, false};
                break;
            }
        }
    }
    return found;
}

// The arm's surfaces as the model's kit holding only that arm lists them; `seen` lists the kit names.
std::size_t findByKit(arm::ArmSide side, const std::byte* model, Side& s, std::string& seen) {
    const std::byte* kits = nullptr;
    const std::byte* list = nullptr;
    std::int32_t count = 0;
    const std::size_t group = static_cast<std::size_t>(kBodyKitGroup) * kKitGroupStride;
    if (!g_byKit || !safeRead(model + kModelKits, kits) || !kits ||
        !safeRead(kits + kKitGroupCount + group, count) || count <= 0 || count > kMaxKits ||
        !safeRead(kits + kKitGroupList + group, list) || !list) {
        return 0;
    }
    for (std::int32_t k = 0; k < count; ++k) {
        const std::byte* kit = list + static_cast<std::size_t>(k) * kKitSize;
        const std::byte* namePtr = nullptr;
        const std::byte* indices = nullptr;
        std::int32_t n = 0;
        if (!safeRead(kit + kKitName, namePtr)) {
            continue;
        }
        const std::string name = readName(namePtr);
        if (seen.size() < 200) {
            seen += (seen.empty() ? "" : ", ") + name;
        }
        if (!arm::equalsIgnoringCase(name, arm::armKitName(side)) || !safeRead(kit + kKitSurfaces, indices) ||
            !indices || !safeRead(kit + kKitSurfaceCount, n) || n <= 0 ||
            static_cast<std::size_t>(n) > kMaxArmSurfaces) {
            continue;
        }
        std::size_t found = 0;
        for (std::int32_t i = 0; i < n; ++i) {
            std::int32_t index = -1;
            if (safeRead(indices + static_cast<std::size_t>(i) * 4, index) && arm::surfaceBit(index)) {
                s.surfaces[found++] = {index, false};
            }
        }
        return found;
    }
    return 0;
}

std::string indexList(const Side& s) {
    std::string out;
    for (std::size_t i = 0; i < s.count; ++i) {
        out += (i ? " " : "") + std::to_string(s.surfaces[i].index);
    }
    return out;
}

// The arm's surfaces on a model it has not looked at yet (logged once per arm).
void resolve(arm::ArmSide side, Side& s) {
    s.count = 0;
    std::int32_t total = -1;
    std::string names;
    std::string kits;
    Side byKit;
    s.count = findByName(side, s.model, s, total, names);
    byKit.count = findByKit(side, s.model, byKit, kits);
    const bool usedName = s.count > 0;
    if (!usedName) {
        s.surfaces = byKit.surfaces;
        s.count = byKit.count;
    }
    if (s.count == 0) {
        if (!s.loggedMissing) {
            s.loggedMissing = true;
            EVR_LOG("%s: the %s's surfaces are not on the hands model (%d surfaces: %s; kits of group %d: "
                    "%s); the weapon's mesh kit decides whether it shows",
                    prefix(side), arm::armLabel(side), total, names.empty() ? "none read" : names.c_str(),
                    kBodyKitGroup, kits.empty() ? "none read" : kits.c_str());
        }
        return;
    }
    if (!s.loggedFound) {
        s.loggedFound = true;
        std::string kitText = "no kit " + std::string(arm::armKitName(side)) + " read";
        if (byKit.count > 0) {
            kitText = "the kit " + std::string(arm::armKitName(side)) + " lists " + indexList(byKit);
        }
        EVR_LOG("%s: %s surface %s of %d on the hands model, found %s (%s); %s", prefix(side),
                arm::armLabel(side), indexList(s).c_str(), total, usedName ? "by name" : "by kit",
                kitText.c_str(),
                g_armsHidden ? "hidden with the game's Hide (arms hidden)"
                             : "shown with the game's Show while the layer poses the arm");
    }
}

void turnOff(arm::ArmSide side, const char* call) {
    g_faulted = true;
    for (Side& s : g_sides) {
        s.state = arm::SurfaceState::Off;
        s.count = 0;
    }
    EVR_LOG("%s: the game's %s faulted; the arms' surfaces are the game's kits' for this session",
            prefix(side), call);
}

void apply(arm::ArmSide side, const std::byte* hands, bool posed, bool afterTrip) {
    Side& s = sideOf(side);
    const std::byte* model = nullptr;
    if (!g_show || g_faulted || !hands || !safeRead(hands + kHandsRenderModel, model) || !model) {
        s.state = arm::SurfaceState::Off;
        return;
    }
    if (hands != s.hands || model != s.model) {
        // A new model (a map load, a respawn): what was shown on the old one is not touched again, and the
        // new one is looked at when the arm is first posed on it (with the arms hidden, at once).
        if (afterTrip || !(posed || g_armsHidden)) {
            if (!afterTrip) {
                s.hands = nullptr;
                s.model = nullptr;
                s.count = 0;
                s.state = arm::SurfaceState::Off;
            }
            return;
        }
        s.hands = hands;
        s.model = model;
        resolve(side, s);
    }
    if (s.count == 0) {
        s.state = arm::SurfaceState::Off;
        return;
    }
    bool anyOurs = false;
    bool allVisible = true;
    bool shown = false;
    bool restored = false;
    for (std::size_t i = 0; i < s.count; ++i) {
        Surface& surface = s.surfaces[i];
        bool visible = false;
        if (!visibleBit(model, surface.index, visible)) {
            s.state = arm::SurfaceState::Off;
            return;
        }
        const arm::SurfacePlan plan = g_armsHidden ? arm::planHiddenSurface(visible, surface.ours)
                                                   : arm::planSurface(posed, visible, surface.ours);
        if (plan.step == arm::SurfaceStep::Show) {
            if (!callSurfaceFn(g_show, model, surface.index)) {
                turnOff(side, "Show");
                return;
            }
            visible = shown = true;
        } else if (plan.step == arm::SurfaceStep::Hide) {
            if (!callSurfaceFn(g_hide, model, surface.index)) {
                turnOff(side, "Hide");
                return;
            }
            visible = false;
            restored = true;
        }
        surface.ours = plan.ours;
        anyOurs = anyOurs || surface.ours;
        allVisible = allVisible && visible;
    }
    if (g_armsHidden) {
        s.state = arm::SurfaceState::Removed;
        if (restored && !s.loggedHidden) {
            s.loggedHidden = true;
            EVR_LOG("%s: %s surface %s hidden (arms hidden)", prefix(side), arm::armLabel(side),
                    indexList(s).c_str());
        }
        return;
    }
    s.state = anyOurs      ? arm::SurfaceState::Shown
              : allVisible ? arm::SurfaceState::Games
                           : arm::SurfaceState::Hidden;
    if (shown && !s.loggedShown) {
        s.loggedShown = true;
        EVR_LOG("%s: %s surface %s shown (the weapon's mesh kit had hidden it)", prefix(side),
                arm::armLabel(side), indexList(s).c_str());
    }
    if (restored && !s.loggedRestored) {
        s.loggedRestored = true;
        EVR_LOG("%s: %s surface %s hidden again as the weapon's mesh kit has it (the arm is the game's)",
                prefix(side), arm::armLabel(side), indexList(s).c_str());
    }
}

} // namespace

bool install(const GameImage& image, bool armsHidden) {
    const std::byte* handsApply =
        findUnique(image, kTag, "idHands show/hide mesh apply", kHandsApplySignature);
    const std::byte* show = findUnique(image, kTag, "surface Show", kShowSignature);
    const std::byte* hide = findUnique(image, kTag, "surface Hide", kHideSignature);
    const std::byte* findSurfaces = findUnique(image, kTag, "FindSurfaces", kFindSurfacesSignature);
    const std::byte* setMeshKit = findUnique(image, kTag, "SetMeshKit", kSetMeshKitSignature);
    const std::byte* jump = handsApply ? handsApply + kHandsApplyJump : nullptr;
    const std::byte* modelApply = jump && image.inText(jump, 6) ? jump + 6 + readI32(jump + 2) : nullptr;
    bool ok = handsApply && show && hide && modelApply && image.inText(modelApply, kApplyShowCall + 5);
    for (const Expect& e : kModelApplyExpected) {
        ok = ok && checked(image, modelApply + e.at, e.bytes);
    }
    if (!ok || callTarget(image, modelApply + kApplyShowCall) != show ||
        callTarget(image, modelApply + kApplyHideCall) != hide) {
        EVR_LOG(
            "%s: the hands' mesh apply does not call Show and Hide where expected; the arms' surfaces stay "
            "the game's kits'",
            kTag);
        return false;
    }
    const std::byte* nameCompare =
        findSurfaces ? callTarget(image, findSurfaces + kFindSurfacesNameCall) : nullptr;
    g_byName = findSurfaces && callTarget(image, modelApply + kApplyFindSurfacesCall) == findSurfaces &&
               checked(image, nameCompare ? nameCompare + kSurfaceNameRead : nullptr, kSurfaceNameReadBytes);
    const std::byte* kitCompare = setMeshKit ? callTarget(image, setMeshKit + kSetMeshKitNameCall) : nullptr;
    g_byKit = setMeshKit && callTarget(image, modelApply + kApplySetMeshKitCall) == setMeshKit &&
              checked(image, setMeshKit + kSetMeshKitStride, kSetMeshKitStrideBytes) &&
              checked(image, kitCompare, kKitNameReadBytes);
    if (!g_byName && !g_byKit) {
        EVR_LOG(
            "%s: neither FindSurfaces nor SetMeshKit checks out; the arms' surfaces stay the game's kits'",
            kTag);
        return false;
    }
    g_show = reinterpret_cast<SurfaceFn>(const_cast<std::byte*>(show));
    g_hide = reinterpret_cast<SurfaceFn>(const_cast<std::byte*>(hide));
    g_armsHidden = armsHidden;
    EVR_LOG("%s: idHands+0x%zX's model, kit group %d (the model's apply at RVA 0x%X); Show 0x%X, Hide 0x%X "
            "(bits at +0x%zX); the arms found %s; %s",
            kTag, kHandsRenderModel, kBodyKitGroup, image.rva(modelApply), image.rva(show), image.rva(hide),
            kModelVisible,
            g_byName && g_byKit ? "by name, else by kit"
            : g_byName          ? "by name (the kits do not check out)"
                                : "by kit (FindSurfaces does not check out)",
            armsHidden ? "both arms hidden (ETERNALVR_ARMS=hidden)" : "a posed arm is shown");
    return true;
}

void update(arm::ArmSide side, const std::byte* hands, bool posed) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    apply(side, hands, posed, false);
}

bool releaseAfterTrip(arm::ArmSide side, const std::byte* hands) {
    Side& s = sideOf(side);
    if (s.tripDone) {
        return true;
    }
    if (g_armsHidden) {
        // No give-back (arm_surfaces.hpp, planHiddenSurface): hiding stops, nothing is written.
        s.tripDone = true;
        EVR_LOG("%s: the multiplayer guard tripped: %s no longer hidden by the layer; the weapon's next mesh "
                "kit or new hands show it as the game wants",
                prefix(side), arm::armLabel(side));
        return true;
    }
    bool anyOurs = false;
    for (std::size_t i = 0; i < s.count; ++i) {
        anyOurs = anyOurs || s.surfaces[i].ours;
    }
    if (anyOurs && hands != s.hands) {
        return false;
    }
    s.tripDone = true;
    if (anyOurs) {
        apply(side, hands, false, true);
        EVR_LOG(
            "%s: the multiplayer guard tripped: %s surface %s hidden again as the weapon's mesh kit has it",
            prefix(side), arm::armLabel(side), indexList(s).c_str());
    }
    return true;
}

arm::SurfaceState state(arm::ArmSide side) {
    return sideOf(side).state;
}

} // namespace evr::vkcore::controllers::hands_surfaces
