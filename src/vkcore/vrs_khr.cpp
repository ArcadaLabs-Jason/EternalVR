// Foveated rendering through VK_KHR_fragment_shading_rate (vrs_khr.hpp): the device's plan, its set-up and
// the hooks.

#include "vkcore/vrs_khr.hpp"

#include "features/foveation/rate_pattern.hpp"
#include "vkcore/dump_format.hpp"
#include "vkcore/log.hpp"
#include "vkcore/vrs_gui.hpp"
#include "vkcore/vrs_khr_impl.hpp"
#include "vkcore/vrs_nv.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace evr::vkcore::vrs_khr {

namespace {

using vrs_nv::Mode;
using vrs_nv::Settings;
using vrs_nv::settings;

VkPhysicalDeviceFragmentShadingRateFeaturesKHR g_feature{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};

// Some device has the path on: from then on the hooks are handed out (each checks its own device).
std::atomic<bool> g_live{false};

// The device's Vulkan version as the game's device sees it: the lower of the instance's and the GPU's.
std::uint32_t deviceVersion(InstanceData& inst, VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceProperties props{};
    inst.vk.GetPhysicalDeviceProperties(physicalDevice, &props);
    return std::min(inst.apiVersion, props.apiVersion);
}

// Why the game's own create info rules this path out, or nullptr.
const char* gameConflict(const VkDeviceCreateInfo& info) {
    const std::vector<const char*> game(info.ppEnabledExtensionNames,
                                        info.ppEnabledExtensionNames + info.enabledExtensionCount);
    if (hasExtension(game, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) ||
        dump::findInChain(info.pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR)) {
        return "the game enables VK_KHR_fragment_shading_rate itself (its own rates would be overridden)";
    }
    const auto* imageless = dump::findInChain<VkPhysicalDeviceImagelessFramebufferFeatures>(
        info.pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES);
    const auto* v12 = dump::findInChain<VkPhysicalDeviceVulkan12Features>(
        info.pNext, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    if ((imageless && imageless->imagelessFramebuffer) || (v12 && v12->imagelessFramebuffer)) {
        return "the game enables imageless framebuffers, whose attachments this path does not extend";
    }
    return nullptr;
}

const char* modeText(const Settings& s) {
    return s.mode == Mode::Uniform
               ? (s.rate == foveation::kRateHalf ? "every pass at 2x2" : "every pass at 4x4")
           : s.mode == Mode::EyeTest ? "eye test: eye L's left half and eye R's right half at 4x4"
                                     : "fixed foveation around head-forward";
}

// Loads the path's own functions and properties into `d`: on when every one was found.
void setUp(VrsDevice& d) {
    DeviceData& data = *d.data;
    const auto next = [&](const char* name) {
        return data.nextGetDeviceProcAddr(data.device, name);
    };
    const bool core12 = deviceVersion(*data.instance, data.physicalDevice) >= VK_API_VERSION_1_2;
    d.createRenderPass2 = reinterpret_cast<PFN_vkCreateRenderPass2>(
        next(core12 ? "vkCreateRenderPass2" : "vkCreateRenderPass2KHR"));
    d.createImageView = reinterpret_cast<PFN_vkCreateImageView>(next("vkCreateImageView"));
    d.destroyImageView = reinterpret_cast<PFN_vkDestroyImageView>(next("vkDestroyImageView"));
    d.cmdCopyBufferToImage = reinterpret_cast<PFN_vkCmdCopyBufferToImage>(next("vkCmdCopyBufferToImage"));

    VkPhysicalDeviceFragmentShadingRatePropertiesKHR props{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &props};
    data.instance->vk.GetPhysicalDeviceProperties2(data.physicalDevice, &props2);
    // The largest texel: the fewest texels to write. Its sides are powers of two within the device's limits.
    const VkExtent2D texel = props.maxFragmentShadingRateAttachmentTexelSize;
    const std::uint32_t aspect = texel.width > 0 && texel.height > 0 ? std::max(texel.width, texel.height) /
                                                                           std::min(texel.width, texel.height)
                                                                     : 0;
    const bool texelOk = aspect > 0 && aspect <= props.maxFragmentShadingRateAttachmentTexelSizeAspectRatio;
    d.texel = texelOk ? texel : VkExtent2D{16, 16};
    d.khr4x4 = props.maxFragmentSize.width >= 4 && props.maxFragmentSize.height >= 4;
    d.on = texelOk && d.createRenderPass2 && d.createImageView && d.destroyImageView &&
           d.cmdCopyBufferToImage && d.createRenderPass && d.destroyRenderPass && d.createFramebuffer &&
           d.destroyFramebuffer && data.vk.CmdClearColorImage && data.vk.CmdPipelineBarrier;
    d.khr = d.on;
    const Settings& s = settings();
    EVR_LOG(
        "vrs: %s through %s: rate texel %ux%u (device %ux%u to %ux%u), largest fragment %ux%u (quarter rate "
        "%s), render passes made with %s; %s",
        d.on ? "on" : "off (a function or the texel size is missing)",
        VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME, d.texel.width, d.texel.height,
        props.minFragmentShadingRateAttachmentTexelSize.width,
        props.minFragmentShadingRateAttachmentTexelSize.height, texel.width, texel.height,
        props.maxFragmentSize.width, props.maxFragmentSize.height, d.khr4x4 ? "4x4" : "clamped to 2x2",
        core12 ? "vkCreateRenderPass2" : "vkCreateRenderPass2KHR", modeText(s));
    if (!d.on) {
        return;
    }
    auto* k = new KhrDevice; // kept for the process's life, like the other device tables
    k->texel = d.texel;
    k->cmdClearColorImage = data.vk.CmdClearColorImage;
    d.khrState = k;
    d.followsFrames = s.mode != Mode::Uniform;
    d.usesGuesses = d.followsFrames && s.guesses;
    if (d.followsFrames) {
        vrs_nv::noteFollowsFrames();
    }
    if (s.mode == Mode::Fovea) {
        EVR_LOG("vrs: foveation: full rate within %.0f deg of head-forward, half rate within %.0f deg, "
                "quarter outside (the same directions in both eyes)",
                s.fullDegrees, s.halfDegrees);
    }
    if (d.usesGuesses) {
        EVR_LOG("vrs: ETERNALVR_TEST_VRS_PARITY=1: render passes whose counters do not agree take the frame "
                "their command buffer's recording or parity guesses (can be the other eye's)");
    }
    if (s.marks) {
        EVR_LOG(
            "vrs: ETERNALVR_VRS_TINT=1: the headset's eye images get a dot every 32 pixels where the eye's "
            "rate image is at half rate (yellow) or quarter rate (red), while that eye's render passes get "
            "it");
    }
}

} // namespace

void loadKhrPassThrough(VrsDevice& d) {
    DeviceData& data = *d.data;
    const auto next = [&](const char* name) {
        return data.nextGetDeviceProcAddr(data.device, name);
    };
    d.createRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(next("vkCreateRenderPass"));
    d.destroyRenderPass = reinterpret_cast<PFN_vkDestroyRenderPass>(next("vkDestroyRenderPass"));
    // vrs_gui's hooks, which follow the framebuffers for the GUI target check, then call the next layer's.
    d.createFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(vrs_gui::findHook("vkCreateFramebuffer"));
    d.destroyFramebuffer =
        reinterpret_cast<PFN_vkDestroyFramebuffer>(vrs_gui::findHook("vkDestroyFramebuffer"));
    // Either name where the device has it (the extension's below 1.2); the game can call it only then.
    PFN_vkVoidFunction begin2 = next("vkCmdBeginRenderPass2KHR");
    d.cmdBeginRenderPass2 =
        reinterpret_cast<PFN_vkCmdBeginRenderPass2>(begin2 ? begin2 : next("vkCmdBeginRenderPass2"));
}

VkPhysicalDeviceFragmentShadingRateFeaturesKHR* planDevice(InstanceData& inst,
                                                           VkPhysicalDevice physicalDevice,
                                                           const VkDeviceCreateInfo& info,
                                                           std::vector<const char*>& extensions) {
    if (!vrs_nv::wanted()) {
        return nullptr;
    }
    std::uint32_t count = 0;
    inst.vk.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    inst.vk.EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, available.data());
    const auto supported = [&](const char* name) {
        return std::any_of(available.begin(), available.end(), [name](const VkExtensionProperties& p) {
            return std::strcmp(p.extensionName, name) == 0;
        });
    };
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR query{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &query};
    const bool listed = supported(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
    if (listed && inst.vk.GetPhysicalDeviceFeatures2) {
        inst.vk.GetPhysicalDeviceFeatures2(physicalDevice, &features);
    }
    if (!listed || !query.attachmentFragmentShadingRate) {
        EVR_LOG("vrs: %s with attachmentFragmentShadingRate is not supported on this device either; foveated "
                "rendering stays off",
                VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
        return nullptr;
    }
    EVR_LOG("vrs: the game's own device extensions: %s",
            nameList(info.enabledExtensionCount, info.ppEnabledExtensionNames, 64).c_str());
    if (const char* why = gameConflict(info)) {
        EVR_LOG("vrs: foveated rendering stays off: %s", why);
        return nullptr;
    }
    // VK_KHR_fragment_shading_rate needs VK_KHR_create_renderpass2 below 1.2, and that needs VK_KHR_multiview
    // and VK_KHR_maintenance2 below 1.1 (T-091); the path takes 1.1 (the game asks for 1.1).
    const std::uint32_t version = deviceVersion(inst, physicalDevice);
    if (version < VK_API_VERSION_1_1) {
        EVR_LOG("vrs: foveated rendering stays off: the device is Vulkan %s; %s needs 1.1 here",
                versionString(version).c_str(), VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
        return nullptr;
    }
    const bool needsRenderPass2 = version < VK_API_VERSION_1_2;
    if (needsRenderPass2 && !supported(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME)) {
        EVR_LOG("vrs: foveated rendering stays off: the device is Vulkan %s without %s",
                versionString(version).c_str(), VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
        return nullptr;
    }
    for (const char* name : {needsRenderPass2 ? VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME : nullptr,
                             VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME}) {
        if (name && !hasExtension(extensions, name)) {
            extensions.push_back(name);
            EVR_LOG("  adding device extension %s (foveated rendering, Vulkan %s device)", name,
                    versionString(version).c_str());
        }
    }
    g_feature = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    g_feature.attachmentFragmentShadingRate = VK_TRUE;
    return &g_feature;
}

void onDeviceCreated(DeviceData& data) {
    if (!vrs_nv::wanted()) {
        return;
    }
    auto* d = new VrsDevice; // kept for the process's life, like the other device tables
    d->data = &data;
    const auto next = [&](const char* name) {
        return data.nextGetDeviceProcAddr(data.device, name);
    };
    d->createGraphicsPipelines =
        reinterpret_cast<PFN_vkCreateGraphicsPipelines>(next("vkCreateGraphicsPipelines"));
    d->cmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(next("vkCmdBeginRenderPass"));
    vrs_nv::loadCommandBufferFunctions(*d);
    loadKhrPassThrough(*d);
    setUp(*d);
    vrs_gui::onDeviceCreated(data, d->on);
    vrs_nv::registerDevice(vrs_nv::keyOf(data.device), d);
    if (d->khr) {
        g_live.store(true, std::memory_order_release);
    }
}

PFN_vkVoidFunction findHook(const char* name) {
    if (!g_live.load(std::memory_order_acquire)) {
        return nullptr;
    }
#define EVR_VRS_KHR_HOOK(name_, fn)                                                                          \
    if (std::strcmp(name, name_) == 0) {                                                                     \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_VRS_KHR_HOOK("vkCreateRenderPass", CreateRenderPass)
    EVR_VRS_KHR_HOOK("vkDestroyRenderPass", DestroyRenderPass)
    EVR_VRS_KHR_HOOK("vkCreateFramebuffer", CreateFramebuffer)
    EVR_VRS_KHR_HOOK("vkDestroyFramebuffer", DestroyFramebuffer)
    EVR_VRS_KHR_HOOK("vkCmdBeginRenderPass2", CmdBeginRenderPass2)
    EVR_VRS_KHR_HOOK("vkCmdBeginRenderPass2KHR", CmdBeginRenderPass2)
#undef EVR_VRS_KHR_HOOK
    return nullptr;
}

} // namespace evr::vkcore::vrs_khr
