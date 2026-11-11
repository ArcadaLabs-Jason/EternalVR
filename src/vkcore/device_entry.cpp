// The layer's device entry points (layer_entry_impl.hpp): vkCreateDevice adds the interop extensions and the
// timeline feature to the game's device (T-082), loads its functions and hands it to the hooks that watch it;
// vkDestroyDevice takes it out of them again; and the lookup of the device hooks, the command buffer check's
// first.

#include "vkcore/layer_entry_impl.hpp"

#include "vkcore/cb_check.hpp"
#include "vkcore/cpu_timing.hpp"
#include "vkcore/device_augment.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/dlss_dll.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/hook_guard.hpp"
#include "vkcore/log.hpp"
#include "vkcore/queue_entry.hpp"
#include "vkcore/shader_dump.hpp"
#include "vkcore/ssr_fade_hooks.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/swapchain_entry.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/view_async.hpp"
#include "vkcore/view_snapshot.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/vrs_nv.hpp"
#include "vkcore/xr_presenter.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace evr::vkcore {

using namespace layer_entry_detail;

namespace {

// ---------------------------------------------------------------------------------------------------
// Device

PFN_vkVoidFunction findDeviceHookAfterCheck(const char* name);

// vkCreateDevice catches what the layer's own work throws (hook_guard.hpp).
HookGuard g_createDeviceGuard("vkCreateDevice");
HookGuard g_planGuard("the interop plan in the game's vkCreateDevice");
HookGuard g_stereoStart("the stereo hooks' early start (vkCreateDevice)");
HookGuard g_dlssStart("the DLSS DLL's install (vkCreateDevice)");
HookGuard g_renderSizeStart("the render size's device note (vkCreateDevice)");

// How far vkCreateDevice got when the layer's part of it threw (CreateDevice).
struct DeviceProgress {
    PFN_vkGetDeviceProcAddr nextGdpa = nullptr;
    bool created = false; // the next layer made the device and it is not recorded yet
};

// The hooks' records of a device, taken out again (vkDestroyDevice, and a create that threw).
void forgetDevice(VkDevice device) {
    shader_dump::onDeviceDestroyed(device);
    ssr_fade::onDeviceDestroyed(device);
    ui_vulkan::onDeviceDestroyed(device);
    gpu_timing::onDeviceDestroyed(device);
    cpu_timing::onDeviceDestroyed(device);
    stall_watch::onDeviceDestroyed(device);
    cb_check::onDeviceDestroyed(device);
}

VkResult createDevice(VkPhysicalDevice physicalDevice,
                      const VkDeviceCreateInfo* pCreateInfo,
                      const VkAllocationCallbacks* pAllocator,
                      VkDevice* pDevice,
                      DeviceProgress& progress) {
    InstanceData* inst = findInstance(physicalDevice);
    auto* link = findLayerCreateInfo<VkLayerDeviceCreateInfo>(
        pCreateInfo->pNext, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, VK_LAYER_LINK_INFO);
    if (!inst || !link || !link->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const PFN_vkGetInstanceProcAddr nextGipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const PFN_vkGetDeviceProcAddr nextGdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    VkLayerDeviceLink* const savedLink = link->u.pLayerInfo;
    auto nextCreate = reinterpret_cast<PFN_vkCreateDevice>(nextGipa(inst->instance, "vkCreateDevice"));
    if (!nextCreate) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto* loaderData = findLayerCreateInfo<VkLayerDeviceCreateInfo>(
        pCreateInfo->pNext, VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO, VK_LOADER_DATA_CALLBACK);

    VkPhysicalDeviceProperties props{};
    inst->vk.GetPhysicalDeviceProperties(physicalDevice, &props);
    EVR_LOG("vkCreateDevice on '%s' (api %s), %u extension(s), %u queue create info(s)", props.deviceName,
            versionString(props.apiVersion).c_str(), pCreateInfo->enabledExtensionCount,
            pCreateInfo->queueCreateInfoCount);
    for (std::uint32_t i = 0; i < pCreateInfo->queueCreateInfoCount; ++i) {
        EVR_LOG("  queue family %u x%u", pCreateInfo->pQueueCreateInfos[i].queueFamilyIndex,
                pCreateInfo->pQueueCreateInfos[i].queueCount);
    }

    DeviceAugment plan;
    // A plan that throws is not used: the game's own create info, no presenter.
    if (inst->isGame && !t_passThrough &&
        !g_planGuard.run([&] { planDeviceAugment(plan, *inst, physicalDevice, *pCreateInfo); })) {
        plan.ok = false;
        plan.releaseImages = false;
    }

    VkDeviceCreateInfo modified = *pCreateInfo;
    if (plan.ok) {
        modified.enabledExtensionCount = static_cast<std::uint32_t>(plan.extensions.size());
        modified.ppEnabledExtensionNames = plan.extensions.data();
        modified.pNext = plan.head();
    }

    progress.nextGdpa = nextGdpa;
    VkResult result = nextCreate(physicalDevice, plan.ok ? &modified : pCreateInfo, pAllocator, pDevice);
    if (result != VK_SUCCESS && plan.releaseImages) {
        EVR_LOG("vkCreateDevice with %s failed (%d); retrying without it", plan.maintenance1, result);
        link->u.pLayerInfo = savedLink;
        plan.dropRelease();
        modified.enabledExtensionCount = static_cast<std::uint32_t>(plan.extensions.size());
        modified.ppEnabledExtensionNames = plan.extensions.data();
        modified.pNext = plan.head();
        result = nextCreate(physicalDevice, &modified, pAllocator, pDevice);
    }
    bool interop = plan.ok;
    if (result != VK_SUCCESS && plan.ok) {
        EVR_LOG("vkCreateDevice with the interop additions failed (%d); retrying the game's own create info, "
                "presenter off",
                result);
        link->u.pLayerInfo = savedLink;
        result = nextCreate(physicalDevice, pCreateInfo, pAllocator, pDevice);
        interop = false;
    }
    if (result != VK_SUCCESS) {
        EVR_LOG("vkCreateDevice failed: %d", result);
        return result;
    }
    progress.created = true;

    auto data = std::make_unique<DeviceData>();
    data->device = *pDevice;
    data->physicalDevice = physicalDevice;
    data->instance = inst;
    data->nextGetDeviceProcAddr = nextGdpa;
    data->setDeviceLoaderData = loaderData ? loaderData->u.pfnSetDeviceLoaderData : nullptr;
    data->interopEnabled = interop && data->setDeviceLoaderData;
    if (inst->isGame && !t_passThrough && !data->interopEnabled) {
        // No presenter on this device: the game runs flat; the launcher must not wait for a first present.
        status::flat("the game's graphics device could not be set up for the headset (details in the log)");
    }
#define EVR_LOAD_DEVICE_FN(name)                                                                             \
    data->vk.name = reinterpret_cast<PFN_vk##name>(nextGdpa(*pDevice, "vk" #name));
    EVR_DEVICE_FUNCTIONS(EVR_LOAD_DEVICE_FN)
#undef EVR_LOAD_DEVICE_FN
    if (interop && plan.releaseImages) {
        data->releaseSwapchainImages = reinterpret_cast<PFN_vkReleaseSwapchainImagesKHR>(nextGdpa(
            *pDevice, std::strcmp(plan.maintenance1, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME) == 0
                          ? "vkReleaseSwapchainImagesEXT"
                          : "vkReleaseSwapchainImagesKHR"));
    }

    std::uint32_t familyCount = 0;
    inst->vk.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    inst->vk.GetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
    for (const VkQueueFamilyProperties& family : families) {
        data->queueFamilyFlags.push_back(family.queueFlags);
    }

    if (inst->vk.GetPhysicalDeviceProperties2) {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
        inst->vk.GetPhysicalDeviceProperties2(physicalDevice, &props2);
        data->luidValid = id.deviceLUIDValid == VK_TRUE;
        std::memcpy(data->luid, id.deviceLUID, VK_LUID_SIZE);
        std::uint32_t low = 0;
        std::int32_t high = 0;
        std::memcpy(&low, id.deviceLUID, 4);
        std::memcpy(&high, id.deviceLUID + 4, 4);
        EVR_LOG("device created: LUID %s %08x:%08x, interop %s", data->luidValid ? "valid" : "invalid",
                static_cast<unsigned>(high), low, data->interopEnabled ? "enabled" : "off");
    }

    if (inst->isGame && !t_passThrough) {
        viewAsyncOnDevice(*pCreateInfo, data->queueFamilyFlags); // Parallel Eye Rendering: async compute off
        // Each on its own: one that throws stays off, the device and the others go on.
        g_stereoStart.run([] { startStereoHooksEarly(); }); // stereo mode only; before the first map loads
        // ETERNALVR_DLSS_DLL; the game initialises NGX right after this returns.
        g_dlssStart.run([] { installDlssDll(); });
        g_renderSizeStart.run(
            [&] { virtual_client::onGameDevice(*inst, physicalDevice, interop && plan.releaseImages); });
    }

    // The shader dump (a rig tool, ETERNALVR_DUMP_SHADERS) only observes Vulkan calls, no game memory, so it
    // is not gated on the multiplayer guard: it keeps recording after a trip.
    shader_dump::onDeviceCreated(*pDevice, nextGdpa, inst->isGame && !t_passThrough);
    ssr_fade::onDeviceCreated(*pDevice, nextGdpa, inst->isGame && !t_passThrough);       // the SSR fade patch
    ui_vulkan::onDeviceCreated(*pDevice, nextGdpa, inst->isGame && !t_passThrough);      // ETERNALVR_UI_LAYER
    gpu_timing::onDeviceCreated(*data, props, families, inst->isGame && !t_passThrough); // _GPU_TIMING
    cpu_timing::onDeviceCreated(*data, inst->isGame && !t_passThrough);  // ETERNALVR_CPU_TIMING, after GPU
    stall_watch::onDeviceCreated(*data, inst->isGame && !t_passThrough); // always on, after the dump
    // ETERNALVR_VRS_TEST, innermost of the chain; vrs_khr's path where planned (VK_KHR_fragment_shading_rate)
    vrs_nv::onDeviceCreated(*data, interop && plan.shadingRate, interop && plan.shadingRateKhr);
    // ETERNALVR_TEST_CB_CHECK, a Parallel Eye Rendering rig tool: last, it chains to every hook above.
    cb_check::onDeviceCreated(*data, inst->isGame && !t_passThrough, &findDeviceHookAfterCheck);

    std::unique_lock lock(g_mapMutex);
    g_devices[keyOf(*pDevice)] = std::move(data);
    progress.created = false; // recorded
    return VK_SUCCESS;
}

} // namespace

namespace layer_entry_detail {

// When the layer's part throws once the device is made (its record, or a hook's record of it: only memory
// fails there), the hooks could not pass the device through, so it is taken out of their records and
// destroyed, and the game gets VK_ERROR_OUT_OF_HOST_MEMORY, a result vkCreateDevice may return. Nothing
// before the create throws but the plan, which is caught on its own.
VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice,
                                            const VkDeviceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDevice* pDevice) {
    DeviceProgress progress;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (g_createDeviceGuard.run(
            [&] { result = createDevice(physicalDevice, pCreateInfo, pAllocator, pDevice, progress); })) {
        return result;
    }
    if (!progress.created) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    forgetDevice(*pDevice);
    if (const auto destroy =
            reinterpret_cast<PFN_vkDestroyDevice>(progress.nextGdpa(*pDevice, "vkDestroyDevice"))) {
        destroy(*pDevice, pAllocator);
    }
    EVR_LOG("vkCreateDevice: the device could not be recorded; destroyed (VK_ERROR_OUT_OF_HOST_MEMORY)");
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

} // namespace layer_entry_detail

namespace {

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    DeviceData* data = findDevice(device);
    if (!data) {
        return;
    }
    EVR_LOG("vkDestroyDevice");
    if (data->presenter) {
        data->presenter->shutdown();
        data->presenter.reset();
    }
    view_snapshot::onDeviceDestroyed(*data);
    forgetDevice(device);
    const PFN_vkDestroyDevice destroy = data->vk.DestroyDevice;
    destroy(device, pAllocator);
    std::unique_lock lock(g_mapMutex);
    g_devices.erase(keyOf(device));
}

PFN_vkVoidFunction findDeviceHookAfterCheck(const char* name) {
    if (const PFN_vkVoidFunction lifecycle = findLifecycleHook(name)) {
        return lifecycle;
    }
    if (const PFN_vkVoidFunction stall = stall_watch::findHook(name)) { // chains to the shader dump's
        return stall;
    }
    const PFN_vkVoidFunction cpu = cpu_timing::findHook(name); // chains to the GPU timing's, UI's or dump's
    const PFN_vkVoidFunction timing = gpu_timing::findHook(name); // chains to the UI layer's where both hook
    const PFN_vkVoidFunction ui = ui_vulkan::findHook(name); // chains to the shader dump's where both hook
    const PFN_vkVoidFunction dump = shader_dump::findHook(name); // chains to the VRS experiment's
    const PFN_vkVoidFunction fade = ssr_fade::findHook(name); // before the dump's: a dump records the patch
    return fade ? fade : cpu ? cpu : timing ? timing : ui ? ui : dump ? dump : vrs_nv::findHook(name);
}

} // namespace

namespace layer_entry_detail {

PFN_vkVoidFunction findLifecycleHook(const char* name) noexcept {
    if (std::strcmp(name, "vkDestroyDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&DestroyDevice);
    }
    if (const PFN_vkVoidFunction queue = findQueueHook(name)) {
        return queue;
    }
    return findSwapchainHook(name);
}

// The command buffer check (a Parallel Eye Rendering rig tool, off by default: then null) goes before every
// other hook of the same function, for the game's device only (`device` null: an instance lookup). It hooks
// none of the lifecycle functions, which are matched first.
PFN_vkVoidFunction findDeviceHook(VkDevice device, const char* name) {
    if (const PFN_vkVoidFunction lifecycle = findLifecycleHook(name)) {
        return lifecycle;
    }
    const PFN_vkVoidFunction check = cb_check::findHook(device, name); // chains to findDeviceHookAfterCheck's
    return check ? check : findDeviceHookAfterCheck(name);
}

} // namespace layer_entry_detail

} // namespace evr::vkcore
