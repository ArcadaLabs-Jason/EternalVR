// The GUI readback's measuring thread (wheel_probe_measure.hpp): the wheel's readings and their log line;
// the corners' are in wheel_probe_corners.cpp.

#include "vkcore/wheel_probe_measure.hpp"

#include "vkcore/log.hpp"
#include "vkcore/wheel_probe.hpp"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <optional>

namespace evr::vkcore {

namespace {

std::mutex g_latestLock;
ui_layer::WheelMeasurement g_latest;

// How many summary lines the log gets (one when the wheel goes down, and every kReadingsPerLine readings).
constexpr std::uint32_t kStatsLines = 30;
constexpr std::uint64_t kReadingsPerLine = 600;

double perReading(double seconds, std::uint64_t n) {
    return n ? seconds / static_cast<double>(n) : 0.0;
}

} // namespace

ui_layer::WheelMeasurement WheelProbe::latest() {
    std::lock_guard lock(g_latestLock);
    return g_latest;
}

std::int64_t readbackTicks() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

double readbackSeconds(std::int64_t from, std::int64_t to) {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return static_cast<double>(to - from) / static_cast<double>(frequency);
}

ReadbackRows readbackRows(const ui_layer::PixelRect& area, std::uint64_t offset) {
    return ReadbackRows{area, ui_layer::gridRows(area, ui_layer::kWheelGridStep), offset};
}

ReadbackRows wheelReadbackRows(std::uint32_t width, std::uint32_t height) {
    return readbackRows(ui_layer::wheelSearchRect(width, height), 0);
}

std::array<ReadbackRows, 2> cornerReadbackRows(std::uint32_t width, std::uint32_t height) {
    const ReadbackRows vitals =
        readbackRows(ui_layer::cornerSearchRect(ui_layer::WristBlock::Vitals, width, height), 0);
    const ReadbackRows weapon =
        readbackRows(ui_layer::cornerSearchRect(ui_layer::WristBlock::Weapon, width, height),
                     std::uint64_t{vitals.area.width} * 4 * vitals.rows);
    return {vitals, weapon};
}

void readReadbackGrid(const std::uint8_t* bytes, const ReadbackRows& part, ui_layer::AlphaGrid& grid) {
    grid.area = part.area;
    grid.step = ui_layer::kWheelGridStep;
    grid.columns = ui_layer::gridColumns(grid.area, grid.step);
    grid.rows = ui_layer::gridRows(grid.area, grid.step);
    grid.alpha.resize(std::size_t{grid.columns} * grid.rows);
    const std::uint8_t* at = bytes + part.offset;
    const std::size_t rowBytes = std::size_t{grid.area.width} * 4;
    for (std::uint32_t r = 0; r < grid.rows; ++r) {
        const std::uint8_t* row = at + rowBytes * r;
        for (std::uint32_t c = 0; c < grid.columns; ++c) {
            grid.alpha[std::size_t{r} * grid.columns + c] = row[std::size_t{c} * grid.step * 4 + 3];
        }
    }
}

void ReadbackMeasure::Latency::add(const ReadbackJob& job, std::int64_t published, std::uint64_t copiesThen) {
    ++readings;
    handSeconds += readbackSeconds(job.recordedTicks, job.handedTicks);
    const double publish = readbackSeconds(job.recordedTicks, published);
    publishSeconds += publish;
    maxPublishSeconds = std::max(maxPublishSeconds, publish);
    handCopies += job.handedCopy - job.recordedCopy;
    const std::uint64_t copies = copiesThen - job.recordedCopy;
    publishCopies += copies;
    maxPublishCopies = std::max(maxPublishCopies, copies);
}

ReadbackResult ReadbackMeasure::measure(const ReadbackJob& job) {
    switch (job.kind) {
    case ReadbackJob::Kind::Wheel:
        return wheel(job);
    case ReadbackJob::Kind::Corners:
        return corners(job);
    case ReadbackJob::Kind::WheelDown:
        break;
    }
    // The wheel went down: the readings of this time up in one line (their time, for the cost).
    present_.merge(job.present);
    if (readings_ > loggedReadings_) {
        logStats(true);
    }
    return ReadbackResult{job.kind, job.width, job.height, std::nullopt, false};
}

ReadbackResult ReadbackMeasure::wheel(const ReadbackJob& job) {
    present_.merge(job.present);
    if (job.gpuSeconds >= 0.0) {
        gpuSeconds_ += job.gpuSeconds;
        maxGpuSeconds_ = std::max(maxGpuSeconds_, job.gpuSeconds);
        ++gpuReadings_;
    }
    const std::int64_t start = readbackTicks();
    readReadbackGrid(job.bytes, wheelReadbackRows(job.width, job.height), grid_);
    const std::optional<ui_layer::PixelRect> found =
        ui_layer::measureWheel(grid_, job.width, job.height, &scratch_);
    const std::int64_t end = readbackTicks();
    const double seconds = readbackSeconds(start, end);
    measureSeconds_ += seconds;
    maxMeasureSeconds_ = std::max(maxMeasureSeconds_, seconds);
    ++readings_;
    if (found) {
        ++found_;
        lastRect_ = *found;
        std::lock_guard lock(g_latestLock);
        ++g_latest.serial;
        g_latest.rect = *found;
        g_latest.width = job.width;
        g_latest.height = job.height;
    }
    latency_.add(job, readbackTicks(), guiCopies.load(std::memory_order_relaxed));
    logStats(found_ == 1 && found);
    return ReadbackResult{job.kind, job.width, job.height, found, false};
}

void ReadbackMeasure::logStats(bool force) {
    if (statsLogged_ >= kStatsLines || (!force && readings_ - loggedReadings_ < kReadingsPerLine)) {
        return;
    }
    ++statsLogged_;
    const std::uint64_t n = readings_ - loggedReadings_;
    const Latency& l = latency_;
    EVR_LOG(
        "ui: weapon wheel bounds: %llu reading(s) in all, %llu with the wheel found; the last at GUI rect "
        "(%d, %d) %u x %u; since the last line (%llu reading(s) over %llu frame(s) with the wheel up): "
        "measuring (its own thread) %.0f us a reading on average, %.0f us at most; present hook %.1f us a "
        "frame on average, %.0f us at most (%llu read(s) with no slot free; recording a reading's copy %.0f "
        "us on average, %.0f us at most); published %.2f ms after the "
        "copy on average (%.2f GUI copies; %.2f ms and %.2f copies to the hand-off), %.2f ms and %llu "
        "copies at most; GPU copy %.1f us on average, %.1f us at most (%llu timed)",
        static_cast<unsigned long long>(readings_), static_cast<unsigned long long>(found_), lastRect_.x,
        lastRect_.y, lastRect_.width, lastRect_.height, static_cast<unsigned long long>(n),
        static_cast<unsigned long long>(present_.copies), perReading(measureSeconds_, n) * 1e6,
        maxMeasureSeconds_ * 1e6, perReading(present_.seconds, present_.copies) * 1e6,
        present_.maxSeconds * 1e6, static_cast<unsigned long long>(present_.noSlot),
        perReading(present_.recordSeconds, present_.records) * 1e6, present_.maxRecordSeconds * 1e6,
        perReading(l.publishSeconds, l.readings) * 1e3,
        perReading(static_cast<double>(l.publishCopies), l.readings),
        perReading(l.handSeconds, l.readings) * 1e3,
        perReading(static_cast<double>(l.handCopies), l.readings), l.maxPublishSeconds * 1e3,
        static_cast<unsigned long long>(l.maxPublishCopies), perReading(gpuSeconds_, gpuReadings_) * 1e6,
        maxGpuSeconds_ * 1e6, static_cast<unsigned long long>(gpuReadings_));
    loggedReadings_ = readings_;
    present_ = PresentCost{};
    latency_ = Latency{};
    measureSeconds_ = 0.0;
    maxMeasureSeconds_ = 0.0;
    gpuSeconds_ = 0.0;
    maxGpuSeconds_ = 0.0;
    gpuReadings_ = 0;
}

} // namespace evr::vkcore
