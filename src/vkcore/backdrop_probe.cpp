#include "vkcore/backdrop_probe.hpp"

#include "vkcore/log.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace evr::vkcore {

bool BackdropProbe::ensureBuffer(DeviceData& dev) {
    if (buffer_ || failed_) {
        return buffer_ != VK_NULL_HANDLE;
    }
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = kReadingBytes * kReadings;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bool ok = dev.vk.CreateBuffer(dev.device, &info, nullptr, &buffer_) == VK_SUCCESS;
    VkMemoryRequirements req{};
    std::uint32_t type = UINT32_MAX;
    if (ok) {
        dev.vk.GetBufferMemoryRequirements(dev.device, buffer_, &req);
        VkPhysicalDeviceMemoryProperties props{};
        dev.instance->vk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &props);
        // Host-visible, coherent where there is such a type.
        for (std::uint32_t t = 0; t < props.memoryTypeCount; ++t) {
            const VkMemoryPropertyFlags f = props.memoryTypes[t].propertyFlags;
            if ((req.memoryTypeBits & (1u << t)) && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (type == UINT32_MAX || (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                type = t;
                coherent_ = (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
                if (coherent_) {
                    break;
                }
            }
        }
        ok = type != UINT32_MAX;
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
        EVR_LOG("ui: no host buffer for the backdrop test; the crosshair mask ignores backdrops");
        destroy(dev);
        failed_ = true;
    }
    return ok;
}

bool BackdropProbe::record(
    DeviceData& dev, VkCommandBuffer cb, VkImage source, VkExtent2D extent, float maskFraction) {
    const auto points =
        ui_layer::backdropProbePoints(extent.width, extent.height, maskFraction, kMarginPixels);
    if (!points) {
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
    if (!slot || !ensureBuffer(dev)) {
        return false;
    }
    const VkDeviceSize base = kReadingBytes * index;
    std::array<VkBufferImageCopy, ui_layer::kBackdropProbePoints> regions{};
    for (std::size_t i = 0; i < regions.size(); ++i) {
        regions[i].bufferOffset = base + VkDeviceSize{4} * i;
        regions[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        regions[i].imageOffset = {static_cast<std::int32_t>((*points)[i].x),
                                  static_cast<std::int32_t>((*points)[i].y), 0};
        regions[i].imageExtent = {1, 1, 1};
    }
    dev.vk.CmdCopyImageToBuffer(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer_,
                                static_cast<std::uint32_t>(regions.size()), regions.data());
    VkBufferMemoryBarrier toHost{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.buffer = buffer_;
    toHost.offset = base;
    toHost.size = kReadingBytes;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
                              1, &toHost, 0, nullptr);
    slot->state = State::Recorded;
    return true;
}

void BackdropProbe::submitted(std::uint64_t timelineValue) {
    for (Reading& r : slots_) {
        if (r.state == State::Recorded) {
            r.state = State::Submitted;
            r.value = timelineValue;
        }
    }
}

void BackdropProbe::cancel() {
    for (Reading& r : slots_) {
        if (r.state == State::Recorded) {
            r.state = State::Idle;
        }
    }
}

void BackdropProbe::poll(DeviceData& dev, std::uint64_t completedTimeline) {
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
    for (const std::uint32_t i : done) {
        slots_[i].state = State::Idle;
        std::array<std::uint8_t, kReadingBytes> rgba{};
        std::memcpy(rgba.data(), static_cast<const std::uint8_t*>(mapped_) + kReadingBytes * i, rgba.size());
        ++readings_;
        if (!detector_.update(ui_layer::readingShowsBackdrop(rgba))) {
            continue;
        }
        const bool up = detector_.backdrop();
        backdrop_.store(up, std::memory_order_relaxed);
        const bool first = up ? ++ups_ == 1 : ++downs_ == 1;
        if (first) {
            char alpha[64] = {};
            int at = 0;
            for (std::size_t p = 0; p < ui_layer::kBackdropProbePoints && at >= 0; ++p) {
                at += std::snprintf(alpha + at, sizeof(alpha) - static_cast<std::size_t>(at),
                                    p ? " %u" : "%u", static_cast<unsigned>(rgba[p * 4 + 3]));
            }
            EVR_LOG("ui: backdrop test, reading %llu (alpha %s around the crosshair square): %s",
                    static_cast<unsigned long long>(readings_), alpha,
                    up ? "a full-screen backdrop (menu or fade); the square is copied, not masked, while it "
                         "shows"
                       : "the backdrop is gone; the crosshair mask is back under hand aim");
        }
    }
}

void BackdropProbe::destroy(DeviceData& dev) {
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
    detector_.reset();
    backdrop_.store(false, std::memory_order_relaxed);
}

} // namespace evr::vkcore
