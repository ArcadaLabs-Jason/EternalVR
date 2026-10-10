#pragma once

// Why an XR frame shows no new image: the frames table's `repeat` column (eternalvr-frames-<pid>.csv,
// presenter_frame_log.cpp, docs/VR_HEAD_TRACKED.md). The present hook notes why its last present handed the
// XR worker no image; a frame of the worker that finds nothing newer than the image it shows takes that
// reason. Pure (tests/vkcore/frame_repeat_tests.cpp).

#include <cstdint>

namespace evr::vkcore {

enum class FrameRepeat : std::uint8_t {
    New,       // a new image
    NoPresent, // the game's last present was handed over and is shown already: no present since
    Kept,      // the game's last present had no new pair (Parallel Eye Rendering): the headset keeps the last
    Dropped,   // ... had a new pair, left out by ETERNALVR_TEST_PE_DROP
    Held,      // ... showed a new swapchain image no frame drew yet (after a swapchain recreate)
    NotHanded, // the game's last present was not handed over (no ring slot free, no copy or no pair record)
    Waiting,   // a newer image was handed over, its frame not rendered yet (or the runtime's image not ready)
};

// The column's word for `r`.
constexpr const char* repeatName(FrameRepeat r) {
    switch (r) {
    case FrameRepeat::New:
        return "new";
    case FrameRepeat::NoPresent:
        return "no_present";
    case FrameRepeat::Kept:
        return "kept";
    case FrameRepeat::Dropped:
        return "dropped";
    case FrameRepeat::Held:
        return "held";
    case FrameRepeat::NotHanded:
        return "not_handed";
    case FrameRepeat::Waiting:
        return "waiting";
    }
    return "unknown";
}

} // namespace evr::vkcore
