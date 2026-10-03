#include "vkcore/presenter_mirror.hpp"

#include "features/render_size/mirror_window.hpp"
#include "vkcore/log.hpp"
#include "vkcore/virtual_client.hpp"

#include <array>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr VkImageSubresourceRange kColour{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

VkImageMemoryBarrier barrier(
    VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = kColour;
    return b;
}

} // namespace

bool DesktopMirror::ensureImage(DeviceData& dev, VkFormat format, VkExtent2D extent) {
    if (image_ && format_ == format && extent_.width == extent.width && extent_.height == extent.height) {
        return true;
    }
    destroy(dev);
    if (failed_) {
        return false;
    }
    // The game presents from more than one queue family: the image is shared by all of them.
    std::vector<std::uint32_t> families(dev.queueFamilyFlags.size());
    for (std::uint32_t i = 0; i < families.size(); ++i) {
        families[i] = i;
    }
    const bool shared = families.size() > 1;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = shared ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    info.queueFamilyIndexCount = shared ? static_cast<std::uint32_t>(families.size()) : 0;
    info.pQueueFamilyIndices = shared ? families.data() : nullptr;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (dev.vk.CreateImage(dev.device, &info, nullptr, &image_) != VK_SUCCESS) {
        image_ = VK_NULL_HANDLE;
        failed_ = true;
        EVR_LOG("mirror: no private image (%ux%u format %d); the window shows the eyes in turn", extent.width,
                extent.height, format);
        return false;
    }
    VkMemoryRequirements req{};
    dev.vk.GetImageMemoryRequirements(dev.device, image_, &req);
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
    if (type == UINT32_MAX || dev.vk.AllocateMemory(dev.device, &alloc, nullptr, &memory_) != VK_SUCCESS ||
        dev.vk.BindImageMemory(dev.device, image_, memory_, 0) != VK_SUCCESS) {
        destroy(dev);
        failed_ = true;
        EVR_LOG("mirror: no memory for the private image; the window shows the eyes in turn");
        return false;
    }
    format_ = format;
    extent_ = extent;
    EVR_LOG("mirror: private image %ux%u format %d", extent.width, extent.height, format);
    return true;
}

bool DesktopMirror::canBlit(DeviceData& dev, VkFormat format) {
    if (blitChecked_ != format) {
        blitChecked_ = format;
        VkFormatProperties props{};
        dev.instance->vk.GetPhysicalDeviceFormatProperties(dev.physicalDevice, format, &props);
        constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                                VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        blitOk_ = (props.optimalTilingFeatures & needed) == needed;
        if (!blitOk_) {
            EVR_LOG("mirror: format %d cannot be blitted; the window shows the whole eye image", format);
        }
    }
    return blitOk_;
}

bool DesktopMirror::blitsOn(DeviceData& dev, std::uint32_t family) {
    if (family < dev.queueFamilyFlags.size() && (dev.queueFamilyFlags[family] & VK_QUEUE_GRAPHICS_BIT)) {
        return true;
    }
    if (!noGraphicsLogged_) {
        noGraphicsLogged_ = true;
        EVR_LOG("mirror: queue family %u has no graphics: no blit on its presents; the window shows the eye "
                "image",
                family);
    }
    return false;
}

