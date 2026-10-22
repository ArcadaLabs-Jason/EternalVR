#pragma once

// The weapon wheel's readback (ui_layer/wheel_bounds.hpp): while the game's wheel is up, each GUI target
// copy also copies every kWheelGridStep-th row of the band's right part (wheelSearchRect) into a host
// buffer, in the same command buffer while the target is in TRANSFER_SRC_OPTIMAL. Once the shared timeline
// shows a copy done, its grid is measured and the wheel's rectangle published (latest()) for the XR worker,
// which shows it on the centred quad and cuts it out of the head-locked HUD (presenter_wrist.cpp). Up to
// kReadings readings are in flight; a present that finds none free is simply not read.
//
// The copies ride on the backdrop test's submission bookkeeping (backdrop_probe.hpp: submitted, cancel,
// poll, destroy are passed on), as both read the same GUI target copy. All calls run on the present hook
// under the presenter's mutex, except latest(), which any thread reads.

#include "ui_layer/wheel_bounds.hpp"
#include "vkcore/dispatch.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace evr::vkcore {

class WheelProbe {
public:
    // Records the reading of `source` (in TRANSFER_SRC_OPTIMAL, `extent`) into `cb` (of queue family
    // `family`) while `wheelUp` (the game's wheel is up, controllers::wheelView); false when nothing was
    // recorded. Timestamps around the copy measure its GPU time for the log.
    bool record(DeviceData& dev,
                VkCommandBuffer cb,
                std::uint32_t family,
                VkImage source,
                VkExtent2D extent,
                bool wheelUp);
    void submitted(std::uint64_t timelineValue);
    void cancel();
    // Measures the completed readings, oldest first, and publishes each wheel found.
    void poll(DeviceData& dev, std::uint64_t completedTimeline);
    void destroy(DeviceData& dev);

    // Any thread: the newest wheel measured (serial 0 before the first).
    static ui_layer::WheelMeasurement latest();

private:
    static constexpr std::uint32_t kReadings = 3;
    enum class State { Idle, Recorded, Submitted };
    struct Reading {
        State state = State::Idle;
        std::uint64_t value = 0;
        VkExtent2D extent{};
        bool timed = false;
    };
    bool ensureBuffer(DeviceData& dev, VkExtent2D extent);
    void freeBuffer(DeviceData& dev);
    // The timestamp queries, when the family has them (set up once per device).
    bool ensureTiming(DeviceData& dev, std::uint32_t family);
    void destroyTiming(DeviceData& dev);
    void logStats(bool force);

    std::array<Reading, kReadings> slots_{};
    // Kept between frames so a reading does not allocate.
    std::vector<VkBufferImageCopy> regions_;
    ui_layer::AlphaGrid grid_;
    ui_layer::WheelScratch scratch_;
    // Which frames are read (every frame until the rectangle is stable, then every few).
    ui_layer::WheelReadCadence cadence_;
    std::uint64_t framesUp_ = 0;  // frames with the wheel up since the last log line
    double maxPollSeconds_ = 0.0; // the most measuring time one present spent, since the last log line
    bool failed_ = false;
    bool coherent_ = true;
    VkExtent2D bufferExtent_{};
    VkDeviceSize readingBytes_ = 0;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    // For the log: readings measured, wheels found, the CPU time measuring them, the last rectangle.
    std::uint64_t readings_ = 0;
    std::uint64_t found_ = 0;
    std::uint64_t loggedReadings_ = 0;
    double measureSeconds_ = 0.0;
    double maxMeasureSeconds_ = 0.0;
    std::uint32_t statsLogged_ = 0;
    ui_layer::PixelRect lastRect_{};
    // GPU time of the copies (timestamps before and after each).
    struct Timing {
        PFN_vkCreateQueryPool createQueryPool = nullptr;
        PFN_vkDestroyQueryPool destroyQueryPool = nullptr;
        PFN_vkGetQueryPoolResults getQueryPoolResults = nullptr;
        PFN_vkCmdResetQueryPool cmdResetQueryPool = nullptr;
        PFN_vkCmdWriteTimestamp cmdWriteTimestamp = nullptr;
        VkQueryPool pool = VK_NULL_HANDLE;
        double periodNs = 0.0;
        bool tried = false;
        std::vector<std::uint32_t> validBits; // per queue family
    };
    Timing timing_;
    double gpuSeconds_ = 0.0;
    double maxGpuSeconds_ = 0.0;
    std::uint64_t gpuReadings_ = 0;
};

} // namespace evr::vkcore
