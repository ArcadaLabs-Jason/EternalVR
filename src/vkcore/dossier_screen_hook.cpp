#include "vkcore/dossier_screen_hook.hpp"

#include "features/menu/dossier_screen.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"

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

std::atomic<int> g_screen{-1};
std::atomic<std::uint32_t> g_count{0};

void onShowScreen(const HookRegisters& r) {
    g_screen.store(static_cast<int>(static_cast<std::uint32_t>(r.rdx)), std::memory_order_relaxed);
    g_count.fetch_add(1, std::memory_order_release);
}

bool installOnce() {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        return false;
    }
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

} // namespace evr::vkcore::dossier_screen
