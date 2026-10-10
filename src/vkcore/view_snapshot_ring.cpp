#include "vkcore/view_snapshot_impl.hpp"

#include "vkcore/dispatch.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace evr::vkcore::view_snapshot {

namespace {

bool createImage(DeviceData& dev, Ring& ring, std::size_t i) {
    std::vector<std::uint32_t> families(dev.queueFamilyFlags.size());
    for (std::uint32_t f = 0; f < families.size(); ++f) {
        families[f] = f;
    }
    const bool shared = families.size() > 1;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = ring.format;
    info.extent = {ring.extent.width, ring.extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = shared ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    info.queueFamilyIndexCount = shared ? static_cast<std::uint32_t>(families.size()) : 0;
    info.pQueueFamilyIndices = shared ? families.data() : nullptr;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (dev.vk.CreateImage(dev.device, &info, nullptr, &ring.images[i]) != VK_SUCCESS) {
        ring.images[i] = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements req{};
    dev.vk.GetImageMemoryRequirements(dev.device, ring.images[i], &req);
    VkPhysicalDeviceMemoryProperties props{};
    dev.instance->vk.GetPhysicalDeviceMemoryProperties(dev.physicalDevice, &props);
    std::uint32_t type = UINT32_MAX;
    for (std::uint32_t t = 0; t < props.memoryTypeCount && type == UINT32_MAX; ++t) {
        if ((req.memoryTypeBits & (1u << t)) &&
            (props.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            type = t;
        }
    }
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    if (type == UINT32_MAX ||
        dev.vk.AllocateMemory(dev.device, &alloc, nullptr, &ring.memory[i]) != VK_SUCCESS) {
        ring.memory[i] = VK_NULL_HANDLE;
        return false;
    }
    ring.bytes += req.size;
    return dev.vk.BindImageMemory(dev.device, ring.images[i], ring.memory[i], 0) == VK_SUCCESS;
}

} // namespace

const char* makeObjects(DeviceData& dev, Ring& ring, std::uint32_t family) {
    if (!ring.semaphore) {
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &type};
        if (dev.vk.CreateSemaphore(dev.device, &info, nullptr, &ring.semaphore) != VK_SUCCESS) {
            ring.semaphore = VK_NULL_HANDLE;
            return "no timeline semaphore";
        }
    }
    if (ring.pool) {
        return nullptr;
    }
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family;
    if (dev.vk.CreateCommandPool(dev.device, &poolInfo, nullptr, &ring.pool) != VK_SUCCESS) {
        ring.pool = VK_NULL_HANDLE;
        return "no command pool";
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = ring.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = static_cast<std::uint32_t>(ring.cbs.size());
    if (dev.vk.AllocateCommandBuffers(dev.device, &alloc, ring.cbs.data()) != VK_SUCCESS) {
        return "no command buffers";
    }
    for (VkCommandBuffer cb : ring.cbs) {
        dev.setDeviceLoaderData(dev.device, cb); // made by a layer: needs the loader's dispatch pointer
    }
    ring.family = family;
    ring.device = dev.device;
    return nullptr;
}

bool makeImages(DeviceData& dev, Ring& ring, std::size_t count, VkFormat format, VkExtent2D extent) {
    destroyImages(dev, ring);
    ring.format = format;
    ring.extent = extent;
    for (std::size_t i = 0; i < count && i < ring.images.size(); ++i) {
        if (!createImage(dev, ring, i)) {
            destroyImages(dev, ring);
            return false;
        }
    }
    ring.count = count;
    return true;
}

void destroyImages(DeviceData& dev, Ring& ring) {
    for (std::size_t i = 0; i < ring.images.size(); ++i) {
        if (ring.images[i]) {
            dev.vk.DestroyImage(dev.device, ring.images[i], nullptr);
        }
        if (ring.memory[i]) {
            dev.vk.FreeMemory(dev.device, ring.memory[i], nullptr);
        }
        ring.images[i] = VK_NULL_HANDLE;
        ring.memory[i] = VK_NULL_HANDLE;
    }
    ring.count = 0;
    ring.bytes = 0;
}

void dropCommands(DeviceData& dev, Ring& ring) {
    if (ring.pool) {
        dev.vk.DestroyCommandPool(dev.device, ring.pool, nullptr); // frees the command buffers
    }
    ring.pool = VK_NULL_HANDLE;
    ring.cbs = {};
    ring.family = UINT32_MAX;
}

void destroyRing(DeviceData& dev, Ring& ring) {
    destroyImages(dev, ring);
    if (ring.pool) {
        dev.vk.DestroyCommandPool(dev.device, ring.pool, nullptr); // frees the command buffers
    }
    if (ring.semaphore) {
        dev.vk.DestroySemaphore(dev.device, ring.semaphore, nullptr);
    }
    ring = Ring{};
}

bool recordCopy(DeviceData& dev,
                VkCommandBuffer cb,
                const Ring& ring,
                std::size_t slot,
                VkImage source,
                VkImageLayout layout) {
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (dev.vk.BeginCommandBuffer(cb, &begin) != VK_SUCCESS) {
        return false;
    }
    const VkImageLayout readLayout =
        layout == VK_IMAGE_LAYOUT_GENERAL ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    std::array<VkImageMemoryBarrier, 2> before{};
    before[0] = VkImageMemoryBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    before[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    before[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before[0].oldLayout = layout;
    before[0].newLayout = readLayout;
    before[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before[0].image = source;
    before[0].subresourceRange = range;
    before[1] = before[0];
    before[1].srcAccessMask = 0;
    before[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    before[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    before[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    before[1].image = ring.images[slot];
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                              nullptr, 0, nullptr, static_cast<std::uint32_t>(before.size()), before.data());
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {ring.extent.width, ring.extent.height, 1};
    dev.vk.CmdCopyImage(cb, source, readLayout, ring.images[slot], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                        &region);
    std::array<VkImageMemoryBarrier, 2> after = before;
    after[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    after[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    after[0].oldLayout = readLayout;
    after[0].newLayout = layout;
    after[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    after[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    after[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    after[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                              nullptr, 0, nullptr, static_cast<std::uint32_t>(after.size()), after.data());
    return dev.vk.EndCommandBuffer(cb) == VK_SUCCESS;
}

} // namespace evr::vkcore::view_snapshot
