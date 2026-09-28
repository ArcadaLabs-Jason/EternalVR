#pragma once

// The backdrop test's readback (ui_layer/backdrop.hpp): each GUI target copy also copies the eight probe
// pixels around the crosshair square into a small host buffer, in the same command buffer while the target
// is in TRANSFER_SRC_OPTIMAL. Once the shared timeline shows a copy done, its reading goes to the detector,
// oldest first; the next copies are masked or not by the detector's state. Up to kReadings readings are in
// flight; a present that finds none free is simply not probed.
//
// All calls run on the present hook under the presenter's mutex, except backdrop(), which any thread reads.

#include "ui_layer/backdrop.hpp"
#include "vkcore/dispatch.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace evr::vkcore {

class BackdropProbe {
public:
    // Records the reading of `source` (in TRANSFER_SRC_OPTIMAL, `extent`) around the centred square of
    // `maskFraction` x its height, and makes it visible to the host; false when nothing was recorded.
    bool record(DeviceData& dev, VkCommandBuffer cb, VkImage source, VkExtent2D extent, float maskFraction);
    // The recorded reading was submitted and completes at `timelineValue`.
    void submitted(std::uint64_t timelineValue);
    // The recorded reading was not submitted after all.
    void cancel();
    // Hands the completed readings to the detector; logs its first changes each way.
    void poll(DeviceData& dev, std::uint64_t completedTimeline);
    void destroy(DeviceData& dev);

    // The target holds a full-screen backdrop (a menu's or a fade): the crosshair square is not masked.
    [[nodiscard]] bool backdrop() const { return backdrop_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t readings() const { return readings_; }
    [[nodiscard]] std::uint64_t backdropsSeen() const { return ups_; }

private:
    static constexpr std::uint32_t kReadings = 4;
    static constexpr std::uint32_t kMarginPixels = 4;
    static constexpr VkDeviceSize kReadingBytes = ui_layer::kBackdropProbePoints * 4;
    enum class State { Idle, Recorded, Submitted };
    struct Reading {
        State state = State::Idle;
        std::uint64_t value = 0;
    };
    bool ensureBuffer(DeviceData& dev);

    std::array<Reading, kReadings> slots_{};
    ui_layer::BackdropDetector detector_;
    std::atomic<bool> backdrop_{false};
    bool failed_ = false;
    bool coherent_ = true;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    std::uint64_t readings_ = 0;
    std::uint64_t ups_ = 0;
    std::uint64_t downs_ = 0;
};

} // namespace evr::vkcore
