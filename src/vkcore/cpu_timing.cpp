// CPU timing (cpu_timing.hpp): the stage timers, the Vulkan call hooks, the per-tick split and the 10 s
// summary with the busiest threads.

#include "vkcore/cpu_timing.hpp"

#include "gpu_timing/stats.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/log.hpp"
#include "vkcore/shader_dump.hpp"
#include "vkcore/ui_vulkan.hpp"

#include <windows.h>

#include <intrin.h>
#include <tlhelp32.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace evr::vkcore::cpu_timing {

namespace {

namespace gt = ::evr::gpu_timing;
using gt::CpuStage;
using gt::kCpuStages;

constexpr double kReportSeconds = 10.0;
constexpr std::size_t kTopThreads = 8;
// Probes timed at start-up to estimate what one stage timer costs.
constexpr int kProbeCalibration = 2000;

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

struct Next {
    bool game = false;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkWaitSemaphores waitSemaphores = nullptr;
    PFN_vkWaitSemaphores waitSemaphoresKhr = nullptr;
};

// Allocated once and never destroyed (see layer_entry.cpp: no teardown at process exit).
std::shared_mutex& g_devicesMutex = *new std::shared_mutex;
auto& g_devices = *new std::unordered_map<DispatchKey, Next>;

// Running totals since start-up, per stage; a tick is the difference between two reads.
struct Totals {
    std::array<std::atomic<std::int64_t>, kCpuStages> ticks{};
    std::array<std::atomic<std::uint64_t>, kCpuStages> cycles{};
    std::array<std::atomic<std::uint64_t>, kCpuStages> calls{};
    std::atomic<std::uint64_t> probes{0};
};
Totals& g_totals = *new Totals;

std::atomic<DWORD> g_frontendThread{0};
std::atomic<DWORD> g_presentThread{0};

// Frontend state, touched only by the frontend (one frame-end job at a time).
struct Frontend {
    bool started = false;
    DWORD thread = 0;
    std::int64_t tickStart = 0;
    std::uint64_t tickCycles = 0;
    std::array<std::int64_t, kCpuStages> ticks{};
    std::array<std::uint64_t, kCpuStages> cycles{};
    std::array<std::uint64_t, kCpuStages> calls{};
    std::uint64_t processCycles = 0;
    std::uint64_t threadSwitches = 0;
    std::uint64_t probes = 0;
    gt::CpuSplitWindow window;
    std::int64_t windowStart = 0;
    std::uint64_t windowTicks = 0;
};
Frontend& g_frontend = *new Frontend;

// A finished 10 s window, handed from the frontend to the reporting thread, which does the slow part (the
// summaries, the thread snapshot, the log) so that the frontend never waits for it.
struct Window {
    gt::CpuSplitWindow split;
    double ms = 0.0;
    std::uint64_t ticks = 0;
    std::uint64_t threadSwitches = 0;
    std::uint64_t probes = 0;
};
struct Reporter {
    std::mutex mutex;
    std::condition_variable wake;
    std::unique_ptr<Window> pending; // a window not taken yet is replaced (the reporter fell behind)
    std::uint64_t dropped = 0;
    // The reporting thread's own.
    std::vector<gt::ThreadCycles> threads;
    std::int64_t threadsAt = 0;
    double lastMs = 0.0; // the last summary's own cost (on the reporting thread)
};
Reporter& g_reporter = *new Reporter;

double g_ticksPerMs = 0.0;
std::int64_t g_calibrationTicks = 0;
std::uint64_t g_calibrationTsc = 0;
double g_probeUs = 0.0;

std::int64_t nowTicks() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

std::uint64_t threadCycles() {
    ULONG64 cycles = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    return cycles;
}

double ticksToMs(std::int64_t ticks) {
    return static_cast<double>(ticks) / g_ticksPerMs;
}

// Thread cycle counts run at the time-stamp counter's rate: measured against the performance counter
// since start-up.
double cyclesPerMs() {
    const double ms = ticksToMs(nowTicks() - g_calibrationTicks);
    return ms > 0.0 ? static_cast<double>(__rdtsc() - g_calibrationTsc) / ms : 0.0;
}

bool readEnabled() {
    std::wstring value;
    return readEnv(L"ETERNALVR_CPU_TIMING", value) && gt::parseEnabled(value);
}

void start() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_ticksPerMs = static_cast<double>(f.QuadPart) / 1000.0;
    g_calibrationTicks = nowTicks();
    g_calibrationTsc = __rdtsc();
    // One stage timer reads both clocks twice.
    const std::int64_t begin = nowTicks();
    std::uint64_t sink = 0;
    for (int i = 0; i < kProbeCalibration; ++i) {
        sink += static_cast<std::uint64_t>(nowTicks()) + threadCycles();
    }
    g_probeUs = 1000.0 * ticksToMs(nowTicks() - begin) / kProbeCalibration * 2.0;
    EVR_LOG("cpu: CPU timing on: one stage timer costs about %.2f us (%llu)", g_probeUs,
            static_cast<unsigned long long>(sink & 1));
}

