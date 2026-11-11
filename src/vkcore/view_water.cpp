#include "vkcore/view_water.hpp"

#include "vkcore/job_nodes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_water_impl.hpp"
#include "vkcore/view_water_sites.hpp"
#include "vkcore/view_water_start.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace evr::vkcore {

namespace view_water_detail {

std::atomic<bool> g_live{false};
const std::byte* g_gridResolution = nullptr; // the cvar objects
const std::byte* g_qualityFft = nullptr;

// View 1's copies, by the counter's parity: its job of the frame before may still read the other one.
alignas(16) std::byte g_copies[2][ws::kStateSize];

std::mutex g_mutex; // the records below
View0Record g_view0;
View1Record g_view1;
Grid g_view1Grid; // view 1's own grid matrix of its render before

std::atomic<std::uint64_t> g_view0Ended{0}; // (counter << 1) | 1 of view 0's last setup that ended
std::atomic<bool> g_view0Dispatched{false};
job_nodes::BoundedWait* g_wait = nullptr; // view 1's setup entry only
Counters g_counters;

} // namespace view_water_detail

using namespace view_water_detail;

namespace {

using namespace view_water_sites;

constexpr const char* const kNotInstalled =
    "not installed: view 1's water steps the simulation again and takes view 0's grid matrix, as before";

bool g_checked = false; // every site view_water_sites checks as known before any change
std::int64_t g_waitTicks = 0;
std::atomic<bool> g_differsLogged{false};

} // namespace

namespace view_water_detail {

Indices indicesOf(const std::byte* state) {
    return {read<std::int32_t>(state + kStateIndices), read<std::int32_t>(state + kStateIndices + 4),
            read<std::int32_t>(state + kStateIndices + 8)};
}

Decided decidedOf(const std::byte* context, const std::byte* state) {
    Decided d;
    std::memcpy(d.steps.data(), context + kContextSpectrum, d.steps.size());
    d.indices = indicesOf(state);
    return d;
}

void add(std::atomic<std::uint64_t>& counter) {
    counter.fetch_add(1, std::memory_order_relaxed);
}

const char* startName(ws::Start s) {
    switch (s) {
    case ws::Start::BeforeView0:
        return "before view 0's setup";
    case ws::Start::AfterView0:
        return "after view 0's setup";
    case ws::Start::DuringView0:
        return "while view 0's setup ran";
    case ws::Start::Engine:
        break;
    }
    return "left to the engine";
}

// Under g_mutex, once both views' setups of a frame ended on view 0's start: whether view 1 decided as view 0
// did (the ripple and caustics flags as its setup set them, before they were cleared).
void compare() {
    if (!g_view1.ended || g_view1.compared || !g_view0.ended || g_view1.counter != g_view0.counter ||
        g_view1.start == ws::Start::Engine || g_view0.end.surfaces == 0) {
        return; // with no water in view 0's view its setup decided nothing to compare with
    }
    g_view1.compared = true;
    if (g_view1.decided == g_view0.decided) {
        add(g_counters.same);
        return;
    }
    add(g_counters.differs);
    if (!g_differsLogged.exchange(true)) {
        const Decided& a = g_view0.decided;
        const Decided& b = g_view1.decided;
        EVR_LOG(
            "%s: first frame where view 1's setup decided otherwise than view 0's (counter %u, %s): "
            "spectrum / waves / ripples / caustics %u %u %u %u, indices %d %d %d; view 1's %u %u %u %u, %d "
            "%d %d",
            kTag, g_view1.counter, startName(g_view1.start), a.steps[0], a.steps[1], a.steps[2], a.steps[3],
            a.indices.displacement, a.indices.caustics, a.indices.ripples, b.steps[0], b.steps[1], b.steps[2],
            b.steps[3], b.indices.displacement, b.indices.caustics, b.indices.ripples);
    }
}

std::byte* waterContextOf(std::byte* renderContext) {
    return renderContext ? renderContext + kWaterContext : nullptr;
}

} // namespace view_water_detail

