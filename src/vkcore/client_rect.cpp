// The game's own GetClientRect calls that size its swapchain and its output (client_rect.hpp).

#include "vkcore/client_rect.hpp"

#include "vkcore/game_code.hpp"
#include "vkcore/import_patch.hpp"
#include "vkcore/log.hpp"

#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

#pragma intrinsic(_ReturnAddress)

namespace evr::vkcore::client_rect {

namespace {

using render_size::Extent;
using GetClientRectFn = BOOL(WINAPI*)(HWND, LPRECT);

// ---- The game's GetClientRect calls that size its swapchain and its output (Steam build 25216728,
// docs/rig-findings/render-size.md section 2). Each signature starts a few bytes before the `call [rip +
// GetClientRect]` (FF 15) and matches once in .text.
struct CallSite {
    const char* name;
    const char* signature;
    std::size_t call; // offset of FF 15 in the signature
};
constexpr std::array<CallSite, 4> kCallSites{{
    // Swapchain resize check (RVA 0x1D09070, call at 0x1D09091): client size against the swapchain's
    // (+0x1C / +0x20); a difference sets the recreate flag (+0x18).
    {"swapchain resize check",
     "48 8D 54 24 20 48 8B 49 08 FF 15 ?? ?? ?? ?? 8B 44 24 28 2B 44 24 20 3B 43 1C 75 ?? 8B 44 24 2C 2B 44 "
     "24 "
     "24 3B 43 20",
     9},
    // Swapchain recreate (RVA 0x1D090E0, call at 0x1D0912E): the client size is the new swapchain's size.
    {"swapchain recreate", "48 8D 54 24 30 FF 15 ?? ?? ?? ?? 8B 4C 24 38 2B 4C 24 30 8B 44 24 3C 2B 44 24 34",
     5},
    // The same on the window's thread (RVA 0x1D09190, call at 0x1D091B4; sent as message 0x9235).
    {"swapchain recreate (window thread)",
     "48 8B 09 48 8B 49 08 FF 15 ?? ?? ?? ?? 8B 54 24 28 2B 54 24 20 44 8B 44 24 2C 44 2B 44 24 24 89 53 08 "
     "44 "
     "89 43 0C",
     7},
    // Window moved or sized (RVA 0x1DC3B00, call at 0x1DC3B22; queued by WM_WINDOWPOSCHANGED): the client
    // size goes to render system vtable 0x298 (0x1CC1240), which sets the output size (0x39AABE4 / 0x39AABE8)
    // and r_windowWidth / r_windowHeight.
    {"window size",
     "48 8D 54 24 38 48 8B 09 FF 15 ?? ?? ?? ?? 85 C0 74 ?? 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 30 48 C7 44 24 "
     "30 "
     "00 00 00 00",
     8},
}};
constexpr std::size_t kCallLength = 6; // FF 15 disp32

std::atomic<GetClientRectFn> g_getClientRect{nullptr};
std::array<std::atomic<const void*>, kCallSites.size()> g_returnAddresses{};
std::atomic<HWND> g_window{nullptr};
std::atomic<std::uint64_t> g_answer{0}; // (width << 32) | height; 0: the real client area
std::atomic<std::uint64_t> g_answers{0};
std::array<std::atomic<std::uint64_t>, kCallSites.size()> g_siteCalls{};
std::atomic<int> g_loggedCalls{0};
constexpr int kLoggedCalls = 24;

// The game's own SetWindowPos calls, logged (first kLoggedMoves) and counted: the window is only the mirror
// once the render size is on, and the game may still move or size it.
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
std::atomic<SetWindowPosFn> g_setWindowPos{nullptr};
std::atomic<std::uint64_t> g_moves{0};
constexpr std::uint64_t kLoggedMoves = 16;
const std::byte* g_module = nullptr;
// The last answer on this thread from one of the call sites: the render size (packed) or 0 (the real size).
thread_local std::uint64_t t_answer = 0;

constexpr std::uint64_t pack(Extent e) {
    return (static_cast<std::uint64_t>(e.width) << 32) | e.height;
}
constexpr Extent unpack(std::uint64_t v) {
    return {static_cast<std::uint32_t>(v >> 32), static_cast<std::uint32_t>(v & 0xFFFFFFFFu)};
}

// The index of the call site `returnAddress` returns to, or kCallSites.size().
std::size_t callSite(const void* returnAddress) {
    for (std::size_t i = 0; i < g_returnAddresses.size(); ++i) {
        if (g_returnAddresses[i].load(std::memory_order_relaxed) == returnAddress) {
            return i;
        }
    }
    return kCallSites.size();
}

BOOL WINAPI hookedSetWindowPos(HWND window, HWND after, int x, int y, int cx, int cy, UINT flags) {
    const void* caller = _ReturnAddress();
    if (window == g_window.load(std::memory_order_relaxed)) {
        const std::uint64_t n = g_moves.fetch_add(1, std::memory_order_relaxed);
        if (n < kLoggedMoves) {
            EVR_LOG("size: the game's SetWindowPos (return RVA 0x%llX): %d,%d %dx%d flags 0x%X",
                    static_cast<unsigned long long>(static_cast<const std::byte*>(caller) - g_module), x, y,
                    cx, cy, flags);
        }
    }
    return g_setWindowPos.load()(window, after, x, y, cx, cy, flags);
}

BOOL WINAPI hookedGetClientRect(HWND window, LPRECT rect) {
    const void* caller = _ReturnAddress();
    const BOOL ok = g_getClientRect.load()(window, rect);
    const std::size_t site = callSite(caller);
    if (site == kCallSites.size()) {
        return ok;
    }
    g_siteCalls[site].fetch_add(1, std::memory_order_relaxed);
    if (ok && rect && g_loggedCalls.fetch_add(1, std::memory_order_relaxed) < kLoggedCalls) {
        const Extent a = unpack(g_answer.load(std::memory_order_acquire));
        EVR_LOG("size: %s asks the client area: real %ldx%ld, answered %ux%u", kCallSites[site].name,
                rect->right - rect->left, rect->bottom - rect->top, a.width, a.height);
    }
    t_answer = 0;
    const std::uint64_t active = g_answer.load(std::memory_order_acquire);
    if (ok && rect && active && window == g_window.load(std::memory_order_relaxed)) {
        const Extent e = unpack(active);
        rect->left = 0;
        rect->top = 0;
        rect->right = static_cast<LONG>(e.width);
        rect->bottom = static_cast<LONG>(e.height);
        t_answer = active;
        g_answers.fetch_add(1, std::memory_order_relaxed);
    }
    return ok;
}

// Locates the call sites and replaces the import.
bool installOnce() {
    GameText text;
    if (!findGameText(text)) {
        EVR_LOG("size: the game's code cannot be read");
        return false;
    }
    std::array<const void*, kCallSites.size()> returns{};
    const void* slot = nullptr;
    for (std::size_t i = 0; i < kCallSites.size(); ++i) {
        const std::byte* at = findUniqueInText(text, "size", kCallSites[i].name, kCallSites[i].signature);
        if (!at) {
            EVR_LOG("size: the %s call is not in this build", kCallSites[i].name);
            return false;
        }
        const std::byte* call = at + kCallSites[i].call;
        const std::byte* target = ripTarget(call + 2, call + kCallLength);
        if (slot && target != slot) {
            EVR_LOG("size: the %s call reads another import slot", kCallSites[i].name);
            return false;
        }
        slot = target;
        returns[i] = call + kCallLength;
    }
    for (std::size_t i = 0; i < returns.size(); ++i) {
        g_returnAddresses[i].store(returns[i]);
    }
    const void* patched = nullptr;
    if (!replaceImport("GetClientRect", g_getClientRect, &hookedGetClientRect, &patched)) {
        EVR_LOG("size: the game's GetClientRect import cannot be replaced");
        return false;
    }
    if (patched != slot) {
        // The calls go through another slot: put this one back and stay off.
        patchImport("GetClientRect", reinterpret_cast<void*>(g_getClientRect.load()));
        EVR_LOG("size: the calls do not read the GetClientRect import slot (RVA 0x%X)", rvaOf(text, patched));
        return false;
    }
    EVR_LOG("size: GetClientRect import (slot RVA 0x%X) answers the game's swapchain and output size calls",
            rvaOf(text, slot));
    g_module = text.base;
    if (!replaceImport("SetWindowPos", g_setWindowPos, &hookedSetWindowPos)) {
        EVR_LOG("size: the game's SetWindowPos calls are not followed (import not replaced)");
    }
    return true;
}

} // namespace

bool install() {
    static std::once_flag once;
    static bool installed = false;
    std::call_once(once, [] { installed = installOnce(); });
    return installed;
}

void setWindow(HWND window) {
    g_window.store(window);
}

void setAnswer(std::optional<Extent> size) {
    g_answer.store(size ? pack(*size) : 0, std::memory_order_release);
}

std::optional<Extent> answer() {
    const std::uint64_t v = g_answer.load(std::memory_order_acquire);
    return v ? std::optional<Extent>(unpack(v)) : std::nullopt;
}

std::optional<Extent> lastAnswerOnThisThread() {
    return t_answer ? std::optional<Extent>(unpack(t_answer)) : std::nullopt;
}

std::uint64_t answers() {
    return g_answers.load(std::memory_order_relaxed);
}

std::array<std::uint64_t, 5> calls() {
    std::array<std::uint64_t, 5> out{};
    for (std::size_t i = 0; i < kCallSites.size(); ++i) {
        out[i] = g_siteCalls[i].load(std::memory_order_relaxed);
    }
    out[4] = g_moves.load(std::memory_order_relaxed);
    return out;
}

} // namespace evr::vkcore::client_rect
