#pragma once

// Parallel Eye Rendering: how view_clones.cpp's builds make view 1's clones (view_clone_make.cpp): an image
// clone through the engine's ScratchImage with the engine image's options, a render target clone through the
// engine's constructor and attach over the image clones, and the checks that tell a target from an image and
// find a clone the engine freed. Build 25216728 (RVAs).
//
// An image clone keeps its name from build to build (clone_names.hpp), so a rebuild (a resize, ray tracing
// turned on or off) does not leave the old set allocated beside the new one until the next map load.
//
// A build never frees anything. The engine's purge (0x1C3C540) puts the GPU image and its views on the
// per-frame garbage lists, destroyed three frame begins later, but frees the image's state block (+0x130)
// at once (0x1C4AC5D..0x1C4AC84), which a pass marking the image written (0x1C4A130) writes: a job of the
// frame before still marking a clone would write freed memory. So a build keeps the clone of a name as it
// is when it still fits; allocates it again with ScratchImage under its name only when it holds no memory
// (purged, or freed by a map load), which frees nothing; and else makes it beside under another name
// (besideName) and leaves the old one for later, as it does with the clones it no longer makes (their engine
// image is gone, as the ray-traced reflection images when ray tracing turns off). The purges all happen in
// releaseClones, at the engine's own resize (view_clones.cpp), where the engine frees its own images' state
// blocks next. Render target clones are made anew each build (0x230 bytes each, the old ones left): the
// target destructor destroys their cached framebuffers at once, which only the engine's resize does.

#include "vkcore/view_clone_map.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::view_clone {

// Told apart under SEH: the pointers are the engine's.
enum class Kind { None, Target, Image };
Kind kindOf(std::uintptr_t p);
void sizeOf(std::uintptr_t p, Kind kind, std::int32_t& w, std::int32_t& h);

// Where an object was found, for the log: "0x66E31F8" (a global slot), "dc+0x568" (a device context field).
std::string fieldText(const char* owner, std::size_t offset);

// The bytes of an engine image, or of a render target's images; `name` gets the (first) image's name.
std::uint64_t objectBytes(std::uintptr_t object, std::string& name);

// Under SEH: null while the image clone is still there, else why not (the engine frees unreferenced scratch
// images at a map load and reuses their memory).
const char* cloneGone(const Clone& clone);

// One build's clones: each engine object cloned once, with the clone's record for the map and the log.
struct Builder {
    int build = 0;                                                  // the build's number (the clones' names)
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> entries; // engine object -> clone
    std::vector<Clone> clones;
    int images = 0;
    int targets = 0;
    int failed = 0;
    std::string source; // where the engine keeps the object being cloned (the log's)
    Group group = Group::GlobalImages;
    // Engine images a cloned target keeps (ETERNALVR_TEST_VIEW_CLONE_SKIP).
    std::vector<std::uintptr_t> sharedImages;
    std::string shared;         // what the build left shared, for the log
    std::vector<Clone> leftOut; // ... for the census (Map::leftOut)
    int sharedCount = 0;
    std::uint64_t sharedBytes = 0;
    bool stableNames = true; // false with ETERNALVR_TEST_VIEW_CLONE_NAMES=build: "_evrView1_<build>_<n>"
    std::vector<std::string> names; // this build's image clone names
    int kept = 0;                   // image clones found by name, as they were
    int remade = 0;                 // ... without memory: allocated again under their name
    int made = 0;                   // new images
    int beside = 0;                 // made beside a clone of their name that is allocated at another size

    std::uintptr_t mapped(std::uintptr_t engine) const;
    std::uint32_t indexOf(std::uintptr_t clone) const; // its record, 0xFFFFFFFF for none
    // An engine object left shared (`source`: where the engine keeps it), for the log.
    void noteShared(const std::string& where, std::uintptr_t object);
    // The clone of an engine image (`part` follows `source` in its record), or 0 when the engine made none;
    // one of sharedImages itself.
    std::uintptr_t cloneImage(std::uintptr_t image, const std::string& part = "");
    // The clone of an engine render target, over clones of its images, or 0.
    std::uintptr_t cloneTarget(std::uintptr_t target);

private:
    // The clone named `name` of an earlier build when it fits `image` as it is (its memory, its options and
    // size), or 0.
    std::uintptr_t reuse(const std::string& name, std::uintptr_t image, const std::byte* options);
    // The name to allocate `name`'s clone under without freeing anything: `name` while it holds no memory,
    // else (left for releaseClones) its beside name, or a spare one.
    std::string freeName(const std::string& name);
};

// After a build with stable names: the image clones of the build before that it did not make again wait for
// releaseClones. How many wait now.
int queueOrphans(const Builder& b);

// At the engine's resize only (view_clones.cpp): purges every image clone of `map` and every clone waiting
// (queueOrphans, the ones a build made a clone beside), found alive by their name and only names of ours
// (clone_names::ours). How many; `waiting` gets the names of the waiting ones purged.
int releaseClones(const Map* map, std::string& waiting);

} // namespace evr::vkcore::view_clone
