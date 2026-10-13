#pragma once

// Adapter steering on two-GPU machines (T-057, T-111): the order in which the layer lists the Vulkan physical
// devices to the game so that the game's own choice lands on the headset's adapter (the OpenXR system's
// LUID). Pure logic; the Vulkan side is adapter_steer.cpp.
//
// The game's choice (DOOMEternalx64vk.exe, the device pick after vkEnumeratePhysicalDevices, read from the
// disassembly): with r_physicalDeviceIndex at its default -1 the game takes the first device whose type is
// DISCRETE_GPU and whose apiVersion is at least 1.1, else the first device; with r_physicalDeviceIndex >= 0
// it takes that index (clamped to the list). It never calls vkEnumeratePhysicalDeviceGroups.
//
// So moving the headset's device to the front is enough when it would win that rule there (a discrete GPU, or
// a list with no other discrete GPU). When it would not (an integrated GPU drives the headset next to a
// discrete one), the list is left as it is and the presenter's LUID check refuses VR: cutting the list to
// that device alone would put the game on the integrated GPU, so it is done only for the rig's forced
// mismatch. With exactly one device the rule takes, no order changes the game's pick (reorderCanMatter).

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace evr::vkcore::adapter_order {

using Luid = std::array<std::uint8_t, VK_LUID_SIZE>;

struct Device {
    std::optional<Luid> luid; // empty when the driver reports no valid LUID (a software device, say)
    VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    std::uint32_t apiVersion = 0;
};

// The game's rule takes this device over any that is not (a DISCRETE_GPU of api 1.1 or later).
bool ruleTakes(const Device& device);

// The index the game picks from `devices` with r_physicalDeviceIndex at -1; 0 for an empty list.
std::size_t gamePick(const std::vector<Device>& devices);

// False when exactly one device is one the rule takes: the game picks it in any order, so the runtime need
// not be asked.
bool reorderCanMatter(const std::vector<Device>& devices);

// A device with this LUID: the first one the rule takes, else the first (two drivers can list one adapter).
std::optional<std::size_t> findLuid(const std::vector<Device>& devices, const Luid& luid);

// The first device without this LUID (one without a valid LUID counts): the rig's forced mismatch.
std::optional<std::size_t> firstOther(const std::vector<Device>& devices, const Luid& luid);

enum class Action {
    None,          // fewer than two devices, or no target: the game's list as it is
    AlreadyPicked, // the game picks the target, or a device with its LUID, by itself: the list as it is
    Reordered,     // the target moved to the front, the others after it in their order
    NotPickable,   // in front the game would still pick another device, and cutting is not allowed: as it is
    Filtered,      // the target alone (cutting allowed: the rig's forced mismatch)
};

struct Plan {
    Action action = Action::None;
    std::vector<std::size_t> order; // indices into the input, in the order the game is given them
};

// What the game is given, for `target` (an index into `devices`; empty or out of range: None). `allowCut`: a
// target the game would not pick even in front is given alone (Filtered) instead of left (NotPickable).
Plan plan(const std::vector<Device>& devices, std::optional<std::size_t> target, bool allowCut);

// The order of the physical device groups for a device plan's action: `holdsTarget[i]` is true when group i
// holds the target device. Reordered: the first such group first; Filtered: that group alone; otherwise, or
// when no group holds the target, every group in its order.
std::vector<std::size_t> groupOrder(const std::vector<bool>& holdsTarget, Action action);

const char* actionName(Action action);

// "hhhhhhhh:llllllll" (the high and low halves, as the presenter logs the runtime's LUID), or "none".
std::string luidText(const std::optional<Luid>& luid);

} // namespace evr::vkcore::adapter_order
