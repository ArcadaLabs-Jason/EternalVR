#pragma once

// The late-image wait (ETERNALVR_PACE_LATE_WAIT=1, T-235; features/pacing/late_image.hpp and
// late_wait_gate.hpp): held, a headset frame that begins while the game is still drawing its image waits for
// it within its own time. Configured once by frame_pacing::configure; the wait itself and the GPU share
// estimate are the XR worker's (frame_pacing::awaitLateImage, the slot choice in presenter_ring.cpp). Off,
// nothing here runs.

#include "features/pacing/late_wait_gate.hpp"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace evr::vkcore::late_wait {

// Once, at frame pacing's set-up: on when `ask` passes the gates (a line when it does, or why not when asked
// for); `gpuMs` is ETERNALVR_TEST_LATE_WAIT_GPU_MS (a rig test: the share left for the GPU, 2 ms by default).
void configure(const pacing::LateWaitAsk& ask, std::string_view gpuMs);
bool on();

// What the XR worker's frame knows when it begins.
struct Frame {
    bool newImage = false;             // an image was handed over since the previous headset frame's choice
    double sinceHandOverSeconds = 0.0; // since the game's last hand-over
    double periodSeconds = 0.0;
};

// XR worker, held, before the choice: waits for the next hand-over (`handOvers` moving past `atFrame`) as
// long as late_image.hpp allows, leaving the larger of the configured GPU share and the recent fence waits.
void await(const Frame& frame,
           long long frameStartQpc,
           const std::atomic<std::uint64_t>& handOvers,
           std::uint64_t atFrame,
           const std::atomic<bool>& stop);

// XR worker, after the slot choice: how long it waited on the fence for the image it took (0: none), or for
// one whose wait ran out. Only while on.
void noteFenceWait(double seconds);

// XR worker, every 10 s: the waits since the last line (nothing when none ran).
void logSummary();

} // namespace evr::vkcore::late_wait
