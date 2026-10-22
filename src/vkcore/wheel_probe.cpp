#include "vkcore/wheel_probe.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

namespace evr::vkcore {

namespace {

std::mutex g_latestLock;
ui_layer::WheelMeasurement g_latest;

double qpcToSeconds(LONGLONG ticks) {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return static_cast<double>(ticks) / static_cast<double>(frequency);
}

LONGLONG qpc() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

// How many summary lines the log gets (one when the wheel goes down, and every kReadingsPerLine readings).
constexpr std::uint32_t kStatsLines = 30;
constexpr std::uint64_t kReadingsPerLine = 600;

} // namespace

ui_layer::WheelMeasurement WheelProbe::latest() {
    std::lock_guard lock(g_latestLock);
    return g_latest;
}

bool WheelProbe::ensureBuffer(DeviceData& dev, VkExtent2D extent) {
    if (failed_) {
        return false;
    }
    if (buffer_ && extent.width == bufferExtent_.width && extent.height == bufferExtent_.height) {
        return true;
    }
    for (const Reading& r : slots_) {
        if (r.state == State::Submitted) {
            return false; // a new size waits for the old readings
        }
    }
    freeBuffer(dev);
    const ui_layer::PixelRect area = ui_layer::wheelSearchRect(extent.width, extent.height);
    const std::uint32_t rows = ui_layer::gridRows(area, ui_layer::kWheelGridStep);
    readingBytes_ = VkDeviceSize{area.width} * 4 * rows;
    if (readingBytes_ == 0) {
        return false;
    }
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = readingBytes_ * kReadings;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bool ok = dev.vk.CreateBuffer(dev.device, &info, nullptr, &buffer_) == VK_SUCCESS;
    VkMemoryRequirements req{};
    std::uint32_t type = UINT32_MAX;
    if (ok) {
        dev.vk.GetBufferMemoryRequirements(dev.device, buffer_, &req);
        VkPhysicalDeviceMemoryProperties props{};
        dev.instance->vk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &props);
        // Host-visible and cached: the grid is read sample by sample on the present hook, which uncached
        // (write-combined) memory would make slow.
        constexpr VkMemoryPropertyFlags kWanted =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        for (std::uint32_t t = 0; t < props.memoryTypeCount; ++t) {
            const VkMemoryPropertyFlags f = props.memoryTypes[t].propertyFlags;
            if ((req.memoryTypeBits & (1u << t)) && (f & kWanted) == kWanted) {
                type = t;
                coherent_ = (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
                break;
            }
        }
        if (type == UINT32_MAX) {
            EVR_LOG("ui: weapon wheel bounds: the GPU has no cached host memory to read them from; the wheel "
                    "stays where the game draws it");
            freeBuffer(dev);
            failed_ = true;
            return false;
        }
    }
    if (ok) {
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = type;
        ok = dev.vk.AllocateMemory(dev.device, &alloc, nullptr, &memory_) == VK_SUCCESS &&
             dev.vk.BindBufferMemory(dev.device, buffer_, memory_, 0) == VK_SUCCESS &&
             dev.vk.MapMemory(dev.device, memory_, 0, VK_WHOLE_SIZE, 0, &mapped_) == VK_SUCCESS;
    }
    if (!ok) {
        EVR_LOG("ui: no host buffer for the weapon wheel's bounds; the wheel stays where the game draws it");
        freeBuffer(dev);
        failed_ = true;
        return false;
    }
    bufferExtent_ = extent;
    EVR_LOG("ui: weapon wheel bounds: GUI rows read while the wheel is up: (%d, %d) %u x %u, every %u-th row "
            "(%u rows, %llu KiB per reading)",
            area.x, area.y, area.width, area.height, ui_layer::kWheelGridStep, rows,
            static_cast<unsigned long long>(readingBytes_ / 1024));
    return true;
}

bool WheelProbe::ensureTiming(DeviceData& dev, std::uint32_t family) {
    Timing& t = timing_;
    if (!t.tried) {
        t.tried = true;
        const auto load = [&dev](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(
                dev.nextGetDeviceProcAddr(dev.device, name));
            return fn != nullptr;
        };
        VkPhysicalDeviceProperties props{};
        dev.instance->vk.GetPhysicalDeviceProperties(dev.physicalDevice, &props);
        t.periodNs = props.limits.timestampPeriod;
        std::uint32_t count = 0;
        dev.instance->vk.GetPhysicalDeviceQueueFamilyProperties(dev.physicalDevice, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        dev.instance->vk.GetPhysicalDeviceQueueFamilyProperties(dev.physicalDevice, &count, families.data());
        for (const VkQueueFamilyProperties& f : families) {
            t.validBits.push_back(f.timestampValidBits);
        }
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = kReadings * 2;
        const bool ok = load(t.createQueryPool, "vkCreateQueryPool") &&
                        load(t.destroyQueryPool, "vkDestroyQueryPool") &&
                        load(t.getQueryPoolResults, "vkGetQueryPoolResults") &&
                        load(t.cmdResetQueryPool, "vkCmdResetQueryPool") &&
                        load(t.cmdWriteTimestamp, "vkCmdWriteTimestamp") && t.periodNs > 0.0 &&
                        t.createQueryPool(dev.device, &info, nullptr, &t.pool) == VK_SUCCESS;
        if (!ok) {
            t.pool = VK_NULL_HANDLE;
            EVR_LOG("ui: weapon wheel bounds: no timestamp queries; the readback's GPU time is not measured");
        }
    }
    return t.pool && family < t.validBits.size() && t.validBits[family] > 0;
}

bool WheelProbe::record(DeviceData& dev,
                        VkCommandBuffer cb,
                        std::uint32_t family,
                        VkImage source,
                        VkExtent2D extent,
                        bool wheelUp) {
    const bool read = cadence_.next(wheelUp);
    if (!wheelUp) {
        // The wheel went down: the readings of this time up in one line (their time, for the cost).
        if (readings_ > loggedReadings_) {
            logStats(true);
        }
        return false;
    }
    ++framesUp_;
    if (!read) {
        return false;
    }
    Reading* slot = nullptr;
    std::uint32_t index = 0;
    for (std::uint32_t i = 0; i < kReadings; ++i) {
        if (slots_[i].state == State::Recorded) {
            slots_[i].state = State::Idle; // recorded but never marked submitted or cancelled
        }
        if (!slot && slots_[i].state == State::Idle) {
            slot = &slots_[i];
            index = i;
        }
    }
    if (!slot || !ensureBuffer(dev, extent)) {
        return false;
    }
    const ui_layer::PixelRect area = ui_layer::wheelSearchRect(extent.width, extent.height);
    const std::uint32_t rows = ui_layer::gridRows(area, ui_layer::kWheelGridStep);
    const VkDeviceSize base = readingBytes_ * index;
    const bool timed = ensureTiming(dev, family);
    if (timed) {
        // Timestamps at the transfer stage before and after the copy: the first once the transfers recorded
        // before it (the GUI copy) are done, the second once this copy is; the buffer barrier comes after.
        timing_.cmdResetQueryPool(cb, timing_.pool, index * 2, 2);
        timing_.cmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, timing_.pool, index * 2);
    }
    regions_.resize(rows);
    for (std::uint32_t r = 0; r < rows; ++r) {
        VkBufferImageCopy& region = regions_[r];
        region.bufferOffset = base + VkDeviceSize{area.width} * 4 * r;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {area.x, area.y + static_cast<std::int32_t>(r * ui_layer::kWheelGridStep), 0};
        region.imageExtent = {area.width, 1, 1};
    }
    dev.vk.CmdCopyImageToBuffer(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_, rows,
                                regions_.data());
    VkBufferMemoryBarrier toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.buffer = buffer_;
    toHost.offset = base;
    toHost.size = readingBytes_;
    if (timed) {
        timing_.cmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, timing_.pool, index * 2 + 1);
    }
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                              1, &toHost, 0, nullptr);
    slot->state = State::Recorded;
    slot->extent = extent;
    slot->timed = timed;
    return true;
}

