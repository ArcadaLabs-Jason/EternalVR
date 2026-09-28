#pragma once

// The layer's surface entry points (surface_entry.cpp): the game's window surface and what the game is told
// about it (the render size, virtual_client.hpp).

#include "vkcore/dispatch.hpp"

namespace evr::vkcore {

// layer_entry.cpp: the instance data of a VkInstance or VkPhysicalDevice (nullptr for one the layer does not
// know), and whether this thread runs the layer's own OpenXR calls (its instances pass through).
InstanceData* findInstanceData(void* dispatchable);
bool passThroughThread();

// vkCreateWin32SurfaceKHR, vkDestroySurfaceKHR and vkGetPhysicalDeviceSurfaceCapabilities(2)KHR, when the
// next layer has them; nullptr for any other name (or without an instance).
PFN_vkVoidFunction findSurfaceHook(VkInstance instance, const char* name);

} // namespace evr::vkcore
