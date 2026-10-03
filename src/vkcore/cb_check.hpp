#pragma once

// Parallel Eye Rendering rig tool (ETERNALVR_TEST_CB_CHECK=1, with Parallel Eye Rendering on): watches how
// the game's device uses its command buffers and logs each misuse with the game address that made the call
// and the command context (category, slot) the buffer belongs to. Reported: two threads inside calls on one
// buffer at once; a begin while the buffer is still recording; a submit of a buffer that is still recording
// or is being recorded. Each kind logs its first lines only. It roughly halves the frame rate: never on for
// measurements. With it, cb_view_skip.hpp's knobs drop one view's GPU work by category or log its callers.
//
// Off (the default) it hooks nothing: findHook returns null and the device's chain is the layer's own.

#include "vkcore/dispatch.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace evr::vkcore::cb_check {

// The layer's own hook of a device function after the check's, or null (then the next layer's is called).
using LayerHookFn = PFN_vkVoidFunction (*)(const char* name);

void onDeviceCreated(DeviceData& data, bool isGame, LayerHookFn layerHook);
void onDeviceDestroyed(VkDevice device);

// The check's hook of a command buffer or submit function for `device`, or nullptr: when the check is off,
// and for any device but the game's (its hooks call the game device's next layer) or none (an instance
// lookup).
PFN_vkVoidFunction findHook(VkDevice device, const char* name);

// The context table (RVA 0x667F018, 13 categories of 4 slots); a context's current command buffer is at
// [[context + 0x118]].
inline constexpr int kCategories = 13;
inline constexpr int kSlots = 4;

// The table cell (category * kSlots + slot) whose current command buffer is `cb`; -1 for none, -2 when the
// table cannot be read.
int findContext(VkCommandBuffer cb);

// "G<rva>" for an address in the game's image, else the pointer.
void describeAddress(void* address, char* out, std::size_t size);

} // namespace evr::vkcore::cb_check
