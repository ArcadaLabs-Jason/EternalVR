#include "vkcore/ui_engine.hpp"

#include "vkcore/game_code.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore::ui_engine {

namespace {

// ---- Signatures (docs/rig-findings/ui-layer.md section 6; unique in build 25216728) ----

// The final composite (view colour upsample, RVA 0x1CDF6E0) loads the GUI target's colour image (RVA
// 0x1CDF7B9): `mov rax, [rip + slot]` (slot = renderSystem + 0x5C8), `lea rcx, [rip + renderSystem]`,
// `mov rax, [rax + 0x10]`, `mov [rsp + 0x70], rax` (the local every guiMap bind reads), `call`.
constexpr const char* kCompositeLoadSignature =
    "48 8B 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 8B 40 10 48 89 44 24 70 E8 ?? ?? ?? ?? 25 01 00 00 80";
constexpr std::size_t kSlotDisp = 3;
constexpr std::size_t kSlotEnd = 7;
constexpr std::size_t kRenderSystemDisp = 10;
constexpr std::size_t kRenderSystemEnd = 14;
constexpr std::size_t kStoreOffset = 0x12; // `mov [rsp + 0x70], rax`: the hook site, rax = the GUI image
constexpr std::size_t kStoreLength = 5;
constexpr unsigned char kStoreBytes[kStoreLength] = {0x48, 0x89, 0x44, 0x24, 0x70};

// Its first guiMap bind (RVA 0x1CDFBFC): `mov r8, [rsp + 0x70]`, `mov rdx, [rip + guiMap]`, `add r8, 0xC8`.
constexpr const char* kGuiMapBindSignature =
    "4C 8B 44 24 70 49 8B CF 48 8B 15 ?? ?? ?? ?? 49 81 C0 C8 00 00 00 E8 "
    "?? ?? ?? ?? 48 8B 15 ?? ?? ?? ?? 4C 8D 83 C8 00 00 00";
constexpr std::size_t kGuiMapBindDisp = 11;
constexpr std::size_t kGuiMapBindEnd = 15;

// The engine's own no-GUI bind in the same function (RVA 0x1CE0178, render thread mode 2): guiMap =
// imageManager->_black (+0x20).
constexpr const char* kBlackBindSignature =
    "41 83 BD F4 02 00 00 02 75 21 48 8B 05 ?? ?? ?? ?? 49 8B CF 48 8B 15 "
    "?? ?? ?? ?? 4C 8B 40 20 49 81 C0 C8 00 00 00 E8 ?? ?? ?? ??";
constexpr std::size_t kImageManagerDisp = 13;
constexpr std::size_t kImageManagerEnd = 17;
constexpr std::size_t kBlackGuiMapDisp = 23;
constexpr std::size_t kBlackGuiMapEnd = 27;

// Both sites lie in the one composite function (0x1CDF6E0 to 0x1CE0340).
constexpr std::ptrdiff_t kMaxSiteDistance = 0x1000;

std::once_flag g_once;
bool g_located = false;
bool g_hooked = false;
const std::byte* g_slot = nullptr;               // renderSystem + 0x5C8: the GUI render target pointer
const std::byte* g_imageManagerGlobal = nullptr; // holds the image manager pointer
// Skipping lasts until this tick (GetTickCount64): the XR worker renews it every frame it shows the quad,
// so a stalled worker (or runtime) never keeps the GUI out of the game's own image.
std::atomic<std::uint64_t> g_skipUntil{0};
constexpr std::uint64_t kSkipLeaseMs = 250;
std::atomic<std::uint64_t> g_composites{0};
std::atomic<std::uint64_t> g_skipped{0};
std::atomic<std::uint64_t> g_readFailures{0};

// Pointer reads that may meet a half-built object (resolution changes rebuild the targets): no C++
// objects here, so structured exception handling can guard them.
bool readPointer(const std::byte* at, std::uintptr_t& out) {
    __try {
        std::memcpy(&out, at, sizeof(out));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool readImageFields(std::uintptr_t image, ui_layer::GuiImageFields& out) {
    using namespace ui_layer::engine;
    const auto* p = reinterpret_cast<const std::byte*>(image);
    __try {
        std::memcpy(&out.format, p + kImageFormat, sizeof(out.format));
        std::memcpy(&out.width, p + kImageWidth, sizeof(out.width));
        std::memcpy(&out.height, p + kImageHeight, sizeof(out.height));
        std::memcpy(&out.flags, p + kImageFlags, sizeof(out.flags));
        std::memcpy(&out.vkImage, p + kImageVkImage, sizeof(out.vkImage));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A user-mode pointer that could be an object (not null, aligned, below the user address limit).
bool plausible(std::uintptr_t p) {
    return p >= 0x10000 && p < 0x00007FFFFFFF0000ull && (p & 7) == 0;
}

// The GUI image the composite would bind, read the way the composite reads it.
std::uintptr_t currentGuiImage() {
    std::uintptr_t target = 0;
    std::uintptr_t image = 0;
    if (!g_slot || !readPointer(g_slot, target) || !plausible(target) ||
        !readPointer(reinterpret_cast<const std::byte*>(target) + ui_layer::engine::kTargetColorImage,
                     image) ||
        !plausible(image)) {
        return 0;
    }
    return image;
}

std::uintptr_t blackImage() {
    std::uintptr_t manager = 0;
    std::uintptr_t black = 0;
    if (!g_imageManagerGlobal || !readPointer(g_imageManagerGlobal, manager) || !plausible(manager) ||
        !readPointer(reinterpret_cast<const std::byte*>(manager) + ui_layer::engine::kImageManagerBlack,
                     black) ||
        !plausible(black)) {
        return 0;
    }
    return black;
}

// The composite's load of the GUI image (render thread, once per frame): rax is about to become the
// guiMap image.
void onCompositeLoad(HookRegisters& regs) {
    g_composites.fetch_add(1, std::memory_order_relaxed);
    if (GetTickCount64() >= g_skipUntil.load(std::memory_order_acquire) || !mp_guard::allowsGameTouch()) {
        return;
    }
    const std::uintptr_t black = blackImage();
    if (black == 0 || regs.rax == 0 || regs.rax != currentGuiImage()) {
        return; // not the image this module expects there: leave the composite as it is
    }
    regs.rax = black;
    g_skipped.fetch_add(1, std::memory_order_relaxed);
}

bool locate(const GameText& text, const std::byte*& hookSite) {
    const std::byte* load = findUniqueInText(text, "ui composite GUI load", kCompositeLoadSignature);
    const std::byte* bind = findUniqueInText(text, "ui composite guiMap bind", kGuiMapBindSignature);
    const std::byte* black = findUniqueInText(text, "ui composite no-GUI bind", kBlackBindSignature);
    if (!load || !bind || !black) {
        return false;
    }
    if (bind - load <= 0 || bind - load > kMaxSiteDistance || black - load <= 0 ||
        black - load > kMaxSiteDistance) {
        EVR_LOG("ui: the composite's GUI load and binds are not in one function; UI layer off");
        return false;
    }
    const std::byte* slot = ripTarget(load + kSlotDisp, load + kSlotEnd);
    const std::byte* renderSystem = ripTarget(load + kRenderSystemDisp, load + kRenderSystemEnd);
    if (slot != renderSystem + ui_layer::engine::kRenderSystemGuiTarget) {
        EVR_LOG("ui: the GUI target slot (RVA 0x%X) is not render system (RVA 0x%X) + 0x%X; UI layer off",
                rvaOf(text, slot), rvaOf(text, renderSystem), ui_layer::engine::kRenderSystemGuiTarget);
        return false;
    }
    const std::byte* guiMap = ripTarget(bind + kGuiMapBindDisp, bind + kGuiMapBindEnd);
    const std::byte* blackGuiMap = ripTarget(black + kBlackGuiMapDisp, black + kBlackGuiMapEnd);
    if (guiMap != blackGuiMap) {
        EVR_LOG("ui: the composite and its no-GUI path bind different parameters (RVA 0x%X, 0x%X); UI layer "
                "off",
                rvaOf(text, guiMap), rvaOf(text, blackGuiMap));
        return false;
    }
    if (std::memcmp(load + kStoreOffset, kStoreBytes, kStoreLength) != 0) {
        EVR_LOG("ui: the composite's GUI store is not `mov [rsp+0x70], rax`; UI layer off");
        return false;
    }
    g_slot = slot;
    g_imageManagerGlobal = ripTarget(black + kImageManagerDisp, black + kImageManagerEnd);
    hookSite = load + kStoreOffset;
    EVR_LOG("ui: GUI target slot at RVA 0x%X (render system RVA 0x%X + 0x%X), guiMap parameter RVA 0x%X, "
            "image manager RVA 0x%X",
            rvaOf(text, slot), rvaOf(text, renderSystem), ui_layer::engine::kRenderSystemGuiTarget,
            rvaOf(text, guiMap), rvaOf(text, g_imageManagerGlobal));
    return true;
}

} // namespace

bool install(bool skipHook) {
    std::call_once(g_once, [skipHook] {
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("ui: the multiplayer guard is not armed; UI layer off");
            return;
        }
        GameText text;
        if (!findGameText(text)) {
            EVR_LOG("ui: the game's .text cannot be read; UI layer off");
            return;
        }
        const std::byte* hookSite = nullptr;
        if (!locate(text, hookSite)) {
            return;
        }
        g_located = true;
        if (!skipHook) {
            EVR_LOG("ui: GUI composite hook not installed (ETERNALVR_UI_SKIP_COMPOSITE=0): the HUD stays in "
                    "the eye images too");
            return;
        }
        std::string error;
        if (!installMidHookEdit(const_cast<std::byte*>(hookSite), &onCompositeLoad, error)) {
            EVR_LOG("ui: GUI composite hook at RVA 0x%X failed (%s); the HUD stays in the eye images",
                    rvaOf(text, hookSite), error.c_str());
            return;
        }
        g_hooked = true;
        EVR_LOG("ui: GUI composite hook at RVA 0x%X", rvaOf(text, hookSite));
    });
    return g_located;
}

bool located() {
    return g_located;
}

bool skipHookInstalled() {
    return g_hooked;
}

std::optional<ui_layer::GuiImageFields> readTarget() {
    if (!g_located || !mp_guard::allowsGameTouch()) {
        return std::nullopt;
    }
    const std::uintptr_t image = currentGuiImage();
    ui_layer::GuiImageFields fields;
    if (image == 0 || !readImageFields(image, fields)) {
        g_readFailures.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    return fields;
}

void setSkipComposite(bool skip) {
    g_skipUntil.store(skip && g_hooked ? GetTickCount64() + kSkipLeaseMs : 0, std::memory_order_release);
}

bool skipComposite() {
    return GetTickCount64() < g_skipUntil.load(std::memory_order_acquire);
}

Counters counters() {
    return Counters{g_composites.load(), g_skipped.load(), g_readFailures.load()};
}

} // namespace evr::vkcore::ui_engine
