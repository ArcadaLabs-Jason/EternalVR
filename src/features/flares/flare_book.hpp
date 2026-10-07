#pragma once

// Route S lens flares per eye (docs/VR_STEREO.md, "Lens flares"): the flare models the engine updated in one
// render, kept until that render's eye has latched its view, so each flare's quads can be built again from
// the eye's own matrices (src/vkcore/flare_views.cpp).
//
// The engine builds a flare's quads on the CPU in clip space, once per render, from the world-views latch of
// the game's head-centred view; both eyes then drew them at the head's position. A record holds what the
// update needs to run again over the same vertex block: the block's start, its quad count and the intensity
// the prepare left (the update scales it in place).
//
// No engine, Windows or Vulkan here: models and views are addresses.

#include <cstddef>
#include <cstdint>
#include <span>

namespace evr::flares {

// Each quad is 4 vertices of 0x30 bytes, written in order from the block's start.
inline constexpr std::uintptr_t kQuadBytes = 0xC0;
// The transparency-quad ring holds 0x18000 vertices: no model has more quads than that.
inline constexpr std::int32_t kMaxQuads = 0x18000 / 4;

struct FlareRecord {
    std::uintptr_t model = 0;
    std::uintptr_t vertices = 0; // the vertex block's start (the model's write pointer before the update)
    std::int32_t quads = 0;
    float intensity = 0.0f;     // before the update scaled it
    std::uintptr_t context = 0; // the update's fourth argument, passed again as the engine did
    std::uintptr_t slotA = 0; // the two query slots the engine's update took (its render entity's, after it)
    std::uintptr_t slotB = 0;
};

// A model, a vertex block and a quad count an update can have written.
bool plausible(const FlareRecord& record);

// The write pointer once the update wrote every quad.
std::uintptr_t vertexEnd(const FlareRecord& record);

// A render: the render view the flares were updated for and the world's frame number, which the world
// update raises once per render.
struct RenderKey {
    std::uintptr_t view = 0;
    std::uint32_t frame = 0;
    friend bool operator==(const RenderKey&, const RenderKey&) = default;
};

enum class AddResult {
    Added,
    Duplicate, // the model is already in this render's records: the first stays (its block is the real one)
    Full,      // kCapacity records in this render: the flare keeps the engine's quads
    Late,      // this render was taken already, or a newer render of the view started
};

struct TakeResult {
    std::size_t count = 0;    // records copied out
    bool otherRender = false; // nothing for this render, but another render's records were never taken
};

// The current render's records. Not thread-safe: the caller locks.
class FlareBook {
public:
    static constexpr std::size_t kCapacity = 256;

    // A record of the render `key`; a new render drops the records of the one before.
    AddResult add(const RenderKey& key, const FlareRecord& record);

    // Copies the records of the render `key` into `out` (at most out.size()), once: Route S renders one eye
    // per render, and running the update twice over one block would only repeat the work.
    TakeResult take(const RenderKey& key, std::span<FlareRecord> out);

    // Renders whose records no eye took (mono renders), since the book was made.
    [[nodiscard]] std::uint64_t rendersNotTaken() const { return notTaken_; }

private:
    void startRender(const RenderKey& key);

    RenderKey key_{};
    bool hasKey_ = false;
    bool taken_ = false;
    std::size_t count_ = 0;
    std::uint64_t notTaken_ = 0;
    FlareRecord records_[kCapacity]{};
};

} // namespace evr::flares
