#include "vkcore/device_features.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <vector>

using evr::vkcore::deviceCreateStructSize;
using evr::vkcore::TimelineFeatureChain;

namespace {

std::vector<VkStructureType> types(const void* head) {
    std::vector<VkStructureType> out;
    for (auto* node = static_cast<const VkBaseInStructure*>(head); node; node = node->pNext) {
        out.push_back(node->sType);
    }
    return out;
}

template <typename T>
const T* find(const void* head, VkStructureType type) {
    for (auto* node = static_cast<const VkBaseInStructure*>(head); node; node = node->pNext) {
        if (node->sType == type) {
            return reinterpret_cast<const T*>(node);
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("without a feature struct the timeline struct is put in front of the game's chain") {
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.features.samplerAnisotropy = VK_TRUE;
    TimelineFeatureChain chain;
    CHECK(chain.enable(&features) == TimelineFeatureChain::Result::Added);
    const auto* timeline = static_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(chain.head());
    REQUIRE(timeline->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
    CHECK(timeline->timelineSemaphore == VK_TRUE);
    CHECK(timeline->pNext == &features);

    TimelineFeatureChain empty;
    CHECK(empty.enable(nullptr) == TimelineFeatureChain::Result::Added);
    CHECK(static_cast<const VkBaseInStructure*>(empty.head())->pNext == nullptr);
}

TEST_CASE("a feature struct that already enables timeline semaphores is used as it is") {
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    v12.timelineSemaphore = VK_TRUE;
    TimelineFeatureChain chain;
    CHECK(chain.enable(&v12) == TimelineFeatureChain::Result::AlreadyEnabled);
    CHECK(chain.head() == &v12);
}

TEST_CASE("the game's Vulkan 1.2 feature struct is enabled on a copy, never in place") {
    VkPhysicalDeviceDescriptorIndexingFeatures tail{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &tail};
    v12.bufferDeviceAddress = VK_TRUE;
    VkPhysicalDeviceVulkan11Features v11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &v12};
    v11.multiview = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &v11};
    features.features.shaderInt64 = VK_TRUE;

    const auto snapshot12 = v12;
    const auto snapshot11 = v11;
    const auto snapshot2 = features;

    TimelineFeatureChain chain;
    REQUIRE(chain.enable(&features) == TimelineFeatureChain::Result::Merged);

    // The game's structs are untouched.
    CHECK(std::memcmp(&v12, &snapshot12, sizeof(v12)) == 0);
    CHECK(std::memcmp(&v11, &snapshot11, sizeof(v11)) == 0);
    CHECK(std::memcmp(&features, &snapshot2, sizeof(features)) == 0);
    CHECK(v12.timelineSemaphore == VK_FALSE);

    // The new chain has the same structs in the same order, the flag set, and the game's tail shared.
    CHECK(types(chain.head()) ==
          std::vector<VkStructureType>{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                       VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                                       VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                       VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES});
    CHECK(chain.head() != &features);
    const auto* copy12 = find<VkPhysicalDeviceVulkan12Features>(
        chain.head(), VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    REQUIRE(copy12);
    CHECK(copy12 != &v12);
    CHECK(copy12->timelineSemaphore == VK_TRUE);
    CHECK(copy12->bufferDeviceAddress == VK_TRUE);
    CHECK(copy12->pNext == &tail);
    CHECK(find<VkPhysicalDeviceVulkan11Features>(chain.head(),
                                                 VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES)
              ->multiview == VK_TRUE);
    CHECK(static_cast<const VkPhysicalDeviceFeatures2*>(chain.head())->features.shaderInt64 == VK_TRUE);
}

TEST_CASE("the per-feature timeline struct is enabled on a copy too") {
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    TimelineFeatureChain chain;
    REQUIRE(chain.enable(&timeline) == TimelineFeatureChain::Result::Merged);
    CHECK(timeline.timelineSemaphore == VK_FALSE);
    const auto* copy = static_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(chain.head());
    CHECK(copy != &timeline);
    CHECK(copy->timelineSemaphore == VK_TRUE);
}

TEST_CASE("an unknown struct before the one to change makes the edit fail without touching anything") {
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan12Features unknown{};
    unknown.sType = static_cast<VkStructureType>(1000999000);
    unknown.pNext = &v12;
    TimelineFeatureChain chain;
    CHECK(chain.enable(&unknown) == TimelineFeatureChain::Result::Unsupported);
    CHECK(chain.unknownType() == static_cast<VkStructureType>(1000999000));
    CHECK(chain.head() == &unknown);
    CHECK(v12.timelineSemaphore == VK_FALSE);
}

TEST_CASE("an unknown struct after the one to change is shared unchanged") {
    VkPhysicalDeviceVulkan12Features unknown{};
    unknown.sType = static_cast<VkStructureType>(1000999000);
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &unknown};
    TimelineFeatureChain chain;
    CHECK(chain.enable(&v12) == TimelineFeatureChain::Result::Merged);
    CHECK(static_cast<const VkBaseInStructure*>(chain.head())->pNext ==
          reinterpret_cast<const VkBaseInStructure*>(&unknown));
}

TEST_CASE("the loader's own create info is copyable") {
    CHECK(deviceCreateStructSize(VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO) ==
          sizeof(VkLayerDeviceCreateInfo));
    CHECK(deviceCreateStructSize(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) ==
          sizeof(VkPhysicalDeviceVulkan12Features));
    CHECK(deviceCreateStructSize(static_cast<VkStructureType>(1000999000)) == 0);
}
