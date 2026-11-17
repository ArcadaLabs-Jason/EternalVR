#include "vkcore/gun_border_probe.hpp"

#include "ui_layer/gun_crosshair.hpp"
#include "vkcore/log.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>

namespace evr::vkcore {

bool GunBorderProbe::ensureBuffer(DeviceData& dev) {
    if (buffer_ || failed_) {
        return buffer_ != VK_NULL_HANDLE;
    }
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = kReadingBytes * kRingSize;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bool ok = dev.vk.CreateBuffer(dev.device, &info, nullptr, &buffer_) == VK_SUCCESS;
    VkMemoryRequirements req{};
    std::uint32_t type = UINT32_MAX;
    if (ok) {
        dev.vk.GetBufferMemoryRequirements(dev.device, buffer_, &req);
        VkPhysicalDeviceMemoryProperties props{};
        dev.instance->vk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &props);
        constexpr VkMemoryPropertyFlags kWanted =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (std::uint32_t t = 0; t < props.memoryTypeCount; ++t) {
            if ((req.memoryTypeBits & (1u << t)) &&
                (props.memoryTypes[t].propertyFlags & kWanted) == kWanted) {
                type = t;
                break;
            }
        }
        ok = type != UINT32_MAX;
    }
    void* mapped = nullptr;
    if (ok) {
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = type;
        ok = dev.vk.AllocateMemory(dev.device, &alloc, nullptr, &memory_) == VK_SUCCESS &&
             dev.vk.BindBufferMemory(dev.device, buffer_, memory_, 0) == VK_SUCCESS &&
             dev.vk.MapMemory(dev.device, memory_, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS;
    }
    if (!ok) {
        EVR_LOG("ui: no host-coherent buffer for the gun's crosshair's border test; copies are moved as the "
                "backdrop test decides");
        destroy(dev);
        failed_ = true;
        return false;
    }
    std::memset(mapped, 0, static_cast<std::size_t>(kReadingBytes * kRingSize));
    mapped_.store(static_cast<const std::uint8_t*>(mapped), std::memory_order_release);
    return true;
}

bool GunBorderProbe::record(DeviceData& dev,
                            VkCommandBuffer cb,
                            VkImage source,
                            VkExtent2D extent,
                            const ui_layer::PixelRect& rect,
                            std::uint32_t slot) {
    const auto points = ui_layer::gunCrosshairBorderPoints(rect, extent.width, extent.height,
                                                           ui_layer::kGunBorderMarginPixels);
    if (!points || slot >= kRingSize || !ensureBuffer(dev)) {
        return false;
    }
    const VkDeviceSize base = kReadingBytes * slot;
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
    return true;
}

bool GunBorderProbe::reading(std::uint32_t slot, Reading& out) const {
    const std::uint8_t* mapped = mapped_.load(std::memory_order_acquire);
    if (!mapped || slot >= kRingSize) {
        return false;
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    std::memcpy(out.data(), mapped + kReadingBytes * slot, out.size());
    return true;
}

void GunBorderProbe::destroy(DeviceData& dev) {
    if (mapped_.exchange(nullptr, std::memory_order_acq_rel)) {
        dev.vk.UnmapMemory(dev.device, memory_);
    }
    if (buffer_) {
        dev.vk.DestroyBuffer(dev.device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_) {
        dev.vk.FreeMemory(dev.device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

} // namespace evr::vkcore
