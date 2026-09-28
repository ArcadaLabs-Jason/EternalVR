#pragma once

// How long the game's window calls take (docs/VR_STEREO.md, Desktop window): the time spent in the driver's
// vkAcquireNextImageKHR and vkQueuePresentKHR for the game's swapchain. A present or an acquire that waits
// for the desktop display shows up here; the presenter logs the last 10 s with its window line.

#include <cstdint>

namespace evr::vkcore::window_timing {

struct Calls {
    std::uint64_t count = 0;
    std::uint64_t micros = 0;
    std::uint64_t maxMicros = 0;
};
struct Totals {
    Calls acquire;
    Calls present;
};

void addAcquire(std::uint64_t micros);
void addPresent(std::uint64_t micros);
// The totals since the last call (and resets them).
Totals take();
// Microseconds on a monotonic clock.
std::uint64_t nowMicros();

} // namespace evr::vkcore::window_timing
