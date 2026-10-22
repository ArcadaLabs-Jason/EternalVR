#pragma once

// The GUI target's sparse readback: where the game drew the weapon wheel (ui_layer/wheel_bounds.hpp) and the
// HUD's corner blocks (ui_layer/hud_blocks.hpp). While the game's wheel is up, each GUI target copy also
// copies every kWheelGridStep-th row of the band's right part (wheelSearchRect) into a host buffer; in wrist
// or weapon mode outside menus and full-screen backdrops with the wheel down, a copy now and then
// (ui_layer::CornerReadCadence) copies every kWheelGridStep-th row of the band's two bottom corners
// (cornerSearchRect) instead. Both are recorded in the same command buffer while the target is in
// TRANSFER_SRC_OPTIMAL. Once the shared timeline shows a copy done, its grid is measured and the result
// published (latest(), latestCorners()) for the XR worker, which centres the wheel and cuts it and the blocks
// the hand HUDs show out of the head-locked HUD (presenter_wrist.cpp). Up to kReadings readings are in
// flight; a present that finds none free is simply not read.
//
// The copies ride on the backdrop test's submission bookkeeping (backdrop_probe.hpp: submitted, cancel,
// poll, destroy are passed on), as both read the same GUI target copy. All calls run on the present hook
// under the presenter's mutex, except latest() and latestCorners(), which any thread reads.

#include "ui_layer/hud_blocks.hpp"
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
    // Measures the completed readings, oldest first, and publishes each wheel found and each corner reading.
    void poll(DeviceData& dev, std::uint64_t completedTimeline);
    void destroy(DeviceData& dev);

    // Any thread: the newest wheel measured (serial 0 before the first).
    static ui_layer::WheelMeasurement latest();
    // Any thread: the newest corner reading (serial 0 before the first).
    static ui_layer::CornerMeasurement latestCorners();

private:
    static constexpr std::uint32_t kReadings = 3;
    enum class State { Idle, Recorded, Submitted };
    enum class Kind { Wheel, Corners };
    struct Reading {
        State state = State::Idle;
        Kind kind = Kind::Wheel;
        std::uint64_t value = 0;
        VkExtent2D extent{};
        bool timed = false;
    };
    // A rectangle read every kWheelGridStep-th row, from `offset` bytes into a reading.
    struct Rows {
        ui_layer::PixelRect area;
        std::uint32_t rows = 0;
        VkDeviceSize offset = 0;
    };
    static Rows rowsOf(const ui_layer::PixelRect& area, VkDeviceSize offset);
    bool ensureBuffer(DeviceData& dev, VkExtent2D extent);
    void freeBuffer(DeviceData& dev);
    // The slot a new reading goes to (marks a recorded-but-lost one idle); nullptr when none is free.
    Reading* freeSlot(std::uint32_t& index);
    // Records the copy of `parts` into slot `index`, with its timestamps; marks it recorded.
    void recordCopy(DeviceData& dev,
                    VkCommandBuffer cb,
                    std::uint32_t family,
                    VkImage source,
                    std::uint32_t index,
                    const Rows* parts,
                    std::uint32_t count);
    // wheel_probe_corners.cpp: the corners' reading and its measuring.
    bool recordCorners(DeviceData& dev,
                       VkCommandBuffer cb,
                       std::uint32_t family,
                       VkImage source,
                       VkExtent2D extent,
                       bool wanted);
    void measureCorners(const std::uint8_t* bytes, VkExtent2D extent, double gpuSeconds);
    void logCornerStats(bool force);
    // The grid of `part` from a reading's bytes into `grid`.
    static void readGrid(const std::uint8_t* bytes, const Rows& part, ui_layer::AlphaGrid& grid);
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
    VkDeviceSize readingBytes_ = 0; // one slot: the larger of the wheel's and the corners' reading
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
    // The corners' readings: their cadence and grids, and for the log their count, what they found, the CPU
    // time measuring them and the GPU time of their copies (since the last line, and in all).
    ui_layer::CornerReadCadence cornerCadence_;
    std::array<ui_layer::AlphaGrid, 2> cornerGrids_;
    struct CornerStats {
        std::uint64_t readings = 0;
        std::uint64_t both = 0;
        std::uint64_t none = 0;
        std::uint64_t wantedCopies = 0; // GUI copies with the corners wanted
        double cpuSeconds = 0.0;
        double maxCpuSeconds = 0.0;
        double readSeconds = 0.0; // of cpuSeconds, reading the grids out of the host buffer
        double gpuSeconds = 0.0;
        double maxGpuSeconds = 0.0;
        std::uint64_t gpuReadings = 0;
    };
    CornerStats cornerSince_; // since the last line
    std::uint64_t cornerReadings_ = 0;
    std::uint32_t cornerLines_ = 0;
    ui_layer::PixelRect lastCornerVitals_{}; // the last reading that found both blocks
    ui_layer::PixelRect lastCornerWeapon_{};
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
