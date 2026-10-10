#include "vkcore/view_swap_watch.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/seh_filter.hpp"
#include "vkcore/swap_images.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_snapshot.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>

namespace evr::vkcore::swap_watch {

namespace {

constexpr const char* kTag = "parallel eyes";

constexpr std::uint32_t kBegin = 0x1CD9750;
constexpr std::uint32_t kBeginJobReturn = 0x1CD6F89; // the begin job 0x1CD6F80's call of it
constexpr std::size_t kViewCount = 0xA8;             // the render thread's render list count
constexpr std::uint32_t kRecreateCall = 0x1CDD5B4;   // before the begin's `call 0x1D090E0` (0x1CDD5BF)
constexpr std::uint32_t kRecreateDone = 0x1CDD5C4;   // after it
constexpr std::uint32_t kDestroy = 0x1D09540;
constexpr std::size_t kSwapImages = 0x38;
constexpr std::size_t kSwapCount = 0x80;
constexpr std::uint64_t kLoggedLines = 32;

struct Bytes {
    std::uint32_t rva;
    std::uint8_t bytes[16];
    std::size_t count;
    const char* what;
};
constexpr Bytes kChecks[] = {
    {kBegin, {0x48, 0x89, 0x5C, 0x24, 0x10}, 5, "the frame begin's entry"},
    {0x1CD9777, {0x48, 0x8B, 0xE9}, 3, "the frame begin's render thread (rbp = rcx)"},
    {0x1CD9845, {0x8B, 0x85, 0xA8, 0x00, 0x00, 0x00}, 6, "the frame begin's view count read (+0xA8)"},
    {0x1CD6F80, {0x48, 0x83, 0xEC, 0x28, 0xE8, 0xC7, 0x27, 0x00, 0x00}, 9, "the frame begin job's call"},
    {kRecreateCall,
     {0x48, 0x8B, 0xC8, 0x48, 0x8D, 0x94, 0x24, 0x80, 0x00, 0x00, 0x00, 0xE8, 0x1C, 0xBB, 0x02, 0x00},
     16,
     "the frame begin's swapchain recreate call"},
    {kRecreateDone, {0x48, 0x8B, 0x08, 0x48, 0x89, 0x4C, 0x24, 0x70}, 8, "the recreate call's return"},
    {kDestroy,
     {0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57},
     11,
     "the swapchain destroy's entry"},
    {0x1D0956D, {0x39, 0xAE, 0x80, 0x00, 0x00, 0x00}, 6, "the swapchain destroy's image count (+0x80)"},
    {0x1D0957E, {0x48, 0x8D, 0x5E, 0x38}, 4, "the swapchain destroy's image array (+0x38)"},
};

// The frame begin running on this thread: called as the job or directly (its caller), and its view count.
struct Begin {
    bool job = false;
    std::uint32_t caller = 0;
    std::int32_t views = -1;
};
thread_local Begin t_begin;

std::uintptr_t g_base = 0;
bool g_checked = false;
// Under g_mutex: the recreate in progress (the begin's thread and its begin) and the destroyed images.
std::mutex& g_mutex = *new std::mutex;
DWORD g_recreateThread = 0;
Begin g_recreateBegin;
swap_images::Swapchain g_destroyed;
std::atomic<bool> g_hooked{false}; // all hooks in: the recreate counter is balanced
std::atomic<int> g_recreating{0};
std::atomic<std::uint64_t> g_destroys{0};
std::atomic<std::uint64_t> g_firstCheck{0};

void onBegin(const HookRegisters& r) {
    std::uintptr_t back = 0;
    t_begin = Begin{};
    if (readAt(r.rsp, back)) {
        t_begin.job = back == g_base + kBeginJobReturn;
        t_begin.caller = static_cast<std::uint32_t>(back - g_base);
    }
    readAt(r.rcx + kViewCount, t_begin.views);
}

void onRecreateCall(const HookRegisters&) {
    {
        std::lock_guard lock(g_mutex);
        g_recreateThread = GetCurrentThreadId();
        g_recreateBegin = t_begin;
    }
    g_recreating.fetch_add(1, std::memory_order_acq_rel);
}

void onRecreateDone(const HookRegisters&) {
    g_recreating.fetch_sub(1, std::memory_order_acq_rel);
}

void onDestroy(const HookRegisters& r) {
    if (!viewSlotsActive()) {
        return;
    }
    view_snapshot::holdNewSwapchainImages();
    const std::uint64_t n = g_destroys.fetch_add(1, std::memory_order_relaxed) + 1;
    DWORD thread = 0;
    Begin begin;
    const bool inBegin = g_recreating.load(std::memory_order_acquire) > 0;
    {
        std::lock_guard lock(g_mutex);
        g_destroyed = swap_images::Swapchain{};
        guardedCopy(g_destroyed.images.data(), r.rcx + kSwapImages, sizeof(g_destroyed.images));
        readAt(r.rcx + kSwapCount, g_destroyed.count);
        thread = g_recreateThread;
        begin = g_recreateBegin;
    }
    g_firstCheck.store(n, std::memory_order_release);
    if (n > kLoggedLines) {
        return;
    }
    if (!inBegin) {
        EVR_LOG("%s: swapchain destroyed (%llu time(s)) on thread %lu; not from a frame begin's recreate",
                kTag, static_cast<unsigned long long>(n), GetCurrentThreadId());
    } else if (begin.job) {
        EVR_LOG("%s: swapchain destroyed (%llu time(s)) on thread %lu; recreated by the frame begin job on "
                "thread %lu, a frame with %d view(s)",
                kTag, static_cast<unsigned long long>(n), GetCurrentThreadId(), thread, begin.views);
    } else {
        EVR_LOG("%s: swapchain destroyed (%llu time(s)) on thread %lu; recreated by the frame begin called "
                "directly (return RVA 0x%X) on thread %lu, a frame with %d view(s)",
                kTag, static_cast<unsigned long long>(n), GetCurrentThreadId(), begin.caller, thread,
                begin.views);
    }
}

} // namespace

bool guardedCopy(void* to, std::uintptr_t from, std::size_t size) {
    if (from < 0x10000) {
        return false;
    }
    __try {
        std::memcpy(to, reinterpret_cast<const void*>(from), size);
        return true;
    } __except (accessViolationOnly(GetExceptionCode())) {
        return false;
    }
}

void prepare(const std::byte* base) {
    g_base = reinterpret_cast<std::uintptr_t>(base);
    g_checked = std::all_of(std::begin(kChecks), std::end(kChecks), [base](const Bytes& b) {
        if (std::memcmp(base + b.rva, b.bytes, b.count) == 0) {
            return true;
        }
        EVR_LOG("%s: RVA 0x%X is not %s as known", kTag, b.rva, b.what);
        return false;
    });
}

void install(const std::byte* base) {
    if (!g_checked) {
        EVR_LOG("%s: swapchain destroys are not logged (a site above is not as known)", kTag);
        return;
    }
    // The return first: the recreate counter only goes up once it can come down.
    const std::pair<std::uint32_t, MidHookCallback> hooks[] = {{kRecreateDone, &onRecreateDone},
                                                               {kRecreateCall, &onRecreateCall},
                                                               {kBegin, &onBegin},
                                                               {kDestroy, &onDestroy}};
    for (const auto& [rva, callback] : hooks) {
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(base + rva), callback, error)) {
            EVR_LOG("%s: swapchain watch hook at RVA 0x%X failed: %s; swapchain destroys are not logged",
                    kTag, rva, error.c_str());
            return;
        }
    }
    g_hooked.store(true, std::memory_order_release);
    EVR_LOG(
        "%s: swapchain watch: hooks at RVA 0x%X (the swapchain destroy), 0x%X (the frame begin) and 0x%X, "
        "0x%X (its recreate call): each destroy is logged with the begin that recreated it",
        kTag, kDestroy, kBegin, kRecreateCall, kRecreateDone);
}

bool recreateRunning() {
    return g_hooked.load(std::memory_order_acquire) && g_recreating.load(std::memory_order_acquire) > 0;
}

std::uint64_t takeFirstCheck() {
    return g_firstCheck.load(std::memory_order_acquire) != 0 ? g_firstCheck.exchange(0) : 0;
}

bool destroyedImage(std::uintptr_t image) {
    std::lock_guard lock(g_mutex);
    swap_images::Swapchain destroyed = g_destroyed;
    destroyed.fallback = 0;
    return swap_images::holds(destroyed, image);
}

std::uint64_t destroys() {
    return g_destroys.load(std::memory_order_relaxed);
}

} // namespace evr::vkcore::swap_watch