void add(CpuStage stage, std::int64_t ticks, std::uint64_t cycles) {
    const auto i = static_cast<std::size_t>(stage);
    g_totals.ticks[i].fetch_add(ticks, std::memory_order_relaxed);
    g_totals.cycles[i].fetch_add(cycles, std::memory_order_relaxed);
    g_totals.calls[i].fetch_add(1, std::memory_order_relaxed);
    g_totals.probes.fetch_add(1, std::memory_order_relaxed);
}

std::string narrow(const wchar_t* text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// Every thread of the process with its CPU cycle count and description.
std::vector<gt::ThreadCycles> snapshotThreads() {
    using DescribeFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const auto describe = reinterpret_cast<DescribeFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
    std::vector<gt::ThreadCycles> threads;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return threads;
    }
    const DWORD pid = GetCurrentProcessId();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snap, &entry); more; more = Thread32Next(snap, &entry)) {
        if (entry.th32OwnerProcessID != pid) {
            continue;
        }
        const HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!h) {
            continue;
        }
        gt::ThreadCycles t;
        t.id = entry.th32ThreadID;
        ULONG64 cycles = 0;
        if (QueryThreadCycleTime(h, &cycles)) {
            t.cycles = cycles;
        }
        PWSTR name = nullptr;
        if (describe && SUCCEEDED(describe(h, &name)) && name) {
            t.name = narrow(name);
            LocalFree(name);
        }
        CloseHandle(h);
        // The threads that ran the latest frame-end job and present (both move between workers).
        const char* role = t.id == g_frontendThread.load()  ? "last frame end"
                           : t.id == g_presentThread.load() ? "last present"
                                                            : nullptr;
        if (role) {
            t.name += std::string(t.name.empty() ? "" : " ") + "(" + role + ")";
        }
        threads.push_back(std::move(t));
    }
    CloseHandle(snap);
    return threads;
}

std::string stageLine(const gt::CpuSplitWindow::Report& r, CpuStage stage) {
    const auto i = static_cast<std::size_t>(stage);
    char text[160];
    std::snprintf(text, sizeof(text), "%s %.2f/%.2f wall (p95 %.2f), %.2f CPU, %.1f call(s)",
                  gt::cpuStageName(stage), r.wall[i].mean, r.wall[i].p50, r.wall[i].p95, r.cpu[i].mean,
                  r.callsPerTick[i]);
    return text;
}

