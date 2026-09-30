#pragma once

// What the layer adds to the game's device (T-082): the external memory, external semaphore and
// timeline semaphore extensions, and the timelineSemaphore feature on a copy of the game's chain.

#include "vkcore/device_features.hpp"
#include "vkcore/dispatch.hpp"

#include <vector>

namespace evr::vkcore {

struct DeviceAugment {
    std::vector<const char*> extensions;
    TimelineFeatureChain features; // the game's feature chain with timelineSemaphore on (copied, T-082)
    bool ok = false;
    // Route S's window presents (stereo_present.hpp): VK_KHR_swapchain_maintenance1 (or the EXT one) and its
    // feature in front of the chain, when the instance has the matching surface extension and the device
    // supports it.
    bool releaseImages = false;
    // The swapchain maintenance extension added: VK_KHR_swapchain_maintenance1 or
    // VK_EXT_swapchain_maintenance1.
    const char* maintenance1 = VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR release{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR};

    // VK_NV_shading_rate_image's feature (vrs_nv.hpp, ETERNALVR_VRS_TEST), in front of the chain when set.
    VkPhysicalDeviceShadingRateImageFeaturesNV* shadingRate = nullptr;

    // The pNext chain to create the device with.
    [[nodiscard]] const void* head() const {
        const void* rest = releaseImages ? static_cast<const void*>(&release) : features.head();
        if (!shadingRate) {
            return rest;
        }
        shadingRate->pNext = const_cast<void*>(rest);
        return shadingRate;
    }
    // Takes the swapchain maintenance additions out again (a create that failed with them).
    void dropRelease();
};

// Fills `plan` for the game's device; plan.ok stays false (logged) when something is unsupported.
void planDeviceAugment(DeviceAugment& plan,
                       InstanceData& inst,
                       VkPhysicalDevice physicalDevice,
                       const VkDeviceCreateInfo& info);

} // namespace evr::vkcore
