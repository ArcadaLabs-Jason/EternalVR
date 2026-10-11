#pragma once

// Parallel Eye Rendering: view 1's own stencil view of its depth clone (view_clone_stencil.cpp). Pure logic,
// unit-tested (tests/vkcore/clone_stencil_tests.cpp).
//
// The device context keeps a stencil view of its depth image (dc+0x230, '_viewdepth') at dc+0x438: a handle
// the engine makes with 0x1C49B50(image) ({image, VkImageView of the stencil aspect}) and binds as
// 'viewStencilImage' itself (0x1C21C56, `mov r8, [rdi + 0x438]`), not as an image + 0xC8 the clone binds map.
// The augment highlight's mask compute ('augmentOutlines', 0x1C61810) reads the outlined demons' stencil
// through it into '_frontStencilCopy', so view 1 drew view 0's outline. Each clone build makes view 1 a
// handle of its own over its depth clone and keeps it in the clone map; view 1's binds of the engine's handle
// take it. Every handle made waits in a ledger for the engine's resize, which releases them all (without
// stable names, ETERNALVR_TEST_VIEW_CLONE_NAMES=build, there is no resize hook: they stay, one per build,
// clone_limits::kMaxBuildsPerProcessNewNames at most).

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace evr::vkcore::clone_stencil {

// An image's flags (image +0x9C): bit 0x100, a set of images (the handle's +8 then points at a view each).
// All the engine's release 0x1C4ACE0 reads of the image.
inline constexpr std::size_t kImageFlags = 0x9C;
inline constexpr std::uint32_t kImageSet = 0x100;

// What a clone build does about view 1's stencil view.
enum class Plan : std::uint8_t {
    Make,     // a handle over view 1's depth clone
    Off,      // the sites are not as known, or ETERNALVR_TEST_VIEW_CLONE_SKIP=stencil
    NoView,   // the device context has no stencil view yet
    NotDepth, // its stencil view is not over the device context's depth image
    NoClone,  // the map holds no clone of that image (or the clone has no GPU image)
};

inline Plan plan(bool on,
                 std::uintptr_t engineView,
                 std::uintptr_t engineImage,
                 std::uintptr_t dcDepth,
                 std::uintptr_t clone,
                 std::uintptr_t gpuImage) {
    if (!on) {
        return Plan::Off;
    }
    if (!engineView) {
        return Plan::NoView;
    }
    if (!engineImage || engineImage != dcDepth) {
        return Plan::NotDepth;
    }
    return clone && gpuImage ? Plan::Make : Plan::NoClone;
}

inline const char* planText(Plan p) {
    switch (p) {
    case Plan::Make:
        return "made";
    case Plan::Off:
        return "off";
    case Plan::NoView:
        return "the device context has no stencil view";
    case Plan::NotDepth:
        return "its stencil view is not over its depth image";
    case Plan::NoClone:
        return "no clone of its depth image";
    }
    return "?";
}

// The maker's handle holds a view: its +8 (`viewField`) is the VkImageView, or for a set of images
// (kImageSet) a list whose first is `firstOfSet`. A failed vkCreateImageView leaves it 0.
inline bool viewMade(std::uint32_t imageFlags, std::uintptr_t viewField, std::uintptr_t firstOfSet) {
    return viewField && (!(imageFlags & kImageSet) || firstOfSet);
}

// A handle the release may take: it skips a null view, but reads a set's list at +8 without a check (a set
// whose list was not made keeps its 0x18 bytes).
inline bool releasable(std::uint32_t imageFlags, std::uintptr_t viewField) {
    return viewField || !(imageFlags & kImageSet);
}

// View 1's own handle in a clone map, and what it stands for.
struct Own {
    std::uintptr_t view = 0;       // the handle view 1 binds, 0: none (view 1 binds the engine's)
    std::uintptr_t engineView = 0; // the device context's handle it takes the place of
    std::uintptr_t clone = 0;      // view 1's depth clone it views
    std::uintptr_t gpuImage = 0;   // the clone's GPU image then
};

// View 1 binds `bound`: the handle it binds instead, or 0 (the bind stays). The device context's handle now
// is `engineViewNow`, the clone's GPU image now `gpuNow` (a clone the engine freed and made again is not
// viewed), read without a fault (`gpuRead`: the memory of a clone the engine freed may be gone).
inline std::uintptr_t bindFor(
    const Own& own, std::uintptr_t bound, std::uintptr_t engineViewNow, bool gpuRead, std::uintptr_t gpuNow) {
    if (!own.view || !bound || bound != own.engineView || bound != engineViewNow || !gpuRead ||
        gpuNow != own.gpuImage) {
        return 0;
    }
    return own.view;
}

// The release gets a stand-in for the image a handle was made over (the clone may be gone by then: a map load
// frees the clones and reuses their memory): zeros with the image's flags at kImageFlags.
using StandIn = std::array<std::byte, kImageFlags + sizeof(std::uint32_t)>;

inline StandIn standIn(std::uint32_t imageFlags) {
    StandIn s{};
    for (std::size_t i = 0; i < sizeof(imageFlags); ++i) {
        s[kImageFlags + i] = static_cast<std::byte>((imageFlags >> (8 * i)) & 0xFF);
    }
    return s;
}

// A handle made for view 1, as its release needs it.
struct Made {
    std::uintptr_t view = 0;
    std::uint32_t imageFlags = 0; // the clone's flags then
};

// Every handle made since the engine's last resize, one per build at most (no cap: the builds are bounded,
// clone_limits::kMaxBuildsPerProcess). A map rebuilt without a resize keeps its handle until then: frames in
// flight may still bind it. Without stable names nothing takes them (see the top).
class Ledger {
public:
    void add(std::uintptr_t view, std::uint32_t imageFlags) { made_.push_back({view, imageFlags}); }
    std::size_t size() const { return made_.size(); }
    // At the engine's resize: all of them, the ledger empty.
    std::vector<Made> take() { return std::exchange(made_, {}); }

private:
    std::vector<Made> made_;
};

} // namespace evr::vkcore::clone_stencil