void logWindow(Reporter& rep, const Window& w, std::uint64_t dropped) {
    const std::int64_t began = nowTicks();
    const gt::CpuSplitWindow::Report r = w.split.report();
    std::vector<gt::ThreadCycles> threads = snapshotThreads();
    const std::int64_t at = nowTicks();
    const double threadMs = ticksToMs(at - rep.threadsAt);
    // The snapshots are a little off the tick window's edges: scale the tick count to their interval.
    const double ticks = w.ms > 0.0 ? static_cast<double>(w.ticks) * threadMs / w.ms : 0.0;
    double total = 0.0;
    const std::vector<gt::ThreadShare> top =
        gt::rankThreads(rep.threads, threads, cyclesPerMs(), threadMs,
                        static_cast<std::uint64_t>(ticks + 0.5), kTopThreads, total);
    EVR_LOG("cpu: last %.1f s: %llu tick(s), %llu with both frame-end jobs on one thread; tick period %s; "
            "timer overhead about %.3f ms per tick; the last summary took %.2f ms on its own thread%s",
            w.ms / 1000.0, static_cast<unsigned long long>(r.ticks),
            static_cast<unsigned long long>(w.ticks - w.threadSwitches), gt::formatSummary(r.period).c_str(),
            w.ticks ? static_cast<double>(w.probes) * g_probeUs / 1000.0 / static_cast<double>(w.ticks) : 0.0,
            rep.lastMs, dropped ? " (windows dropped: the reporter fell behind)" : "");
    EVR_LOG("cpu: frontend per tick (ms mean/p50, p95): CPU %.2f/%.2f (p95 %.2f; ticks on one thread only); "
            "outside the frame-end jobs (game frame + eye L views) %.2f/%.2f wall (p95 %.2f), %.2f CPU; %s; "
            "%s; %s",
            r.frontendCpu.mean, r.frontendCpu.p50, r.frontendCpu.p95, r.outsideWall.mean, r.outsideWall.p50,
            r.outsideWall.p95, r.outsideCpu.mean, stageLine(r, CpuStage::FrameEndLeft).c_str(),
            stageLine(r, CpuStage::RightEye).c_str(), stageLine(r, CpuStage::Drain).c_str());
    EVR_LOG("cpu: Vulkan calls per tick (all threads, ms): %s; %s; %s; %s; whole process CPU %.2f/%.2f "
            "(p95 %.2f)",
            stageLine(r, CpuStage::Submit).c_str(), stageLine(r, CpuStage::FenceWait).c_str(),
            stageLine(r, CpuStage::SemaphoreWait).c_str(), stageLine(r, CpuStage::Present).c_str(),
            r.processCpu.mean, r.processCpu.p50, r.processCpu.p95);
    std::string list;
    for (const gt::ThreadShare& t : top) {
        list += (list.empty() ? "" : ", ") + gt::formatShare(t);
    }
    EVR_LOG("cpu: CPU per tick by thread: whole process %.2f ms (%.1f cores busy); busiest: %s", total,
            threadMs > 0.0 ? total * ticks / threadMs : 0.0, list.empty() ? "none" : list.c_str());
    rep.threads = std::move(threads);
    rep.threadsAt = at;
    rep.lastMs = ticksToMs(nowTicks() - began);
}

