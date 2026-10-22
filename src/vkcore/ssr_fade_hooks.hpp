#pragma once

// The SSR fade patch at shader-module creation (ssr_fade_patch.hpp) and the coherence the view-rotation shear
// depends on (screen_space_view_hooks.hpp).
//
// vkCreateShaderModule on the game's device: a module whose hash is one of the 22 known SSR modules gets the
// patched words, if the patch gives exactly the validated output; any other module passes unchanged. The
// shear may only run while every SSR shader fades in the shared frame, so coherent() is true once all 22 were
// patched, and false for the rest of the session after any known module failed to patch or a module of
// unknown hash holds the fade's signature (another build's shaders, a variant this table does not know).

#include <vulkan/vulkan.h>

namespace evr::vkcore::ssr_fade {

// Registers a device with the next vkCreateShaderModule of the chain (the shader dump's when that runs); only
// the game's device (`isGame`) has its modules patched.
void onDeviceCreated(VkDevice device, PFN_vkGetDeviceProcAddr nextGetDeviceProcAddr, bool isGame);
void onDeviceDestroyed(VkDevice device);

// The hook for vkCreateShaderModule, or nullptr for any other name.
PFN_vkVoidFunction findHook(const char* name);

// All 22 known modules patched, none failed, no unknown module with the fade's signature. Lock-free, any
// thread.
bool coherent();

} // namespace evr::vkcore::ssr_fade
