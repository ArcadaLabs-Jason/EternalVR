#pragma once

// Fixed foveated rendering and its experiments, through NVIDIA's VK_NV_shading_rate_image (RTX 20 and
// later; docs/research/08-foveated-rendering.md). The retail game has no working VRS on Vulkan (its r_VRS*
// cvars fill a tile buffer nothing reads), so the layer enables the extension at device creation, gives every
// graphics pipeline a shading rate palette (1x1, 2x2, 4x4) and binds a rate image before each render pass of
// at least 256x256: one per render target size and eye, the eye taken from the backend frame's tag. Passes
// into the game's GUI target (its menus and HUD) keep full rate (vrs_gui.hpp).
// ETERNALVR_FOVEATION (the player's setting: off, subtle, balanced, aggressive): full rate within the
// preset's angle of head-forward (foveation_preset.hpp), half rate for 16 degrees more, quarter rate outside.
// ETERNALVR_VRS_TEST (experiments, wins over the setting): 2x2 or 4x4 everywhere (the upper bound), eyetest
// (eye L's left half and eye R's right half at 4x4, to check the eye tags in captures), fovea (angles from
// ETERNALVR_VRS_FOVEA=<full>,<half>, default 24,40). Off unless one is set; a device without the extension
// keeps the game's own shading.

#include "common/quat.hpp"
#include "vkcore/dispatch.hpp"
#include "xr_math/fov.hpp"

#include <vulkan/vulkan.h>

#include <vector>

namespace evr::vkcore::vrs_nv {

// Foveated rendering or a VRS experiment is on (ETERNALVR_FOVEATION, ETERNALVR_VRS_TEST).
[[nodiscard]] bool wanted();

// At device creation (game device only): adds VK_NV_shading_rate_image and its shadingRateImage feature to
// `extensions` and returns the feature struct to chain (its pNext set by the caller), or nullptr when not
// wanted or not supported (logged).
VkPhysicalDeviceShadingRateImageFeaturesNV*
planDevice(InstanceData& inst, VkPhysicalDevice physicalDevice, std::vector<const char*>& extensions);

// After the device is created with the plan. These hooks are the innermost of the layer's own chain: the
// modules that hook the same functions call them, and they call the next layer.
void onDeviceCreated(DeviceData& data, bool enabled);
void onDeviceDestroyed(VkDevice device);

// The presenter's eye `eye` (0 = L, 1 = R): its FOV and its orientation in the head. The first one seen
// shapes that eye's foveation.
void noteEye(int eye, const xr_math::Fov& fov, const Quat& orientationInHead);

// vkCreateGraphicsPipelines and vkCmdBeginRenderPass while wanted, and vrs_gui's hooks; nullptr otherwise.
PFN_vkVoidFunction findHook(const char* name);

} // namespace evr::vkcore::vrs_nv