void reporterMain() {
    using SetDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    if (const auto set = reinterpret_cast<SetDescriptionFn>(reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")))) {
        set(GetCurrentThread(), L"EternalVR CPU timing");
    }
    Reporter& rep = g_reporter;
    rep.threads = snapshotThreads();
    rep.threadsAt = nowTicks();
    for (;;) {
        std::unique_ptr<Window> w;
        std::uint64_t dropped = 0;
        {
            std::unique_lock lock(rep.mutex);
            rep.wake.wait(lock, [&] { return rep.pending != nullptr; });
            w = std::move(rep.pending);
            dropped = std::exchange(rep.dropped, 0);
        }
        logWindow(rep, *w, dropped);
    }
}

// On the frontend: hands the finished window to the reporting thread.
void handOff(Frontend& f, std::int64_t now) {
    auto w = std::make_unique<Window>();
    w->split = std::move(f.window);
    w->ms = ticksToMs(now - f.windowStart);
    w->ticks = f.windowTicks;
    w->threadSwitches = f.threadSwitches;
    const std::uint64_t probes = g_totals.probes.load(std::memory_order_relaxed);
    w->probes = probes - f.probes;
    {
        std::lock_guard lock(g_reporter.mutex);
        g_reporter.dropped += g_reporter.pending ? 1 : 0;
        g_reporter.pending = std::move(w);
    }
    g_reporter.wake.notify_one();
    f.window.clear();
    f.windowTicks = 0;
    f.threadSwitches = 0;
    f.probes = probes;
    f.windowStart = now;
}

// ---- Vulkan call hooks ----

Next nextOf(DispatchKey key) {
    std::shared_lock lock(g_devicesMutex);
    const auto it = g_devices.find(key);
    return it == g_devices.end() ? Next{} : it->second;
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue,
                                           std::uint32_t submitCount,
                                           const VkSubmitInfo* pSubmits,
                                           VkFence fence) {
    const Next next = nextOf(keyOf(queue));
    if (!next.game) {
        return next.queueSubmit(queue, submitCount, pSubmits, fence);
    }
    Scope scope(CpuStage::Submit);
    return next.queueSubmit(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL WaitForFences(VkDevice device,
                                             std::uint32_t fenceCount,
                                             const VkFence* pFences,
                                             VkBool32 waitAll,
                                             std::uint64_t timeout) {
    const Next next = nextOf(keyOf(device));
    if (!next.game || timeout == 0) {
        return next.waitForFences(device, fenceCount, pFences, waitAll, timeout);
    }
    Scope scope(CpuStage::FenceWait);
    return next.waitForFences(device, fenceCount, pFences, waitAll, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL WaitSemaphores(VkDevice device,
                                              const VkSemaphoreWaitInfo* pWaitInfo,
                                              std::uint64_t timeout) {
    const Next next = nextOf(keyOf(device));
    if (!next.game || timeout == 0) {
        return next.waitSemaphores(device, pWaitInfo, timeout);
    }
    Scope scope(CpuStage::SemaphoreWait);
    return next.waitSemaphores(device, pWaitInfo, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL WaitSemaphoresKHR(VkDevice device,
                                                 const VkSemaphoreWaitInfo* pWaitInfo,
                                                 std::uint64_t timeout) {
    const Next next = nextOf(keyOf(device));
    if (!next.game || timeout == 0) {
        return next.waitSemaphoresKhr(device, pWaitInfo, timeout);
    }
    Scope scope(CpuStage::SemaphoreWait);
    return next.waitSemaphoresKhr(device, pWaitInfo, timeout);
}

// The hook of the same function further along this layer's own chain, else the next layer's function.
PFN_vkVoidFunction chained(DeviceData& data, const char* name) {
    if (const PFN_vkVoidFunction gpu = gpu_timing::findHook(name)) {
        return gpu;
    }
    if (const PFN_vkVoidFunction ui = ui_vulkan::findHook(name)) {
        return ui;
    }
    if (const PFN_vkVoidFunction dump = shader_dump::findHook(name)) {
        return dump;
    }
    return data.nextGetDeviceProcAddr(data.device, name);
}

} // namespace

bool enabled() {
    static const bool on = [] {
        const bool value = readEnabled();
        if (value) {
            start();
        }
        return value;
    }();
    return on;
}

void onDeviceCreated(DeviceData& data, bool isGame) {
    if (!enabled()) {
        return;
    }
    Next next;
    next.game = isGame;
    next.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(chained(data, "vkQueueSubmit"));
    next.waitForFences = reinterpret_cast<PFN_vkWaitForFences>(chained(data, "vkWaitForFences"));
    next.waitSemaphores = reinterpret_cast<PFN_vkWaitSemaphores>(chained(data, "vkWaitSemaphores"));
    next.waitSemaphoresKhr = reinterpret_cast<PFN_vkWaitSemaphores>(chained(data, "vkWaitSemaphoresKHR"));
    if (isGame) {
        EVR_LOG("cpu: timing vkQueueSubmit, vkWaitForFences and vkWaitSemaphores%s on the game's device",
                next.waitSemaphoresKhr ? " (and the KHR alias)" : "");
    }
    std::unique_lock lock(g_devicesMutex);
    g_devices[keyOf(data.device)] = next;
}

void onDeviceDestroyed(VkDevice device) {
    if (!enabled()) {
        return;
    }
    std::unique_lock lock(g_devicesMutex);
    g_devices.erase(keyOf(device));
}

PFN_vkVoidFunction findHook(const char* name) {
    if (!enabled()) {
        return nullptr;
    }
#define EVR_HOOK(fn)                                                                                         \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_HOOK(QueueSubmit)
    EVR_HOOK(WaitForFences)
    EVR_HOOK(WaitSemaphores)
    EVR_HOOK(WaitSemaphoresKHR)
#undef EVR_HOOK
    return nullptr;
}

void onFrontendTick() {
    if (!enabled()) {
        return;
    }
    Frontend& f = g_frontend;
    const std::int64_t now = nowTicks();
    const std::uint64_t cycles = threadCycles();
    const DWORD thread = GetCurrentThreadId();
    g_frontendThread.store(thread, std::memory_order_relaxed);
    gt::CpuTick tick;
    for (std::size_t i = 0; i < kCpuStages; ++i) {
        const std::int64_t ticks = g_totals.ticks[i].load(std::memory_order_relaxed);
        const std::uint64_t c = g_totals.cycles[i].load(std::memory_order_relaxed);
        const std::uint64_t calls = g_totals.calls[i].load(std::memory_order_relaxed);
        tick.wallMs[i] = ticksToMs(ticks - f.ticks[i]);
        tick.cpuMs[i] = static_cast<double>(c - f.cycles[i]); // cycles until scaled below
        tick.calls[i] = static_cast<std::uint32_t>(calls - f.calls[i]);
        f.ticks[i] = ticks;
        f.cycles[i] = c;
        f.calls[i] = calls;
    }
    ULONG64 process = 0;
    QueryProcessCycleTime(GetCurrentProcess(), &process);
    if (f.started) {
        const double perMs = cyclesPerMs();
        tick.periodMs = ticksToMs(now - f.tickStart);
        tick.processCpuMs = perMs > 0.0 ? static_cast<double>(process - f.processCycles) / perMs : 0.0;
        for (double& c : tick.cpuMs) {
            c = perMs > 0.0 ? c / perMs : 0.0;
        }
        ++f.windowTicks;
        if (thread == f.thread && perMs > 0.0) {
            tick.frontendCpuMs = static_cast<double>(cycles - f.tickCycles) / perMs;
        } else {
            ++f.threadSwitches; // the frame-end job ran on another thread: its CPU time is unknown
        }
        f.window.add(tick);
    } else {
        f.started = true;
        f.windowStart = now;
        f.probes = g_totals.probes.load(std::memory_order_relaxed);
        std::thread(reporterMain).detach();
    }
    f.processCycles = process;
    f.thread = thread;
    f.tickStart = now;
    f.tickCycles = cycles;
    if (ticksToMs(now - f.windowStart) >= kReportSeconds * 1000.0) {
        handOff(f, now);
    }
}

Scope::Scope(CpuStage stage) : stage_(stage), on_(enabled()) {
    if (on_) {
        startTicks_ = nowTicks();
        startCycles_ = threadCycles();
    }
}

Scope::~Scope() {
    if (!on_) {
        return;
    }
    const std::uint64_t cycles = threadCycles();
    add(stage_, nowTicks() - startTicks_, cycles - startCycles_);
    if (stage_ == CpuStage::Present) {
        g_presentThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    }
}

} // namespace evr::vkcore::cpu_timing
