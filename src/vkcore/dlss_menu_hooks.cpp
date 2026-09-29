#include "vkcore/dlss_menu_hooks.hpp"

#include "stereo_seq/dlss_menu.hpp"
#include "vkcore/game_code.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/taa_locate.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "dlss-menu";

// The profile's DLSS index in the game's settings object (advDlssQualityIndex).
constexpr std::size_t kSettingsDlssIndex = 0x122A0;

// The DLSS setter (RVA 0x1420FD0): stores the index at settings + 0x122A0, then writes r_antialiasing
// (0: 1, else 2; `lea rcx, [r_antialiasing]` at +0x15) and r_dlssQuality. The detour moves the first 14
// bytes, none RIP-relative.
constexpr const char* kSetterSignature =
    "40 53 48 83 EC 20 8B C2 89 91 A0 22 01 00 33 D2 48 8B D9 85 C0 48 8D 0D "
    "?? ?? ?? ?? 41 B0 01 0F 95 C2 FF C2 E8 ?? ?? ?? ?? 8B 83 A0 22 01 00";
constexpr std::size_t kSetterAntialiasingLea = 0x15;

// The video page refresh (RVA 0x15E8F29, in 0x15E6A20): `mov rcx, r14` (the settings object), `call` the
// index getter (+0x7), then the list fill reads eax (`mov edx, eax`) for the DLSS list at page + 0x1130.
// The hook goes after the call (+0xC), on a 7-byte `mov r8d, [rbp + disp32]`.
constexpr const char* kRefreshSignature = "49 8B 5D 08 49 8B CE E8 ?? ?? ?? ?? 44 8B 85 ?? ?? ?? ?? 4C 8D 8D "
                                          "?? ?? ?? ?? 8B D0 48 8D 8B 30 11 00 00";
constexpr std::size_t kRefreshGetterCall = 0x7;
constexpr std::size_t kRefreshHook = 0xC;
// The getter: `mov eax, [rcx + 0x122A0]; ret`.
constexpr unsigned char kGetterBytes[] = {0x8B, 0x81, 0xA0, 0x22, 0x01, 0x00, 0xC3};

// The video page apply (RVA 0x15E1D76, in 0x15E1600): the DLSS list (page + 0x1130), its value, then
// `mov edx, eax; mov rcx, rdi; call` the setter (+0x19).
constexpr const char* kApplySignature =
    "48 81 C1 30 11 00 00 E8 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 8B D0 48 8B CF E8";
constexpr std::size_t kApplySetterCall = 0x19;

constexpr int kLoggedRefreshes = 20;

using SetterFn = void (*)(void* settings, int index);
SetterFn g_setterOriginal = nullptr;
std::byte* g_dlssQuality = nullptr; // the r_dlssQuality cvar object

std::once_flag g_installOnce;
std::atomic<int> g_shown{-1}; // the index the list was last filled with by the layer (-1: none)
std::atomic<int> g_refreshes{0};

stereo_seq::DlssMenuHold currentHold() {
    stereo_seq::DlssMenuHold hold;
    hold.perEyeTaa = taaRequested() && !taaFailedClosed();
    hold.dlssOption = taaDlssRequested();
    hold.dlssPerEye = taaDlssPerEyeReady();
    const int quality = taaDlssQuality();
    hold.dlssQuality = quality >= 0 ? quality : (g_dlssQuality ? cvarInt(g_dlssQuality) : -1);
    return hold;
}

const char* holdName(const stereo_seq::DlssMenuHold& hold) {
    if (!hold.perEyeTaa) {
        return "no temporal AA";
    }
    if (hold.dlssOption) {
        return hold.dlssPerEye ? "the launcher's DLSS" : "TAA (no DLSS feature for eye R)";
    }
    return hold.dlssPerEye ? "the game's own choice" : "the game's own choice, DLSS as TAA";
}

// After the getter call in the page refresh: eax is the profile's index, r14 the settings object.
void onRefresh(HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const int profile = static_cast<int>(static_cast<std::uint32_t>(regs.rax));
    const stereo_seq::DlssMenuHold hold = currentHold();
    const int shown = stereo_seq::dlssMenuShown(hold, profile);
    const int before = g_shown.exchange(shown);
    regs.rax = static_cast<std::uint32_t>(shown);
    const int n = g_refreshes.fetch_add(1) + 1;
    if (n <= kLoggedRefreshes || shown != before) {
        EVR_LOG("%s: video menu refresh %d: the profile's DLSS index %d, shown %d (%s, r_dlssQuality %d)",
                kTag, n, profile, shown, holdName(hold), hold.dlssQuality);
    }
}

