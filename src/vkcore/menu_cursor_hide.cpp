// The game's menu cursor hidden while a stick drags the Dossier map (menu_cursor_hide.hpp).

#include "vkcore/menu_cursor_hide.hpp"

#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore::menu_cursor_hide {

namespace {

constexpr const char* kTag = "menu";

// idCursor::Update (RVA 0x1800260), from its start to `cmp byte [rdi+0xC], 0` (showHitState): r8b says
// whether the cursor is shown (`je` to the hidden path); shown, `active` = 1, the cursor's GUI model
// (rdi+0x30) sized to the render size, its blend state set to 0x2C (source alpha, one minus source alpha),
// the colour (1, 1, 1, 1) at a RIP-relative constant packed (call) and stored into the model at +0x4D0.
constexpr const char* kUpdateSignature =
    "48 89 74 24 10 57 48 83 EC 70 48 8B F2 48 8B F9 45 84 C0 0F 84 ?? ?? ?? ?? C6 01 01 48 8B 0D ?? ?? ?? "
    "?? "
    "48 89 9C 24 80 00 00 00 48 8B 01 FF 90 08 02 00 00 48 8B 0D ?? ?? ?? ?? 8B D8 4C 8B 01 41 FF 90 00 02 "
    "00 "
    "00 48 8B 4F 30 44 8B C3 8B D0 E8 ?? ?? ?? ?? 48 8B 47 30 48 8D 4C 24 60 0F 28 05 ?? ?? ?? ?? 0F 11 44 "
    "24 "
    "60 48 C7 80 D8 04 00 00 2C 00 00 00 48 8B 5F 30 E8 ?? ?? ?? ?? 89 83 D0 04 00 00 80 7F 0C 00";
constexpr std::size_t kColourConstant = 0x5E; // movaps xmm0, [rip+disp32]: the disp32 at +3, 7 bytes
constexpr std::size_t kColourStore = 0x7E;    // mov [rbx+0x4D0], eax (6 bytes): the hook

std::once_flag g_once;
std::atomic<bool> g_installed{false};
std::atomic<bool> g_hide{false};
std::atomic<std::uint64_t> g_hiddenDraws{0};
bool g_wasHidden = false; // the XR worker (setHidden)
bool g_loggedHide = false;
bool g_loggedShow = false;

// On `mov [rbx+0x4D0], eax`: eax is the packed draw colour of the cursor about to be drawn.
void onColourStore(HookRegisters& r) {
    if (!g_hide.load(std::memory_order_relaxed) || !mp_guard::allowsGameTouch()) {
        return;
    }
    r.rax &= ~std::uintptr_t{0xFFFFFFFF};
    g_hiddenDraws.fetch_add(1, std::memory_order_relaxed);
}

void dropOnTrip() {
    g_hide.store(false, std::memory_order_relaxed);
}

bool switchedOff() {
    std::wstring value;
    return readEnv(L"ETERNALVR_MAP_CURSOR_HIDE", value) && value == L"0";
}

bool locate() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    const std::byte* update = findUnique(image, kTag, "cursor update", kUpdateSignature);
    if (!update) {
        EVR_LOG("%s: the cursor's update is not in this build; the cursor stays shown on map drags", kTag);
        return false;
    }
    // The colour it packs must be opaque white, as found offline.
    const std::byte* colour = ripTarget(image, update + kColourConstant + 3, update + kColourConstant + 7);
    float rgba[4] = {};
    if (!colour || !image.contains(colour, sizeof(rgba))) {
        EVR_LOG("%s: the cursor's draw colour is outside the module; the cursor stays shown on map drags",
                kTag);
        return false;
    }
    constexpr float kWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::memcpy(rgba, colour, sizeof(rgba));
    if (std::memcmp(rgba, kWhite, sizeof(rgba)) != 0) {
        EVR_LOG("%s: the cursor's draw colour is not (1, 1, 1, 1); the cursor stays shown on map drags",
                kTag);
        return false;
    }
    std::string error;
    if (!installMidHookEdit(const_cast<std::byte*>(update + kColourStore), &onColourStore, error)) {
        EVR_LOG("%s: cursor colour hook failed: %s; the cursor stays shown on map drags", kTag,
                error.c_str());
        return false;
    }
    if (!mp_guard::addTripListener(&dropOnTrip)) {
        // The hook still checks the guard on every draw; the request is dropped there.
        EVR_LOG("%s: no room in the multiplayer guard's trip listeners for the cursor hide", kTag);
    }
    EVR_LOG("%s: cursor update at RVA 0x%X, its draw colour hooked at RVA 0x%X: the cursor is hidden while a "
            "stick drags the Dossier map (ETERNALVR_MAP_CURSOR_HIDE=0 turns this off)",
            kTag, image.rva(update), image.rva(update + kColourStore));
    return true;
}

} // namespace

bool install() {
    std::call_once(g_once, [] {
        if (switchedOff()) {
            EVR_LOG("%s: the cursor stays shown on map drags (ETERNALVR_MAP_CURSOR_HIDE=0)", kTag);
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: cursor hide not installed: the multiplayer guard is %s", kTag,
                    mp_policy::toString(mp_guard::state()));
            return;
        }
        g_installed.store(locate());
    });
    return g_installed.load();
}

void setHidden(bool hidden) {
    if (!g_installed.load(std::memory_order_relaxed)) {
        return;
    }
    hidden = hidden && mp_guard::allowsGameTouch();
    g_hide.store(hidden, std::memory_order_relaxed);
    if (hidden == g_wasHidden) {
        return;
    }
    g_wasHidden = hidden;
    if (hidden && !g_loggedHide) {
        g_loggedHide = true;
        EVR_LOG("%s: a stick drags the map: the game's cursor is hidden (first time)", kTag);
    } else if (!hidden && !g_loggedShow) {
        g_loggedShow = true;
        EVR_LOG(
            "%s: the game's cursor is shown again after the drag (first time; %llu cursor draw(s) hidden)",
            kTag, static_cast<unsigned long long>(g_hiddenDraws.load(std::memory_order_relaxed)));
    }
}

} // namespace evr::vkcore::menu_cursor_hide
