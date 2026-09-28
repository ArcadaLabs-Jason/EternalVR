#pragma once

// GPU timing (ETERNALVR_GPU_TIMING=1, docs/VR_STEREO.md "GPU timing").
//
// Every vkQueueSubmit batch of the game's device is bracketed with two timestamps from a ring of query
// pairs: a command buffer before the batch's own writes one at the top of the pipe, one after them at the
// bottom. A game frame is every batch submitted since the previous present (all queues, async compute
// included); each present closes one. A few presents later the frame's results are read without waiting
// (a frame whose results are not ready yet is tried again on the next present) and summarized every 10 s
// in the log, per eye under Route S, with a row per frame in eternalvr-gpu-<pid>.csv next to the frames
// CSV. Off (no hook handed out, every entry point returns at once) unless ETERNALVR_GPU_TIMING=1.

#include "stereo_seq/eye_tags.hpp"
#include "vkcore/dispatch.hpp"

#include <vulkan/vulkan.h>

#include <vector>

namespace evr::vkcore::gpu_timing {

// ETERNALVR_GPU_TIMING=1 (read once).
bool enabled();

void onDeviceCreated(DeviceData& data,
                     const VkPhysicalDeviceProperties& properties,
                     const std::vector<VkQueueFamilyProperties>& families,
                     bool isGame);
void onDeviceDestroyed(VkDevice device);

// The layer's hook for `name` (vkQueueSubmit), or nullptr; always nullptr when GPU timing is off.
PFN_vkVoidFunction findHook(const char* name);

// A present on `queue` (before it goes down the chain): closes the device's current frame, reads the
// results of earlier frames that are ready and logs the 10 s summary when due.
void onPresent(VkQueue queue);

// Route S: the eye of the frame this thread's last present closed (called by the present hook).
void tagEye(stereo_seq::Eye eye);

// The presenter's pose age of a shown frame (XR worker), summarized with the GPU times.
void notePoseAge(double ms);

} // namespace evr::vkcore::gpu_timing
