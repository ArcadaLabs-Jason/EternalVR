#include "vkcore/view_water.hpp"

#include "vkcore/job_nodes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/view_water_sites.hpp"
#include "vkcore/view_water_start.hpp"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <utility>

namespace evr::vkcore {

namespace {

namespace ws = view_water_start;
using namespace view_water_sites;

constexpr const char* kTag = "view-water";

// ---- Engine layouts ----

constexpr std::size_t kWaterContext = 0x705B78; // in the render context
constexpr std::size_t kContextState = 0xE0;
constexpr std::size_t kContextCounter = 0xF0;
constexpr std::size_t kContextSurfaces = 0x104;  // the water surfaces the setup found in view
constexpr std::size_t kContextSpectrum = 0x1124; // the wave spectrum is rebuilt (the FFT reads it)
constexpr std::size_t kContextWaves = 0x1125;
constexpr std::size_t kContextRipples = 0x1126;
constexpr std::size_t kContextCaustics = 0x1127;
static_assert(kContextWaves == kContextSpectrum + 1 && kContextRipples == kContextSpectrum + 2 &&
              kContextCaustics == kContextSpectrum + 3); // the four flags Decided reads at once
constexpr std::size_t kStateGridMesh = 0xC18; // the grid mesh object (0x188 bytes, remade by the setup)
constexpr std::size_t kStateIndices = 0xC30;  // displacement, caustics, ripples
constexpr std::size_t kCvarFlags = 0x40;      // in the cvar's values block
constexpr std::uint32_t kCvarModified = 1u << 18;

constexpr const char* const kNotInstalled =
    "not installed: view 1's water steps the simulation again and takes view 0's grid matrix, as before";

template <typename T>
T read(const std::byte* at) {
    T value{};
    std::memcpy(&value, at, sizeof(value));
    return value;
}

struct Indices {
    std::int32_t displacement = 0;
    std::int32_t caustics = 0;
    std::int32_t ripples = 0;
    friend bool operator==(const Indices&, const Indices&) = default;
};

Indices indicesOf(const std::byte* state) {
    return {read<std::int32_t>(state + kStateIndices), read<std::int32_t>(state + kStateIndices + 4),
            read<std::int32_t>(state + kStateIndices + 8)};
}

// A setup's decisions as it left them: the spectrum, waves, ripples and caustics flags and the indices.
struct Decided {
    std::array<std::uint8_t, 4> steps{};
    Indices indices;
    friend bool operator==(const Decided&, const Decided&) = default;
};

Decided decidedOf(const std::byte* context, const std::byte* state) {
    Decided d;
    std::memcpy(d.steps.data(), context + kContextSpectrum, d.steps.size());
    d.indices = indicesOf(state);
    return d;
}

std::atomic<bool> g_live{false};
bool g_checked = false;                      // every site view_water_sites checks as known before any change
const std::byte* g_gridResolution = nullptr; // the cvar objects
const std::byte* g_qualityFft = nullptr;

// View 1's copies, by the counter's parity: its job of the frame before may still read the other one.
alignas(16) std::byte g_copies[2][ws::kStateSize];

std::mutex g_mutex; // the records below
struct View0Record {
    std::uint32_t counter = 0;
    const std::byte* state = nullptr; // the world's
    bool begun = false;
    bool ended = false;
    std::array<std::byte, ws::kStateSize> entry{};
    Decided decided; // at its end
    ws::View0End end;
};
View0Record g_view0;
struct View1Record {
    const std::byte* context = nullptr; // null: no render of view 1 on a copy now
    std::uint32_t counter = 0;
    std::byte* world = nullptr;
    std::byte* copy = nullptr;
    ws::Start start = ws::Start::Engine;
    bool ended = false;
    bool compared = false;
    Decided decided; // at its end, the ripple and caustics flags as its setup set them
    std::int32_t surfaces = 0;
    bool jobStarted = false;
    bool waitedOut = false; // its entry's wait for view 0's setup ran out
    bool onWorld = false; // view 0 saw no water: view 1's steps stayed and its job runs on the world's state
};
View1Record g_view1;
struct Grid {
    const std::byte* world = nullptr; // the world state the matrix is for
    std::uint32_t counter = 0;        // the frame it was made in
    std::array<std::byte, ws::kGridMatrix.size> matrix{};
};
Grid g_view1Grid; // view 1's own grid matrix of its render before

std::atomic<std::uint64_t> g_view0Ended{0}; // (counter << 1) | 1 of view 0's last setup that ended
std::atomic<bool> g_view0Dispatched{false};
std::int64_t g_waitTicks = 0;
job_nodes::BoundedWait* g_wait = nullptr; // view 1's setup entry only

struct Counters {
    std::array<std::atomic<std::uint64_t>, 4> starts{}; // by ws::Start
    std::array<std::atomic<std::uint64_t>, 5> left{};   // by ws::Left (None unused)
    std::array<std::atomic<std::uint64_t>, 4> waited{}; // by BoundedWait::Result
    std::atomic<std::uint64_t> ripplesCleared{0};       // view 1's ripple steps left out
    std::atomic<std::uint64_t> causticsCleared{0};      // view 1's caustics steps left out
    std::atomic<std::uint64_t> spectrum{0};             // view 0's spectrum rebuild given to view 1
    std::atomic<std::uint64_t> same{0};                 // view 1 decided as view 0 did
    std::atomic<std::uint64_t> differs{0};              // ... or not
    std::atomic<std::uint64_t> meshes{0};               // view 1's job took the world's new grid mesh
    std::atomic<std::uint64_t> alone{0};                // view 1 stepped the world's water: view 0 saw none
    std::atomic<std::uint64_t> late{0};      // ... but view 0's setup ended after view 1's job began
    std::atomic<std::uint64_t> meshLeft{0};  // view 1's setup left the grid mesh's remake to view 0
    std::atomic<std::uint64_t> meshOwn{0};   // ... or made one into its copy (the world had none)
    std::atomic<std::uint64_t> spectrum0{0}; // view 1's spectrum rebuild given to view 0
    std::atomic<std::uint64_t> odd{0};       // the context's state was not the copy at the end
    std::atomic<std::uint64_t> lastReport{0};
} g_counters;
std::atomic<std::uint32_t> g_firstStarts{0}; // a bit per ws::Start logged once
std::atomic<bool> g_differsLogged{false};

void add(std::atomic<std::uint64_t>& counter) {
    counter.fetch_add(1, std::memory_order_relaxed);
}

std::int64_t ticks() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

bool modified(const std::byte* cvar) {
    const auto* values = cvar ? read<const std::byte*>(cvar) : nullptr;
    return values && (read<std::uint32_t>(values + kCvarFlags) & kCvarModified) != 0;
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
        "none; %llu more not, its setup ended after view 1's job began); grid mesh taken at the job "
        "%llu; odd %llu",
        kTag, starts(S::BeforeView0), starts(S::AfterView0), starts(S::DuringView0), left(L::NotDispatched),
        left(L::OtherWorld), left(L::Later), take(c.meshLeft), take(c.meshOwn), waits(R::Waited),
        waits(R::TimedOut), waits(R::Off), take(c.ripplesCleared), take(c.causticsCleared), take(c.spectrum),
        take(c.spectrum0), take(c.same), take(c.differs), take(c.alone), take(c.late), take(c.meshes),
        take(c.odd));
}

std::byte* waterContextOf(std::byte* renderContext) {
    return renderContext ? renderContext + kWaterContext : nullptr;
}

bool isCopy(const std::byte* p) {
    const auto at = reinterpret_cast<std::uintptr_t>(p);
    const auto from = reinterpret_cast<std::uintptr_t>(&g_copies[0][0]);
    return at >= from && at < from + sizeof(g_copies);
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

// Under g_mutex, at view 1's setup end and at its job's state read: when view 0's setup ended this frame with
// no water in view (ws::view1StepsWorld), view 1 steps the world's water. Its ripple and caustics flags come
// back, the fields its setup wrote into the copy go into the world's state, and its job reads the world's.
// True once that is so for this render.
bool takeWorld(std::byte* context) {
    if (g_view1.onWorld) {
        return true;
    }
    const bool view0 = g_view0.ended && g_view0.counter == g_view1.counter && g_view0.state == g_view1.world;
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
    return true;
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
// mesh; the make then runs only when nothing is left to remake, or when the world has no mesh at all (view 1
// then makes one into its copy).
using GridMeshFn = void (*)(std::byte* state);
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
        std::memcpy(state + kStateGridMesh, &mesh, sizeof(mesh));
    }
    if (mesh != 0 && modified(g_gridResolution)) {
        add(g_counters.meshLeft); // view 0's setup remakes it; view 1's job takes the new one
        return;
    }
    if (mesh == 0) {
        add(g_counters.meshOwn);
    } else if (pending) {
        add(g_counters.meshLeft);
    }
    g_gridMeshOriginal(state);
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
