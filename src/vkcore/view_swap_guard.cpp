#include "vkcore/view_swap_guard.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/swap_images.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_swap_watch.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "parallel eyes";

constexpr std::uint32_t kScreenPass = 0x1CDF6E0;
constexpr std::uint32_t kFinishReturn = 0x1CDF449; // after the finish job's `call 0x1CDF6E0` (0x1CDF444)
constexpr std::uint32_t kPlainRet = 0x1CBB5E7;
// The renderer (0x66E2C30) keeps the device context at +0xF58; the swapchain lies in it at +0xB8 (0x1CBF910).
constexpr std::uint32_t kRenderer = 0x66E2C30;
constexpr std::uint32_t kDeviceContext = 0x66E3B88;
static_assert(kDeviceContext == kRenderer + 0xF58);
constexpr std::uint32_t kFallbackImage = 0x66E31D0;
constexpr std::size_t kScreenTarget = 0x508;
constexpr std::size_t kTargetColour = 0x10;
constexpr std::size_t kSwapchain = 0xB8;
constexpr std::size_t kSwapImages = 0x38;
constexpr std::size_t kSwapCount = 0x80;
// r8 at the pass: {command context, render-list entry} (the job's parameter, render context + 0x71AEF0, or
// the inline call's and the finish's stack pair); the entry's view index at +0x20.
constexpr std::size_t kParamEntry = 0x8;
constexpr std::size_t kEntryViewIndex = 0x20;
constexpr std::uint64_t kLoggedLines = 32; // of each kind; the periodic line keeps counting

struct Bytes {
    std::uint32_t rva;
    std::uint8_t bytes[26];
    std::size_t count;
    const char* what;
};
constexpr Bytes kChecks[] = {
    {kScreenPass,
     {0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57},
     11,
     "the screen pass's entry"},
    {0x1CDF70A, {0x48, 0x8B, 0x05, 0x77, 0x44, 0xA0, 0x04}, 7, "the screen pass's device context load"},
    {0x1CDF723, {0x4C, 0x8B, 0xB0, 0x08, 0x05, 0x00, 0x00}, 7, "the screen pass's target load (dc + 0x508)"},
    {0x1CDF731, {0x4D, 0x8B, 0x60, 0x08}, 4, "the screen pass's entry load ([r8 + 8])"},
    {0x1CDF7A5,
     {0x49, 0x8B, 0x5E, 0x10, 0x48, 0x89, 0x5C, 0x24, 0x60},
     9,
     "the screen pass's colour image load"},
    {0x1CDF444, {0xE8, 0x97, 0x02, 0x00, 0x00}, 5, "the finish job's call of the screen pass"},
    {0x1C58030,
     {0x48, 0x8B, 0x11, 0x4C, 0x8B, 0xC1, 0x48, 0x8B, 0x0D, 0x2B, 0xBB, 0xA8, 0x04, 0xE9, 0x9E, 0x76, 0x08,
      0x00},
     18,
     "the per-view screen pass job"},
    // The inline call after its `lea r8, [rcx + 0x71AEF0]` (0x1C5755D, a view redirect's hook site,
    // view_redirects.cpp): `mov [r8], rdx`, `mov [r8 + 8], r13` (the entry), the screen pass's first
    // argument, `call 0x1CDF6E0`.
    {0x1C57564,
     {0x49, 0x89, 0x10, 0x4D, 0x89, 0x68, 0x08, 0x48, 0x8B, 0x0D, 0xF6, 0xC5, 0xA8, 0x04, 0xE8, 0x69, 0x81,
      0x08, 0x00},
     19,
     "the inline per-view screen pass call"},
    {kPlainRet, {0xC3}, 1, "a `ret`"},
    {0x1D0956D, {0x39, 0xAE, 0x80, 0x00, 0x00, 0x00}, 6, "the swapchain destroy's image count (+0x80)"},
    {0x1D0957E, {0x48, 0x8D, 0x5E, 0x38}, 4, "the swapchain destroy's image array (+0x38)"},
    {0x1D0A6F4,
     {0x8B, 0x41, 0x78, 0x48, 0x8B, 0x4C, 0xC1, 0x38, 0x48, 0x89, 0x4A, 0x10},
     12,
     "the acquire's image store"},
    {0x1D09310, {0x48, 0x8B, 0x05, 0xB9, 0x9E, 0x9D, 0x04}, 7, "the acquire's fallback image load"},
    {0x1D0931F, {0x48, 0x89, 0x47, 0x10}, 4, "the acquire's fallback image store"},
    {0x1CBF910,
     {0x48, 0x8B, 0x81, 0x58, 0x0F, 0x00, 0x00, 0x48, 0x05, 0xB8, 0x00, 0x00, 0x00, 0xC3},
     14,
     "the swapchain's place in the device context"},
};
constexpr const char* kNotInstalled = "swapchain guard not installed: view 0's screen pass is never left out";

