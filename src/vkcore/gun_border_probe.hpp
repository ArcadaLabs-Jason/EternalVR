#pragma once

// The same-copy border test of the game's crosshair on the gun's ray (ui_layer::gunCrosshairBorderPoints;
// presenter_gun_crosshair.hpp). A GUI copy the present hook marked for the move also copies the eight points
// around the crosshair's rectangle into its ring slot's part of a small host buffer, in the same command
// buffer while the target is in TRANSFER_SRC_OPTIMAL. The XR worker takes a slot only once the shared
// timeline shows its copy done (presenter_ring.cpp, takeRenderedSlot), so it reads that copy's own points
// before it moves the rectangle, and keeps the copy whole when they show a full-screen layer: the first
// copies of a pause menu, a death screen or a loading fade, which the backdrop test (backdrop_probe.hpp)
// only sees a few copies later.
//
// record() runs on the present hook under the presenter's mutex; reading() on the XR worker, for a slot it
// holds (kSlotReading), whose copy the present hook does not record again meanwhile. The buffer is made once
// and freed only with the presenter's Vulkan objects, after the worker has stopped (destroy()): a ring
// rebuild leaves it, so the worker never reads freed memory. Host-coherent memory only (the worker has no
// Vulkan calls to make the writes visible); without it there is no border test and the copies are moved as
// the present hook decided.

#include "ui_layer/backdrop.hpp"
#include "ui_layer/ui_settings.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/presenter_types.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace evr::vkcore {

class GunBorderProbe {
public:
    using Reading = std::array<std::uint8_t, ui_layer::kBackdropProbePoints * 4>;

    // Records the reading of `source` (in TRANSFER_SRC_OPTIMAL, `extent`) around `rect` into ring slot
    // `slot`'s part of the buffer, and makes it visible to the host; false when nothing was recorded (no
    // points, no buffer).
    bool record(DeviceData& dev,
                VkCommandBuffer cb,
                VkImage source,
                VkExtent2D extent,
                const ui_layer::PixelRect& rect,
                std::uint32_t slot);
    // XR worker: ring slot `slot`'s reading, from the copy whose completion the worker has seen; false when
    // there is no buffer.
    bool reading(std::uint32_t slot, Reading& out) const;
    // With the presenter's Vulkan objects, the XR worker stopped.
    void destroy(DeviceData& dev);

private:
    static constexpr VkDeviceSize kReadingBytes = ui_layer::kBackdropProbePoints * 4;
    bool ensureBuffer(DeviceData& dev);

    bool failed_ = false;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    std::atomic<const std::uint8_t*> mapped_{nullptr};
};

} // namespace evr::vkcore
