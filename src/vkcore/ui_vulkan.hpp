#pragma once

// Vulkan side of the UI layer (ETERNALVR_UI_LAYER=1, docs/rig-findings/ui-layer.md section 5).
//
// The game's GUI target `_gui` is created without TRANSFER_SRC and moved between layouts by the game's
// own barriers. With the UI layer on, this module:
// - adds TRANSFER_SRC to every image created exactly like `_gui` (ui_layer::candidateUsage) and keeps a
//   record of each (size, usage, queue family sharing);
// - follows the layout of the one the game uses as its GUI target (watch()) through vkCmdPipelineBarrier,
// vkBeginCommandBuffer,
//   vkCmdExecuteCommands and vkQueueSubmit (ui_layer::LayoutTracker).
// It reads and writes no game memory. Off (no hook handed out) unless ETERNALVR_UI_LAYER=1; image
// creation is changed only while the multiplayer guard allows touching the game.

#include "ui_layer/gui_target.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>

namespace evr::vkcore::ui_vulkan {

// ETERNALVR_UI_LAYER=1 (read once).
bool enabled();

void onDeviceCreated(VkDevice device, PFN_vkGetDeviceProcAddr nextGetDeviceProcAddr, bool isGame);
void onDeviceDestroyed(VkDevice device);

// ETERNALVR_CAPTURE_MOTION is set (read once): images that could be the game's motion-vector targets get
// TRANSFER_SRC and are followed from their creation (ui_layer/motion_target.hpp). Needs the UI layer on.
bool motionCaptureRequested();

// ETERNALVR_DEPTH_LAYER is on (read once): images that can be the game's scene depth get a record (and
// SAMPLED, features/depth/depth_layer.hpp) when the game makes them, for the depth layer
// (presenter_depth.hpp).
bool depthLayerRequested();

// The layer's hook for `name`, or nullptr (always nullptr when the UI layer is off).
PFN_vkVoidFunction findHook(const char* name);

// The record of a candidate image, or nullopt when `image` is not one.
std::optional<ui_layer::ImageRecord> recordOf(VkImage image);

// Follows `image`'s layout from now on (the GUI target read from the game); the image followed before is
// dropped. Its layout is known once the game's next barrier on it has been submitted.
void watch(VkImage image);

// The image watch() follows now (the game's GUI target), or VK_NULL_HANDLE before the first and once it is
// destroyed. Lock-free: foveated rendering asks before each render pass (vrs_gui.hpp).
VkImage guiTarget();

// Follows a prepared image's layout from now on (the motion-vector capture, for the images it finds bound as
// velocity); true when it is followed. A no-op for an image followed already.
bool follow(VkImage image);

struct ImageState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkQueue queue = VK_NULL_HANDLE; // the queue whose submit last moved it
};
// The layout a candidate image is in once all submitted work has run; nullopt when unknown.
std::optional<ImageState> stateOf(VkImage image);

// The depth layer (presenter_depth.hpp): the game's next submit on every queue but `except` first waits for
// `timeline` to reach `value` (one wait batch ahead of its own), so the game's work submitted after this call
// on another queue does not write the depth image before the layer's read of it on `except` has run (a submit
// made before the call is not ordered). Each call raises the value; a queue waits once per value.
// stopHolding() ends it; holdWaits() counts the batches added.
void holdQueues(VkQueue except, VkSemaphore timeline, std::uint64_t value);
void stopHolding();
std::uint64_t holdWaits();

// The depth layer's source image: takeDestroyed() is true once after the game destroyed it.
void watchDestroy(VkImage image);
bool takeDestroyed();

struct Counters {
    std::uint64_t candidates = 0; // images prepared (TRANSFER_SRC added)
    std::uint64_t candidatesNotFollowed = 0;
    std::uint64_t watchChanges = 0;      // times the followed GUI target changed
    std::uint64_t motionCandidates = 0;  // images prepared for the motion-vector capture
    std::uint64_t motionNotFollowed = 0; // follow() found no free place
    std::uint64_t depthCandidates = 0;   // images recorded as a possible scene depth (depth layer)
};
Counters counters();

} // namespace evr::vkcore::ui_vulkan
