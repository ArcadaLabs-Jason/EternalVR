#pragma once

// The GUI target's sparse readback: where the game drew the weapon wheel (ui_layer/wheel_bounds.hpp) and the
// HUD's corner blocks (ui_layer/hud_blocks.hpp). While the game's wheel is up, each GUI target copy also
// copies every kWheelGridStep-th row of the band's right part (wheelSearchRect) into a host buffer; in wrist
// or weapon mode outside menus and full-screen backdrops with the wheel down, a copy now and then
// (ui_layer::CornerReadCadence) copies every kWheelGridStep-th row of the band's two bottom corners
// (cornerSearchRect) instead. Both are recorded in the same command buffer while the target is in
// TRANSFER_SRC_OPTIMAL. Once the shared timeline shows a copy done, the present hook hands its slot to the
// readback's own thread (readback_worker.hpp), which reads the grid out of the host buffer, measures it and
// publishes the result (latest(), latestCorners()) for the XR worker, which centres the wheel and cuts it and
// the blocks the hand HUDs show out of the head-locked HUD (presenter_wrist.cpp). The present hook only
// records the copies and hands the done ones over; what each reading found comes back to its read cadences
// at a later poll. Up to kReadings readings are in flight or being measured; a present that finds none free
// is simply not read.
//
// The copies ride on the backdrop test's submission bookkeeping (backdrop_probe.hpp: submitted, cancel,
// poll, destroy are passed on), as both read the same GUI target copy. All calls run on the present hook
// under the presenter's mutex, except latest() and latestCorners(), which any thread reads. destroy() stops
// the measuring thread before it frees the host buffer.

#include "ui_layer/hud_blocks.hpp"
#include "ui_layer/wheel_bounds.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/readback_worker.hpp"
#include "vkcore/wheel_probe_measure.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace evr::vkcore {

class WheelProbe {
public:
    WheelProbe();
    ~WheelProbe();
    WheelProbe(const WheelProbe&) = delete;
    WheelProbe& operator=(const WheelProbe&) = delete;

    // Records the reading of `source` (in TRANSFER_SRC_OPTIMAL, `extent`) into `cb` (of queue family
    // `family`): the wheel's while `wheelUp` (the game's wheel is up, controllers::wheelView), else the
    // corners' when `cornersWanted` (wrist or weapon mode, no menu or full-screen backdrop) and their cadence
    // says so; false when nothing was recorded. Timestamps around the copy measure its GPU time for the log.
    bool record(DeviceData& dev,
                VkCommandBuffer cb,
                std::uint32_t family,
                VkImage source,
                VkExtent2D extent,
                bool wheelUp,
                bool cornersWanted);
    void submitted(std::uint64_t timelineValue);
    void cancel();
    // Feeds the cadences what the readings measured since the last poll found, and hands the completed
    // readings, oldest first, to the measuring thread.
    void poll(DeviceData& dev, std::uint64_t completedTimeline);
    void destroy(DeviceData& dev);

    // Any thread: the newest wheel measured (serial 0 before the first).
    static ui_layer::WheelMeasurement latest();
    // Any thread: the newest corner reading (serial 0 before the first).
    static ui_layer::CornerMeasurement latestCorners();

private:
    static constexpr std::uint32_t kReadings = 3;
    struct Reading {
        ReadbackJob::Kind kind = ReadbackJob::Kind::Wheel;
        VkExtent2D extent{};
        bool timed = false;
        std::int64_t recordedTicks = 0;
        std::uint64_t recordedCopy = 0;
    };
    bool ensureBuffer(DeviceData& dev, VkExtent2D extent);
    void freeBuffer(DeviceData& dev);
    // The slot a new reading goes to; readback::kNoSlot when none is free.
    std::uint32_t freeSlot();
    // Records the copy of `parts` into slot `index`, with its timestamps; marks it recorded. Its time goes
    // into `cost`.
    void recordCopy(DeviceData& dev,
                    VkCommandBuffer cb,
                    std::uint32_t family,
                    VkImage source,
                    std::uint32_t index,
                    const ReadbackRows* parts,
                    std::uint32_t count,
                    PresentCost& cost);
    // wheel_probe_corners.cpp: the corners' reading.
    bool recordCorners(DeviceData& dev,
                       VkCommandBuffer cb,
                       std::uint32_t family,
                       VkImage source,
                       VkExtent2D extent,
                       bool wanted);
    // Hands slot `index`'s completed reading to the measuring thread.
    void handReading(DeviceData& dev, std::uint32_t index);
    // A wheel reading is in flight on the GPU.
    [[nodiscard]] bool wheelInFlight() const;
    void handWheelDown();
    // The timestamp queries, when the family has them (set up once per device).
    bool ensureTiming(DeviceData& dev, std::uint32_t family);
    void destroyTiming(DeviceData& dev);

    readback::SlotRing<kReadings> ring_;
    std::array<Reading, kReadings> slots_{};
    // Kept between frames so a reading does not allocate.
    std::vector<VkBufferImageCopy> regions_;
    std::vector<ReadbackResult> results_;
    // Which frames are read (every frame until the rectangle is stable, then every few); the corners'
    // cadence.
    ui_layer::WheelReadCadence cadence_;
    ui_layer::CornerReadCadence cornerCadence_;
    bool wheelWasUp_ = false;
    bool wheelDownDue_ = false; // the wheel went down; its line goes to the log once no reading is in flight
    // The present hook's own cost since the last job of each kind, and poll's, counted with the next copy.
    PresentCost wheelCost_;
    PresentCost cornerCost_;
    double pollSeconds_ = 0.0;
    std::uint64_t copyCount_ = 0; // GUI copies seen by record()
    bool failed_ = false;
    bool coherent_ = true;
    VkExtent2D bufferExtent_{};
    VkDeviceSize readingBytes_ = 0; // one slot: the larger of the wheel's and the corners' reading
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
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
    // The measuring thread's state, then the thread itself: declared last, so it is stopped first.
    ReadbackMeasure measure_;
    readback::Worker<ReadbackJob, ReadbackResult> worker_;
};

} // namespace evr::vkcore
