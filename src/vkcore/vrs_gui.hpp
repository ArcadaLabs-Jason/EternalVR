#pragma once

// The game's menus and HUD at full rate under foveated rendering (vrs_nv.hpp). They are drawn into the GUI
// target (ui_vulkan::guiTarget()), which has the eye images' size, so the render area alone cannot tell a
// GUI pass from an eye pass. This module follows the game's image views and framebuffers
// (features/foveation/attachment_images.hpp) and tells whether a render pass draws into the GUI target.
// The UI layer finds the GUI target; with it off, every pass counts as an eye pass, as before.

#include "vkcore/dispatch.hpp"

#include <vulkan/vulkan.h>

namespace evr::vkcore::vrs_gui {

// After vrs_nv's: `enabled` as there (the extension is on). Views and framebuffers are followed only then,
// and only with the UI layer on.
void onDeviceCreated(DeviceData& data, bool enabled);

// True when the render pass `begin`, recorded into `commandBuffer`, draws into the game's GUI target. A
// shared lock and a lookup; false at once while no GUI target is known.
[[nodiscard]] bool drawsGuiTarget(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo& begin);

// vkCreateImageView, vkDestroyImageView, vkCreateFramebuffer and vkDestroyFramebuffer while foveated
// rendering or a VRS experiment is wanted; nullptr otherwise. Like vrs_nv's, they call the next layer.
PFN_vkVoidFunction findHook(const char* name);

} // namespace evr::vkcore::vrs_gui
