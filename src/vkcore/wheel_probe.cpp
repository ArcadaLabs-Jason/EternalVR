#include "vkcore/wheel_probe.hpp"

#include "vkcore/log.hpp"
#include "vkcore/xr_presenter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace evr::vkcore {

WheelProbe::WheelProbe() {
    worker_.setMeasure([this](const ReadbackJob& job) { return measure_.measure(job); });
}

WheelProbe::~WheelProbe() {
    if (processTerminating()) {
        worker_.abandon(); // the thread is gone already and may have held the worker's lock
    }
}

bool WheelProbe::ensureBuffer(DeviceData& dev, VkExtent2D extent) {
    if (failed_) {
        return false;
    }
    if (buffer_ && extent.width == bufferExtent_.width && extent.height == bufferExtent_.height) {
        return true;
    }
    if (ring_.inUse(worker_.busy())) {
        return false; // a new size waits for the old readings, in flight or being measured
    }
    freeBuffer(dev);
    // One slot holds either reading: the wheel's search rectangle, or the two corners one after the other.
    const ReadbackRows wheel = wheelReadbackRows(extent.width, extent.height);
    const std::array<ReadbackRows, 2> corners = cornerReadbackRows(extent.width, extent.height);
    const ReadbackRows& vitals = corners[0];
    const ReadbackRows& weapon = corners[1];
    const VkDeviceSize wheelBytes = VkDeviceSize{wheel.area.width} * 4 * wheel.rows;
    const VkDeviceSize cornerBytes = weapon.offset + VkDeviceSize{weapon.area.width} * 4 * weapon.rows;
    readingBytes_ = std::max(wheelBytes, cornerBytes);
    if (wheelBytes == 0 || cornerBytes == 0) {
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
        // Host-visible and cached: the grid is read sample by sample (on the measuring thread), which
        // uncached (write-combined) memory would make slow.
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
            EVR_LOG("ui: GUI readback: the GPU has no cached host memory to read the GUI image into; the "
                    "wheel stays where the game draws it and the hand HUDs are not shown");
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
        EVR_LOG("ui: no host buffer for the GUI readback; the wheel stays where the game draws it and the "
                "hand HUDs are not shown");
        freeBuffer(dev);
        failed_ = true;
        return false;
    }
    bufferExtent_ = extent;
    EVR_LOG("ui: GUI readback: every %u-th row of (%d, %d) %u x %u while the wheel is up (%llu KiB a "
            "reading); of the corners (%d, %d) %u x %u and (%d, %d) %u x %u now and then in wrist or weapon "
            "mode (%llu KiB a reading)",
            ui_layer::kWheelGridStep, wheel.area.x, wheel.area.y, wheel.area.width, wheel.area.height,
            static_cast<unsigned long long>(wheelBytes / 1024), vitals.area.x, vitals.area.y,
            vitals.area.width, vitals.area.height, weapon.area.x, weapon.area.y, weapon.area.width,
            weapon.area.height, static_cast<unsigned long long>(cornerBytes / 1024));
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
            EVR_LOG("ui: GUI readback: no timestamp queries; the readback's GPU time is not measured");
        }
    }
    return t.pool && family < t.validBits.size() && t.validBits[family] > 0;
}

std::uint32_t WheelProbe::freeSlot() {
    return ring_.pick(worker_.busy());
}

void WheelProbe::recordCopy(DeviceData& dev,
                            VkCommandBuffer cb,
                            std::uint32_t family,
                            VkImage source,
                            std::uint32_t index,
                            const ReadbackRows* parts,
                            std::uint32_t count,
                            PresentCost& cost) {
    const std::int64_t start = readbackTicks();
    const VkDeviceSize base = readingBytes_ * index;
    const bool timed = ensureTiming(dev, family);
    if (timed) {
        // Timestamps at the transfer stage before and after the copy: the first once the transfers recorded
        // before it (the GUI copy) are done, the second once this copy is; the buffer barrier comes after.
        timing_.cmdResetQueryPool(cb, timing_.pool, index * 2, 2);
        timing_.cmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, timing_.pool, index * 2);
    }
    regions_.clear();
    for (std::uint32_t p = 0; p < count; ++p) {
        const ReadbackRows& part = parts[p];
        for (std::uint32_t r = 0; r < part.rows; ++r) {
            VkBufferImageCopy region{};
            region.bufferOffset = base + part.offset + VkDeviceSize{part.area.width} * 4 * r;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageOffset = {part.area.x,
                                  part.area.y + static_cast<std::int32_t>(r * ui_layer::kWheelGridStep), 0};
            region.imageExtent = {part.area.width, 1, 1};
            regions_.push_back(region);
        }
    }
    dev.vk.CmdCopyImageToBuffer(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_,
                                static_cast<std::uint32_t>(regions_.size()), regions_.data());
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
    ring_.recorded(index);
    slots_[index].timed = timed;
    slots_[index].recordedTicks = readbackTicks();
    slots_[index].recordedCopy = copyCount_;
    cost.addRecord(readbackSeconds(start, slots_[index].recordedTicks));
}

