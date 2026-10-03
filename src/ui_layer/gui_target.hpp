#pragma once

// The game's GUI render target `_gui` (docs/rig-findings/ui-layer.md sections 2 and 5; Steam build
// 25216728): the engine layouts that lead to it, which images the layer prepares for copying, and the
// checks a target read from the game must pass before it is copied.
//
// Plain integers only (Vulkan enum values as numbers), so this module needs no Vulkan headers.

#include <cstdint>
#include <optional>
#include <vector>

namespace evr::ui_layer {

namespace engine {
// idRenderSystemLocal (0x66E2C30): the GUI render target (color `_gui`, depth `_upscaledOpaqueDepth`).
inline constexpr std::uint32_t kRenderSystemGuiTarget = 0x5C8;
// Render target object (0x230 bytes): width, height, override width, override height, then the color
// image.
inline constexpr std::uint32_t kTargetColorImage = 0x10;
// idImage (0x138 bytes): its idImageOpts copy starts at +0x58.
inline constexpr std::uint32_t kImageFormat = 0x5C; // textureFormat_t
inline constexpr std::uint32_t kImageWidth = 0x64;
inline constexpr std::uint32_t kImageHeight = 0x68;
inline constexpr std::uint32_t kImageFlags = 0x9C;   // bit 8: a set of images (not one VkImage)
inline constexpr std::uint32_t kImageVkImage = 0xE8; // VkImage (flag bit 8 clear)
inline constexpr std::uint32_t kImageSetFlag = 0x100;
inline constexpr std::uint32_t kFormatRgba8 = 3; // FMT_RGBA8 -> VK_FORMAT_R8G8B8A8_UNORM
// The image manager (global pointer at 0x5BF13C8): `_black`, 16x16 RGBA8 of zeros, at +0x20.
inline constexpr std::uint32_t kImageManagerBlack = 0x20;
} // namespace engine

// Vulkan values used here.
namespace vk {
inline constexpr std::int32_t kImageType2D = 1;
inline constexpr std::int32_t kFormatR8G8B8A8Unorm = 37;
inline constexpr std::uint32_t kUsageTransferSrc = 0x01;
inline constexpr std::uint32_t kUsageSampled = 0x04;
inline constexpr std::uint32_t kUsageStorage = 0x08;
inline constexpr std::uint32_t kUsageColorAttachment = 0x10;
inline constexpr std::int32_t kSharingConcurrent = 1;
// Layouts a copy source may come from (the game's colour images are in one of these between passes).
inline constexpr std::int32_t kLayoutGeneral = 1;
inline constexpr std::int32_t kLayoutColorAttachment = 2;
inline constexpr std::int32_t kLayoutShaderReadOnly = 5;
inline constexpr std::int32_t kLayoutTransferSrc = 6;
} // namespace vk

// What vkCreateImage is asked for (the fields the decision needs).
struct ImageCreateDesc {
    std::int32_t imageType = 0;
    std::int32_t format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    std::uint32_t mipLevels = 0;
    std::uint32_t arrayLayers = 0;
    std::uint32_t samples = 0;
    std::uint32_t usage = 0;
};

// The engine creates `_gui` as a 2D RGBA8 render target with one mip and layer and usage colour
// attachment | sampled | storage (idImageOpts flags 0x207; no TRANSFER_SRC). Images created exactly so
// get TRANSFER_SRC added (the usage this returns) and are followed; nullopt for every other image.
std::optional<std::uint32_t> candidateUsage(const ImageCreateDesc& desc);

// Parallel Eye Rendering's eye copy only (vkcore/presenter_eyes.hpp): the screen-sized images it may read
// (each view's final image), 2D with one mip, layer and sample, drawn (colour attachment or storage) and
// sampled, at least 256 wide, four bytes per texel (RGBA8, BGRA8, 10:10:10:2 or B10G11R11 float). They get
// TRANSFER_SRC (the usage this returns) and are followed; nullopt for every other image.
std::optional<std::uint32_t> eyeCopyCandidateUsage(const ImageCreateDesc& desc);

// A candidate image as created.
struct ImageRecord {
    std::int32_t format = 0; // VkFormat
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t usage = 0;
    std::int32_t sharingMode = 0;
    std::vector<std::uint32_t> queueFamilies; // for concurrent sharing
};

// The fields read from the game's idImage.
struct GuiImageFields {
    std::uint32_t format = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint32_t flags = 0;
    std::uint64_t vkImage = 0;
};

enum class TargetCheck {
    Ok,
    NotRgba8,        // textureFormat_t is not FMT_RGBA8
    ImageSet,        // the idImage holds a set of images
    BadSize,         // width or height outside 1..16384
    NoVkImage,       // no VkImage yet
    UnknownImage,    // the VkImage is not a candidate the layer saw created
    SizeMismatch,    // the VkImage was created with another size
    NoTransferSrc,   // created without TRANSFER_SRC
    NoLayout,        // no barrier on it submitted yet
    BadLayout,       // in a layout a copy cannot start from
    OtherQueueFamily // exclusive, or concurrent without the copying queue's family
};
const char* toString(TargetCheck check);

// Whether the target read from the game may be copied now on a queue of family `copyFamily`: `record` is
// the VkImage's candidate record (nullptr when it is not one), `layout` its current layout and
// `lastFamily` the queue family whose submit last moved it (nullopt when unknown). An exclusive image is
// copied only on the family that used it last (the game never transfers ownership).
TargetCheck checkTarget(const GuiImageFields& fields,
                        const ImageRecord* record,
                        std::optional<std::int32_t> layout,
                        std::optional<std::uint32_t> lastFamily,
                        std::uint32_t copyFamily);

} // namespace evr::ui_layer
