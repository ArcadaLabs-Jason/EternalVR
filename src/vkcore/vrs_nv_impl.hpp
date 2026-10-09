#pragma once

// Foveated rendering's shared parts (vrs_nv.hpp): its settings, each device's state, the rate images
// (vrs_rate_images.cpp), the eye of a render pass (vrs_pass_eye.cpp) and the presenter's marks
// (vrs_marks.cpp).

#include "features/foveation/eye_targets.hpp"
#include "stereo_seq/pass_frames.hpp"
#include "vkcore/dispatch.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace evr::vkcore::vrs_nv {

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

enum class Mode { Off, Uniform, EyeTest, Fovea };

struct Settings {
    Mode mode = Mode::Off;
    std::uint8_t rate = 0;     // Uniform: the palette index every texel holds
    float fullDegrees = 24.0f; // Fovea: the full-rate region's angle (foveation_region.hpp)
    float halfDegrees = 40.0f; // Fovea: the half-rate region's angle; quarter rate outside
    bool marks = false;        // ETERNALVR_VRS_TINT: the presenter marks the reduced-rate areas
    bool guesses = false;      // ETERNALVR_TEST_VRS_PARITY: passes also take their recording's guessed frame
};

// Read once from ETERNALVR_VRS_TEST, ETERNALVR_VRS_FOVEA, ETERNALVR_FOVEATION, ETERNALVR_VRS_TINT and
// ETERNALVR_TEST_VRS_PARITY.
const Settings& settings();

// A render pass's eye: 0 (L), 1 (R) or kNoEye (mono, untagged or not known: full rate).
inline constexpr int kNoEye = 2;

struct RateImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkCommandBuffer uploadedBy = VK_NULL_HANDLE;
    double uploadedAt = 0.0;
};

struct VrsDevice {
    bool on = false; // the extension is enabled
    DeviceData* data = nullptr;
    VkExtent2D texel{16, 16};
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines = nullptr;
    PFN_vkCmdBeginRenderPass cmdBeginRenderPass = nullptr;
    PFN_vkCmdBindShadingRateImageNV cmdBindShadingRateImage = nullptr;
    PFN_vkCreateImageView createImageView = nullptr;
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage = nullptr;
    std::mutex imagesMutex;
    std::unordered_map<std::uint64_t, RateImage> images; // by eye shape generation, size and eye
    std::uint32_t imagesGeneration = 0;                  // the generation made last, and its images
    std::size_t generationImages = 0;
    bool imagesFull = false;
    std::atomic<std::uint64_t> pipelines{0};
    std::array<std::atomic<std::uint64_t>, 3> binds{}; // eye L, eye R, mono, untagged or not known
    std::atomic<std::uint64_t> passes{0};
    std::atomic<std::uint64_t> fullRate{0};
    std::atomic<std::uint64_t> guiPasses{0}; // passes into the GUI target, kept at full rate
    // Passes bound with a rate image, by eye (with one rate for every pass, all eye L's); the marks drawn up
    // to each eye's count so far (vrs_marks.cpp).
    std::array<std::atomic<std::uint64_t>, 2> coarse{};
    std::array<std::atomic<std::uint64_t>, 2> coarseMarked{};
    // The eye image's size (width << 32 | height), from the GUI target, which has it; 0 until its first pass.
    // Until then the game's swapchain's size, the same output size (0 before the swapchain): passes into the
    // GUI target are found only with the UI layer on.
    std::atomic<std::uint64_t> eyeSize{0};
    std::atomic<std::uint64_t> swapchainSize{0};
    // Render target sizes kept at full rate because they are not in the eye's space, logged once each.
    std::mutex otherMutex;
    std::vector<std::uint64_t> otherSizes;
    std::atomic<bool> otherSizesFull{false};
    // Which backend frame each pass is recorded for (vrs_pass_eye.cpp), from where each recording starts and
    // ends (vrs_command_buffers.cpp); `followsFrames`: on, and the passes' eyes are asked (not one rate for
    // every pass); `usesGuesses`: the recordings' guessed frames are used too (ETERNALVR_TEST_VRS_PARITY).
    bool followsFrames = false;
    bool usesGuesses = false;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkFreeCommandBuffers freeCommandBuffers = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkResetCommandPool resetCommandPool = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    std::mutex framesMutex;
    stereo_seq::PassFrames frames;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> poolBuffers; // under framesMutex
    std::atomic<bool> framesFullLogged{false};
    std::atomic<std::uint64_t> inFlightOther{0}; // passes the tag in flight would have given the other eye
};

VrsDevice* deviceOf(DispatchKey key);

double nowSeconds();

// The first memory type in `bits` with every property in `wanted`; UINT32_MAX for none.
std::uint32_t memoryType(DeviceData& data, std::uint32_t bits, VkMemoryPropertyFlags wanted);

inline std::uint64_t packed(VkExtent2D extent) {
    return (static_cast<std::uint64_t>(extent.width) << 32) | extent.height;
}

// One more each time an eye's shape for foveation changes (noteEye): rate images and marks are made again.
std::uint32_t eyeShapeGeneration();

// The texels of `eye`'s pattern for a render target of `extent` (`texel` pixels per texel); empty while the
// eye's shape is not known yet. `log`: the regions are logged (once per rate image).
std::vector<std::uint8_t> patternFor(VkExtent2D texel, VkExtent2D extent, int eye, bool log);

// The rate image for a render target of `extent` drawn for `eye`, or VK_NULL_HANDLE (full rate) while it is
// not ready. Made and uploaded in `commandBuffer` the first time.
VkImageView viewFor(VrsDevice& d, VkCommandBuffer commandBuffer, VkExtent2D extent, int eye);

// A render target that is not the eye image or a scaled copy of it (a shadow map, a look-up table) keeps
// full rate; each size is logged once.
void noteOtherTarget(VrsDevice& d, VkExtent2D extent, foveation::TargetSize eye);

// The eye of the backend frame a render pass recorded now into `commandBuffer` belongs to.
int passEye(VrsDevice& d, VkCommandBuffer commandBuffer);

// The command buffer functions the pass frames follow (vrs_command_buffers.cpp): the next layer's, loaded at
// device creation, and their hooks (nullptr for other names).
void loadCommandBufferFunctions(VrsDevice& d);
PFN_vkVoidFunction findCommandBufferHook(const char* name);

// One line for the render pass summary: how the passes' frames were found.
void logPassFrames(VrsDevice& d);

} // namespace evr::vkcore::vrs_nv
