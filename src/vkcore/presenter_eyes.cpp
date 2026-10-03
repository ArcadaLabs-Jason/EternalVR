#include "vkcore/presenter_eyes.hpp"

#include "ui_layer/gui_target.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/ui_engine.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/view_clones.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "eye-copy";
constexpr std::uint32_t kFinalTarget = 0x66E3208; // build 25216728: the post-process final target

bool copyableLayout(VkImageLayout layout) {
    return layout == VK_IMAGE_LAYOUT_GENERAL || layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ||
           layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
           layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
}

// Null when `family` can blit `from` into `to` (same size, nearest filter), else why not.
const char* blitUnsupported(DeviceData& dev, std::uint32_t family, VkFormat from, VkFormat to) {
    if (family >= dev.queueFamilyFlags.size() || !(dev.queueFamilyFlags[family] & VK_QUEUE_GRAPHICS_BIT)) {
        return "a blit needs a graphics queue";
    }
    VkFormatProperties src{};
    VkFormatProperties dst{};
    dev.instance->vk.GetPhysicalDeviceFormatProperties(dev.physicalDevice, from, &src);
    dev.instance->vk.GetPhysicalDeviceFormatProperties(dev.physicalDevice, to, &dst);
    if (!(src.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        !(dst.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        return "no blit support for these formats";
    }
    return nullptr;
}

struct EyeLog {
    std::atomic<int> failures{0};
    std::atomic<bool> copiedOnce{false};
    std::string lastReason; // under g_logMutex
};
std::array<EyeLog, 2> g_logs;
std::mutex g_logMutex;
std::atomic<std::uint64_t> g_copies{0};
std::atomic<bool> g_formatNoted{false};
constexpr int kAssumeAfter = 30;
std::array<std::atomic<int>, 2> g_unknownLayout{};
std::array<std::atomic<bool>, 2> g_assumedNoted{};

void fail(int eye, const std::string& reason) {
    std::lock_guard lock(g_logMutex);
    EyeLog& log = g_logs[static_cast<std::size_t>(eye)];
    if (reason != log.lastReason && log.failures.fetch_add(1) < 12) {
        EVR_LOG("%s: eye %d takes the presented image: %s", kTag, eye, reason.c_str());
    }
    log.lastReason = reason;
}

// The eyes after the engine's screen pass (tone map; the default): eye 0 is the presented image (view 0's
// screen pass), eye 1 view 1's screen pass output. ETERNALVR_TEST_EYE_COPY=1 copies each view's scene colour
// before it.
bool eyeCopyScreen() {
    return parallelEyesSettings().eyeCopy == parallel_eyes::EyeCopy::Screen;
}

// The engine pointer whose image is view `eye`'s final image: a render target for view 0, an image for
// view 1.
std::uintptr_t finalPointer(int eye) {
    if (eyeCopyScreen()) {
        return eye == 0 ? 0 : reinterpret_cast<std::uintptr_t>(viewClonesScreenImage());
    }
    if (eye == 0) {
        const auto* base = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
        std::uintptr_t target = 0;
        std::memcpy(&target, base + kFinalTarget, sizeof(target));
        return target;
    }
    return reinterpret_cast<std::uintptr_t>(viewClonesFinalImage());
}

// Records view `eye`'s final image into the slot's eye; false (logged) when it cannot.
bool copyViewImage(
    DeviceData& dev, VkCommandBuffer cb, std::uint32_t family, int eye, const EyeCopyTarget& t) {
    const std::uintptr_t pointer = finalPointer(eye);
    if (!pointer) {
        fail(eye, eye == 0 ? "no final target yet" : "no view 1 clone yet");
        return false;
    }
    const std::optional<ui_layer::GuiImageFields> fields = ui_engine::readImageOrTarget(pointer);
    if (!fields) {
        fail(eye, "the final pointer reads as neither a render target nor an image");
        return false;
    }
    std::uint64_t vkImage = fields->vkImage;
    if ((fields->flags & ui_layer::engine::kImageSetFlag) != 0) {
        const auto member = ui_engine::readSetMember(fields->vkImage);
        vkImage = member ? member->second : 0;
    }
    const auto image = reinterpret_cast<VkImage>(vkImage);
    if (!image) {
        fail(eye, "no VkImage yet");
        return false;
    }
    const std::optional<ui_layer::ImageRecord> record = ui_vulkan::recordOf(image);
    if (!record) {
        char what[160];
        std::snprintf(what, sizeof(what),
                      "VkImage %p (engine format %u, %dx%d, flags 0x%X) is not one the UI layer prepared",
                      reinterpret_cast<void*>(image), fields->format, fields->width, fields->height,
                      fields->flags);
        fail(eye, what);
        return false;
    }
    ui_vulkan::follow(image);
    if (record->width != t.eyeExtent.width || record->height != t.eyeExtent.height) {
        fail(eye, "final image " + std::to_string(record->width) + "x" + std::to_string(record->height) +
                      ", eye " + std::to_string(t.eyeExtent.width) + "x" +
                      std::to_string(t.eyeExtent.height));
        return false;
    }
    // Another format (the engine's final images are B10G11R11 float) is blitted, which converts it; that
    // needs a graphics queue and blit support for both formats.
    const auto format = static_cast<VkFormat>(record->format);
    const bool blit = format != t.format;
    if (blit) {
        if (const char* why = blitUnsupported(dev, family, format, t.format)) {
            fail(eye, "final image format " + std::to_string(format) + ", ring " + std::to_string(t.format) +
                          ": " + why);
            return false;
        }
        if (!g_formatNoted.exchange(true)) {
            EVR_LOG("%s: final image format %d blitted into ring format %d", kTag, format, t.format);
        }
    }
    std::optional<ui_vulkan::ImageState> state = ui_vulkan::stateOf(image);
    // View 1's final image is written and never read by the engine, so it may never pass another barrier
    // once followed: after kAssumeAfter tries it is taken to be in GENERAL, where a compute pass leaves it.
    bool assumed = false;
    std::atomic<int>& unknown = g_unknownLayout[static_cast<std::size_t>(eye)];
    if (!state) {
        if (unknown.fetch_add(1) < kAssumeAfter) {
            fail(eye, "layout not known yet");
            return false;
        }
        state = ui_vulkan::ImageState{VK_IMAGE_LAYOUT_GENERAL, VK_NULL_HANDLE};
        assumed = true;
        if (!g_assumedNoted[static_cast<std::size_t>(eye)].exchange(true)) {
            EVR_LOG("%s: eye %d: VkImage %p passed no barrier in %d tries; taken to be in GENERAL", kTag, eye,
                    reinterpret_cast<void*>(image), kAssumeAfter);
        }
    } else {
        unknown.store(0);
    }
    if (!copyableLayout(state->layout)) {
        fail(eye, "layout " + std::to_string(state->layout) + " is not one a copy starts from");
        return false;
    }
    if (record->sharingMode != VK_SHARING_MODE_CONCURRENT && !assumed) {
        std::optional<std::uint32_t> lastFamily;
        {
            std::lock_guard lock(dev.queueMutex);
            const auto it = dev.queueFamilies.find(state->queue);
            if (it != dev.queueFamilies.end()) {
                lastFamily = it->second;
            }
        }
        if (!lastFamily || *lastFamily != family) {
            fail(eye, "exclusive to queue family " +
                          std::to_string(lastFamily ? static_cast<int>(*lastFamily) : -1) + ", copy on " +
                          std::to_string(family));
            return false;
        }
    }
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageMemoryBarrier before{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    before.oldLayout = state->layout;
    before.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    before.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    before.image = image;
    before.subresourceRange = range;
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                              nullptr, 0, nullptr, 1, &before);
    const auto x = static_cast<std::int32_t>(eye) * static_cast<std::int32_t>(t.eyeExtent.width);
    const auto w = static_cast<std::int32_t>(t.eyeExtent.width);
    const auto h = static_cast<std::int32_t>(t.eyeExtent.height);
    if (blit) {
        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[1] = {w, h, 1};
        region.dstOffsets[0] = {x, 0, 0};
        region.dstOffsets[1] = {x + w, h, 1};
        dev.vk.CmdBlitImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.slot,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
    } else {
        VkImageCopy region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffset = {x, 0, 0};
        region.extent = {t.eyeExtent.width, t.eyeExtent.height, 1};
        dev.vk.CmdCopyImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.slot,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    VkImageMemoryBarrier after = before;
    after.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    after.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    after.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    after.newLayout = state->layout; // where the game's own tracking has it
    dev.vk.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                              nullptr, 0, nullptr, 1, &after);
    EyeLog& log = g_logs[static_cast<std::size_t>(eye)];
    if (!log.copiedOnce.exchange(true)) {
        EVR_LOG("%s: eye %d takes view %d's image (VkImage %p, %ux%u, format %d, layout %d)", kTag, eye, eye,
                reinterpret_cast<void*>(image), record->width, record->height, record->format, state->layout);
    }
    {
        std::lock_guard lock(g_logMutex);
        log.lastReason.clear();
    }
    return true;
}

} // namespace

bool eyeCopyRequested() {
    return viewSlotsActive() && parallelEyesSettings().eyeCopy != parallel_eyes::EyeCopy::Off;
}

void recordEyeCopies(DeviceData& dev,
                     VkCommandBuffer cb,
                     std::uint32_t family,
                     VkImage source,
                     VkExtent2D sourceExtent,
                     bool sameShape,
                     const EyeCopyTarget& t) {
    // The eyes that take the presented image: all of them, less the ones the eye copy filled (with the screen
    // pass's eye copy, eye 0 is the presented image). Nothing of the game is read after a guard trip.
    const bool viewImages = t.viewImages && mp_guard::allowsGameTouch();
    std::array<std::uint32_t, 2> eyes{};
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < std::min<std::uint32_t>(t.eyeCount, 2); ++i) {
        const std::uint32_t eye = t.firstEye + i;
        // Eye 1 takes view 1's image only when the frame rendered view 1 into it: on a frame with view 0
        // alone (a loading screen, the async compute safety net, ETERNALVR_TEST_VIEW_ONLY=0) or one sent
        // before the clones were last made it holds an older frame's or nothing, so eye 1 takes the
        // presented image, as eye 0.
        const bool viewImage = viewImages && eye < 2 && !(eye == 0 && eyeCopyScreen());
        if (viewImage && eye == 1 && !viewSlotsView1Rendered()) {
            fail(1, "view 1 was not rendered for this frame");
        } else if (viewImage && copyViewImage(dev, cb, family, static_cast<int>(eye), t)) {
            continue;
        }
        eyes[count++] = eye;
    }
    if (viewImages && count == (eyeCopyScreen() ? 1u : 0u) && g_copies.fetch_add(1) % 50000 == 0) {
        EVR_LOG("%s: %llu present(s) with each eye from its own view", kTag,
                static_cast<unsigned long long>(g_copies.load()));
    }
    const auto eyeWidth = static_cast<std::int32_t>(t.eyeExtent.width);
    const auto eyeHeight = static_cast<std::int32_t>(t.eyeExtent.height);
    if (sameShape) {
        std::array<VkImageCopy, 2> regions{};
        for (std::uint32_t i = 0; i < count; ++i) {
            regions[i].srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            regions[i].dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            regions[i].dstOffset = {static_cast<std::int32_t>(eyes[i]) * eyeWidth, 0, 0};
            regions[i].extent = {t.eyeExtent.width, t.eyeExtent.height, 1};
        }
        if (count > 0) {
            dev.vk.CmdCopyImage(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.slot,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions.data());
        }
        if (t.carry && count > 0) {
            dev.vk.CmdCopyImage(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.carry,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions.data());
        }
        return;
    }
    std::array<VkImageBlit, 2> regions{};
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::int32_t x = static_cast<std::int32_t>(eyes[i]) * eyeWidth;
        regions[i].srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        regions[i].dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        regions[i].srcOffsets[1] = {static_cast<std::int32_t>(sourceExtent.width),
                                    static_cast<std::int32_t>(sourceExtent.height), 1};
        regions[i].dstOffsets[0] = {x, 0, 0};
        regions[i].dstOffsets[1] = {x + eyeWidth, eyeHeight, 1};
    }
    if (count > 0) {
        dev.vk.CmdBlitImage(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.slot,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions.data(), VK_FILTER_LINEAR);
    }
    if (t.carry && count > 0) {
        dev.vk.CmdBlitImage(cb, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.carry,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions.data(), VK_FILTER_LINEAR);
    }
}

} // namespace evr::vkcore
