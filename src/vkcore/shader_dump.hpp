#pragma once

// Opt-in shader and draw dump for the stereo census (T-049, T-070, T-105; tools/shader_census).
//
// ETERNALVR_DUMP_SHADERS=<dir> turns it on. Unset, enabled() is false, findHook() hands out nothing and
// the layer's dispatch is unchanged. On, the layer writes to <dir>:
//
// - modules/<fnv1a64>.spv: every SPIR-V module the game creates, once per distinct code;
// - pipelines.jsonl: each pipeline's stages (module hash, entry point, specialization data), layout and
//   libraries;
// - layouts.jsonl: descriptor set layouts (bindings) and pipeline layouts (set layouts, push ranges);
// - sets.jsonl: descriptor set allocations (set -> layout) and buffer descriptor writes (ranges);
// - drawlog.jsonl: for presents [ETERNALVR_DUMP_SKIP_FRAMES, + ETERNALVR_DUMP_FRAMES) (defaults 0 and
//   60), every pipeline bind, descriptor set bind, push constant update, draw and dispatch recorded by
//   the game, tagged with the frame (present count) and the command buffer.
//
// The dump only observes Vulkan calls: it reads no game memory and changes nothing the game sees.

#include <vulkan/vulkan.h>

namespace evr::vkcore::shader_dump {

// Reads ETERNALVR_DUMP_SHADERS once (thread-safe).
bool enabled();

// A dump hook for a device-level function, or nullptr when the dump is off or `name` is not hooked.
PFN_vkVoidFunction findHook(const char* name);

// Registers every device so the hooks can reach the next layer; only the game's device is logged.
void onDeviceCreated(VkDevice device, PFN_vkGetDeviceProcAddr nextGetDeviceProcAddr, bool isGame);
void onDeviceDestroyed(VkDevice device);

// Counts one present of the game's device; flushes the draw log of the finished frame.
void onPresent(VkDevice device);

} // namespace evr::vkcore::shader_dump
