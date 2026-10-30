// The XR worker's wait for a new image under ETERNALVR_PACE=new (new_frame_wait.hpp).

#include "vkcore/new_frame_wait.hpp"

#include "features/pacing/pace_policy.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace evr::vkcore::new_frame_wait {

namespace {

using Clock = std::chrono::steady_clock;

// Auto-reset: a hand-over wakes the one waiter. Created once, never closed (no teardown at process exit).
HANDLE event() {
    static const HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return e;
}

// The XR worker's counters since the last 10 s line (the worker alone writes them).
struct Counters {
    std::uint64_t ready = 0;    // a new image was there already: no wait
    std::uint64_t waited = 0;   // waits that ended with a new image
    std::uint64_t timedOut = 0; // waits that ended without one
    double waitSeconds = 0.0;
    double longestSeconds = 0.0;
};
Counters g_counters;
std::uint64_t g_begun = 0; // the newest published timeline value when the last headset frame began

bool isNew(const std::atomic<std::uint64_t>& latest) {
    return (latest.load(std::memory_order_acquire) >> 2) > g_begun;
}

} // namespace

void onPublished() {
    if (const HANDLE e = event()) {
        SetEvent(e);
    }
}

void wait(const std::atomic<std::uint64_t>& latest, double periodSeconds, const std::atomic<bool>& stop) {
    if (isNew(latest)) {
        ++g_counters.ready;
        g_begun = latest.load(std::memory_order_acquire) >> 2;
        return;
    }
    const HANDLE e = event();
    const Clock::time_point start = Clock::now();
    const Clock::time_point deadline =
        start + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(pacing::newFrameWaitSeconds(periodSeconds)));
    bool got = false;
    while (!stop.load()) {
        if (isNew(latest)) {
            got = true;
            break;
        }
        const Clock::time_point now = Clock::now();
        if (now >= deadline || !e) {
            break;
        }
        const double leftMs = std::chrono::duration<double, std::milli>(deadline - now).count();
        WaitForSingleObject(e, static_cast<DWORD>(std::max(1.0, std::ceil(leftMs))));
    }
    const double took = std::chrono::duration<double>(Clock::now() - start).count();
    g_counters.waitSeconds += took;
    g_counters.longestSeconds = std::max(g_counters.longestSeconds, took);
    ++(got ? g_counters.waited : g_counters.timedOut);
    g_begun = latest.load(std::memory_order_acquire) >> 2;
}

void logSummary() {
    const Counters c = g_counters;
    g_counters = {};
    const std::uint64_t waits = c.waited + c.timedOut;
    if (waits == 0 && c.ready == 0) {
        return;
    }
    EVR_LOG(
        "pace: new frames only, last 10 s: %llu headset frame(s) began with a new image ready, %llu after a "
        "wait for one (average %.2f ms, longest %.2f ms), %llu after the wait ran out (the previous image "
        "shown again)",
        static_cast<unsigned long long>(c.ready), static_cast<unsigned long long>(c.waited),
        waits ? c.waitSeconds * 1000.0 / static_cast<double>(waits) : 0.0, c.longestSeconds * 1000.0,
        static_cast<unsigned long long>(c.timedOut));
}

} // namespace evr::vkcore::new_frame_wait
