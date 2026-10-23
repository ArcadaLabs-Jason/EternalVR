// The corner blocks' reading of the GUI readback (wheel_probe.hpp, ui_layer/hud_blocks.hpp): in wrist or
// weapon mode, outside menus and full-screen backdrops with the wheel down, a GUI copy now and then copies
// every kWheelGridStep-th row of the band's two bottom corners into the reading's slot (the vitals' rows,
// then the weapon block's); once done, the measuring thread (wheel_probe_measure.hpp) measures each corner's
// grid and publishes the reading for the XR worker (latestCorners()).

#include "vkcore/wheel_probe.hpp"

#include "vkcore/log.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace evr::vkcore {

namespace {

std::mutex g_cornersLock;
ui_layer::CornerMeasurement g_corners;

// The log: the first reading that finds both blocks, then a line every kCornerReadingsPerLine readings (at
// most kCornerLines in all).
constexpr std::uint64_t kCornerReadingsPerLine = 60;
constexpr std::uint32_t kCornerLines = 20;

bool sameRect(const ui_layer::PixelRect& a, const ui_layer::PixelRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

ui_layer::CornerMeasurement WheelProbe::latestCorners() {
    std::lock_guard lock(g_cornersLock);
    return g_corners;
}

// ---- Present hook ------------------------------------------------------------------------------------

bool WheelProbe::recordCorners(DeviceData& dev,
                               VkCommandBuffer cb,
                               std::uint32_t family,
                               VkImage source,
                               VkExtent2D extent,
                               bool wanted) {
    if (!cornerCadence_.due(wanted, extent.width, extent.height)) {
        return false;
    }
    const std::uint32_t index = freeSlot();
    if (index == readback::kNoSlot || !ensureBuffer(dev, extent)) {
        ++cornerCost_.noSlot;
        return false; // still due: the next copy reads
    }
    cornerCadence_.consume();
    const std::array<ReadbackRows, 2> parts = cornerReadbackRows(extent.width, extent.height);
    recordCopy(dev, cb, family, source, index, parts.data(), static_cast<std::uint32_t>(parts.size()),
               cornerCost_);
    slots_[index].kind = ReadbackJob::Kind::Corners;
    slots_[index].extent = extent;
    return true;
}

// ---- Measuring thread --------------------------------------------------------------------------------

ReadbackResult ReadbackMeasure::corners(const ReadbackJob& job) {
    CornerStats& s = cornerSince_;
    s.present.merge(job.present);
    const std::int64_t start = readbackTicks();
    const std::array<ReadbackRows, 2> parts = cornerReadbackRows(job.width, job.height);
    readReadbackGrid(job.bytes, parts[0], cornerGrids_[0]);
    readReadbackGrid(job.bytes, parts[1], cornerGrids_[1]);
    const double read = readbackSeconds(start, readbackTicks());
    ui_layer::CornerMeasurement m;
    m.vitals = ui_layer::measureCornerBlock(cornerGrids_[0], ui_layer::WristBlock::Vitals, job.width,
                                            job.height, &scratch_);
    m.weapon = ui_layer::measureCornerBlock(cornerGrids_[1], ui_layer::WristBlock::Weapon, job.width,
                                            job.height, &scratch_);
    m.width = job.width;
    m.height = job.height;
    const double cpu = readbackSeconds(start, readbackTicks());
    const bool both = m.vitals && m.weapon;
    {
        std::lock_guard lock(g_cornersLock);
        m.serial = g_corners.serial + 1;
        g_corners = m;
    }
    s.latency.add(job, readbackTicks(), guiCopies.load(std::memory_order_relaxed));
    ++s.readings;
    ++cornerReadings_;
    s.both += both ? 1 : 0;
    s.none += !m.vitals && !m.weapon ? 1 : 0;
    s.cpuSeconds += cpu;
    s.maxCpuSeconds = std::max(s.maxCpuSeconds, cpu);
    s.readSeconds += read;
    if (job.gpuSeconds >= 0.0) {
        s.gpuSeconds += job.gpuSeconds;
        s.maxGpuSeconds = std::max(s.maxGpuSeconds, job.gpuSeconds);
        ++s.gpuReadings;
    }
    const ReadbackResult result{job.kind, job.width, job.height, std::nullopt, both};
    if (both && (!sameRect(*m.vitals, lastCornerVitals_) || !sameRect(*m.weapon, lastCornerWeapon_))) {
        // Where the blocks are, each time it changes (a HUD Scale, the extra lives row, the equipment row).
        lastCornerVitals_ = *m.vitals;
        lastCornerWeapon_ = *m.weapon;
        logCornerStats(true);
        return result;
    }
    logCornerStats(false);
    return result;
}

void ReadbackMeasure::logCornerStats(bool force) {
    CornerStats& s = cornerSince_;
    if (cornerLines_ >= kCornerLines || (!force && s.readings < kCornerReadingsPerLine)) {
        return;
    }
    ++cornerLines_;
    const auto n = static_cast<double>(std::max<std::uint64_t>(s.readings, 1));
    const auto copies = static_cast<double>(std::max<std::uint64_t>(s.present.copies, 1));
    const Latency& l = s.latency;
    const auto readings = static_cast<double>(std::max<std::uint64_t>(l.readings, 1));
    EVR_LOG(
        "ui: corner blocks: %llu reading(s) in all; health and armor at GUI rect (%d, %d) %u x %u, ammo "
        "at (%d, %d) %u x %u; since the last line %llu reading(s) over %llu GUI copies in wrist or weapon "
        "mode outside menus (%llu with both blocks, %llu with neither): measuring (its own thread) %.0f us "
        "a reading on average (%.0f reading the grid), %.0f us at most; present hook %.2f us a copy on "
        "average, %.0f us at most (%llu read(s) with no slot free; recording a reading's copy %.0f us on "
        "average, %.0f us at most); published %.2f ms after the copy on average (%.2f GUI copies; %.2f ms "
        "and %.2f copies to the hand-off), %.2f ms and %llu copies at "
        "most; GPU copy %.1f us on average, %.1f us at most (%llu timed)",
        static_cast<unsigned long long>(cornerReadings_), lastCornerVitals_.x, lastCornerVitals_.y,
        lastCornerVitals_.width, lastCornerVitals_.height, lastCornerWeapon_.x, lastCornerWeapon_.y,
        lastCornerWeapon_.width, lastCornerWeapon_.height, static_cast<unsigned long long>(s.readings),
        static_cast<unsigned long long>(s.present.copies), static_cast<unsigned long long>(s.both),
        static_cast<unsigned long long>(s.none), s.cpuSeconds / n * 1e6, s.readSeconds / n * 1e6,
        s.maxCpuSeconds * 1e6, s.present.seconds / copies * 1e6, s.present.maxSeconds * 1e6,
        static_cast<unsigned long long>(s.present.noSlot),
        s.present.records ? s.present.recordSeconds / static_cast<double>(s.present.records) * 1e6 : 0.0,
        s.present.maxRecordSeconds * 1e6, l.publishSeconds / readings * 1e3,
        static_cast<double>(l.publishCopies) / readings, l.handSeconds / readings * 1e3,
        static_cast<double>(l.handCopies) / readings, l.maxPublishSeconds * 1e3,
        static_cast<unsigned long long>(l.maxPublishCopies),
        s.gpuReadings ? s.gpuSeconds / static_cast<double>(s.gpuReadings) * 1e6 : 0.0, s.maxGpuSeconds * 1e6,
        static_cast<unsigned long long>(s.gpuReadings));
    s = CornerStats{};
}

} // namespace evr::vkcore
