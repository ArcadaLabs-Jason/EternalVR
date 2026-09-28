#pragma once

// The game's swapchain and its presents (swapchain_entry.cpp): the present mode and image count under Route
// S, the render size's present scaling, and the hand-over of every present to the XR presenter.

#include "vkcore/dispatch.hpp"

namespace evr::vkcore {

// layer_entry.cpp: the device data of a VkDevice or VkQueue (nullptr for one the layer does not know).
DeviceData* findDeviceData(void* dispatchable);

// vkCreateSwapchainKHR, vkDestroySwapchainKHR, vkAcquireNextImageKHR and vkQueuePresentKHR; nullptr for any
// other name.
PFN_vkVoidFunction findSwapchainHook(const char* name);

} // namespace evr::vkcore
