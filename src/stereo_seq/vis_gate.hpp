#pragma once

// The renderer's first-visible gate (docs/rig-findings/stereo-visibility-counter.md): a non-static model is
// added to a view only after it has been counted in more consecutive renders of that view slot than its
// firstVisibleFrameCount. Counting stores `lastVisible = counter + 1`, and the count goes on when the model
// was counted in the previous render (the engine's test: `lastVisible >= counter`). Under Route S both eyes
// render the same view slot, so the previous render is the other eye's; the layer widens the test to the last
// two renders, so a model in one eye's frustum only counts once per tick and is drawn, after the engine's own
// delay in ticks.

#include <cstdint>

namespace evr::stereo_seq {

// True when the model's consecutive count goes on: it was counted in one of the last `renders` renders (1:
// the engine's own test; 2 under Route S). Counters are the engine's 32-bit ones, compared as it does
// (signed).
constexpr bool visGateContinues(std::int32_t lastVisible, std::int32_t counter, std::int32_t renders) {
    return static_cast<std::int64_t>(lastVisible) >= static_cast<std::int64_t>(counter) - (renders - 1);
}

// True when an UpdateInView model (particles) simulates in this render: it was updated in one of the last
// `renders` renders (1: the engine's own test, `stamp == frame - 1`; 2 under Route S). The engine steps the
// 32-bit frame number and compares for equality, so the distance is taken modulo 2^32 as it would wrap.
constexpr bool updateInViewContinues(std::int32_t stamp, std::int32_t frame, std::int32_t renders) {
    const std::uint32_t behind = static_cast<std::uint32_t>(frame) - static_cast<std::uint32_t>(stamp);
    return behind >= 1 && behind <= static_cast<std::uint32_t>(renders);
}

} // namespace evr::stereo_seq
