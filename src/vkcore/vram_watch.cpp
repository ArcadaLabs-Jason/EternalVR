#include "vkcore/vram_watch.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <mutex>

namespace evr::vkcore::vram {

namespace {

using Microsoft::WRL::ComPtr;

constexpr ULONGLONG kPollMs = 1000;
constexpr double kMiB = 1024.0 * 1024.0;

// XR worker state, under `mutex` (a worker left behind at shutdown can still run beside a new one).
// Allocated once and never destroyed: releasing the adapter from a static destructor at process exit
// would run under the loader lock (see layer_entry.cpp).
struct State {
    std::mutex mutex;
    ComPtr<IDXGIAdapter3> adapter;
    ULONGLONG lastPoll = 0;
    bool loggedFailure = false;
    // The 10 s window.
    std::uint64_t peakUsage = 0;
    std::uint64_t readings = 0;
    std::uint64_t overBudget = 0;
};
State& g_state = *new State;

std::atomic<bool> g_valid{false};
std::atomic<std::uint64_t> g_usage{0};
std::atomic<std::uint64_t> g_budget{0};

} // namespace

void watch(IDXGIAdapter1* adapter) {
    std::lock_guard lock(g_state.mutex);
    g_state.adapter.Reset();
    g_state.lastPoll = 0;
    if (!adapter || FAILED(adapter->QueryInterface(IID_PPV_ARGS(&g_state.adapter)))) {
        EVR_LOG("vram: the adapter cannot report its memory budget (no IDXGIAdapter3); no vram line");
    }
}

void poll() {
    std::lock_guard lock(g_state.mutex);
    const ULONGLONG now = GetTickCount64();
    if (!g_state.adapter || (g_state.lastPoll != 0 && now - g_state.lastPoll < kPollMs)) {
        return;
    }
    g_state.lastPoll = now;
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    const HRESULT hr = g_state.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
    if (FAILED(hr)) {
        if (!g_state.loggedFailure) {
            g_state.loggedFailure = true;
            EVR_LOG("vram: QueryVideoMemoryInfo failed: 0x%08lx", static_cast<unsigned long>(hr));
        }
        return;
    }
    g_usage.store(info.CurrentUsage, std::memory_order_relaxed);
    g_budget.store(info.Budget, std::memory_order_relaxed);
    g_valid.store(true, std::memory_order_release);
    g_state.peakUsage = std::max<std::uint64_t>(g_state.peakUsage, info.CurrentUsage);
    ++g_state.readings;
    if (info.CurrentUsage > info.Budget) {
        ++g_state.overBudget;
    }
}

void logSummary() {
    const Reading r = latest();
    if (!r.valid) {
        return;
    }
    std::lock_guard lock(g_state.mutex);
    EVR_LOG("vram: the process uses %.0f MB of local video memory, budget %.0f MB (%.0f%%); last 10 s: peak "
            "%.0f MB, %llu of %llu reading(s) over the budget",
            static_cast<double>(r.usage) / kMiB, static_cast<double>(r.budget) / kMiB,
            r.budget ? 100.0 * static_cast<double>(r.usage) / static_cast<double>(r.budget) : 0.0,
            static_cast<double>(g_state.peakUsage) / kMiB,
            static_cast<unsigned long long>(g_state.overBudget),
            static_cast<unsigned long long>(g_state.readings));
    g_state.peakUsage = 0;
    g_state.readings = 0;
    g_state.overBudget = 0;
}

Reading latest() {
    Reading r;
    r.valid = g_valid.load(std::memory_order_acquire);
    r.usage = g_usage.load(std::memory_order_relaxed);
    r.budget = g_budget.load(std::memory_order_relaxed);
    return r;
}

} // namespace evr::vkcore::vram
