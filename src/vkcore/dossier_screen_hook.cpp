#include "vkcore/dossier_screen_hook.hpp"

#include "features/menu/dossier_screen.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <atomic>
#include <string>

namespace evr::vkcore::dossier_screen {

namespace {

constexpr const char* kTag = "menu";

// idHUDMenu_Dossier::ShowScreen's start (RVA 0xF39010 in build 25216728): `mov [rsp+0x18], rbx; push rsi;
// sub rsp, 0x20; mov esi, edx; mov rbx, rcx; call <the base menu's>; mov rbx, [rbx+0x40]; test rbx, rbx;
// je; mov rcx, rbx; call`. Unique there, and the Dossier's vtable is the only pointer to it.
constexpr const char* kSignature =
    "48 89 5C 24 18 56 48 83 EC 20 8B F2 48 8B D9 E8 ?? ?? ?? ?? 48 8B 5B 40 48 85 DB 74 ?? 48 8B CB E8";

// The Dossier's open (vtable slot 6, RVA 0xF38450 in build 25216728): `push rbx; sub rsp, 0x20; mov rbx, rcx;
// call <the base menu's>; mov rcx, rbx; call; mov rcx, rbx; call; mov rcx, [rbx+0x40]; xor r9d, r9d; mov
// dword [rbx+0x188], 0; mov rax, [rcx]; lea edx, [r9+0x21]` (the open event). Its close (slot 7, RVA
// 0xF38560): the frame's set-up, `mov rbx, rcx; call <the base menu's>; mov rcx, [rbx+0x40]; xor r9d, r9d;
// mov rax, [rcx]; lea edx, [r9+0x22]` (the close event). Each unique there.
constexpr const char* kOpenSignature =
    "40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8B CB E8 ?? ?? ?? ?? 48 8B CB "
    "E8 ?? ?? ?? ?? 48 8B 4B 40 45 33 C9 C7 83 88 01 00 00 00 00 00 00 48 8B "
    "01 41 8D 51 21";
constexpr const char* kCloseSignature =
    "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 48 8D AC 24 40 FF FF FF 48 81 EC C0 01 00 00 48 8B 05 "
    "?? ?? "
    "?? ?? 48 33 C4 48 89 85 B0 00 00 00 48 8B D9 E8 ?? ?? ?? ?? 48 8B 4B 40 45 33 C9 48 8B 01 41 8D 51 22";

std::atomic<int> g_screen{-1};
std::atomic<std::uint32_t> g_count{0};
// The open calls so far (high half) and the map loads at the last one (low half, mp_guard::mapLoads),
// published together; and whether the last of the open and close calls was an open.
std::atomic<std::uint64_t> g_opens{0};
std::atomic<bool> g_open{false};

void onShowScreen(const HookRegisters& r) {
    g_screen.store(static_cast<int>(static_cast<std::uint32_t>(r.rdx)), std::memory_order_relaxed);
    g_count.fetch_add(1, std::memory_order_release);
}

void onOpen(const HookRegisters&) {
    g_open.store(true, std::memory_order_relaxed);
    const std::uint64_t loads = mp_guard::mapLoads();
    std::uint64_t was = g_opens.load(std::memory_order_relaxed);
    while (!g_opens.compare_exchange_weak(was, (((was >> 32) + 1) << 32) | loads, std::memory_order_release,
                                          std::memory_order_relaxed)) {
    }
}

void onClose(const HookRegisters&) {
    g_open.store(false, std::memory_order_release);
}

// The open and close hooks, the close first (an open alone would never end).
void installOpenClose(const GameImage& image) {
    const std::byte* open = findUnique(image, kTag, "the Dossier's open", kOpenSignature);
    const std::byte* close = findUnique(image, kTag, "the Dossier's close", kCloseSignature);
    if (!open || !close) {
        EVR_LOG(
            "%s: the Dossier's open and close are not hooked; in gamepad mode its map and CODEX pages do not "
            "hold the body",
            kTag);
        return;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(close), &onClose, error) ||
        !installMidHook(const_cast<std::byte*>(open), &onOpen, error)) {
        EVR_LOG(
            "%s: the Dossier's open or close hook failed: %s; in gamepad mode its map and CODEX pages do not "
            "hold the body",
            kTag, error.c_str());
        return;
    }
    EVR_LOG(
        "%s: the Dossier's open and close hooked at RVA 0x%X and 0x%X: in gamepad mode the open Dossier is a "
        "menu over the game",
        kTag, image.rva(open), image.rva(close));
}

bool installOnce() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
    installOpenClose(image);
    const std::byte* site = findUnique(image, kTag, "idHUDMenu_Dossier::ShowScreen", kSignature);
    if (!site) {
        EVR_LOG("%s: the Dossier's page is counted from its map (no ShowScreen hook)", kTag);
        return false;
    }
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(site), &onShowScreen, error)) {
        EVR_LOG("%s: Dossier ShowScreen hook at RVA 0x%X failed: %s; the page is counted from its map", kTag,
                image.rva(site), error.c_str());
        return false;
    }
    EVR_LOG("%s: Dossier ShowScreen hook at RVA 0x%X: the router follows the page the game shows", kTag,
            image.rva(site));
    return true;
}

} // namespace

bool install() {
    static const bool installed = installOnce();
    return installed;
}

void readShown(std::uint32_t& count, int& page) {
    count = g_count.load(std::memory_order_acquire);
    page = menu::dossierPageOfScreen(g_screen.load(std::memory_order_relaxed));
}

void readOpen(std::uint32_t& opens, bool& open, std::uint32_t& loadsAtOpen) {
    const std::uint64_t state = g_opens.load(std::memory_order_acquire);
    opens = static_cast<std::uint32_t>(state >> 32);
    loadsAtOpen = static_cast<std::uint32_t>(state);
    open = g_open.load(std::memory_order_acquire);
}

} // namespace evr::vkcore::dossier_screen
