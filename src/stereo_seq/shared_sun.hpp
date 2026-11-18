#pragma once

// The sun's shadow cascades under Route S: eye R shading with the cascades eye L rendered in the same tick
// (docs/rig-findings/perf-sun-cascades.md).
//
// Each render's shadow setup (the record function 0x1CFE2B0 of the lighting job, Steam build 25216728) walks
// the sun's slices, two per cascade (even the world's casters, odd the player's). A cascade is placed around
// the view's origin, not fitted to the view's frustum: its cache entry keeps the centre, and the setup places
// the cascade again only when the origin moves further from it, across the sun's direction, than
// r_shadowParallelMoveMargin (0.05) of the cascade's radius. The dynamic part of cascade 0, and of the far
// cascade due in turn (r_amortizedSunCascades), is past its stale limit (r_shadowParallelMaxStaleFrames
// 0,0,1,2) in every render, so the setup releases the cascade's entry and renders the whole cascade again,
// the static casters too. Under Route S eye R does that a second time in each tick for the same casters, the
// same sun and, the eyes being 6.4 cm apart, almost always the same centre. The shading takes each cascade's
// projection from its cache entry, so a cascade eye L rendered is sampled by eye R as it was drawn.
//
// ETERNALVR_SHARED_SUN (off by default): `count` counts the sun's slices each eye lists, by cascade; `1`
// makes eye R keep, at the stale check, the entry of a sun world slice eye L listed for rendering in the same
// tick (stamped at eye L's listing with the tick) and whose counter is 1 there (eye L's render or allocation
// set it to 0, eye R's check raised it), so eye R neither releases nor renders it and shades with eye L's of
// the same tick: never an older one. `ceiling` (a rig bound) keeps every sun world slice's entry in eye R,
// whatever its counter or tick, and leaves the rest of eye R's sun world slices off its list.

#include "stereo_seq/eye_tags.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace evr::stereo_seq {

enum class SharedSunMode : std::uint8_t {
    Off,     // nothing located, nothing hooked
    Count,   // the sun's slices counted per eye and cascade; the game unchanged
    Shared,  // eye R keeps the sun world slices eye L rendered since its last check
    Ceiling, // eye R renders none of the sun's world slices (a rig bound)
};

// ETERNALVR_SHARED_SUN: unset, "", "0", "off", "false", "no" Off; "count" Count; "1", "on", "true", "yes",
// "shared" Shared; "ceiling" Ceiling; anything else Off (`known` false, for the log).
SharedSunMode sharedSunMode(std::string_view value, bool* known = nullptr);
const char* sharedSunModeName(SharedSunMode mode);

// The light type the setup keeps at [rsp + 0x48] (+0x74 of the light: 0 and 4 point, 1 spot, 2 the sun's
// parallel light).
inline constexpr std::int32_t kParallelLightType = 2;
inline constexpr std::uint32_t kSunCascades = 4;

// A slice of the sun that holds the world's casters (an even slice index of the four cascades), not the
// player's (odd).
constexpr bool isSunWorldSlice(std::int32_t lightType, std::uint32_t slice) {
    return lightType == kParallelLightType && (slice & 1u) == 0 && slice < 2 * kSunCascades;
}

// A sun slice's cascade (two slices per cascade), the last cascade for anything past it.
constexpr std::uint32_t sunCascade(std::uint32_t slice) {
    const std::uint32_t cascade = slice >> 1;
    return cascade < kSunCascades ? cascade : kSunCascades - 1;
}

// The sun world slices eye L listed for rendering, stamped with the tick. Keyed by the light and the slice,
// not by the cache entry: eye L's stale check releases the cascade's entry and its listing allocates a new
// one, so the entry eye R checks is not the one eye L checked. A fixed table of packed (hash, tick) words: a
// race or a collision can only lose a stamp (eye R then renders the slice as the engine would) or, with 40
// bits of the hash to match, practically never match another slice. A tick more than 16 below the highest
// stamped (the game frame count started again: a map load) and clear() empty the table; one a few ticks
// behind (a worker still listing the previous tick) only stamps its own tick, which the per-tick match keeps
// harmless to the others.
class SunTickStamps {
public:
    static constexpr std::size_t kSlots = 256;

    void stamp(std::uintptr_t light, std::uint32_t slice, std::uint64_t tick);
    bool stamped(std::uintptr_t light, std::uint32_t slice, std::uint64_t tick) const;
    void clear();

private:
    std::array<std::atomic<std::uint64_t>, kSlots> slots_{};
    std::atomic<std::uint64_t> highest_{0};
};

// A slice at the stale check, as the hook sees it.
struct SunCheck {
    Eye eye = Eye::Mono;
    std::uint64_t tick = 0; // the render's game frame (0: unknown)
    std::int32_t lightType = -1;
    std::uint32_t slice = 0;
    std::uint32_t counter = 0;         // the entry's counter after the check raised it
    bool listedByLeftThisTick = false; // eye L listed this slice for rendering in this tick
};

// True when eye R keeps the slice's entry and leaves the slice off its list. Shared: a sun world slice of eye
// R in a known tick that eye L listed for rendering in the same tick and whose entry's counter is 1 (2 or
// more: eye L has not rendered it since eye R's last check, or its render-list job has not run yet; eye R
// then renders it as the engine would). Eye R so never shades with a cascade older than eye L's of the same
// tick. Ceiling: any sun world slice of eye R in a known tick.
constexpr bool sharedSunKeeps(SharedSunMode mode, const SunCheck& c) {
    if (c.eye != Eye::Right || c.tick == 0 || !isSunWorldSlice(c.lightType, c.slice)) {
        return false;
    }
    if (mode == SharedSunMode::Ceiling) {
        return true;
    }
    return mode == SharedSunMode::Shared && c.counter == 1 && c.listedByLeftThisTick;
}

// The registers the stale check's hook hands back for a kept slice: eax takes the counter (`cmp edx, eax`
// finds no excess, so `ja` does not release the entry; rax's upper half kept), and bl loses bits 2 and 4
// (`test bl, 4` and `test bl, 6` find nothing, so the slice is not listed; the rest of rbx kept).
struct SunKeepRegisters {
    std::uint64_t rax = 0;
    std::uint64_t rbx = 0;
};
constexpr SunKeepRegisters sunKeepRegisters(std::uint64_t rax, std::uint64_t rbx, std::uint32_t counter) {
    return {(rax & ~std::uint64_t{0xFFFFFFFF}) | counter, rbx & ~std::uint64_t{6}};
}

// True when eye R leaves a listed slice off its list at the listing (Ceiling only: a sun world slice that
// never reached the stale check).
constexpr bool sharedSunSkipsListing(
    SharedSunMode mode, Eye eye, std::uint64_t tick, std::int32_t lightType, std::uint32_t slice) {
    return mode == SharedSunMode::Ceiling && eye == Eye::Right && tick != 0 &&
           isSunWorldSlice(lightType, slice);
}

} // namespace evr::stereo_seq