void WheelProbe::submitted(std::uint64_t timelineValue) {
    for (Reading& r : slots_) {
        if (r.state == State::Recorded) {
            r.state = State::Submitted;
            r.value = timelineValue;
        }
    }
}

void WheelProbe::cancel() {
    for (Reading& r : slots_) {
        if (r.state == State::Recorded) {
            r.state = State::Idle;
        }
    }
}

void WheelProbe::poll(DeviceData& dev, std::uint64_t completedTimeline) {
    std::vector<std::uint32_t> done;
    for (std::uint32_t i = 0; i < kReadings; ++i) {
        if (slots_[i].state == State::Submitted && slots_[i].value <= completedTimeline) {
            done.push_back(i);
        }
    }
    if (done.empty() || !mapped_) {
        return;
    }
    std::sort(done.begin(), done.end(),
              [this](std::uint32_t a, std::uint32_t b) { return slots_[a].value < slots_[b].value; });
    if (!coherent_) {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = memory_;
        range.size = VK_WHOLE_SIZE;
        dev.vk.InvalidateMappedMemoryRanges(dev.device, 1, &range);
    }
    double pollSeconds = 0.0;
    for (const std::uint32_t i : done) {
        slots_[i].state = State::Idle;
        const VkExtent2D extent = slots_[i].extent;
        std::array<std::uint64_t, 2> stamps{};
        if (slots_[i].timed &&
            timing_.getQueryPoolResults(dev.device, timing_.pool, i * 2, 2, sizeof(stamps), stamps.data(),
                                        sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            stamps[1] >= stamps[0]) {
            const double gpu = static_cast<double>(stamps[1] - stamps[0]) * timing_.periodNs * 1e-9;
            gpuSeconds_ += gpu;
            maxGpuSeconds_ = std::max(maxGpuSeconds_, gpu);
            ++gpuReadings_;
        }
        const LONGLONG start = qpc();
        ui_layer::AlphaGrid& grid = grid_;
        grid.area = ui_layer::wheelSearchRect(extent.width, extent.height);
        grid.step = ui_layer::kWheelGridStep;
        grid.columns = ui_layer::gridColumns(grid.area, grid.step);
        grid.rows = ui_layer::gridRows(grid.area, grid.step);
        grid.alpha.resize(std::size_t{grid.columns} * grid.rows);
        const auto* bytes = static_cast<const std::uint8_t*>(mapped_) + readingBytes_ * i;
        const std::size_t rowBytes = std::size_t{grid.area.width} * 4;
        for (std::uint32_t r = 0; r < grid.rows; ++r) {
            const std::uint8_t* row = bytes + rowBytes * r;
            for (std::uint32_t c = 0; c < grid.columns; ++c) {
                grid.alpha[std::size_t{r} * grid.columns + c] = row[std::size_t{c} * grid.step * 4 + 3];
            }
        }
        const std::optional<ui_layer::PixelRect> wheel =
            ui_layer::measureWheel(grid, extent.width, extent.height, &scratch_);
        const double seconds = qpcToSeconds(qpc() - start);
        measureSeconds_ += seconds;
        pollSeconds += seconds;
        cadence_.measured(wheel);
        maxMeasureSeconds_ = std::max(maxMeasureSeconds_, seconds);
        ++readings_;
        if (wheel) {
            ++found_;
            lastRect_ = *wheel;
            std::lock_guard lock(g_latestLock);
            ++g_latest.serial;
            g_latest.rect = *wheel;
            g_latest.width = extent.width;
            g_latest.height = extent.height;
        }
        maxPollSeconds_ = std::max(maxPollSeconds_, pollSeconds);
        logStats(found_ == 1 && wheel);
    }
}

void WheelProbe::logStats(bool force) {
    if (statsLogged_ >= kStatsLines || (!force && readings_ - loggedReadings_ < kReadingsPerLine)) {
        return;
    }
    ++statsLogged_;
    const std::uint64_t n = readings_ - loggedReadings_;
    EVR_LOG(
        "ui: weapon wheel bounds: %llu reading(s) in all, %llu with the wheel found; the last at GUI rect "
        "(%d, %d) %u x %u; since the last line (%llu reading(s) over %llu frame(s) with the wheel up): CPU "
        "%.0f us a reading on average, %.0f us at most; %.1f us a frame on average, %.0f us at most; GPU "
        "copy %.1f us on average, %.1f us at most (%llu timed)",
        static_cast<unsigned long long>(readings_), static_cast<unsigned long long>(found_), lastRect_.x,
        lastRect_.y, lastRect_.width, lastRect_.height, static_cast<unsigned long long>(n),
        static_cast<unsigned long long>(framesUp_), n ? measureSeconds_ / static_cast<double>(n) * 1e6 : 0.0,
        maxMeasureSeconds_ * 1e6, framesUp_ ? measureSeconds_ / static_cast<double>(framesUp_) * 1e6 : 0.0,
        maxPollSeconds_ * 1e6, gpuReadings_ ? gpuSeconds_ / static_cast<double>(gpuReadings_) * 1e6 : 0.0,
        maxGpuSeconds_ * 1e6, static_cast<unsigned long long>(gpuReadings_));
    loggedReadings_ = readings_;
    framesUp_ = 0;
    maxPollSeconds_ = 0.0;
    measureSeconds_ = 0.0;
    maxMeasureSeconds_ = 0.0;
    gpuSeconds_ = 0.0;
    maxGpuSeconds_ = 0.0;
    gpuReadings_ = 0;
}

void WheelProbe::destroy(DeviceData& dev) {
    freeBuffer(dev);
    destroyTiming(dev);
}

void WheelProbe::freeBuffer(DeviceData& dev) {
    if (mapped_) {
        dev.vk.UnmapMemory(dev.device, memory_);
        mapped_ = nullptr;
    }
    if (buffer_) {
        dev.vk.DestroyBuffer(dev.device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_) {
        dev.vk.FreeMemory(dev.device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    slots_ = {};
    bufferExtent_ = {};
}

void WheelProbe::destroyTiming(DeviceData& dev) {
    if (timing_.pool) {
        timing_.destroyQueryPool(dev.device, timing_.pool, nullptr);
    }
    timing_ = Timing{};
}

} // namespace evr::vkcore
