// Parallel Eye Rendering's water, view 1's side (view_water.hpp): its setup and job work on a copy of the
// world's water state started from view 0's, wait for view 0's setup where it remakes the grid mesh, leave
// the world's images to view 0 and take its grid mesh, or step the world's water when view 0 saw none.

#include "vkcore/view_water_impl.hpp"

#include "vkcore/job_nodes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_water_start.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>

namespace evr::vkcore {

using namespace view_water_detail;

namespace {

std::atomic<std::uint32_t> g_firstStarts{0}; // a bit per ws::Start logged once

std::int64_t ticks() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

bool modified(const std::byte* cvar) {
    const auto* values = cvar ? read<const std::byte*>(cvar) : nullptr;
    return values && (read<std::uint32_t>(values + kCvarFlags) & kCvarModified) != 0;
}

bool isCopy(const std::byte* p) {
    const auto at = reinterpret_cast<std::uintptr_t>(p);
    const auto from = reinterpret_cast<std::uintptr_t>(&g_copies[0][0]);
    return at >= from && at < from + sizeof(g_copies);
}

// ---- View 1 ----

// Outside g_mutex: waits (at most 30 ms) for view 0's setup of `counter` to end. False when it did not.
bool waitForView0(std::uint32_t counter) {
    const std::uint64_t mark = (static_cast<std::uint64_t>(counter) << 1) | 1;
    const auto result = g_wait->wait([&] { return g_view0Ended.load(std::memory_order_acquire) == mark; },
                                     ticks, [] { YieldProcessor(); });
    add(g_counters.waited[static_cast<int>(result)]);
    if (result == job_nodes::BoundedWait::Result::TimedOut && g_wait->off()) {
        EVR_LOG(
            "%s: view 0's water setup did not end within 30 ms of view 1's in 16 frames in a row; view 1's "
            "no longer waits for it (it still leaves the grid mesh's remake to view 0)",
            kTag);
    }
    return result == job_nodes::BoundedWait::Result::Ready ||
           result == job_nodes::BoundedWait::Result::Waited;
}

// Under g_mutex, at view 1's setup end and at its job's state read: when view 0's setup ended this frame with
// no water in view (ws::view1StepsWorld), view 1 steps the world's water. Its ripple and caustics flags come
// back, the fields its setup wrote into the copy go into the world's state, and its job reads the world's.
// Eye R seeing a map's water first leaves the world with no grid mesh: it takes the one view 1's setup made
// into the copy (ws::writeBack). True once that is so for this render.
bool takeWorld(std::byte* context) {
    if (g_view1.onWorld) {
        return true;
    }
    const bool view0 = g_view0.ended && g_view0.counter == g_view1.counter && g_view0.state == g_view1.world;
    const bool carried = g_view1.world && read<std::uintptr_t>(g_view1.world + kStateGridMesh) == 0;
    if (!g_view1.ended || !view0 || !ws::view1StepsWorld(g_view0.end, g_view1.surfaces) ||
        !ws::writeBack(std::span<std::byte>(g_view1.world, ws::kStateSize),
                       std::span<const std::byte>(g_view1.copy, ws::kStateSize))) {
        return false;
    }
    context[kContextRipples] = std::byte{g_view1.decided.steps[2]};
    context[kContextCaustics] = std::byte{g_view1.decided.steps[3]};
    std::memcpy(context + kContextState, &g_view1.world, sizeof(g_view1.world));
    g_view1.onWorld = true;
    add(g_counters.alone);
    g_counters.carried.fetch_add(carried ? 1 : 0, std::memory_order_relaxed);
    return true;
}

} // namespace

namespace view_water_detail {

void onView1Entry(std::byte* context) {
    auto* world = read<std::byte*>(context + kContextState);
    const auto counter = read<std::uint32_t>(context + kContextCounter);
    if (!world || isCopy(world)) {
        add(g_counters.odd);
        return;
    }
    const bool remade = read<const std::byte*>(world + kStateGridMesh) == nullptr ||
                        modified(g_gridResolution) || modified(g_qualityFft);
    const auto seen = [&] {
        ws::View0 v;
        v.dispatched = g_view0Dispatched.load(std::memory_order_acquire) && viewSlotsContext0();
        v.begun = g_view0.begun && g_view0.counter == counter;
        v.ended = v.begun && g_view0.ended;
        v.sameWorld = v.begun && g_view0.state == world;
        // Wrap-safe: view 0's last setup began for a counter after this one.
        v.later = g_view0.begun && static_cast<std::int32_t>(g_view0.counter - counter) > 0;
        return v;
    };
    ws::View0 before;
    {
        std::lock_guard lock(g_mutex);
        before = seen();
    }
    const bool waitedOut = ws::shouldWait(before, remade) && !waitForView0(counter);
    std::lock_guard lock(g_mutex);
    const ws::Decision d = ws::decide(seen());
    g_view1 = View1Record{};
    g_view1.waitedOut = waitedOut;
    if (d.start == ws::Start::Engine) {
        add(g_counters.left[static_cast<int>(d.left)]);
        return;
    }
    std::byte* copy = g_copies[counter & 1];
    const bool fromView0 = d.start != ws::Start::BeforeView0;
    const bool ownGrid = ws::ownGridUsable(g_view1Grid.world == world, g_view1Grid.counter, counter);
    if (!ws::startState(
            std::span<std::byte>(copy, ws::kStateSize), std::span<const std::byte>(world, ws::kStateSize),
            fromView0 ? std::span<const std::byte>(g_view0.entry) : std::span<const std::byte>(),
            ownGrid ? std::span<const std::byte>(g_view1Grid.matrix) : std::span<const std::byte>())) {
        add(g_counters.odd);
        return;
    }
    std::memcpy(context + kContextState, &copy, sizeof(copy));
    g_view1.context = context;
    g_view1.counter = counter;
    g_view1.world = world;
    g_view1.copy = copy;
    g_view1.start = d.start;
    add(g_counters.starts[static_cast<int>(d.start)]);
    const std::uint32_t bit = 1u << static_cast<int>(d.start);
    if (!(g_firstStarts.fetch_or(bit, std::memory_order_relaxed) & bit)) {
        const Indices i = indicesOf(copy);
        EVR_LOG("%s: first view 1 water render on its copy, started %s: counter %u, world state %p, copy %p, "
                "indices %d %d %d, its own grid matrix %s",
                kTag, startName(d.start), counter, static_cast<const void*>(world), static_cast<void*>(copy),
                i.displacement, i.caustics, i.ripples, ownGrid ? "yes" : "not yet (view 0's starting one)");
    }
}

void onView1Exit(std::byte* context) {
    const auto counter = read<std::uint32_t>(context + kContextCounter);
    std::lock_guard lock(g_mutex);
    if (g_view1.context != context || g_view1.counter != counter || g_view1.ended) {
        return;
    }
    std::byte* copy = g_view1.copy;
    if (read<std::byte*>(context + kContextState) != copy) {
        add(g_counters.odd);
        g_view1.context = nullptr;
        return;
    }
    g_view1.decided = decidedOf(context, copy);
    g_view1.surfaces = read<std::int32_t>(context + kContextSurfaces);
    g_view1.ended = true;
    std::memcpy(g_view1Grid.matrix.data(), copy + ws::kGridMatrix.at, ws::kGridMatrix.size);
    g_view1Grid.world = g_view1.world;
    g_view1Grid.counter = counter;
    // The ripples and the caustics are the world's images: view 0's job steps them.
    if (context[kContextRipples] != std::byte{0}) {
        context[kContextRipples] = std::byte{0};
        add(g_counters.ripplesCleared);
    }
    if (context[kContextCaustics] != std::byte{0}) {
        context[kContextCaustics] = std::byte{0};
        add(g_counters.causticsCleared);
    }
    // View 0's setup consumed a "modified" r_waterQualityFFT with parameters that did not change: view 1's
    // own spectrum images are rebuilt too.
    if (g_view0.ended && g_view0.counter == counter && g_view0.decided.steps[0] != 0 &&
        context[kContextSpectrum] == std::byte{0}) {
        context[kContextSpectrum] = std::byte{1};
        add(g_counters.spectrum);
    }
    takeWorld(context);
    compare();
}

// The job's read of the state (r14 the context): view 1's copy takes the world's grid mesh, which view 0's
// setup may have remade since the copy was made.
void onJobState(const HookRegisters& r) {
    if (!g_live.load(std::memory_order_acquire) || !parallelEyesTouch()) {
        return;
    }
    auto* context = reinterpret_cast<std::byte*>(r.r14);
    if (!context || context != waterContextOf(viewSlotsContext1())) {
        return;
    }
    std::lock_guard lock(g_mutex);
    std::byte* copy = read<std::byte*>(context + kContextState);
    if (!g_view1.context || copy != g_view1.copy || !g_view1.world) {
        return;
    }
    g_view1.jobStarted = true;
    if (takeWorld(context)) {
        return; // the instruction hooked reads the world's state now
    }
    const auto mesh = read<std::uintptr_t>(g_view1.world + kStateGridMesh);
    if (mesh != 0 && read<std::uintptr_t>(copy + kStateGridMesh) != mesh) {
        std::memcpy(copy + kStateGridMesh, &mesh, sizeof(mesh));
        add(g_counters.meshes);
    }
}

// The grid mesh's (re)make (0x1CE3430, from the setup only). It frees the state's mesh and makes a new one
// when the state has none or r_waterGridResolution is "modified" (and clears the flag). On view 1's copy the
// mesh is the world's: view 1 never remakes it, so view 0's setup cannot free a mesh view 1's copy holds, nor
// both free it. View 1 waits for view 0's setup to end (at most 30 ms, once a render) and takes the world's
// mesh; the make then runs only when the world has no mesh at all (view 1 then makes one into its copy, which
// the world takes when view 1 steps the world's water: takeWorld).
GridMeshFn g_gridMeshOriginal = nullptr;

void gridMesh(std::byte* state) {
    if (!g_live.load(std::memory_order_acquire) || !isCopy(state)) {
        g_gridMeshOriginal(state);
        return;
    }
    std::uint32_t counter = 0;
    bool waitedOut = false;
    {
        std::lock_guard lock(g_mutex);
        counter = g_view1.counter;
        waitedOut = g_view1.copy != state || g_view1.waitedOut;
    }
    const bool pending =
        read<const std::byte*>(state + kStateGridMesh) == nullptr || modified(g_gridResolution);
    if (pending && !waitedOut) {
        waitForView0(counter);
    }
    std::lock_guard lock(g_mutex);
    const auto mesh = g_view1.world ? read<std::uintptr_t>(g_view1.world + kStateGridMesh) : 0;
    if (mesh != 0) {
        // Never the make on the world's mesh from the copy: with the cvar "modified" since, it would free it.
        std::memcpy(state + kStateGridMesh, &mesh, sizeof(mesh));
        if (pending || modified(g_gridResolution)) {
            add(g_counters.meshLeft);
        }
        return;
    }
    add(g_counters.meshOwn);
    g_gridMeshOriginal(state);
}

} // namespace view_water_detail

} // namespace evr::vkcore
