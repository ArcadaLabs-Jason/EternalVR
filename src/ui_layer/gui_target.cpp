#include "ui_layer/gui_target.hpp"

#include <algorithm>

namespace evr::ui_layer {

std::optional<std::uint32_t> candidateUsage(const ImageCreateDesc& desc) {
    constexpr std::uint32_t kGuiUsage = vk::kUsageColorAttachment | vk::kUsageSampled | vk::kUsageStorage;
    if (desc.imageType != vk::kImageType2D || desc.format != vk::kFormatR8G8B8A8Unorm || desc.depth != 1 ||
        desc.mipLevels != 1 || desc.arrayLayers != 1 || desc.samples != 1 || desc.usage != kGuiUsage ||
        desc.width == 0 || desc.height == 0) {
        return std::nullopt;
    }
    return desc.usage | vk::kUsageTransferSrc;
}

const char* toString(TargetCheck check) {
    switch (check) {
    case TargetCheck::Ok:
        return "ok";
    case TargetCheck::NotRgba8:
        return "not FMT_RGBA8";
    case TargetCheck::ImageSet:
        return "an image set";
    case TargetCheck::BadSize:
        return "size out of range";
    case TargetCheck::NoVkImage:
        return "no VkImage";
    case TargetCheck::UnknownImage:
        return "a VkImage the layer did not prepare";
    case TargetCheck::SizeMismatch:
        return "a VkImage of another size";
    case TargetCheck::NoTransferSrc:
        return "no TRANSFER_SRC usage";
    case TargetCheck::NoLayout:
        return "layout not known yet";
    case TargetCheck::BadLayout:
        return "in a layout a copy cannot start from";
    case TargetCheck::OtherQueueFamily:
        return "not shared with the copying queue family";
    }
    return "?";
}

TargetCheck checkTarget(const GuiImageFields& fields,
                        const ImageRecord* record,
                        std::optional<std::int32_t> layout,
                        std::optional<std::uint32_t> lastFamily,
                        std::uint32_t copyFamily) {
    constexpr std::int32_t kMaxSize = 16384;
    if (fields.format != engine::kFormatRgba8) {
        return TargetCheck::NotRgba8;
    }
    if ((fields.flags & engine::kImageSetFlag) != 0) {
        return TargetCheck::ImageSet;
    }
    if (fields.width < 1 || fields.height < 1 || fields.width > kMaxSize || fields.height > kMaxSize) {
        return TargetCheck::BadSize;
    }
    if (fields.vkImage == 0) {
        return TargetCheck::NoVkImage;
    }
    if (!record) {
        return TargetCheck::UnknownImage;
    }
    if (record->width != static_cast<std::uint32_t>(fields.width) ||
        record->height != static_cast<std::uint32_t>(fields.height)) {
        return TargetCheck::SizeMismatch;
    }
    if ((record->usage & vk::kUsageTransferSrc) == 0) {
        return TargetCheck::NoTransferSrc;
    }
    if (!layout) {
        return TargetCheck::NoLayout;
    }
    if (*layout != vk::kLayoutGeneral && *layout != vk::kLayoutColorAttachment &&
        *layout != vk::kLayoutShaderReadOnly && *layout != vk::kLayoutTransferSrc) {
        return TargetCheck::BadLayout;
    }
    const bool shared = record->sharingMode == vk::kSharingConcurrent
                            ? std::find(record->queueFamilies.begin(), record->queueFamilies.end(),
                                        copyFamily) != record->queueFamilies.end()
                            : lastFamily == copyFamily;
    if (!shared) {
        return TargetCheck::OtherQueueFamily;
    }
    return TargetCheck::Ok;
}

} // namespace evr::ui_layer