std::uintptr_t g_base = 0;
bool g_checked = false; // every site of kChecks as known before any change (prepareViewSwapGuard)
std::atomic<bool> g_guard{false};
std::atomic<bool> g_view0LeftOut{false}; // view 0's pass of the frame in flight was left out
std::atomic<std::uint64_t> g_leftOut{0};
std::atomic<std::uint64_t> g_leftOutRecreating{0};
std::atomic<std::uint64_t> g_copiesLeftOut{0};
std::atomic<std::uint64_t> g_firstChecks{0};

using swap_watch::guardedCopy;
using swap_watch::readAt;

// The screen target's colour image and what the swapchain holds now; false when a read fails or there is no
// target (the pass is left alone then). The target is dc + 0x508's: with a renderer flag set ([0x5BF1420] +
// 0x2A79) the pass draws into the device context's other screen target (+0x588 or +0x590, picked at
// 0x1CDF73F) when it has one, which is no swapchain image. It is left out all the same when dc + 0x508's
// image is stale: once per recreate, a pass that would not have crashed.
bool readState(std::uintptr_t& colour, swap_images::Swapchain& swapchain) {
    std::uintptr_t dc = 0;
    std::uintptr_t target = 0;
    return readAt(g_base + kDeviceContext, dc) && readAt(dc + kScreenTarget, target) && target != 0 &&
           readAt(target + kTargetColour, colour) &&
           guardedCopy(swapchain.images.data(), dc + kSwapchain + kSwapImages, sizeof(swapchain.images)) &&
           readAt(dc + kSwapchain + kSwapCount, swapchain.count) &&
           readAt(g_base + kFallbackImage, swapchain.fallback);
}

// The first view 0 pass checked after a destroy: what its colour image was, so a log tells a pass left out
// from one kept because an acquire ran since or because the new swapchain reuses a destroyed image's address.
void logFirstCheck(std::uint64_t destroy,
                   std::uintptr_t colour,
                   bool read,
                   bool held,
                   bool recreating,
                   const swap_images::Swapchain& swapchain) {
    if (g_firstChecks.fetch_add(1, std::memory_order_relaxed) >= kLoggedLines) {
        return;
    }
    const char* what = recreating                     ? "not checked: the recreate is running (left out)"
                       : !read                        ? "not read (kept)"
                       : !held                        ? "not the swapchain's (left out)"
                       : colour == swapchain.fallback ? "the fallback image (kept)"
                       : swap_watch::destroyedImage(colour)
                           ? "a new swapchain image at a destroyed one's address (kept)"
                           : "a new swapchain image (kept: acquired since)";
    EVR_LOG("%s: view 0's first screen pass after swapchain destroy %llu, on thread %lu: its colour image %p "
            "is %s",
            kTag, static_cast<unsigned long long>(destroy), GetCurrentThreadId(),
            reinterpret_cast<void*>(colour), what);
}