// The DLSS setter, called only when the video page is applied.
void onSetter(void* settings, int index) {
    const int shown = g_shown.load();
    if (!mp_guard::allowsGameTouch() || !settings) {
        g_setterOriginal(settings, index);
        return;
    }
    int profile = 0;
    std::memcpy(&profile, static_cast<const std::byte*>(settings) + kSettingsDlssIndex, sizeof(profile));
    const stereo_seq::DlssMenuHold hold = currentHold();
    const stereo_seq::DlssMenuApply apply = stereo_seq::dlssMenuApply(hold, index, shown);
    EVR_LOG("%s: video menu applied DLSS index %d (shown %d, the profile's %d, %s): %s", kTag, index, shown,
            profile, holdName(hold), stereo_seq::dlssMenuApplyName(apply));
    if (apply == stereo_seq::DlssMenuApply::Apply) {
        g_setterOriginal(settings, index);
    }
}

// The rel32 call at `call` reaches `target`.
bool callsTo(const GameImage& image, const std::byte* call, const std::byte* target) {
    return image.inText(call, 5) && relativeTarget(call) == target;
}

bool locate(const GameImage& image,
            const std::byte*& setter,
            const std::byte*& refreshHook,
            std::vector<std::byte*>& cvars) {
    setter = findUnique(image, kTag, "DLSS setter", kSetterSignature);
    const std::byte* refresh = findUnique(image, kTag, "video page refresh", kRefreshSignature);
    const std::byte* apply = findUnique(image, kTag, "video page apply", kApplySignature);
    if (!setter || !refresh || !apply) {
        return false;
    }
    const std::byte* getter = relativeTarget(refresh + kRefreshGetterCall);
    if (!getter || !image.inText(getter, sizeof(kGetterBytes)) ||
        std::memcmp(getter, kGetterBytes, sizeof(kGetterBytes)) != 0) {
        EVR_LOG("%s: the page refresh does not call the DLSS index getter (settings + 0x122A0)", kTag);
        return false;
    }
    if (!callsTo(image, apply + kApplySetterCall, setter)) {
        EVR_LOG("%s: the page apply does not call the DLSS setter at RVA 0x%X", kTag, image.rva(setter));
        return false;
    }
    cvars = findCvarObjects(image, {"r_antialiasing", "r_dlssQuality"});
    const std::byte* lea = setter + kSetterAntialiasingLea;
    if (!cvars[0] || ripTarget(image, lea + 3, lea + 7) != cvars[0]) {
        EVR_LOG("%s: the DLSS setter does not write r_antialiasing", kTag);
        return false;
    }
    refreshHook = refresh + kRefreshHook;
    EVR_LOG(
        "%s: DLSS setter at RVA 0x%X, index getter at RVA 0x%X, page refresh hook at RVA 0x%X, page apply "
        "call at RVA 0x%X",
        kTag, image.rva(setter), image.rva(getter), image.rva(refreshHook),
        image.rva(apply + kApplySetterCall));
    return true;
}

} // namespace

void installDlssMenuHooks() {
    std::call_once(g_installOnce, [] {
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; the video menu shows the profile's DLSS", kTag);
            return;
        }
        GameImage image;
        const std::byte* setter = nullptr;
        const std::byte* refreshHook = nullptr;
        std::vector<std::byte*> cvars;
        if (!locateGameImage(image, kTag) || !locate(image, setter, refreshHook, cvars)) {
            EVR_LOG("%s: not hooked; the video menu shows the profile's DLSS, not what runs", kTag);
            return;
        }
        g_dlssQuality = cvars[1];
        std::string error;
        if (!installInlineHook(const_cast<std::byte*>(setter), reinterpret_cast<void*>(&onSetter),
                               reinterpret_cast<void**>(&g_setterOriginal), error)) {
            EVR_LOG("%s: DLSS setter hook failed: %s; the video menu shows the profile's DLSS", kTag,
                    error.c_str());
            return;
        }
        // Without the setter detour the list must not show another index: an apply would save it.
        if (!installMidHookEdit(const_cast<std::byte*>(refreshHook), &onRefresh, error)) {
            EVR_LOG("%s: page refresh hook failed: %s; the video menu shows the profile's DLSS", kTag,
                    error.c_str());
            return;
        }
        const stereo_seq::DlssMenuHold hold = currentHold();
        EVR_LOG("%s: the video menu's DLSS entry shows what runs (%s); an entry the player leaves keeps the "
                "profile's own index, a change is %s",
                kTag, holdName(hold),
                stereo_seq::dlssMenuFollowsGame(hold)
                    ? "applied as in the flat game"
                    : "not used in VR (the launcher's Anti-aliasing decides)");
    });
}

} // namespace evr::vkcore
