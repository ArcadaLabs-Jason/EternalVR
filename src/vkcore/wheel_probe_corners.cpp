// The corner blocks' reading of the GUI readback (wheel_probe.hpp, ui_layer/hud_blocks.hpp): in wrist or
// weapon mode, outside menus and full-screen backdrops with the wheel down, a GUI copy now and then copies
// every kWheelGridStep-th row of the band's two bottom corners into the reading's slot (the vitals' rows,
// then the weapon block's); once done, each corner's grid is measured and the reading published for the XR
// worker (latestCorners()).

#include "vkcore/wheel_probe.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>

namespace evr::vkcore {

namespace {

std::mutex g_cornersLock;
ui_layer::CornerMeasurement g_corners;

double secondsSince(LONGLONG start) {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - start) / static_cast<double>(frequency);
}

LONGLONG qpcStart() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

// The log: the first reading that finds both blocks, then a line every kCornerReadingsPerLine readings (at
// most kCornerLines in all).
constexpr std::uint64_t kCornerReadingsPerLine = 60;
constexpr std::uint32_t kCornerLines = 20;

} // namespace

ui_layer::CornerMeasurement WheelProbe::latestCorners() {
    std::lock_guard lock(g_cornersLock);
    return g_corners;
}

bool WheelProbe::recordCorners(DeviceData& dev,
                               VkCommandBuffer cb,
                               std::uint32_t family,
                               VkImage source,
                               VkExtent2D extent,
                               bool wanted) {
    cornerSince_.wantedCopies += wanted ? 1 : 0;
    if (!cornerCadence_.due(wanted, extent.width, extent.height)) {
        return false;
    }
    std::uint32_t index = 0;
    Reading* slot = freeSlot(index);
    if (!slot || !ensureBuffer(dev, extent)) {
        return false; // still due: the next copy reads
    }
    cornerCadence_.consume();
    const Rows vitals =
        rowsOf(ui_layer::cornerSearchRect(ui_layer::WristBlock::Vitals, extent.width, extent.height), 0);
    const Rows weapon =
        rowsOf(ui_layer::cornerSearchRect(ui_layer::WristBlock::Weapon, extent.width, extent.height),
               VkDeviceSize{vitals.area.width} * 4 * vitals.rows);
    const std::array<Rows, 2> parts{vitals, weapon};
    recordCopy(dev, cb, family, source, index, parts.data(), static_cast<std::uint32_t>(parts.size()));
    slot->kind = Kind::Corners;
    slot->extent = extent;
    return true;
}

void WheelProbe::measureCorners(const std::uint8_t* bytes, VkExtent2D extent, double gpuSeconds) {
    const LONGLONG start = qpcStart();
    const Rows vitals =
        rowsOf(ui_layer::cornerSearchRect(ui_layer::WristBlock::Vitals, extent.width, extent.height), 0);
    const Rows weapon =
        rowsOf(ui_layer::cornerSearchRect(ui_layer::WristBlock::Weapon, extent.width, extent.height),
               VkDeviceSize{vitals.area.width} * 4 * vitals.rows);
    readGrid(bytes, vitals, cornerGrids_[0]);
    readGrid(bytes, weapon, cornerGrids_[1]);
    const double read = secondsSince(start);
    ui_layer::CornerMeasurement m;
    m.vitals = ui_layer::measureCornerBlock(cornerGrids_[0], ui_layer::WristBlock::Vitals, extent.width,
                                            extent.height, &scratch_);
    m.weapon = ui_layer::measureCornerBlock(cornerGrids_[1], ui_layer::WristBlock::Weapon, extent.width,
                                            extent.height, &scratch_);
    m.width = extent.width;
    m.height = extent.height;
    const double cpu = secondsSince(start);
    const bool both = m.vitals && m.weapon;
    cornerCadence_.measured(extent.width, extent.height, both);
    {
        std::lock_guard lock(g_cornersLock);
        m.serial = g_corners.serial + 1;
        g_corners = m;
    }
    CornerStats& s = cornerSince_;
    ++s.readings;
    ++cornerReadings_;
    s.both += both ? 1 : 0;
    s.none += !m.vitals && !m.weapon ? 1 : 0;
    s.cpuSeconds += cpu;
    s.maxCpuSeconds = std::max(s.maxCpuSeconds, cpu);
    s.readSeconds += read;
    if (gpuSeconds >= 0.0) {
        s.gpuSeconds += gpuSeconds;
        s.maxGpuSeconds = std::max(s.maxGpuSeconds, gpuSeconds);
        ++s.gpuReadings;
    }
    if (both &&
        (m.vitals->x != lastCornerVitals_.x || m.vitals->y != lastCornerVitals_.y ||
         m.vitals->width != lastCornerVitals_.width || m.vitals->height != lastCornerVitals_.height ||
         m.weapon->x != lastCornerWeapon_.x || m.weapon->y != lastCornerWeapon_.y ||
         m.weapon->width != lastCornerWeapon_.width || m.weapon->height != lastCornerWeapon_.height)) {
        // Where the blocks are, each time it changes (a HUD Scale, the extra lives row, the equipment row).
        lastCornerVitals_ = *m.vitals;
        lastCornerWeapon_ = *m.weapon;
        logCornerStats(true);
        return;
    }
    logCornerStats(false);
}

void WheelProbe::logCornerStats(bool force) {
    CornerStats& s = cornerSince_;
    if (cornerLines_ >= kCornerLines || (!force && s.readings < kCornerReadingsPerLine)) {
        return;
    }
    ++cornerLines_;
    const auto n = static_cast<double>(std::max<std::uint64_t>(s.readings, 1));
    const auto copies = static_cast<double>(std::max<std::uint64_t>(s.wantedCopies, 1));
    EVR_LOG(
        "ui: corner blocks: %llu reading(s) in all; health and armor at GUI rect (%d, %d) %u x %u, ammo "
        "at (%d, %d) %u x %u; since the last line %llu reading(s) over %llu GUI copies in wrist or weapon "
        "mode outside menus (%llu with both blocks, %llu with neither): CPU %.0f us a reading on average "
        "(%.0f "
        "reading the grid), %.0f us at most (%.2f us a copy on average); GPU copy %.1f us on average, %.1f "
        "us at most (%llu timed)",
        static_cast<unsigned long long>(cornerReadings_), lastCornerVitals_.x, lastCornerVitals_.y,
        lastCornerVitals_.width, lastCornerVitals_.height, lastCornerWeapon_.x, lastCornerWeapon_.y,
        lastCornerWeapon_.width, lastCornerWeapon_.height, static_cast<unsigned long long>(s.readings),
        static_cast<unsigned long long>(s.wantedCopies), static_cast<unsigned long long>(s.both),
        static_cast<unsigned long long>(s.none), s.cpuSeconds / n * 1e6, s.readSeconds / n * 1e6,
        s.maxCpuSeconds * 1e6, s.cpuSeconds / copies * 1e6,
        s.gpuReadings ? s.gpuSeconds / static_cast<double>(s.gpuReadings) * 1e6 : 0.0, s.maxGpuSeconds * 1e6,
        static_cast<unsigned long long>(s.gpuReadings));
    s = CornerStats{};
}

} // namespace evr::vkcore
