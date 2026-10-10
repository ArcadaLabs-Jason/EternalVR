#pragma once

// Parallel Eye Rendering: what view_clones.cpp (the clones and their map), view_clone_binds.cpp (view 1's
// passes store and bind the clones) and view_one_passes.cpp (view 1's own screen pass, environment, light
// scattering wait, shadow atlas skip and tile list pool) share. Build 25216728 (RVAs).

#include "vkcore/clone_census.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/parallel_eyes_settings.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::view_clone {

inline constexpr std::uint32_t kDeviceContext = 0x66E3B88; // [ ] = the device context
inline constexpr std::size_t kRenderContextSize = 0x71B170;
inline constexpr std::size_t kDcSize = 0x680; // a device context (0x1CC6440)
// The screen pass (0x1CDF6E0) draws each view's tone-mapped image into this target ('_swapchain0'); view 1
// draws into its clone instead (view_one_passes.cpp).
inline constexpr std::size_t kDcScreenTarget = 0x508;
// Render target object: colour images at +0x10 (8). Image: its name at +0x8, its GPU image (what binds pass)
// at +0xC8.
inline constexpr std::size_t kTargetColors = 0x10;
inline constexpr std::size_t kImageName = 0x8;
inline constexpr std::size_t kImageHandle = 0xC8;
// The view's light block X = render context + this (shadows, light binning, scattering volumes).
inline constexpr std::size_t kShadowBlock = 0x5226F8;

// The command context table (13 categories of 4): view 1 records into slot 1 of the categories with one
// context, slots 2-3 of the three split ones (view_redirects.cpp). A context's parameter block is at +0x100.
inline constexpr std::uint32_t kCommandTable = 0x667F018;
inline constexpr int kTableCategories = 13;
inline constexpr int kTableSlots = 4;
inline constexpr std::size_t kContextBlock = 0x100;

// Where the engine keeps a clone's object (the build log's summary groups).
enum class Group : std::uint8_t { GlobalImages, GlobalTargets, ContextFields, View1Context };

// A clone the map hands out, as made: an image, or a render target (`target`) that holds image clones.
struct Clone {
    std::uintptr_t image = 0; // the clone
    std::string name;         // an image's name as the engine keeps it
    std::int32_t width = 0;
    std::int32_t height = 0;
    bool states = false; // had its state block when made
    bool target = false;
    Group group = Group::GlobalImages;
    std::string source;     // where the engine keeps its object: "0x66E31F8.c0", "dc+0x568", "dc1+0x2F8"
    std::string engineName; // the engine image's name (a target: its first image's)
    std::uint32_t format = 0;
    std::int32_t depth = 1;
    std::int32_t mips = 1;
    std::int32_t layers = 1;
    std::uint64_t bytes = 0;          // from its options (clone_census::imageBytes)
    std::uint64_t vkBytes = 0;        // vkGetImageMemoryRequirements; 0 when not read
    std::vector<std::uint32_t> parts; // a target's images (indices into the map's clones)
};

// The clone map: engine object or address -> view 1's, published once built (old maps leak: frames in
// flight may still read them).
struct Map {
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> entries; // sorted by the engine's
    std::uintptr_t finalImage = 0;                                  // view 1's post-process final image
    std::vector<Clone> clones;
    std::vector<std::pair<std::size_t, std::uintptr_t>> dcFields; // view 1's device context: offset, value
    std::uintptr_t screenImage = 0;                               // view 1's screen-pass output
    // The census (view_clone_census.cpp): clone -> its index in `clones`, sorted, and the counts,
    // clone_census:: kUses per clone.
    std::vector<std::pair<std::uintptr_t, std::uint32_t>> byClone;
    std::unique_ptr<std::atomic<std::uint64_t>[]> uses;
    // The engine objects the build left out (ETERNALVR_TEST_VIEW_CLONE_SKIP; a target's images follow it,
    // in its parts), by object and with their counts, as the clones'.
    std::vector<Clone> leftOut;
    std::vector<std::pair<std::uintptr_t, std::uint32_t>> byLeftOut;
    std::unique_ptr<std::atomic<std::uint64_t>[]> leftUses;
};

template <typename T>
T read(std::uintptr_t p) {
    T v{};
    std::memcpy(&v, reinterpret_cast<const void*>(p), sizeof(v));
    return v;
}

// The game's image base (set by prepareViewClones).
const std::byte* base();
std::uintptr_t baseAddress();

// The device context the engine uses now, or 0.
std::uintptr_t deviceContext();

// The published map, or null (none yet, or the clones are off).
const Map* currentMap();

// The clone of an engine object or address, or 0.
std::uintptr_t lookup(const Map& map, std::uintptr_t engine);

// View 1's command contexts (`blocks` false) or their parameter blocks (true).
bool isView1Context(std::uintptr_t p, bool blocks);

// The part is not turned off with ETERNALVR_TEST_VIEW_OFF.
bool partOn(parallel_eyes::ViewPart part);

// Installs a hook, logging a failure under the clones' tag.
bool hookAt(std::uint32_t rva, MidHookEditCallback callback, const char* what);
bool watchAt(std::uint32_t rva, MidHookCallback callback, const char* what);

// view_clone_binds.cpp: the store and bind hooks (checkBinds first: every site checked, nothing changed;
// false when one is not as known); their counts.
bool checkBinds();
bool installBinds();
void reportBinds();

// view_one_passes.cpp: view 1's own passes (prepareViewOnePasses first: every site checked and the tile list
// pool's memory taken, nothing of the game changed), and the two bind hooks' parts for them.
bool prepareViewOnePasses();
bool installViewOnePasses();
void notePassTargetBind(HookRegisters& r, const Map* map); // the target bind: view 1's screen pass output
bool swapPassImageBind(HookRegisters& r, const Map* map);  // the image bind: pool and screen pass source
bool screenPassOutputBind(const HookRegisters& r);         // the image bind: view 1's screen pass output
void reportPasses();

// view_clone_census.cpp: each clone's description and log line after a build, and the census of what view 1
// does with them (on unless ETERNALVR_TEST_VIEW_CLONE_LOG=0), reported once 120 s after a build.
void describeImage(Clone& clone, std::uintptr_t engineImage); // name, format, size and bytes of the clone
std::string engineImageName(std::uintptr_t image);
std::uint64_t engineImageBytes(std::uintptr_t image); // from its options (clone_census::imageBytes)
void indexClones(Map& map);                           // byClone, byLeftOut and the counts
void logBuild(const Map& map, int build);
bool censusOn();
// The clone's index in the map, or -1 (not a clone of this map).
int cloneIndex(const Map& map, std::uintptr_t clone);
void countUse(const Map& map, std::uintptr_t clone, clone_census::Use use);
// A view 1 store, bind or mark the map had no clone for: the clone itself (kAsClone) or a left-out object.
void countMiss(const Map& map, std::uintptr_t object, clone_census::Use use);
void reportCensus(const Map& map, int build);

} // namespace evr::vkcore::view_clone