namespace {

void report() {
    const std::uint64_t now = GetTickCount64();
    std::uint64_t last = g_counters.lastReport.load(std::memory_order_relaxed);
    if (now - last < 10000 || !g_counters.lastReport.compare_exchange_strong(last, now)) {
        return;
    }
    const auto take = [](std::atomic<std::uint64_t>& c) {
        return static_cast<unsigned long long>(c.exchange(0));
    };
    auto& c = g_counters;
    using S = ws::Start;
    using L = ws::Left;
    const auto starts = [&](S s) {
        return take(c.starts[static_cast<int>(s)]);
    };
    const auto left = [&](L l) {
        return take(c.left[static_cast<int>(l)]);
    };
    using R = job_nodes::BoundedWait::Result;
    const auto waits = [&](R r) {
        return take(c.waited[static_cast<int>(r)]);
    };
    EVR_LOG(
        "%s: last 10 s: view 1's water from view 0's starting state %llu time(s) before its setup, %llu "
        "after it, %llu while it ran; left to the engine %llu (view 0 not rendered), %llu (another "
        "world), %llu (view 0's next frame begun); grid mesh remakes left to view 0 %llu, made for view 1 "
        "alone %llu; waits for view "
        "0's setup %llu (ran out %llu, off "
        "%llu); view 1's ripple / caustics steps left out %llu / %llu, spectrum rebuilds given to it "
        "%llu, to view 0 %llu; decided as view 0 %llu, otherwise %llu; view 1 stepped the world's water %llu "
        "(view 0 saw "
        "none; %llu more not, its setup ended after view 1's job began; view 1's grid mesh carried into the "
        "world %llu); grid mesh taken at the job %llu; odd %llu",
        kTag, starts(S::BeforeView0), starts(S::AfterView0), starts(S::DuringView0), left(L::NotDispatched),
        left(L::OtherWorld), left(L::Later), take(c.meshLeft), take(c.meshOwn), waits(R::Waited),
        waits(R::TimedOut), waits(R::Off), take(c.ripplesCleared), take(c.causticsCleared), take(c.spectrum),
        take(c.spectrum0), take(c.same), take(c.differs), take(c.alone), take(c.late), take(c.carried),
        take(c.meshes), take(c.odd));
}

// ---- View 0 ----

void onView0Entry(const std::byte* context) {
    const auto* state = read<const std::byte*>(context + kContextState);
    if (!state) {
        return;
    }
    std::lock_guard lock(g_mutex);
    g_view0.counter = read<std::uint32_t>(context + kContextCounter);
    g_view0.state = state;
    g_view0.begun = true;
    g_view0.ended = false;
    g_view0.end = ws::View0End{};
    std::memcpy(g_view0.entry.data(), state, ws::kStateSize);
}

void onView0Exit(std::byte* context) {
    const auto* state = read<const std::byte*>(context + kContextState);
    const auto counter = read<std::uint32_t>(context + kContextCounter);
    std::lock_guard lock(g_mutex);
    if (!g_view0.begun || g_view0.ended || g_view0.counter != counter || g_view0.state != state) {
        return;
    }
    g_view0.ended = true;
    g_view0.decided = decidedOf(context, state);
    g_view0.end.ended = true;
    g_view0.end.surfaces = read<std::int32_t>(context + kContextSurfaces);
    g_view0.end.stepped = g_view0.decided.steps != std::array<std::uint8_t, 4>{};
    g_view0.end.moved = g_view0.decided.indices != indicesOf(g_view0.entry.data());
    g_view0Ended.store((static_cast<std::uint64_t>(counter) << 1) | 1, std::memory_order_release);
    if (g_view1.counter == counter && g_view1.jobStarted && !g_view1.onWorld &&
        ws::view1StepsWorld(g_view0.end, g_view1.surfaces)) {
        add(g_counters.late);
    }
    // View 1's setup ran first and took a "modified" r_waterQualityFFT (it clears the flag): view 0's own
    // spectrum is rebuilt too (its job's FFT reads the flag).
    if (g_view1.ended && g_view1.counter == counter && g_view1.start != ws::Start::Engine &&
        g_view1.decided.steps[0] != 0 && context[kContextSpectrum] == std::byte{0}) {
        context[kContextSpectrum] = std::byte{1};
        add(g_counters.spectrum0);
    }
    compare();
}

void onSetupEntry(const HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire) || !parallelEyesTouch() || r.rcx == 0) {
        return;
    }
    auto* context = reinterpret_cast<std::byte*>(r.rcx);
    std::byte* context1 = waterContextOf(viewSlotsContext1());
    if (!context1) {
        return;
    }
    if (context == context1) {
        onView1Entry(context);
    } else if (context == waterContextOf(viewSlotsContext0())) {
        onView0Entry(context);
    }
}

