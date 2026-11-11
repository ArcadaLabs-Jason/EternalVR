#pragma once

// Internal to Parallel Eye Rendering's water (view_water.hpp), shared by view_water.cpp (view 0's setup, the
// hooks' dispatch and install, the counters) and view_water_view1.cpp (view 1's setup and job on its copy of
// the world's water state, the grid mesh).

#include "vkcore/job_nodes.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/view_water_start.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace evr::vkcore::view_water_detail {

namespace ws = view_water_start;

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
              kContextCaustics == kContextSpectrum + 3);      // the four flags Decided reads at once
constexpr std::size_t kStateGridMesh = ws::kGridMeshField.at; // the grid mesh object (0x188 bytes)
constexpr std::size_t kStateIndices = 0xC30;                  // displacement, caustics, ripples
constexpr std::size_t kCvarFlags = 0x40;                      // in the cvar's values block
constexpr std::uint32_t kCvarModified = 1u << 18;

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

// A setup's decisions as it left them: the spectrum, waves, ripples and caustics flags and the indices.
struct Decided {
    std::array<std::uint8_t, 4> steps{};
    Indices indices;
    friend bool operator==(const Decided&, const Decided&) = default;
};

struct View0Record {
    std::uint32_t counter = 0;
    const std::byte* state = nullptr; // the world's
    bool begun = false;
    bool ended = false;
    std::array<std::byte, ws::kStateSize> entry{};
    Decided decided; // at its end
    ws::View0End end;
};
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
struct Grid {
    const std::byte* world = nullptr; // the world state the matrix is for
    std::uint32_t counter = 0;        // the frame it was made in
    std::array<std::byte, ws::kGridMatrix.size> matrix{};
};

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
    std::atomic<std::uint64_t> carried{0}; // of those, the world took the grid mesh view 1 made (it had none)
    std::atomic<std::uint64_t> late{0};    // ... view 0 saw none but its setup ended after view 1's job began
    std::atomic<std::uint64_t> meshLeft{0};  // view 1's setup left the grid mesh's remake to view 0
    std::atomic<std::uint64_t> meshOwn{0};   // ... or made one into its copy (the world had none)
    std::atomic<std::uint64_t> spectrum0{0}; // view 1's spectrum rebuild given to view 0
    std::atomic<std::uint64_t> odd{0};       // the context's state was not the copy at the end
    std::atomic<std::uint64_t> lastReport{0};
};

// ---- State (view_water.cpp) ----

extern std::atomic<bool> g_live;
extern const std::byte* g_gridResolution;
extern const std::byte* g_qualityFft;
alignas(16) extern std::byte g_copies[2][ws::kStateSize];
extern std::mutex g_mutex;
extern View0Record g_view0;
extern View1Record g_view1;
extern Grid g_view1Grid;
extern std::atomic<std::uint64_t> g_view0Ended;
extern std::atomic<bool> g_view0Dispatched;
extern job_nodes::BoundedWait* g_wait;
extern Counters g_counters;

// ---- Helpers (view_water.cpp) ----

Indices indicesOf(const std::byte* state);
Decided decidedOf(const std::byte* context, const std::byte* state);
void add(std::atomic<std::uint64_t>& counter);
const char* startName(ws::Start s);
void compare();
std::byte* waterContextOf(std::byte* renderContext);

// ---- View 1 (view_water_view1.cpp) ----

void onView1Entry(std::byte* context);
void onView1Exit(std::byte* context);
void onJobState(const HookRegisters& r);
using GridMeshFn = void (*)(std::byte* state);
extern GridMeshFn g_gridMeshOriginal;
void gridMesh(std::byte* state);

} // namespace evr::vkcore::view_water_detail
