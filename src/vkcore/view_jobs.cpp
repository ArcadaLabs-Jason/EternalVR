// Engine jobs Parallel Eye Rendering wraps (view_jobs.hpp): frame work for view 0 only, and jobs that take
// turns.

#include "vkcore/view_jobs.hpp"

#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_slots.hpp"

#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "view-redirects";

// View 1's MVP-culling command context (the command context table's slot 1 of that category,
// view_redirects.cpp).
void* const* g_mvpContext1 = nullptr;

// ---- Jobs for both views at once: view 0 only ----

// Skinning (0x1C58C40, 0x1C59130 -> 0x1BF2680 on the skinning manager 0x5BF1F10): both views draw the same
// tick, so view 0's skinning serves both (view 1's consumers do not wait for it). Ray
// tracing: 0x1C58D30 builds the blended BLASes in the engine's one static pool (0x5D63F20, no lock) into
// the shared async GPU Particles context; two views at once freed the same geometry lists, destroyed the same
// structures twice and recorded one command buffer from two threads. Its TLAS jobs 0x1C58EC0 and 0x1C59160
// build into the render world, which both views trace.
// The per-view chain also runs frame work once per view, on state of the world's: the GPU particle simulation
// (0x1C25DD0 -> 0x1C2A630 on the world's particle manager: run twice it integrated the particles twice and
// rewrote the buffers view 0's draws read, the cyan shards of a big emission), the geometry-cache upload
// (0x1943730 -> 0x1945520: each run flips its frame halves, so two runs left every frame on the half the GPU
// still read), the pending-work drain (0x1D26220 -> 0x1D265C0: shared lists recorded into whichever view ran
// first). docs/rig-findings/pe-frame-jobs.md.
// View 1's jobs return at once.
constexpr std::size_t kMvpContext = 0; // the job's parameter is the view's MVP-culling command context
struct View0Job {
    std::uint32_t rva;
    std::size_t block; // the job's parameter: render context + block, or kMvpContext
};
constexpr View0Job kView0Jobs[] = {
    {0x1C58C40, 0x6FC858},    {0x1C59130, 0x6FC858},    // skinning
    {0x1C58D30, 0x701898},                              // blended BLAS
    {0x1C58EC0, 0x7038E0},    {0x1C59160, 0x7038C0},    // blended and opaque TLAS
    {0x1C25DD0, 0x6A7DD0},                              // GPU particle simulation
    {0x1943730, kMvpContext}, {0x1D26220, kMvpContext}, // geometry-cache upload, pending-work drain
};
using JobFn = void (*)(void* param);
JobFn g_view0Jobs[std::size(kView0Jobs)] = {};
std::atomic<std::uint64_t> g_view1Skips[std::size(kView0Jobs)] = {};

template <std::size_t I>
void view0Job(void* param) {
    const auto* context1 = viewSlotsContext1();
    const bool view1 = kView0Jobs[I].block == kMvpContext
                           ? param != nullptr && param == *g_mvpContext1
                           : context1 && param == context1 + kView0Jobs[I].block;
    if (view1 && parallelEyesTouch()) {
        g_view1Skips[I].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_view0Jobs[I](param);
}
constexpr JobFn kView0Wrappers[] = {&view0Job<0>, &view0Job<1>, &view0Job<2>, &view0Job<3>,
                                    &view0Job<4>, &view0Job<5>, &view0Job<6>, &view0Job<7>};
static_assert(std::size(kView0Wrappers) == std::size(kView0Jobs));

// ---- Jobs with the renderer's scratch: one view at a time ----

// Job 0x1C58050 (queued by the render-view job with the record at 0x39A1D50) builds its surfaces in a static
// list (RVA 0x39A3640..0x39A3658). Job 0x1D2D3B0 copies the view's parameters into one global object whose
// list at +0x178 it reassigns (0x6031D0): two views at once freed its old buffer twice when a new world's
// first frames resized it (heap failure 'block not busy', run fh10). The two views' jobs take turns.
// Installed means locked (uncontended with one view).
constexpr std::uint32_t kSerialJobs[] = {0x1C58050, 0x1D2D3B0};
JobFn g_serialJobs[std::size(kSerialJobs)] = {};
std::mutex g_serialJobMutex[std::size(kSerialJobs)];

template <std::size_t I>
void serialJob(void* param) {
    if (!viewSlotsActive()) {
        g_serialJobs[I](param);
        return;
    }
    std::lock_guard lock(g_serialJobMutex[I]);
    g_serialJobs[I](param);
}
constexpr JobFn kSerialWrappers[] = {&serialJob<0>, &serialJob<1>};
static_assert(std::size(kSerialWrappers) == std::size(kSerialJobs));

} // namespace

bool installViewJobs(const std::byte* base, void* const* mvpContext1) {
    g_mvpContext1 = mvpContext1;
    // Serial jobs first, then view 0's.
    for (std::size_t i = 0; i < std::size(kSerialJobs) + std::size(kView0Jobs); ++i) {
        const bool serial = i < std::size(kSerialJobs);
        const std::size_t j = serial ? i : i - std::size(kSerialJobs);
        const std::uint32_t rva = serial ? kSerialJobs[j] : kView0Jobs[j].rva;
        std::string error;
        if (!installInlineHook(const_cast<std::byte*>(base + rva),
                               reinterpret_cast<void*>(serial ? kSerialWrappers[j] : kView0Wrappers[j]),
                               reinterpret_cast<void**>(serial ? &g_serialJobs[j] : &g_view0Jobs[j]),
                               error)) {
            EVR_LOG("%s: job hook at RVA 0x%X failed: %s", kTag, rva, error.c_str());
            return false;
        }
    }
    return true;
}

void viewJobsLogCounts() {
    EVR_LOG(
        "%s: view 1 skipped: skinning %llu + %llu, blended BLAS %llu, blended TLAS %llu, opaque TLAS %llu, "
        "particles %llu, geometry cache %llu, drain %llu",
        kTag, static_cast<unsigned long long>(g_view1Skips[0].load()),
        static_cast<unsigned long long>(g_view1Skips[1].load()),
        static_cast<unsigned long long>(g_view1Skips[2].load()),
        static_cast<unsigned long long>(g_view1Skips[3].load()),
        static_cast<unsigned long long>(g_view1Skips[4].load()),
        static_cast<unsigned long long>(g_view1Skips[5].load()),
        static_cast<unsigned long long>(g_view1Skips[6].load()),
        static_cast<unsigned long long>(g_view1Skips[7].load()));
}

} // namespace evr::vkcore
