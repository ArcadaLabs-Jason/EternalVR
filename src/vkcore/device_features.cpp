#include "vkcore/device_features.hpp"

#include <cstring>
#include <utility>

namespace evr::vkcore {

namespace {

// Structures a device create chain commonly carries: the loader's own, the core feature structs and
// the extension feature structs a renderer like the game's enables.
#define EVR_DEVICE_CREATE_STRUCTS(X)                                                                         \
    X(VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, VkLayerDeviceCreateInfo)                                  \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, VkPhysicalDeviceFeatures2)                               \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, VkPhysicalDeviceVulkan11Features)               \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, VkPhysicalDeviceVulkan12Features)               \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, VkPhysicalDeviceVulkan13Features)               \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,                                         \
      VkPhysicalDeviceTimelineSemaphoreFeatures)                                                             \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES, VkPhysicalDevice16BitStorageFeatures)        \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES, VkPhysicalDevice8BitStorageFeatures)          \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,                                        \
      VkPhysicalDeviceShaderFloat16Int8Features)                                                             \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,                                        \
      VkPhysicalDeviceDescriptorIndexingFeatures)                                                            \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,                                      \
      VkPhysicalDeviceBufferDeviceAddressFeatures)                                                           \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,                                        \
      VkPhysicalDeviceScalarBlockLayoutFeatures)                                                             \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES,                                      \
      VkPhysicalDeviceImagelessFramebufferFeatures)                                                          \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES,                             \
      VkPhysicalDeviceUniformBufferStandardLayoutFeatures)                                                   \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES,                             \
      VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures)                                                   \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES,                             \
      VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures)                                                   \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES, VkPhysicalDeviceHostQueryResetFeatures)   \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES,                                        \
      VkPhysicalDeviceVulkanMemoryModelFeatures)                                                             \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES,                                        \
      VkPhysicalDeviceShaderAtomicInt64Features)                                                             \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES, VkPhysicalDeviceMultiviewFeatures)               \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES,                                          \
      VkPhysicalDeviceVariablePointersFeatures)                                                              \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,                                   \
      VkPhysicalDeviceSamplerYcbcrConversionFeatures)                                                        \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES,                                     \
      VkPhysicalDeviceShaderDrawParametersFeatures)                                                          \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,                                          \
      VkPhysicalDeviceDynamicRenderingFeatures)                                                              \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,                                          \
      VkPhysicalDeviceSynchronization2Features)                                                              \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES, VkPhysicalDeviceMaintenance4Features)        \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,                                 \
      VkPhysicalDeviceAccelerationStructureFeaturesKHR)                                                      \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,                                   \
      VkPhysicalDeviceRayTracingPipelineFeaturesKHR)                                                         \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, VkPhysicalDeviceRayQueryFeaturesKHR)         \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR,                                  \
      VkPhysicalDeviceFragmentShadingRateFeaturesKHR)                                                        \
    X(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT, VkPhysicalDeviceMeshShaderFeaturesEXT)     \
    X(VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO, VkDeviceGroupDeviceCreateInfo)                      \
    X(VK_STRUCTURE_TYPE_DEVICE_DIAGNOSTICS_CONFIG_CREATE_INFO_NV, VkDeviceDiagnosticsConfigCreateInfoNV)     \
    X(VK_STRUCTURE_TYPE_DEVICE_MEMORY_OVERALLOCATION_CREATE_INFO_AMD,                                        \
      VkDeviceMemoryOverallocationCreateInfoAMD)

VkBool32* timelineFlag(VkBaseOutStructure* node) {
    if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
        return &reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(node)->timelineSemaphore;
    }
    if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES) {
        return &reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(node)->timelineSemaphore;
    }
    return nullptr;
}

bool holdsTimelineFlag(VkStructureType type) {
    return type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES ||
           type == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
}

} // namespace

std::size_t deviceCreateStructSize(VkStructureType type) {
    switch (type) {
#define EVR_STRUCT_SIZE(sType, Struct)                                                                       \
    case sType:                                                                                              \
        return sizeof(Struct);
        EVR_DEVICE_CREATE_STRUCTS(EVR_STRUCT_SIZE)
#undef EVR_STRUCT_SIZE
    default:
        return 0;
    }
}

TimelineFeatureChain::Result TimelineFeatureChain::enable(const void* pNext) {
    copies_.clear();
    unknownType_ = VK_STRUCTURE_TYPE_MAX_ENUM;
    head_ = pNext;

    const VkBaseInStructure* target = nullptr;
    for (auto* node = static_cast<const VkBaseInStructure*>(pNext); node; node = node->pNext) {
        if (holdsTimelineFlag(node->sType)) {
            target = node;
            break;
        }
    }
    if (!target) {
        added_ = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
        added_.pNext = const_cast<void*>(pNext);
        added_.timelineSemaphore = VK_TRUE;
        head_ = &added_;
        return Result::Added;
    }
    VkBool32 enabled = VK_FALSE;
    if (target->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
        enabled = reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(target)->timelineSemaphore;
    } else {
        enabled =
            reinterpret_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(target)->timelineSemaphore;
    }
    if (enabled) {
        return Result::AlreadyEnabled;
    }

    // Copy the structs up to and including the target, linked to each other; the copy of the target
    // keeps the game's tail.
    VkBaseOutStructure* previous = nullptr;
    for (auto* node = static_cast<const VkBaseInStructure*>(pNext); node; node = node->pNext) {
        const std::size_t size = deviceCreateStructSize(node->sType);
        if (size == 0) {
            copies_.clear();
            head_ = pNext;
            unknownType_ = node->sType;
            return Result::Unsupported;
        }
        auto bytes = std::make_unique<std::byte[]>(size);
        std::memcpy(bytes.get(), node, size);
        auto* copy = reinterpret_cast<VkBaseOutStructure*>(bytes.get());
        copies_.push_back(std::move(bytes));
        if (previous) {
            previous->pNext = copy;
        } else {
            head_ = copy;
        }
        previous = copy;
        if (node == target) {
            *timelineFlag(copy) = VK_TRUE;
            return Result::Merged;
        }
    }
    return Result::Unsupported; // not reached: the target is in the chain
}

} // namespace evr::vkcore
