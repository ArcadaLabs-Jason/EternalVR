#pragma once

// The light scattering's temporal history per eye (docs/rig-findings/stereo-scatter.md).
//
// The scattering volume is filtered over time (r_lightScatteringTAA): each render writes one pair of volume
// images and reads the pair the previous render wrote. The engine keeps the two pairs in the device
// context (+0x2D0/+0x2D8 for an even render counter, +0x2E0/+0x2E8 for an odd one: it writes the pair of
// its counter's parity and reads the other) and the filter's state (last frame, volume size, camera
// position, reset count, blend) in one struct. With two renders per tick, eye L always gets one parity and
// eye R the other, so each eye would filter its fog with the other eye's volume, from the other position:
// the stereo set holds the filter off, and without it the volume's coarse cells crawl as the head moves.
//
// Here each eye has two pairs of its own (eye L the engine's four images, eye R four more) and its own copy
// of the state. Before each render the plan puts the eye's last written pair where the engine reads and its
// other pair where it writes, and swaps the state in when the eye changes. Mono renders count as eye L.
// An eye whose own last render is not two renders back (a skipped eye R, a mono stretch, a load) starts
// its history over.

#include "stereo_seq/eye_tags.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace evr::stereo_seq {

struct ScatterPair {
    void* packed0 = nullptr;
    void* packed1 = nullptr;
    friend constexpr bool operator==(ScatterPair, ScatterPair) = default;
};

// The filter's state: the engine's struct from +0x60 to +0x94. `kScatterLastFrame` is the last frame the
// volume was computed for; 0 makes the next compute clear its history first.
inline constexpr std::size_t kScatterStateOffset = 0x60;
inline constexpr std::size_t kScatterStateBytes = 0x34;
inline constexpr std::size_t kScatterLastFrame = 0x70 - kScatterStateOffset;
using ScatterState = std::array<std::byte, kScatterStateBytes>;

struct ScatterPlan {
    // The device context's pairs: [counter & 1] is written this render, the other one read.
    std::array<ScatterPair, 2> slots{};
    // The state to put in the engine's struct before the render (the eye changed).
    std::optional<ScatterState> load;
    // The eye changed and its own last render was not two back: its history starts over.
    bool restarted = false;
};

class ScatterHistory {
public:
    // The engine's pairs as it built them (eye L's) and eye R's; forgets every eye's state.
    void reset(const std::array<ScatterPair, 2>& engine, const std::array<ScatterPair, 2>& eyeR);
    bool ready() const { return ready_; }
    // The volumes were resized (their contents are gone): each eye clears both of its pairs again.
    void invalidate();

    // Before a render of `eye` with the engine's render counter `counter`; `current` is the engine's state as
    // the previous render left it (it belongs to the eye that rendered last).
    ScatterPlan beforeRender(Eye eye, std::uint32_t counter, const ScatterState& current);

private:
    struct PerEye {
        std::array<ScatterPair, 2> pairs{};
        int written = -1; // the pair its last render wrote; -1 before its first
        std::optional<ScatterState> state;
        // Renders that still start with no last frame: the engine then clears the pair it reads (only that
        // one), so each of eye R's new pairs is cleared once before it is filtered; the compute pass does not
        // write every cell of the volume, and a cell never cleared shows as a block of stale light.
        int clears = 0;
        std::optional<std::uint32_t> lastRender; // the render counter of its last render
    };
    std::array<PerEye, 2> eyes_{};
    int active_ = 0; // the eye whose state the engine's struct holds (0 = eye L and mono)
    bool ready_ = false;
};

} // namespace evr::stereo_seq