void onSetupExit(const HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire) || !parallelEyesTouch() || r.rdi == 0) {
        return;
    }
    auto* context = reinterpret_cast<std::byte*>(r.rdi);
    std::byte* context1 = waterContextOf(viewSlotsContext1());
    if (!context1) {
        return;
    }
    if (context == context1) {
        onView1Exit(context);
        report();
    } else if (context == waterContextOf(viewSlotsContext0())) {
        onView0Exit(context);
    }
}

} // namespace

void prepareViewWater(const std::byte* base) {
    g_checked = view_water_sites::known(base);
}

void installViewWater(const std::byte* base) {
    if (!parallelEyesSettings().water) {
        EVR_LOG(
            "%s: off (ETERNALVR_TEST_PE_WATER=0): view 1's water steps the simulation again and takes view "
            "0's grid matrix",
            kTag);
        return;
    }
    if (!g_checked) {
        EVR_LOG("%s: a site was not as known (above); %s", kTag, kNotInstalled);
        return;
    }
    g_gridResolution = ripTarget(base, kGridResolutionRead);
    g_qualityFft = ripTarget(base, kQualityFftRead);
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    // 30 ms: the waits come only on frames that remake the grid mesh or the spectrum (a map's first water
    // frame, a water setting changed), where view 0's setup can take long.
    g_waitTicks = frequency.QuadPart * 30 / 1000;
    static job_nodes::BoundedWait wait(g_waitTicks, 16);
    g_wait = &wait;
    // The end, the job and the mesh first: they act only on a render whose entry set a copy.
    std::string meshError;
    if (!installInlineHook(const_cast<std::byte*>(base + kGridMesh), reinterpret_cast<void*>(&gridMesh),
                           reinterpret_cast<void**>(&g_gridMeshOriginal), meshError)) {
        EVR_LOG("%s: grid mesh hook at RVA 0x%X failed: %s; %s", kTag, kGridMesh, meshError.c_str(),
                kNotInstalled);
        return;
    }
    const std::pair<std::uint32_t, MidHookCallback> hooks[] = {
        {kSetupEnd, &onSetupExit}, {kJobState, &onJobState}, {kSetup, &onSetupEntry}};
    for (const auto& [rva, callback] : hooks) {
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(base + rva), callback, error)) {
            EVR_LOG("%s: hook at RVA 0x%X failed: %s; %s", kTag, rva, error.c_str(), kNotInstalled);
            return;
        }
    }
    g_live.store(true, std::memory_order_release);
    EVR_LOG(
        "%s: hooks at RVA 0x%X (the water setup), 0x%X (its end) and 0x%X (the water job's state): view "
        "1's setup and job work on a copy of the world's water state from view 0's starting state, so the "
        "simulation steps once a frame, both views bind the same ripples and caustics and each keeps its "
        "own grid matrix (r_waterGridResolution and r_waterQualityFFT at RVA 0x%X, 0x%X; "
        "ETERNALVR_TEST_PE_WATER=0 leaves it out)",
        kTag, kSetup, kSetupEnd, kJobState, static_cast<unsigned>(g_gridResolution - base),
        static_cast<unsigned>(g_qualityFft - base));
}

void viewWaterFrameStart(bool view0Dispatched) {
    g_view0Dispatched.store(view0Dispatched, std::memory_order_release);
}

} // namespace evr::vkcore
