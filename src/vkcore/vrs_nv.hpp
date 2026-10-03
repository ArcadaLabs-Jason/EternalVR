#pragma once

// Fixed foveated rendering and its experiments, through NVIDIA's VK_NV_shading_rate_image (RTX 20 and
// later; docs/research/08-foveated-rendering.md). The retail game has no working VRS on Vulkan (its r_VRS*
// cvars fill a tile buffer nothing reads), so the layer enables the extension at device creation, gives every
// graphics pipeline a shading rate palette (1x1, 2x2, 4x4) and binds a rate image before each render pass of
// at least 256x256 into the eye image or a scaled copy of it (the DLSS render size, half size buffers;
// features/foveation/eye_targets.hpp): one per render target size and eye, the eye taken from the backend
// frame's own tag (vrs_pass_eye.cpp). Shadow maps and other targets, mono frames (the cinema screen, menus),
// passes whose frame is not known and, with the UI layer on, passes into the game's GUI target (its menus and
// HUD; vrs_gui.hpp) keep full rate.
// ETERNALVR_FOVEATION (the player's setting: off, subtle, balanced, aggressive, maximum): full rate within
// the region of the preset's angle around head-forward (foveation_preset.hpp, foveation_region.hpp), half
// rate within the region of the preset's band more (16 degrees, 12 for maximum), quarter rate outside.
// ETERNALVR_VRS_TEST (experiments, wins over the setting): 2x2 or 4x4 everywhere (the upper bound), eyetest
// (eye L's left half and eye R's right half at 4x4, to check the eye tags in captures), fovea (angles from
// ETERNALVR_VRS_FOVEA=<full>,<half>, default 24,40). Off unless one is set; a device without the extension
// keeps the game's own shading.

#include "common/quat.hpp"
#include "vkcore/dispatch.hpp"
#include "xr_math/fov.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace evr::vkcore::vrs_nv {

// Foveated rendering or a VRS experiment is on (ETERNALVR_FOVEATION, ETERNALVR_VRS_TEST).
[[nodiscard]] bool wanted();

// A device with the extension on asks each render pass's frame (foveation or the eye test, not one rate for
// every pass): the hooks that have the render-view job's counter at hand note it (seqNoteRenderViewCounter).
[[nodiscard]] bool passesFollowFrames();

// At device creation (game device only): adds VK_NV_shading_rate_image and its shadingRateImage feature to
// `extensions` and returns the feature struct to chain (its pNext set by the caller), or nullptr when not
// wanted or not supported (logged).
VkPhysicalDeviceShadingRateImageFeaturesNV*
planDevice(InstanceData& inst, VkPhysicalDevice physicalDevice, std::vector<const char*>& extensions);

// After the device is created with the plan. These hooks are the innermost of the layer's own chain: the
// modules that hook the same functions call them, and they call the next layer.
void onDeviceCreated(DeviceData& data, bool enabled);
void onDeviceDestroyed(VkDevice device);

// The presenter's eye `eye` (0 = L, 1 = R): its FOV and its orientation in the head, from a frame whose eye
// passed the plausibility check. The first one seen shapes that eye's foveation for the whole process.
void noteEye(int eye, const xr_math::Fov& fov, const Quat& orientationInHead);

// The game's swapchain on `device` was created with `extent`, the game's output size: the eye image's size
// until a render pass into the GUI target gives it, and without the UI layer, which finds that target.
void noteSwapchain(VkDevice device, VkExtent2D extent);

// ETERNALVR_VRS_TINT=1 with foveated rendering or a VRS experiment on: the presenter marks where each eye's
// rate image is at a lower rate (a dot every 32 pixels, yellow at half rate, red at quarter rate), to see the
// regions in the headset (vrs_marks.cpp). An eye image gets them only when some of that eye's render passes
// got a rate image since its previous one; passes kept at full rate (frame not known, the GUI target, other
// targets) are dotted all the same where they draw.
[[nodiscard]] bool marksWanted();

// Records eye `eye`'s marks (0 = L, 1 = R) into each of `images` that is not VK_NULL_HANDLE: the eye's image
// was just copied into it by a transfer at `offset`, `extent` big, and it is in TRANSFER_DST_OPTIMAL.
// `commandBuffer` is of queue family `family` of the game's device `dev`. Nothing is recorded unless the
// marks are wanted and the extension is on, on a family without graphics or compute, while the eye's
// pattern is not known, or while none of that eye's passes got a rate image since its previous marks.
void recordMarks(DeviceData& dev,
                 VkCommandBuffer commandBuffer,
                 std::uint32_t family,
                 const std::array<VkImage, 2>& images,
                 VkFormat format,
                 int eye,
                 VkOffset2D offset,
                 VkExtent2D extent);

// vkCreateGraphicsPipelines, vkCmdBeginRenderPass and the command buffer functions that start and end a
// recording (vrs_command_buffers.cpp) while wanted, and vrs_gui's hooks; nullptr otherwise.
PFN_vkVoidFunction findHook(const char* name);

} // namespace evr::vkcore::vrs_nv
