#include "vkcore/device_augment.hpp"

#include "vkcore/log.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/vrs_nv.hpp"
#include "vkcore/window_cap.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace evr::vkcore {

namespace {

// The GPU's vendor and driver, once (an AMD driver without present scaling caps the render size,
// window_cap.hpp).
void logDriver(InstanceData& inst, VkPhysicalDevice physicalDevice, bool driverProperties) {
    VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                      driverProperties ? &driver : nullptr};
    if (!inst.vk.GetPhysicalDeviceProperties2) {
        return;
    }
    inst.vk.GetPhysicalDeviceProperties2(physicalDevice, &props);
    EVR_LOG("  GPU vendor 0x%04x device 0x%04x, driver version 0x%08x, driver '%s' '%s'",
            props.properties.vendorID, props.properties.deviceID, props.properties.driverVersion,
            driverProperties ? driver.driverName : "?", driverProperties ? driver.driverInfo : "?");
}

} // namespace

void planDeviceAugment(DeviceAugment& plan,
                       InstanceData& inst,
                       VkPhysicalDevice physicalDevice,
                       const VkDeviceCreateInfo& info) {
    plan.extensions.assign(info.ppEnabledExtensionNames,
                           info.ppEnabledExtensionNames + info.enabledExtensionCount);

    std::uint32_t count = 0;
    inst.vk.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    inst.vk.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, available.data());
    auto supported = [&](const char* name) {
        return std::any_of(available.begin(), available.end(), [name](const VkExtensionProperties& p) {
            return std::strcmp(p.extensionName, name) == 0;
        });
    };

    for (const char* name :
         {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
          VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
          VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME}) {
        if (hasExtension(plan.extensions, name)) {
            continue;
        }
        if (!supported(name)) {
            EVR_LOG("  device extension %s is not supported; the presenter stays off", name);
            status::flat("the graphics driver lacks a feature the mod needs to share images with the headset "
                         "(update the graphics driver; details in the log)");
            return;
        }
        plan.extensions.push_back(name);
        EVR_LOG("  adding device extension %s", name);
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &timeline};
    if (!inst.vk.GetPhysicalDeviceFeatures2) {
        EVR_LOG("  vkGetPhysicalDeviceFeatures2 unavailable; the presenter stays off");
        status::flat("the graphics driver is too old for the mod (update the graphics driver)");
        return;
    }
    inst.vk.GetPhysicalDeviceFeatures2(physicalDevice, &features);
    if (!timeline.timelineSemaphore) {
        EVR_LOG("  timelineSemaphore is not supported; the presenter stays off");
        status::flat("the graphics driver lacks timeline semaphores (update the graphics driver)");
        return;
    }

    // Merged into whichever struct the game already chains (VUID-VkDeviceCreateInfo-pNext-02830), on a
    // copy of its chain; otherwise the per-feature struct is added.
    switch (plan.features.enable(info.pNext)) {
    case TimelineFeatureChain::Result::AlreadyEnabled:
        break;
    case TimelineFeatureChain::Result::Merged:
        EVR_LOG("  enabling timelineSemaphore in a copy of the game's feature struct");
        break;
    case TimelineFeatureChain::Result::Added:
        EVR_LOG("  adding VkPhysicalDeviceTimelineSemaphoreFeatures to the feature chain");
        break;
    case TimelineFeatureChain::Result::Unsupported:
        EVR_LOG(
            "  the game's device create chain holds structure type %d before its feature struct, which the "
            "layer cannot copy; the presenter stays off",
            static_cast<int>(plan.features.unknownType()));
        return;
    }
    plan.ok = true;
    plan.shadingRate = vrs_nv::planDevice(inst, physicalDevice, plan.extensions);
    logDriver(inst, physicalDevice, supported(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME));

    // Optional: the window's presents (Route S). Leaves the interop plan as it is when unavailable.
    // The swapchain extension of the same family as an instance surface one: KHR when both have it, else the
    // older EXT.
    if (!inst.surfaceMaintenance1) {
        EVR_LOG(
            "  the instance has no surface maintenance extension: no swapchain maintenance, so the game's "
            "window takes every present and the game renders at its window's size");
        return;
    }
    if (hasExtension(plan.extensions, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) ||
        hasExtension(plan.extensions, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
        return;
    }
    const bool hideKhr = window_cap::testHideKhrMaintenance();
    const bool khr = supported(VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) && !hideKhr;
    const bool ext = supported(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
    EVR_LOG("  the device lists %s %s%s and %s %s; the instance has the surface maintenance extension%s%s",
            VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME, khr ? "yes" : "no",
            hideKhr ? " (hidden by ETERNALVR_TEST_HIDE_KHR_MAINTENANCE, a test knob)" : "",
            VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME, ext ? "yes" : "no",
            inst.surfaceMaintenance1Khr ? " KHR" : "", inst.surfaceMaintenance1Ext ? " EXT" : "");
    const char* extension = khr && inst.surfaceMaintenance1Khr ? VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME
                            : ext && inst.surfaceMaintenance1Ext
                                ? VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME
                                : nullptr;
    if (!extension) {
        EVR_LOG("  no swapchain maintenance extension matches the instance's: the game's window takes every "
                "present and the game renders at its window's size");
        return;
    }
    if (window_cap::testNoPresentScaling()) {
        EVR_LOG("  device extension %s left out (ETERNALVR_TEST_NO_PRESENT_SCALING, a test knob): the game's "
                "window takes every present and the game renders at its window's size",
                extension);
        return;
    }
    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &maintenance};
    inst.vk.GetPhysicalDeviceFeatures2(physicalDevice, &query);
    if (!maintenance.swapchainMaintenance1) {
        return;
    }
    plan.extensions.push_back(extension);
    plan.maintenance1 = extension;
    plan.release.swapchainMaintenance1 = VK_TRUE;
    plan.release.pNext = const_cast<void*>(plan.features.head());
    plan.releaseImages = true;
    EVR_LOG("  adding device extension %s (the game's window takes only the presents it shows)", extension);
}

void DeviceAugment::dropRelease() {
    if (!releaseImages) {
        return;
    }
    releaseImages = false;
    extensions.erase(std::remove_if(extensions.begin(), extensions.end(),
                                    [this](const char* e) { return std::strcmp(e, maintenance1) == 0; }),
                     extensions.end());
}

} // namespace evr::vkcore
