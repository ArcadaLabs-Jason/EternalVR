#pragma once

// Adapter steering on two-GPU machines (T-057, T-111): the layer's vkEnumeratePhysicalDevices and
// vkEnumeratePhysicalDeviceGroups give the game's instance its devices in an order that makes the game's own
// pick the headset's adapter (adapter_order.hpp has the rule and the order).
//
// Off by default: ETERNALVR_ADAPTER_STEER=1 (or on) turns it on, until a two-GPU machine has been tried with
// a headset. Off, the first enumeration of the game's instance logs the devices and "steering off" (one extra
// enumeration for that line), and every call goes straight down: no OpenXR work at all.
//
// On, the headset's adapter is the OpenXR system's LUID (xrGetD3D12GraphicsRequirementsKHR). The presenter's
// instance comes too late for it (the XR worker starts at the game's first vkCreateSwapchainKHR), so the
// first enumeration of the game's instance asks the runtime once: a short-lived OpenXR instance (the layer's
// own openxr_loader.dll, XR_KHR_D3D12_enable only), one xrGetSystem, the graphics requirements, then
// xrDestroyInstance. It runs on the game's thread with the pass-through flag set, so a VkInstance the runtime
// makes from inside it passes straight through the layer; the loader's lock (held around the whole call) is
// recursive on its own thread. A slow runtime holds the game's start-up for as long.
//
// The runtime is not asked, and the list goes straight down unchanged, with one device; with exactly one
// device the game's rule takes (no order changes its pick); when the game's command line sets
// r_physicalDeviceIndex; for an instance that is not the game's. With no runtime, no headset or a runtime
// without D3D12 at launch, or a list the game already picks the headset's adapter from, the list is unchanged
// too. Steering only reorders: when the runtime names a device the game would not take even first (an
// integrated GPU next to a discrete one), the list is left and the presenter's LUID check refuses VR.
//
// ETERNALVR_TEST_STEER (rig only): "mismatch" steers to the first device without the headset's LUID and may
// cut the list to it, the positive control for the presenter's LUID refusal (on whatever the switch says);
// "off" is the switch at 0.

#include <vulkan/vulkan.h>

namespace evr::vkcore::adapter_steer {

// The layer's vkEnumeratePhysicalDevices, vkEnumeratePhysicalDeviceGroups and its KHR alias, when the next
// layer has the function for this instance; nullptr for any other name or an instance the layer does not
// know.
PFN_vkVoidFunction findHook(VkInstance instance, const char* name);

// vkDestroyInstance of a game instance: its steering decision is dropped (a later instance may reuse the
// handle). Not for instances made inside the runtime query, which hold none.
void onInstanceDestroyed(VkInstance instance);

} // namespace evr::vkcore::adapter_steer
