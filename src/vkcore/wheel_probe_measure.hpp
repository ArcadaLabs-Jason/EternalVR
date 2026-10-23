#pragma once

// The GUI readback's measuring (wheel_probe.hpp), on its own thread (readback_worker.hpp). The present hook
// hands each completed reading over as a ReadbackJob: where its bytes are (a slot of the cached host buffer,
// the worker's until the job returns), the GUI target's size, the copy's GPU time and the present hook's own
// cost since the last job. ReadbackMeasure reads the grids out of the bytes, measures the wheel
// (ui_layer/wheel_bounds.hpp) or the corner blocks (ui_layer/hud_blocks.hpp), publishes the result for the
// XR worker (WheelProbe::latest(), latestCorners()) and logs the costs; the present hook's cadences learn
// what each reading found from the ReadbackResult it takes back.
//
// No Vulkan: the bytes are plain host memory by the time a job is handed.

#include "ui_layer/hud_blocks.hpp"
#include "ui_layer/wheel_bounds.hpp"
#include "vkcore/readback_worker.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>

namespace evr::vkcore {

// A rectangle read every kWheelGridStep-th row, from `offset` bytes into a reading.
struct ReadbackRows {
    ui_layer::PixelRect area;
    std::uint32_t rows = 0;
    std::uint64_t offset = 0;
};
ReadbackRows readbackRows(const ui_layer::PixelRect& area, std::uint64_t offset);
// The wheel's reading of a `width` x `height` target: its search rectangle.
ReadbackRows wheelReadbackRows(std::uint32_t width, std::uint32_t height);
// The corners' reading: the vitals' rows, then the weapon block's.
std::array<ReadbackRows, 2> cornerReadbackRows(std::uint32_t width, std::uint32_t height);
// The grid of `part` from a reading's bytes into `grid`.
void readReadbackGrid(const std::uint8_t* bytes, const ReadbackRows& part, ui_layer::AlphaGrid& grid);

// QueryPerformanceCounter ticks, and seconds between two of them.
std::int64_t readbackTicks();
double readbackSeconds(std::int64_t from, std::int64_t to);

// The present hook's own time for the readback (WheelProbe's record and poll) over some GUI copies.
struct PresentCost {
    std::uint64_t copies = 0;
    double seconds = 0.0;
    double maxSeconds = 0.0;  // the most one GUI copy spent
    std::uint64_t noSlot = 0; // reads due that found no slot free
    // Of `seconds`, recording the readings' copies (the copy commands, their timestamps and barrier).
    std::uint64_t records = 0;
    double recordSeconds = 0.0;
    double maxRecordSeconds = 0.0;
    void add(double s) {
        ++copies;
        seconds += s;
        maxSeconds = std::max(maxSeconds, s);
    }
    void addRecord(double s) {
        ++records;
        recordSeconds += s;
        maxRecordSeconds = std::max(maxRecordSeconds, s);
    }
    void merge(const PresentCost& o) {
        copies += o.copies;
        seconds += o.seconds;
        maxSeconds = std::max(maxSeconds, o.maxSeconds);
        noSlot += o.noSlot;
        records += o.records;
        recordSeconds += o.recordSeconds;
        maxRecordSeconds = std::max(maxRecordSeconds, o.maxRecordSeconds);
    }
};

struct ReadbackJob {
    // WheelDown: the wheel went down and none of its readings is in flight: its line is logged.
    enum class Kind : std::uint8_t { Wheel, Corners, WheelDown };
    Kind kind = Kind::Wheel;
    std::uint32_t slot = readback::kNoSlot;
    const std::uint8_t* bytes = nullptr; // the slot's reading
    std::uint32_t width = 0;             // the GUI target it was read from
    std::uint32_t height = 0;
    double gpuSeconds = -1.0;       // the copy's GPU time; negative: not timed
    std::int64_t recordedTicks = 0; // when its copy was recorded
    std::uint64_t recordedCopy = 0; // the GUI copy it was read from (WheelProbe counts them from 1)
    std::int64_t handedTicks = 0;   // when the present hook saw it done and handed it over
    std::uint64_t handedCopy = 0;   // the GUI copies recorded by then
    PresentCost present;            // the present hook's cost since the last job of this kind
};

struct ReadbackResult {
    ReadbackJob::Kind kind = ReadbackJob::Kind::Wheel;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::optional<ui_layer::PixelRect> wheel; // a wheel reading: the rectangle found
    bool both = false;                        // a corner reading: both blocks found
};

// The measuring thread's side: everything here but `guiCopies` is the worker's alone.
class ReadbackMeasure {
public:
    ReadbackResult measure(const ReadbackJob& job);

    // The GUI copies recorded so far (the present hook stores, the worker reads): the latency in copies.
    std::atomic<std::uint64_t> guiCopies{0};

private:
    ReadbackResult wheel(const ReadbackJob& job);
    ReadbackResult corners(const ReadbackJob& job); // wheel_probe_corners.cpp
    void logStats(bool force);
    void logCornerStats(bool force); // wheel_probe_corners.cpp

    // From the copy's record to the hand-off (where the measuring used to be) and to the publication.
    struct Latency {
        std::uint64_t readings = 0;
        double handSeconds = 0.0;
        double publishSeconds = 0.0;
        double maxPublishSeconds = 0.0;
        std::uint64_t handCopies = 0;
        std::uint64_t publishCopies = 0;
        std::uint64_t maxPublishCopies = 0;
        void add(const ReadbackJob& job, std::int64_t published, std::uint64_t copiesThen);
    };

    ui_layer::AlphaGrid grid_;
    ui_layer::WheelScratch scratch_;
    // The wheel's log: readings measured, wheels found, the measuring time, the last rectangle; since the
    // last line, the present hook's cost and the latency.
    std::uint64_t readings_ = 0;
    std::uint64_t found_ = 0;
    std::uint64_t loggedReadings_ = 0;
    double measureSeconds_ = 0.0;
    double maxMeasureSeconds_ = 0.0;
    std::uint32_t statsLogged_ = 0;
    ui_layer::PixelRect lastRect_{};
    PresentCost present_;
    Latency latency_;
    double gpuSeconds_ = 0.0;
    double maxGpuSeconds_ = 0.0;
    std::uint64_t gpuReadings_ = 0;
    // The corners' grids, and for the log their count, what they found, the measuring time, the GPU time of
    // their copies, the present hook's cost and the latency (since the last line, and in all).
    std::array<ui_layer::AlphaGrid, 2> cornerGrids_;
    struct CornerStats {
        std::uint64_t readings = 0;
        std::uint64_t both = 0;
        std::uint64_t none = 0;
        double cpuSeconds = 0.0;
        double maxCpuSeconds = 0.0;
        double readSeconds = 0.0; // of cpuSeconds, reading the grids out of the host buffer
        double gpuSeconds = 0.0;
        double maxGpuSeconds = 0.0;
        std::uint64_t gpuReadings = 0;
        PresentCost present; // copies: GUI copies with the corners wanted
        Latency latency;
    };
    CornerStats cornerSince_;
    std::uint64_t cornerReadings_ = 0;
    std::uint32_t cornerLines_ = 0;
    ui_layer::PixelRect lastCornerVitals_{}; // the last reading that found both blocks
    ui_layer::PixelRect lastCornerWeapon_{};
};

} // namespace evr::vkcore