bool WheelProbe::record(DeviceData& dev,
                        VkCommandBuffer cb,
                        std::uint32_t family,
                        VkImage source,
                        VkExtent2D extent,
                        bool wheelUp,
                        bool cornersWanted) {
    const std::int64_t start = readbackTicks();
    ++copyCount_;
    measure_.guiCopies.store(copyCount_, std::memory_order_relaxed);
    const bool read = cadence_.next(wheelUp);
    bool recorded = false;
    if (!wheelUp) {
        // The wheel went down: its line is logged once its last readings are measured (poll).
        wheelDownDue_ = wheelDownDue_ || wheelWasUp_;
        wheelWasUp_ = false;
        recorded = recordCorners(dev, cb, family, source, extent, cornersWanted);
        if (cornersWanted) {
            cornerCost_.add(pollSeconds_ + readbackSeconds(start, readbackTicks()));
        }
        pollSeconds_ = 0.0;
        return recorded;
    }
    if (wheelDownDue_) {
        handWheelDown(); // up again before the last time up's line went out
    }
    wheelWasUp_ = true;
    const std::uint32_t index = read ? freeSlot() : readback::kNoSlot;
    if (read && index == readback::kNoSlot) {
        ++wheelCost_.noSlot;
    }
    if (index != readback::kNoSlot && ensureBuffer(dev, extent)) {
        const ReadbackRows part = wheelReadbackRows(extent.width, extent.height);
        recordCopy(dev, cb, family, source, index, &part, 1, wheelCost_);
        slots_[index].kind = ReadbackJob::Kind::Wheel;
        slots_[index].extent = extent;
        recorded = true;
    }
    wheelCost_.add(pollSeconds_ + readbackSeconds(start, readbackTicks()));
    pollSeconds_ = 0.0;
    return recorded;
}

void WheelProbe::submitted(std::uint64_t timelineValue) {
    ring_.submitted(timelineValue);
}

void WheelProbe::cancel() {
    ring_.cancel();
}

void WheelProbe::handReading(DeviceData& dev, std::uint32_t index) {
    const Reading& slot = slots_[index];
    ReadbackJob job;
    job.kind = slot.kind;
    job.slot = index;
    job.bytes = static_cast<const std::uint8_t*>(mapped_) + readingBytes_ * index;
    job.width = slot.extent.width;
    job.height = slot.extent.height;
    std::array<std::uint64_t, 2> stamps{};
    if (slot.timed &&
        timing_.getQueryPoolResults(dev.device, timing_.pool, index * 2, 2, sizeof(stamps), stamps.data(),
                                    sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
        stamps[1] >= stamps[0]) {
        job.gpuSeconds = static_cast<double>(stamps[1] - stamps[0]) * timing_.periodNs * 1e-9;
    }
    job.recordedTicks = slot.recordedTicks;
    job.recordedCopy = slot.recordedCopy;
    job.handedTicks = readbackTicks();
    job.handedCopy = copyCount_;
    PresentCost& cost = slot.kind == ReadbackJob::Kind::Corners ? cornerCost_ : wheelCost_;
    job.present = cost;
    cost = PresentCost{};
    worker_.hand(job);
}

bool WheelProbe::wheelInFlight() const {
    for (std::uint32_t i = 0; i < kReadings; ++i) {
        if (ring_.state(i) == readback::SlotRing<kReadings>::State::Submitted &&
            slots_[i].kind == ReadbackJob::Kind::Wheel) {
            return true;
        }
    }
    return false;
}

void WheelProbe::handWheelDown() {
    ReadbackJob job;
    job.kind = ReadbackJob::Kind::WheelDown;
    job.present = wheelCost_;
    wheelCost_ = PresentCost{};
    worker_.hand(job);
    wheelDownDue_ = false;
}

void WheelProbe::poll(DeviceData& dev, std::uint64_t completedTimeline) {
    const std::int64_t start = readbackTicks();
    // What the readings measured since the last poll found, in the order they were made.
    worker_.take(results_);
    for (const ReadbackResult& r : results_) {
        if (r.kind == ReadbackJob::Kind::Wheel) {
            cadence_.measured(r.wheel);
        } else if (r.kind == ReadbackJob::Kind::Corners) {
            cornerCadence_.measured(r.width, r.height, r.both);
        }
    }
    std::array<std::uint32_t, kReadings> done{};
    const std::uint32_t count = ring_.completed(completedTimeline, done);
    if (count > 0 && mapped_) {
        if (!coherent_) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = memory_;
            range.size = VK_WHOLE_SIZE;
            dev.vk.InvalidateMappedMemoryRanges(dev.device, 1, &range);
        }
        for (std::uint32_t k = 0; k < count; ++k) {
            handReading(dev, done[k]);
        }
    }
    if (wheelDownDue_ && !wheelInFlight()) {
        handWheelDown();
    }
    pollSeconds_ += readbackSeconds(start, readbackTicks());
}

void WheelProbe::destroy(DeviceData& dev) {
    worker_.stop(); // nothing reads the host buffer past this
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
    ring_.reset();
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
