#pragma once

// Stalls of the game's presents (docs/VR_STEREO.md, "Stalls"), always on. The present hook stamps every
// present of the game's device; a gap longer than gpu_timing::kStallGapMs in play gets one `stall:` line
// with what the layer's own code and the game's heavier Vulkan calls took during it: the layer's present
// hook (its wait for the presenter's lock, the driver's present it calls), Route S drains, the game's
// pipeline creation and vkAllocateMemory calls, and the last VRAM reading (vram_watch.hpp). Which gaps
// count, and the line limit, are gpu_timing/present_stall.hpp's. The hot paths only add to atomics: no
// allocation, lock or log except for a stall.

#include "vkcore/dispatch.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace evr::vkcore::stall_watch {

void onDeviceCreated(DeviceData& data, bool isGame);
void onDeviceDestroyed(VkDevice device);

// The timing hook for vkCreateGraphicsPipelines, vkCreateComputePipelines or vkAllocateMemory, or nullptr.
// Each chains to the shader dump's hook of the same function where it hands one out, else to the next
// layer. Only the game's device is timed.
PFN_vkVoidFunction findHook(const char* name);

// The present hook of the game's device, on entry: closes the gap since the previous present (a line when
// it is a stall) and returns the entry time for presentLeft, which adds the hook's own time.
std::uint64_t presentEntered();
void presentLeft(std::uint64_t entered);

// Inside the present hook: the wait for the presenter's lock, and the driver's present (microseconds).
void addLockWait(std::uint64_t micros);
void addDriverPresent(std::uint64_t micros);
// A Route S drain held the game's frontend (seq_hooks.cpp).
void addDrain(std::uint64_t micros);

// The camera hook ran a game frame. trackGameTicks(true) once the hook is installed: only then are stalls
// away from play (loading screens, menus) told apart; without it every stall gets a line.
void onGameTick();
void trackGameTicks(bool on);

// XR worker, every 10 s: the stalls of the last 10 s (nothing when there were none).
void logSummary();

} // namespace evr::vkcore::stall_watch
