#pragma once

// Adds the timeline semaphore feature to the game's device create info without writing into the
// game's own structures (T-082: the create info is copied before any change).
//
// VUID-VkDeviceCreateInfo-pNext-02830: when the game chains VkPhysicalDeviceVulkan12Features, the
// feature must be enabled there, not in an added VkPhysicalDeviceTimelineSemaphoreFeatures. The game's
// chain is const, so the struct that needs the flag is copied, and so is every struct before it (their
// pNext must point at the copies); the structs after it are shared unchanged. Only structures whose
// size is known can be copied: an unknown one before the struct to change makes the edit fail, and
// the layer then leaves the device as the game asked for it.

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace evr::vkcore {

// sizeof the structure with this sType, for the structures a device create chain may carry; 0 when
// unknown.
std::size_t deviceCreateStructSize(VkStructureType type);

class TimelineFeatureChain {
public:
    enum class Result {
        AlreadyEnabled, // the game's chain enables it; use the chain as it is
        Merged,         // a copy of the game's feature struct carries the flag
        Added,          // a VkPhysicalDeviceTimelineSemaphoreFeatures was put in front of the chain
        Unsupported,    // a structure of unknown size precedes the one to change (see unknownType)
    };

    // Plans the chain for `pNext` (the game's VkDeviceCreateInfo::pNext). Never writes through it.
    Result enable(const void* pNext);

    // The chain to pass down (valid while this object lives and is not moved).
    [[nodiscard]] const void* head() const { return head_; }
    [[nodiscard]] VkStructureType unknownType() const { return unknownType_; }

private:
    std::vector<std::unique_ptr<std::byte[]>> copies_;
    VkPhysicalDeviceTimelineSemaphoreFeatures added_{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    const void* head_ = nullptr;
    VkStructureType unknownType_ = VK_STRUCTURE_TYPE_MAX_ENUM;
};

} // namespace evr::vkcore
