#include "vkcore/view_binning.hpp"

#include "vkcore/job_nodes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "view-redirects";

std::uintptr_t g_base = 0;

bool isView1(std::uintptr_t renderContext) {
    const auto context1 = reinterpret_cast<std::uintptr_t>(viewSlotsContext1());
    return context1 && renderContext == context1;
}

std::int64_t ticks() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

std::int64_t ticksPerSecond() {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
}

// The binning (queued by 0x1CFA640 as nodes of the engine's job graph) works in the renderer's scratch
// (RVA 0x66EFFE8..0x66F035F), which it copies out into the view's own entry at three sink nodes. View 1's
// binning waits for view 0's sinks: edges through the scheduler's locked AddEdge (vtable +0x68: scheduler,
// predecessor, successor, uncounted; job_nodes.hpp). The sink handles are recorded where 0x1CFA640 builds
// them and cleared for each dispatch (the scheduler recycles nodes).
constexpr std::uint32_t kScheduler = 0x5BF1270; // pointer to the job graph scheduler
std::atomic<std::uint64_t> g_view0Sinks[3];
std::atomic<bool> g_view0Passed{false}; // view 0's 0x1CFA640 is past its sink sites this frame
std::atomic<std::uint64_t> g_binningEdges{0};
std::atomic<std::uint64_t> g_binningEdgesDone{0};
std::atomic<std::uint64_t> g_waits[3]{};  // view 1 waited for view 0's mark, ran out, did not wait (off)
std::atomic<std::uint64_t> g_fewSinks{0}; // frames where view 1 found fewer than three sinks
std::atomic<int> g_edgeLogs{0};

// The last frame's view 1 roots that got edges, for the watchdog.
constexpr std::size_t kMaxRoots = 8;
std::atomic<std::uint64_t> g_roots[kMaxRoots];
std::atomic<std::uint32_t> g_rootCount{0};
std::atomic<std::uint64_t> g_frames{0};     // every frame the dispatcher saw, one view or two
std::atomic<std::uint64_t> g_twoViewRun{0}; // two-view frames in a row up to the last frame (0: one view)
// The watchdog looks at a stall only after this many two-view frames in a row (about a second of play): a
// load (the shell world's few two-view frames, then loading screens) is not one.
constexpr std::uint64_t kPlayRun = 120;

// Where 0x1CFA640 builds each sink node (r13 = the view's light block X = render context + 0x5226F8): N12
// (job 0x1CEF430) is r15 + 0x80000 at 0x1CFAFD8, N13 (0x1CEED40) rbx at 0x1CFB070, N14 (0x1CEF620) r14 at
// 0x1CFB158. A frame that does not build one leaves its handle 0.
constexpr std::size_t kLightBinningBlock = 0x5226F8;

DWORD g_testDelayMs = 0; // ETERNALVR_TEST_BINNING_DELAY

template <int Sink>
void onBinningSink(HookRegisters& r) {
    if (isView1(r.r13 - kLightBinningBlock) || !parallelEyesTouch()) {
        return;
    }
    if constexpr (Sink == 0) {
        if (g_testDelayMs) {
            Sleep(g_testDelayMs);
        }
    }
    const std::uint64_t handle = Sink == 0 ? r.r15 + 0x80000 : Sink == 1 ? r.rbx : r.r14;
    g_view0Sinks[Sink].store(handle, std::memory_order_release);
}
struct SinkSite {
    std::uint32_t rva;
    MidHookEditCallback callback;
};
constexpr SinkSite kSinkSites[] = {
    {0x1CFAFD8, &onBinningSink<0>}, {0x1CFB070, &onBinningSink<1>}, {0x1CFB158, &onBinningSink<2>}};

// ETERNALVR_TEST_VIEW_OFF=edges leaves them out (parallel_eyes_settings.hpp). Also turned off by the watchdog
// after a lost wakeup.
std::atomic<bool> g_edgesOn{true};

std::int64_t g_waitTicks = 0;
job_nodes::BoundedWait* g_wait = nullptr; // view 1's roots site only: one frame at a time

// View 1's binning roots wait for view 0's sinks. At 0x1CFB413 in 0x1CFA640 (r13 = X) the roots are in a
// local array (rcx, count eax) that nothing has started yet; the submit that follows (vtable +0x28) hangs
// them under the view's Begin Frame node X+0x18, uncounted as r9b = 1 says. For view 1 each root gets an edge
// from each of view 0's sinks, and the submit is made counted (r9 = 0): an uncounted trigger would start a
// root whatever its count, and it would run again when the last sink finished. View 0 passes the same site
// after its sink sites: its mark tells view 1 the sinks of this frame are all recorded.
constexpr std::uint32_t kBinningRoots = 0x1CFB413;

