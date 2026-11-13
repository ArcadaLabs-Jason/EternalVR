#pragma once

// The Vulkan entry points of the depth layer's copy shader (presenter_depth.hpp), from the next layer: the
// presenter's own table (dispatch.hpp) has none of them. Private to presenter_depth_setup.cpp and
// presenter_depth_vk.cpp.

#include "vkcore/dispatch.hpp"
#include "vkcore/presenter_depth.hpp"

namespace evr::vkcore {

struct DepthLayer::Functions {
#define EVR_DEPTH_FUNCTIONS(X)                                                                               \
    X(CreateShaderModule)                                                                                    \
    X(DestroyShaderModule)                                                                                   \
    X(CreateDescriptorSetLayout)                                                                             \
    X(DestroyDescriptorSetLayout)                                                                            \
    X(CreatePipelineLayout)                                                                                  \
    X(DestroyPipelineLayout)                                                                                 \
    X(CreateComputePipelines)                                                                                \
    X(DestroyPipeline)                                                                                       \
    X(CreateSampler)                                                                                         \
    X(DestroySampler)                                                                                        \
    X(CreateDescriptorPool)                                                                                  \
    X(DestroyDescriptorPool)                                                                                 \
    X(AllocateDescriptorSets)                                                                                \
    X(UpdateDescriptorSets)                                                                                  \
    X(CreateImageView)                                                                                       \
    X(DestroyImageView)                                                                                      \
    X(CreateQueryPool)                                                                                       \
    X(DestroyQueryPool)                                                                                      \
    X(GetQueryPoolResults)                                                                                   \
    X(CmdBindPipeline)                                                                                       \
    X(CmdBindDescriptorSets)                                                                                 \
    X(CmdPushConstants)                                                                                      \
    X(CmdDispatch)                                                                                           \
    X(CmdResetQueryPool)                                                                                     \
    X(CmdWriteTimestamp)
    EVR_DEPTH_FUNCTIONS(EVR_DECLARE_FN)
};

} // namespace evr::vkcore