// At the pass's first instruction: [rsp] is the return address (the finish job's, the inline call's, or the
// job executor's: the job tail-jumps to the pass).
void onScreenPass(HookRegisters& r) {
    if (!g_guard.load(std::memory_order_acquire) || !parallelEyesTouch()) {
        return;
    }
    std::uintptr_t back = 0;
    std::uintptr_t entry = 0;
    std::int32_t view = -1;
    if (!readAt(r.rsp, back)) {
        return;
    }
    const bool finish = back == g_base + kFinishReturn;
    if (finish || !readAt(r.r8 + kParamEntry, entry) || !readAt(entry + kEntryViewIndex, view) || view != 0) {
        return; // the finish job's pass, or view 1's: drawn as always
    }
    // The begin job runs before the views' jobs: it resizes the device context's targets, which every view's
    // jobs bind, so with them alongside it a resize would break one view as well (an inference, not read
    // from the job graph). Should a recreate run all the same (view_swap_watch.hpp), the image may be deleted
    // while the pass reads it: the pass is left out then too.
    const bool recreating = swap_watch::recreateRunning();
    std::uintptr_t colour = 0;
    swap_images::Swapchain swapchain;
    const bool read = readState(colour, swapchain);
    const bool held = !recreating && (!read || swap_images::holds(swapchain, colour));
    if (const std::uint64_t destroy = swap_watch::takeFirstCheck()) {
        logFirstCheck(destroy, colour, read, held, recreating, swapchain);
    }
    if (!swap_images::leaveOut(finish, view, held)) {
        g_view0LeftOut.store(false, std::memory_order_release);
        return;
    }
    r.resumeAt = g_base + kPlainRet;
    g_view0LeftOut.store(true, std::memory_order_release);
    if (recreating) {
        const std::uint64_t n = g_leftOutRecreating.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= kLoggedLines) {
            EVR_LOG("%s: view 0's screen pass left out: a swapchain recreate is running (%llu time(s))", kTag,
                    static_cast<unsigned long long>(n));
        }
        return;
    }
    const std::uint64_t n = g_leftOut.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= kLoggedLines) {
        EVR_LOG("%s: view 0's screen pass left out: its swapchain image was destroyed since the last acquire "
                "(%llu time(s))",
                kTag, static_cast<unsigned long long>(n));
    }
}

} // namespace

void prepareViewSwapGuard(const std::byte* base) {
    g_base = reinterpret_cast<std::uintptr_t>(base);
    swap_watch::prepare(base);
    g_checked = std::all_of(std::begin(kChecks), std::end(kChecks), [base](const Bytes& b) {
        if (std::memcmp(base + b.rva, b.bytes, b.count) == 0) {
            return true;
        }
        EVR_LOG("%s: RVA 0x%X is not %s as known", kTag, b.rva, b.what);
        return false;
    });
}

void installViewSwapGuard(const std::byte* base) {
    swap_watch::install(base); // logs only: whatever becomes of it, the guard below is installed on its own
    if (!parallelEyesSettings().swapGuard) {
        EVR_LOG(
            "%s: swapchain guard off (ETERNALVR_TEST_PE_SWAP_GUARD=0): view 0's screen pass is never left "
            "out",
            kTag);
        return;
    }
    if (!g_checked) {
        EVR_LOG("%s: %s (a site above is not as known)", kTag, kNotInstalled);
        return;
    }
    std::string error;
    if (!installMidHookEdit(const_cast<std::byte*>(base + kScreenPass), &onScreenPass, error)) {
        EVR_LOG("%s: screen pass hook at RVA 0x%X failed: %s; %s", kTag, kScreenPass, error.c_str(),
                kNotInstalled);
        return;
    }
    g_guard.store(true, std::memory_order_release);
    EVR_LOG("%s: swapchain guard: hook at RVA 0x%X (the screen pass): view 0's screen pass before the finish "
            "job is left out when its swapchain image was destroyed since the last acquire "
            "(ETERNALVR_TEST_PE_SWAP_GUARD=0 leaves it in)",
            kTag, kScreenPass);
}

bool viewSwapGuardTakeView0LeftOut() {
    if (!g_view0LeftOut.load(std::memory_order_acquire) ||
        !g_view0LeftOut.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    g_copiesLeftOut.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void viewSwapGuardLogCounts() {
    EVR_LOG("%s: swapchain destroyed %llu time(s); view 0's screen pass left out %llu time(s) (its swapchain "
            "image destroyed since the last acquire) and %llu while a recreate ran, view 1's copy of such a "
            "frame %llu",
            kTag, static_cast<unsigned long long>(swap_watch::destroys()),
            static_cast<unsigned long long>(g_leftOut.load()),
            static_cast<unsigned long long>(g_leftOutRecreating.load()),
            static_cast<unsigned long long>(g_copiesLeftOut.load()));
}

} // namespace evr::vkcore