void onBinningRoots(HookRegisters& r) {
    if (!g_edgesOn.load(std::memory_order_relaxed) || !parallelEyesTouch()) {
        return;
    }
    if (!isView1(r.r13 - kLightBinningBlock)) {
        g_view0Passed.store(true, std::memory_order_release);
        return;
    }
    const auto result = g_wait->wait([] { return g_view0Passed.load(std::memory_order_acquire); }, ticks,
                                     [] { YieldProcessor(); });
    using Result = job_nodes::BoundedWait::Result;
    if (result != Result::Ready) {
        g_waits[result == Result::Waited ? 0 : result == Result::TimedOut ? 1 : 2].fetch_add(1);
        if (result == Result::TimedOut && g_wait->off()) {
            EVR_LOG("%s: view 0's binning did not pass its sinks within 2 ms in 16 frames in a row; view 1 "
                    "no longer waits for it (the edges stay)",
                    kTag);
        }
    }
    void* scheduler = nullptr;
    std::memcpy(&scheduler, reinterpret_cast<const void*>(g_base + kScheduler), sizeof(scheduler));
    using AddEdgeFn =
        bool (*)(void* scheduler, std::uint64_t predecessor, std::uint64_t successor, bool uncounted);
    const auto addEdge = reinterpret_cast<AddEdgeFn>((*static_cast<void***>(scheduler))[13]);
    const auto* roots = reinterpret_cast<const std::uint64_t*>(r.rcx);
    const auto count = static_cast<std::uint32_t>(r.rax);
    std::uint64_t sinks[std::size(g_view0Sinks)];
    int recorded = 0;
    for (std::size_t s = 0; s < std::size(sinks); ++s) {
        sinks[s] = g_view0Sinks[s].load(std::memory_order_acquire);
        recorded += sinks[s] ? 1 : 0;
    }
    if (recorded < static_cast<int>(std::size(sinks))) {
        g_fewSinks.fetch_add(1, std::memory_order_relaxed);
    }
    if (g_edgeLogs.fetch_add(1, std::memory_order_relaxed) < 6) {
        EVR_LOG("%s: view 1's %u binning root(s) wait for view 0's sinks %llx %llx %llx", kTag, count,
                static_cast<unsigned long long>(sinks[0]), static_cast<unsigned long long>(sinks[1]),
                static_cast<unsigned long long>(sinks[2]));
    }
    std::uint32_t kept = 0;
    for (std::uint32_t i = 0; i < count && i < kMaxRoots; ++i) {
        for (const std::uint64_t handle : sinks) {
            if (!handle) {
                continue;
            }
            if (addEdge(scheduler, handle, roots[i], false)) {
                g_binningEdges.fetch_add(1, std::memory_order_relaxed);
            } else {
                g_binningEdgesDone.fetch_add(1, std::memory_order_relaxed); // the sink had already finished
            }
        }
        g_roots[kept++].store(roots[i], std::memory_order_relaxed);
    }
    g_rootCount.store(recorded ? kept : 0, std::memory_order_release);
    r.r9 &= ~std::uintptr_t{0xFF};
}

// ---- The watchdog ----

// Whether the node is still waiting or running; false for a node that finished or a null handle.
bool nodePending(std::uint64_t handle) {
    if (!handle) {
        return false;
    }
    std::uintptr_t scheduler = 0;
    std::memcpy(&scheduler, reinterpret_cast<const void*>(g_base + kScheduler), sizeof(scheduler));
    if (!scheduler) {
        return false;
    }
    std::uintptr_t nodes = 0;
    std::memcpy(&nodes, reinterpret_cast<const void*>(scheduler + job_nodes::kNodesField), sizeof(nodes));
    if (!nodes) {
        return false;
    }
    std::uint64_t state = 0;
    std::memcpy(&state, reinterpret_cast<const void*>(nodes + job_nodes::stateOffset(handle)), sizeof(state));
    return !job_nodes::finished(handle, state);
}

