#pragma once

// Parallel Eye Rendering: the water state view 1's setup and job work on (view_water.hpp). Pure logic,
// unit-tested (tests/vkcore/view_water_start_tests.cpp).
//
// The world keeps one water state (render world + 0xB4550, 0xC90 bytes). Each view's water setup decides the
// frame's simulation steps from it and moves its indices on; each view's water job steps what its setup
// asked for. View 1 gets a copy of the state instead: the world's as it is, with the fields the setup
// decides from and moves on taken from the state view 0's setup started from this frame (when view 0's setup
// already began: before it, the world's are that state), and the grid matrix of view 1's own render before.
// View 1 then takes the steps and binds the images view 0 does, and the world's state is moved on once a
// frame, by view 0.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace evr::vkcore::view_water_start {

inline constexpr std::size_t kStateSize = 0xC90;

struct Range {
    std::size_t at;
    std::size_t size;
};

// The fields the setup decides the steps from and moves on: the wave parameters it compares with the cvars
// (+0x0, five floats), the frames the waves, the world, the caustics and the ripples last stepped in and the
// displacement, caustics and ripple indices (+0xC20, seven ints), and the ripples' grid camera (+0xC88).
inline constexpr Range kStartRanges[] = {{0x0, 0x14}, {0xC20, 0x1C}, {0xC88, 0x8}};
// The grid's view-projection matrix of the render before (four rows of four floats): the setup binds it as
// the previous one, then stores this render's over it.
inline constexpr Range kGridMatrix = {0xC48, 0x40};

// Where view 1's render takes its start from.
enum class Start : std::uint8_t {
    BeforeView0, // view 0's setup has not begun this frame: the world's state is the frame's start
    AfterView0,  // view 0's setup ended: its starting state, from its entry
    DuringView0, // view 0's setup is running: its starting state, from its entry
    Engine,      // left to the engine (`Left` says why): view 1's setup works on the world's state
};

enum class Left : std::uint8_t {
    None,
    NotDispatched, // view 0 is not rendered this frame (ETERNALVR_TEST_VIEW_ONLY=1): view 1 steps alone
    OtherWorld,    // view 0's setup this frame was for another world's state
    Later,         // view 0's setup of a later frame already began (the world's state is no frame's start)
};

// What view 1's setup entry knows of view 0's this frame.
struct View0 {
    bool dispatched = false; // view 0 is rendered this frame
    bool begun = false;      // its setup began for this frame's counter
    bool ended = false;      // ... and ended
    bool sameWorld = false;  // ... for the world state view 1's render has
    bool later = false;      // its last setup began for a later counter than view 1's render has
};

struct Decision {
    Start start = Start::Engine;
    Left left = Left::None;
};

// Whether view 1's setup should wait (bounded) for view 0's to end before deciding: only when the setup would
// remake shared engine objects (`remade`: the grid mesh is missing, r_waterGridResolution or
// r_waterQualityFFT changed) and view 0's has not done it yet. If the wait runs out view 1 still starts on
// its copy: view 1 never remakes the world's grid mesh (view_water.cpp), and it takes view 0's at its job.
inline constexpr bool shouldWait(const View0& v, bool remade) {
    return v.dispatched && remade && !v.ended && !v.later;
}

inline constexpr Decision decide(const View0& v) {
    if (!v.dispatched) {
        return {Start::Engine, Left::NotDispatched};
    }
    if (v.later) {
        return {Start::Engine, Left::Later};
    }
    if (v.begun && !v.sameWorld) {
        return {Start::Engine, Left::OtherWorld};
    }
    if (!v.begun) {
        return {Start::BeforeView0, Left::None};
    }
    return {v.ended ? Start::AfterView0 : Start::DuringView0, Left::None};
}

// View 1's copy: `world` as it is, the start ranges from `view0Entry` (the state view 0's setup found; empty
// before it began) and the grid matrix from `ownGrid` (view 1's own render before; empty when it has none,
// then view 0's starting one, else the world's). Every span but the empty ones is kStateSize bytes, the grid
// one kGridMatrix.size. False (nothing written) for spans of other sizes.
inline bool startState(std::span<std::byte> copy,
                       std::span<const std::byte> world,
                       std::span<const std::byte> view0Entry,
                       std::span<const std::byte> ownGrid) {
    if (copy.size() != kStateSize || world.size() != kStateSize ||
        (!view0Entry.empty() && view0Entry.size() != kStateSize) ||
        (!ownGrid.empty() && ownGrid.size() != kGridMatrix.size)) {
        return false;
    }
    std::memcpy(copy.data(), world.data(), kStateSize);
    if (!view0Entry.empty()) {
        for (const Range& r : kStartRanges) {
            std::memcpy(copy.data() + r.at, view0Entry.data() + r.at, r.size);
        }
        std::memcpy(copy.data() + kGridMatrix.at, view0Entry.data() + kGridMatrix.at, kGridMatrix.size);
    }
    if (!ownGrid.empty()) {
        std::memcpy(copy.data() + kGridMatrix.at, ownGrid.data(), kGridMatrix.size);
    }
    return true;
}

// Whether view 1's own grid matrix (made in frame `gridCounter` for the same world state, `sameWorld`) is its
// render before for frame `counter`: only from the frame just before. A map load (frames without a render in
// between, a new world at the same address) or a frame left to the engine makes view 1 take view 0's starting
// matrix instead, as a map load resets the engine's own (wrap-safe).
inline constexpr bool ownGridUsable(bool sameWorld, std::uint32_t gridCounter, std::uint32_t counter) {
    return sameWorld && static_cast<std::uint32_t>(counter - gridCounter) == 1;
}

// The fields the setup itself writes into the state (the wave parameters, the indices, the ripple camera);
// the jobs write the last-step frames.
inline constexpr Range kSetupWrites[] = {{0x0, 0x14}, {0xC30, 0xC}, {0xC88, 0x8}};

// View 0's setup as it ended this frame.
struct View0End {
    bool ended = false;        // for this frame's counter and view 1's world state
    std::int32_t surfaces = 0; // the water surfaces it found in view (context + 0x104)
    bool stepped = false;      // it asked its job for any step (context + 0x1124..+0x1127)
    bool moved = false;        // its indices differ from those it started from
};

// Whether view 1 steps the world's water this frame: view 0's setup ended with no water in view, so it
// stepped nothing and moved nothing (the setup leaves at once, 0x1CE3BD0), while view 1 sees water (eye R's
// view reaches it first). View 1's steps then stay, the fields its setup wrote go back into the world's state
// and its job runs on the world's: otherwise nobody steps the ripples and caustics view 1 binds, and its copy
// moves on indices whose slots nobody writes. False for every other frame, and while view 0's setup has not
// ended (its decision is not known yet).
inline constexpr bool view1StepsWorld(const View0End& v0, std::int32_t view1Surfaces) {
    return v0.ended && v0.surfaces == 0 && !v0.stepped && !v0.moved && view1Surfaces > 0;
}

// The fields view 1's setup wrote into `copy`, back into `world`. False (nothing written) for spans of other
// sizes than kStateSize.
inline bool writeBack(std::span<std::byte> world, std::span<const std::byte> copy) {
    if (world.size() != kStateSize || copy.size() != kStateSize) {
        return false;
    }
    for (const Range& r : kSetupWrites) {
        std::memcpy(world.data() + r.at, copy.data() + r.at, r.size);
    }
    return true;
}

} // namespace evr::vkcore::view_water_start