bool DesktopMirror::canTakePanel(DeviceData& dev, VkFormat format) {
    if (panelChecked_ != format) {
        panelChecked_ = format;
        // The GUI target's bytes are display-encoded, as the eye images' are: an RGBA8 swapchain takes them
        // as they are (a copy); another UNORM one through a blit, which reorders the channels. An sRGB
        // swapchain of another channel order would re-encode them, so it gets no panel.
        switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            panelOk_ = true;
            break;
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: {
            VkFormatProperties gui{};
            VkFormatProperties swap{};
            dev.instance->vk.GetPhysicalDeviceFormatProperties(dev.physicalDevice, VK_FORMAT_R8G8B8A8_UNORM,
                                                               &gui);
            dev.instance->vk.GetPhysicalDeviceFormatProperties(dev.physicalDevice, format, &swap);
            panelOk_ = (gui.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
                       (swap.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
            break;
        }
        default:
            panelOk_ = false;
            break;
        }
        if (!panelOk_) {
            EVR_LOG("mirror: format %d cannot take the menu panel's image; the window shows the eye in menus",
                    format);
        }
    }
    return panelOk_;
}

void DesktopMirror::setMenu(bool menu) {
    if (menu == menu_) {
        return;
    }
    menu_ = menu;
    kept_ = false; // an eye kept before the menu, or the menu's panel after it, is not shown again
    if (!menu && panelLogged_) {
        EVR_LOG("mirror: the menu is down: the window shows the eye again");
    }
    panelLogged_ = false;
}

stereo_seq::PanelMirror DesktopMirror::plan(bool menu,
                                            stereo_seq::Mirror mirror,
                                            bool gated,
                                            stereo_seq::PresentKind kind,
                                            bool toWindow,
                                            stereo_seq::MirrorStep eyeStep) {
    setMenu(menu && mirror != stereo_seq::Mirror::Off); // `off` stays black in menus too
    if (!menu_) {
        return stereo_seq::PanelMirror{false, eyeStep};
    }
    const stereo_seq::PanelMirror panel = stereo_seq::panelMirror(mirror, gated, kind, toWindow);
    if (panel.keep) {
        kept_ = false; // until keepPanel records this present's GUI (a failed GUI copy leaves it out)
    }
    return panel;
}

bool DesktopMirror::keepPanel(DeviceData& dev,
                              VkCommandBuffer cb,
                              std::uint32_t family,
                              VkImage gui,
                              VkExtent2D guiExtent,
                              VkFormat format,
                              VkExtent2D extent) {
    const bool copy = format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
    if (guiExtent.width != extent.width || guiExtent.height != extent.height || !canTakePanel(dev, format) ||
        (!copy && !blitsOn(dev, family)) || !ensureImage(dev, format, extent)) {
        kept_ = false;
        return false;
    }
    const VkImageMemoryBarrier toWrite =
        barrier(image_, inGeneral_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                              nullptr, 0, nullptr, 1, &toWrite);
    if (copy) {
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {extent.width, extent.height, 1};
        dev.vk.CmdCopyImage(cb, gui, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_, VK_IMAGE_LAYOUT_GENERAL, 1,
                            &region);
    } else {
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = {static_cast<std::int32_t>(extent.width),
                                static_cast<std::int32_t>(extent.height), 1};
        region.dstOffsets[1] = region.srcOffsets[1];
        dev.vk.CmdBlitImage(cb, gui, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_, VK_IMAGE_LAYOUT_GENERAL, 1,
                            &region, VK_FILTER_NEAREST);
    }
    inGeneral_ = true;
    kept_ = true;
    ++counters_.panels;
    if (!panelLogged_) {
        panelLogged_ = true;
        const double aspect = failed_ ? 0.0 : virtual_client::mirrorCropAspect();
        const render_size::Band band = render_size::centredBand(extent.width, extent.height, aspect);
        const bool cut = band.height != extent.height && canBlit(dev, format) && blitsOn(dev, family);
        EVR_LOG("mirror: a menu is up: the window shows the panel's image (%ux%u %s)", extent.width,
                cut ? band.height : extent.height, cut ? "band, ETERNALVR_MIRROR_CROP" : "whole image");
    }
    return true;
}

void DesktopMirror::blitBand(DeviceData& dev,
                             VkCommandBuffer cb,
                             VkImage source,
                             VkExtent2D extent,
                             std::uint32_t y,
                             std::uint32_t height) {
    const std::array<VkImageMemoryBarrier, 2> before{
        barrier(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT),
        barrier(image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT)};
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                              nullptr, 0, nullptr, static_cast<std::uint32_t>(before.size()), before.data());
    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[0] = {0, static_cast<std::int32_t>(y), 0};
    region.srcOffsets[1] = {static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(y + height),
                            1};
    region.dstOffsets[1] = {static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(extent.height),
                            1};
    dev.vk.CmdBlitImage(cb, image_, VK_IMAGE_LAYOUT_GENERAL, source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                        &region, VK_FILTER_LINEAR);
    ++counters_.crops;
}

VkImageLayout DesktopMirror::record(DeviceData& dev,
                                    VkCommandBuffer cb,
                                    std::uint32_t family,
                                    VkImage source,
                                    VkFormat format,
                                    VkExtent2D extent,
                                    stereo_seq::MirrorStep step,
                                    bool toWindow) {
    using stereo_seq::MirrorStep;
    const double aspect =
        toWindow && step != MirrorStep::Clear && !failed_ ? virtual_client::mirrorCropAspect() : 0.0;
    const render_size::Band band = render_size::centredBand(extent.width, extent.height, aspect);
    const bool crop = band.height != extent.height && canBlit(dev, format) && blitsOn(dev, family);
    if (step == MirrorStep::None && !crop) {
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    if (step == MirrorStep::Clear) {
        const VkImageMemoryBarrier toDst =
            barrier(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                  nullptr, 0, nullptr, 1, &toDst);
        const VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
        dev.vk.CmdClearColorImage(cb, source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &kColour);
        ++counters_.clears;
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    const bool sameShape =
        image_ && format_ == format && extent_.width == extent.width && extent_.height == extent.height;
    if (step == MirrorStep::Load && (!sameShape || !kept_)) {
        ++counters_.skipped; // nothing of this shape kept yet: the image goes out as rendered
        if (!crop) {
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        step = MirrorStep::None;
    }
    if (!ensureImage(dev, format, extent)) {
        ++counters_.skipped;
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.extent = {extent.width, extent.height, 1};
    if (step == MirrorStep::Store || step == MirrorStep::None) {
        // The private image stays in GENERAL. The last load read it in an earlier ring copy, which this one
        // waits for on the shared timeline; the barrier orders the write after that read.
        const VkImageMemoryBarrier toWrite =
            barrier(image_, inGeneral_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                  nullptr, 0, nullptr, 1, &toWrite);
        dev.vk.CmdCopyImage(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image_, VK_IMAGE_LAYOUT_GENERAL,
                            1, &region);
        inGeneral_ = true;
        // Only a store keeps an eye for the other present; a crop's own copy replaces what was kept.
        kept_ = step == MirrorStep::Store;
        counters_.stores += kept_ ? 1 : 0;
        if (!crop) {
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        }
        blitBand(dev, cb, source, extent, band.y, band.height);
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    if (crop) {
        // Load, cut: the kept eye's band goes straight over this present's image.
        ++counters_.loads;
        blitBand(dev, cb, source, extent, band.y, band.height);
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }
    // Load: the kept eye image replaces this present's image.
    const std::array<VkImageMemoryBarrier, 2> before{
        barrier(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT),
        barrier(image_, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT)};
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                              nullptr, 0, nullptr, static_cast<std::uint32_t>(before.size()), before.data());
    dev.vk.CmdCopyImage(cb, image_, VK_IMAGE_LAYOUT_GENERAL, source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                        &region);
    ++counters_.loads;
    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

void DesktopMirror::destroy(DeviceData& dev) {
    if (image_) {
        dev.vk.DestroyImage(dev.device, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_) {
        dev.vk.FreeMemory(dev.device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    inGeneral_ = false;
    kept_ = false;
}

} // namespace evr::vkcore