DWORD WINAPI watchdog(void*) {
    const std::int64_t second = ticksPerSecond();
    std::uint64_t seen = 0;
    std::int64_t seenAt = ticks();
    std::uint64_t reported = 0;
    int reports = 0;
    for (;;) {
        Sleep(250);
        const std::uint64_t frame = g_frames.load(std::memory_order_acquire);
        const std::int64_t now = ticks();
        if (frame != seen) {
            seen = frame;
            seenAt = now;
            continue;
        }
        // Only a stall in play: the last frame was a two-view one, after a run of them.
        if (frame == 0 || frame == reported || now - seenAt < 3 * second || reports >= 8 ||
            g_twoViewRun.load(std::memory_order_acquire) < kPlayRun) {
            continue;
        }
        reported = frame;
        ++reports;
        int sinks = 0;
        int sinksPending = 0;
        for (const auto& sink : g_view0Sinks) {
            const std::uint64_t handle = sink.load(std::memory_order_acquire);
            sinks += handle ? 1 : 0;
            sinksPending += nodePending(handle) ? 1 : 0;
        }
        const std::uint32_t roots = g_rootCount.load(std::memory_order_acquire);
        int rootsPending = 0;
        for (std::uint32_t i = 0; i < roots && i < kMaxRoots; ++i) {
            rootsPending += nodePending(g_roots[i].load(std::memory_order_relaxed)) ? 1 : 0;
        }
        EVR_LOG(
            "%s: no frame for %.1f s after two-view frame %llu; its binning: view 0's sinks %d of %d still "
            "waiting or running, view 1's roots with edges %d of %u",
            kTag, static_cast<double>(now - seenAt) / static_cast<double>(second),
            static_cast<unsigned long long>(frame), sinksPending, sinks, rootsPending, roots);
        if (rootsPending > 0 && sinksPending == 0 && g_edgesOn.exchange(false)) {
            EVR_LOG("%s: view 1's binning waits on finished sinks (a lost wakeup); binning edges off for the "
                    "session",
                    kTag);
        }
    }
}

bool hook(const std::byte* base, std::uint32_t rva, MidHookEditCallback callback) {
    std::string error;
    if (!installMidHookEdit(const_cast<std::byte*>(base + rva), callback, error)) {
        EVR_LOG("%s: hook at RVA 0x%X failed: %s", kTag, rva, error.c_str());
        return false;
    }
    return true;
}

} // namespace

bool installViewBinning(const std::byte* base) {
    g_base = reinterpret_cast<std::uintptr_t>(base);
    std::wstring value;
    if (parallelEyesSettings().off & parallel_eyes::kEdges) {
        g_edgesOn.store(false);
        EVR_LOG("%s: binning edges off (ETERNALVR_TEST_VIEW_OFF)", kTag);
    }
    if (readEnv(L"ETERNALVR_TEST_BINNING_DELAY", value)) {
        const unsigned long ms = std::wcstoul(value.c_str(), nullptr, 10);
        g_testDelayMs = ms > 100 ? 100 : static_cast<DWORD>(ms);
        EVR_LOG("%s: view 0's binning sleeps %lu ms before its sinks (ETERNALVR_TEST_BINNING_DELAY)", kTag,
                g_testDelayMs);
    }
    g_waitTicks = ticksPerSecond() / 500; // 2 ms
    static job_nodes::BoundedWait wait(g_waitTicks, 16);
    g_wait = &wait;
    if (!hook(base, kBinningRoots, &onBinningRoots)) {
        return false;
    }
    for (const SinkSite& site : kSinkSites) {
        if (!hook(base, site.rva, site.callback)) {
            return false;
        }
    }
    if (HANDLE thread = CreateThread(nullptr, 0, &watchdog, nullptr, 0, nullptr)) {
        CloseHandle(thread);
    }
    return true;
}

void viewBinningFrameStart() {
    for (auto& sink : g_view0Sinks) {
        sink.store(0, std::memory_order_relaxed);
    }
    g_view0Passed.store(false, std::memory_order_relaxed);
    g_rootCount.store(0, std::memory_order_relaxed);
    g_twoViewRun.fetch_add(1, std::memory_order_relaxed);
    g_frames.fetch_add(1, std::memory_order_release);
}

void viewBinningOneViewFrame() {
    g_twoViewRun.store(0, std::memory_order_relaxed);
    g_frames.fetch_add(1, std::memory_order_release);
}

void viewBinningLogCounts() {
    EVR_LOG(
        "%s: binning edges view 0 -> view 1: %llu added, %llu sink(s) already done; view 1 waited for "
        "view 0's sinks %llu time(s), ran out %llu, did not wait %llu; fewer than 3 sinks in %llu frame(s)",
        kTag, static_cast<unsigned long long>(g_binningEdges.load()),
        static_cast<unsigned long long>(g_binningEdgesDone.load()),
        static_cast<unsigned long long>(g_waits[0].load()),
        static_cast<unsigned long long>(g_waits[1].load()),
        static_cast<unsigned long long>(g_waits[2].load()),
        static_cast<unsigned long long>(g_fewSinks.load()));
}

} // namespace evr::vkcore
