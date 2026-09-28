#pragma once

// Diagnostic: when the object-transform ring (stereo_seq/object_prev.hpp) is written and read, per render.
// ETERNALVR_STEREO_RING_TRACE=1 makes the ring's hooks (object_prev_hooks.cpp) record, after 20,000 events,
// 600 of them: the site, the engine's counter, the counter handed back, the chain's eye (seqChainEye), the
// eye of the tag in flight, the eye and tick of the render's own tag (backend frame counter + 1), the thread
// and the time; then logs them once. Off by default.

#include <cstdint>

namespace evr::vkcore {

enum class RingSite : std::uint8_t {
    Upload,
    CurrentJoints,
    CurrentMatrices,
    PreviousJoints,
    PreviousMatrices
};

// Reads ETERNALVR_STEREO_RING_TRACE once.
void initRingTrace();

// Cheap when the trace is off. `engine`: the counter the engine read; `given`: the one handed back.
void ringTraceRecord(RingSite site, std::uint32_t engine, std::uint32_t given);

} // namespace evr::vkcore
