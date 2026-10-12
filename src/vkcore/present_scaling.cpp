// The window surface's present scaling for the render size (present_scaling.hpp).

#include "vkcore/present_scaling.hpp"

#include "vkcore/window_cap.hpp"

#include <algorithm>
#include <vector>

namespace evr::vkcore::present_scaling {

std::optional<Scaling>
query(const InstanceData& inst, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    if (!inst.surfaceMaintenance1 || !inst.nextSurfaceCapabilities2 ||
        !inst.vk.GetPhysicalDeviceSurfacePresentModesKHR) {
        return std::nullopt;
    }
    std::uint32_t count = 0;
    inst.vk.GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    inst.vk.GetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data());
    Scaling all;
    all.behavior = ~0u;
    all.gravity = ~0u;
    bool any = false;
    for (const VkPresentModeKHR mode :
         {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
        if (std::find(modes.begin(), modes.end(), mode) == modes.end()) {
            continue;
        }
        VkSurfacePresentModeKHR presentMode{VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_KHR};
        presentMode.presentMode = mode;
        VkPhysicalDeviceSurfaceInfo2KHR info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
                                             &presentMode};
        info.surface = surface;
        VkSurfacePresentScalingCapabilitiesKHR scaling{
            VK_STRUCTURE_TYPE_SURFACE_PRESENT_SCALING_CAPABILITIES_KHR};
        VkSurfaceCapabilities2KHR caps{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR, &scaling};
        if (inst.nextSurfaceCapabilities2(physicalDevice, &info, &caps) != VK_SUCCESS) {
            return std::nullopt;
        }
        window_cap::logPresentMode(mode, scaling, caps.surfaceCapabilities);
        any = true;
        all.behavior &= scaling.supportedPresentScaling;
        all.gravity &= scaling.supportedPresentGravityX & scaling.supportedPresentGravityY;
        all.minScaled.width = std::max(all.minScaled.width, scaling.minScaledImageExtent.width);
        all.minScaled.height = std::max(all.minScaled.height, scaling.minScaledImageExtent.height);
        all.maxScaled.width = std::min(all.maxScaled.width, scaling.maxScaledImageExtent.width);
        all.maxScaled.height = std::min(all.maxScaled.height, scaling.maxScaledImageExtent.height);
    }
    if (!any) {
        return std::nullopt;
    }
    return all;
}

VkPresentScalingFlagsKHR chooseBehavior(const Scaling& s) {
    if ((s.behavior & VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR) && s.gravity) {
        return VK_PRESENT_SCALING_ASPECT_RATIO_STRETCH_BIT_KHR;
    }
    if (s.behavior & VK_PRESENT_SCALING_STRETCH_BIT_KHR) {
        return VK_PRESENT_SCALING_STRETCH_BIT_KHR;
    }
    if (s.behavior & VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR) {
        return VK_PRESENT_SCALING_ONE_TO_ONE_BIT_KHR;
    }
    return 0;
}

VkPresentGravityFlagsKHR chooseGravity(const Scaling& s) {
    if (s.gravity & VK_PRESENT_GRAVITY_CENTERED_BIT_KHR) {
        return VK_PRESENT_GRAVITY_CENTERED_BIT_KHR;
    }
    return s.gravity & VK_PRESENT_GRAVITY_MIN_BIT_KHR ? VK_PRESENT_GRAVITY_MIN_BIT_KHR : 0;
}

} // namespace evr::vkcore::present_scaling
