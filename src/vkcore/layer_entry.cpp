// Vulkan loader entry points and dispatch for the EternalVR layer (ARCHITECTURE sections 4 and 6).
//
// The layer gates itself to the game (T-079), adds the interop extensions and the timeline feature to
// the game's device (T-082), tracks the game's swapchain (T-111: the game's device is the one that
// creates it) and hands presents to the XR presenter, which copies the presented image into a ring
// shared with its own D3D12 device and shows it in the headset (head-tracked or on a flat screen).

#include "platform/layer_gate/layer_gate.hpp"
#include "vkcore/cb_check.hpp"
#include "vkcore/cpu_timing.hpp"
#include "vkcore/device_augment.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/dlss_dll.hpp"
#include "vkcore/gpu_timing.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/shader_dump.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/stereo_hooks.hpp"
#include "vkcore/stereo_present.hpp"
#include "vkcore/surface_entry.hpp"
#include "vkcore/swapchain_entry.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/ui_vulkan.hpp"
#include "vkcore/view_async.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/vrs_nv.hpp"
#include "vkcore/xr_presenter.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace evr::vkcore {

namespace {

// ---------------------------------------------------------------------------------------------------
// Dispatch maps, keyed by the loader's dispatch key (the first pointer inside a dispatchable handle).

using DispatchKey = void*;

template <typename Handle>
DispatchKey keyOf(Handle handle) {
    return *reinterpret_cast<void**>(handle);
}

// Allocated once and never destroyed. The game usually exits without vkDestroyDevice, and destructors
// run from DLL_PROCESS_DETACH would tear the presenter down under the loader lock after its threads
// are gone (see processTerminating).
std::shared_mutex& g_mapMutex = *new std::shared_mutex;
auto& g_instances = *new std::unordered_map<DispatchKey, std::unique_ptr<InstanceData>>;
auto& g_devices = *new std::unordered_map<DispatchKey, std::unique_ptr<DeviceData>>;

std::atomic<bool> g_processTerminating{false};

// Set on threads that run our own OpenXR calls, so a VkInstance a runtime creates from inside them
// passes straight through (T-082).
thread_local bool t_passThrough = false;

template <typename Handle>
InstanceData* findInstance(Handle handle) {
    std::shared_lock lock(g_mapMutex);
    const auto it = g_instances.find(keyOf(handle));
    return it == g_instances.end() ? nullptr : it->second.get();
}

template <typename Handle>
DeviceData* findDevice(Handle handle) {
    std::shared_lock lock(g_mapMutex);
    const auto it = g_devices.find(keyOf(handle));
    return it == g_devices.end() ? nullptr : it->second.get();
}

// ---------------------------------------------------------------------------------------------------
// Instance

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkInstance* pInstance) {
    auto* link = findLayerCreateInfo<VkLayerInstanceCreateInfo>(
        pCreateInfo->pNext, VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO, VK_LAYER_LINK_INFO);
    if (!link || !link->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const PFN_vkGetInstanceProcAddr nextGipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    VkLayerInstanceLink* const savedLink = link->u.pLayerInfo;
    auto nextCreate = reinterpret_cast<PFN_vkCreateInstance>(nextGipa(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!nextCreate) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const VkApplicationInfo* app = pCreateInfo->pApplicationInfo;
    const char* appName = app && app->pApplicationName ? app->pApplicationName : "";
    const char* engineName = app && app->pEngineName ? app->pEngineName : "";
    const std::uint32_t apiVersion = app && app->apiVersion ? app->apiVersion : VK_API_VERSION_1_0;
    const bool isGame =
        !t_passThrough && std::strcmp(appName, "DOOMEternal") == 0 && std::strcmp(engineName, "idTech") == 0;
    EVR_LOG("vkCreateInstance: app '%s' engine '%s' api %s, %u extension(s)%s", appName, engineName,
            versionString(apiVersion).c_str(), pCreateInfo->enabledExtensionCount,
            isGame ? "" : " (not the game's; pass-through)");

    // Below 1.1 the LUID and external capability queries need these instance extensions (T-082). The
    // game asks for 1.1, where they are core, so normally nothing is added.
    std::vector<const char*> extensions(pCreateInfo->ppEnabledExtensionNames,
                                        pCreateInfo->ppEnabledExtensionNames +
                                            pCreateInfo->enabledExtensionCount);
    VkInstanceCreateInfo modified = *pCreateInfo;
    bool augmented = false;
    if (isGame && apiVersion < VK_API_VERSION_1_1) {
        for (const char* name : {VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
                                 VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
                                 VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME}) {
            if (!hasExtension(extensions, name)) {
                extensions.push_back(name);
                augmented = true;
                EVR_LOG("  adding instance extension %s", name);
            }
        }
        modified.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        modified.ppEnabledExtensionNames = extensions.data();
    }
    // Route S: the game's window takes only the presents it shows (stereo_present.hpp), and the render size
    // scales the game's images into its window (virtual_client.hpp); both need VK_KHR_swapchain_maintenance1
    // (or the older EXT one) on the device and this on the instance. Optional: without it every present
    // reaches the window and the game renders at its window's size. Parallel Eye Rendering is installed (with
    // the multiplayer guard) before this: whether Route S and its present policy run follows its result.
    if (isGame && apiVersion >= VK_API_VERSION_1_1) {
        installViewSlotsEarly();
    }
    // Both surface maintenance names when the instance takes them: the device extension must match the
    // instance's family, and some drivers list only VK_EXT_swapchain_maintenance1 on the device although
    // their instance has the KHR surface one (NVIDIA 581.80). Drivers from before the KHR promotion only
    // have the EXT names; the structures and values are the same. Attempts: KHR and EXT, KHR, EXT, none.
    const std::size_t beforeSurface = extensions.size();
    const int firstAttempt = isGame && (windowPresentsGated() || virtual_client::wanted()) ? 0 : 3;
    const bool gameKhr = hasExtension(extensions, VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    const bool gameExt = hasExtension(extensions, VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    bool surfaceMaintenance = false;
    bool surfaceKhr = false;
    bool surfaceExt = false;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    for (int attempt = firstAttempt; attempt < 4 && result != VK_SUCCESS; ++attempt) {
        if (attempt > firstAttempt) {
            EVR_LOG("vkCreateInstance with surface maintenance%s%s failed (%d); retrying with%s",
                    surfaceKhr ? " KHR" : "", surfaceExt ? " EXT" : "", result,
                    attempt == 1   ? " KHR"
                    : attempt == 2 ? " EXT"
                                   : "out it");
            link->u.pLayerInfo = savedLink;
            extensions.resize(beforeSurface);
        }
        surfaceMaintenance = attempt < 3;
        surfaceKhr = surfaceMaintenance && (attempt < 2 || gameKhr);
        surfaceExt = surfaceMaintenance && (attempt != 1 || gameExt);
        for (const char* name :
             {surfaceMaintenance ? VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME : nullptr,
              surfaceKhr ? VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME : nullptr,
              surfaceExt ? VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME : nullptr}) {
            if (name && !hasExtension(extensions, name)) {
                extensions.push_back(name);
                EVR_LOG("  adding instance extension %s", name);
            }
        }
        modified.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        modified.ppEnabledExtensionNames = extensions.data();
        result = nextCreate(augmented || surfaceMaintenance ? &modified : pCreateInfo, pAllocator, pInstance);
    }
    if (result != VK_SUCCESS && augmented) {
        EVR_LOG("vkCreateInstance with added extensions failed (%d); retrying the game's own create info",
                result);
        link->u.pLayerInfo = savedLink;
        result = nextCreate(pCreateInfo, pAllocator, pInstance);
        augmented = false;
    }
    if (result != VK_SUCCESS) {
        EVR_LOG("vkCreateInstance failed: %d", result);
        return result;
    }

    auto data = std::make_unique<InstanceData>();
    data->instance = *pInstance;
    data->nextGetInstanceProcAddr = nextGipa;
    data->apiVersion = apiVersion;
    data->isGame = isGame && (apiVersion >= VK_API_VERSION_1_1 || augmented);
    data->surfaceMaintenance1 = data->isGame && surfaceMaintenance && result == VK_SUCCESS;
    data->surfaceMaintenance1Khr = data->surfaceMaintenance1 && surfaceKhr;
    data->surfaceMaintenance1Ext = data->surfaceMaintenance1 && surfaceExt;
#define EVR_LOAD_INSTANCE_FN(name)                                                                           \
    data->vk.name = reinterpret_cast<PFN_vk##name>(nextGipa(*pInstance, "vk" #name));
    EVR_INSTANCE_FUNCTIONS(EVR_LOAD_INSTANCE_FN)
#undef EVR_LOAD_INSTANCE_FN
    data->nextSurfaceCapabilities2 = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(
        nextGipa(*pInstance, "vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
    // KHR aliases for a 1.0 instance.
    if (!data->vk.GetPhysicalDeviceProperties2) {
        data->vk.GetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            nextGipa(*pInstance, "vkGetPhysicalDeviceProperties2KHR"));
    }
    if (!data->vk.GetPhysicalDeviceFeatures2) {
        data->vk.GetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            nextGipa(*pInstance, "vkGetPhysicalDeviceFeatures2KHR"));
    }
    if (!data->vk.GetPhysicalDeviceImageFormatProperties2) {
        data->vk.GetPhysicalDeviceImageFormatProperties2 =
            reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
                nextGipa(*pInstance, "vkGetPhysicalDeviceImageFormatProperties2KHR"));
    }
    if (!data->vk.GetPhysicalDeviceExternalSemaphoreProperties) {
        data->vk.GetPhysicalDeviceExternalSemaphoreProperties =
            reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(
                nextGipa(*pInstance, "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"));
    }

    // The multiplayer guard goes in before the game can reach its menus: every later feature asks it.
    if (data->isGame) {
        mp_guard::install();
        installViewSlotsEarly(); // Parallel Eye Rendering: before the renderer sizes its views
        installTaaEarly();       // per-eye TAA: eye R's images are built with the device context, after this
    }
    std::unique_lock lock(g_mapMutex);
    g_instances[keyOf(*pInstance)] = std::move(data);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    InstanceData* data = findInstance(instance);
    if (!data) {
        return;
    }
    const PFN_vkDestroyInstance destroy = data->vk.DestroyInstance;
    destroy(instance, pAllocator);
    std::unique_lock lock(g_mapMutex);
    g_instances.erase(keyOf(instance));
}

// ---------------------------------------------------------------------------------------------------
// Device

PFN_vkVoidFunction findDeviceHookAfterCheck(const char* name);

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physicalDevice,
                                            const VkDeviceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDevice* pDevice) {
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
    if (inst->isGame && !t_passThrough) {
        planDeviceAugment(plan, *inst, physicalDevice, *pCreateInfo);
    }

    VkDeviceCreateInfo modified = *pCreateInfo;
    if (plan.ok) {
        modified.enabledExtensionCount = static_cast<std::uint32_t>(plan.extensions.size());
        modified.ppEnabledExtensionNames = plan.extensions.data();
        modified.pNext = plan.head();
    }

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

    auto data = std::make_unique<DeviceData>();
    data->device = *pDevice;
    data->physicalDevice = physicalDevice;
    data->instance = inst;
    data->nextGetDeviceProcAddr = nextGdpa;
    data->setDeviceLoaderData = loaderData ? loaderData->u.pfnSetDeviceLoaderData : nullptr;
    data->interopEnabled = interop && data->setDeviceLoaderData;
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
        startStereoHooksEarly(); // stereo mode only; before the first map loads
        installDlssDll();        // ETERNALVR_DLSS_DLL; the game initialises NGX right after this returns
        virtual_client::onGameDevice(*inst, physicalDevice, interop && plan.releaseImages);
    }

    // The shader dump (a rig tool, ETERNALVR_DUMP_SHADERS) only observes Vulkan calls, no game memory, so it
    // is not gated on the multiplayer guard: it keeps recording after a trip.
    shader_dump::onDeviceCreated(*pDevice, nextGdpa, inst->isGame && !t_passThrough);
    ui_vulkan::onDeviceCreated(*pDevice, nextGdpa, inst->isGame && !t_passThrough);      // ETERNALVR_UI_LAYER
    gpu_timing::onDeviceCreated(*data, props, families, inst->isGame && !t_passThrough); // _GPU_TIMING
    cpu_timing::onDeviceCreated(*data, inst->isGame && !t_passThrough);  // ETERNALVR_CPU_TIMING, after GPU
    stall_watch::onDeviceCreated(*data, inst->isGame && !t_passThrough); // always on, after the dump
    vrs_nv::onDeviceCreated(*data, interop && plan.shadingRate); // ETERNALVR_VRS_TEST, innermost of the chain
    // ETERNALVR_TEST_CB_CHECK, a Parallel Eye Rendering rig tool: last, it chains to every hook above.
    cb_check::onDeviceCreated(*data, inst->isGame && !t_passThrough, &findDeviceHookAfterCheck);

    std::unique_lock lock(g_mapMutex);
    g_devices[keyOf(*pDevice)] = std::move(data);
    return VK_SUCCESS;
}

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
    shader_dump::onDeviceDestroyed(device);
    ui_vulkan::onDeviceDestroyed(device);
    gpu_timing::onDeviceDestroyed(device);
    cpu_timing::onDeviceDestroyed(device);
    stall_watch::onDeviceDestroyed(device);
    cb_check::onDeviceDestroyed(device);
    const PFN_vkDestroyDevice destroy = data->vk.DestroyDevice;
    destroy(device, pAllocator);
    std::unique_lock lock(g_mapMutex);
    g_devices.erase(keyOf(device));
}

void recordQueue(DeviceData& data, VkQueue queue, std::uint32_t family) {
    std::lock_guard lock(data.queueMutex);
    data.queueFamilies[queue] = family;
}

VKAPI_ATTR void VKAPI_CALL GetDeviceQueue(VkDevice device,
                                          std::uint32_t family,
                                          std::uint32_t index,
                                          VkQueue* pQueue) {
    DeviceData* data = findDevice(device);
    data->vk.GetDeviceQueue(device, family, index, pQueue);
    if (*pQueue) {
        recordQueue(*data, *pQueue, family);
    }
}

VKAPI_ATTR void VKAPI_CALL GetDeviceQueue2(VkDevice device,
                                           const VkDeviceQueueInfo2* pInfo,
                                           VkQueue* pQueue) {
    DeviceData* data = findDevice(device);
    data->vk.GetDeviceQueue2(device, pInfo, pQueue);
    if (*pQueue) {
        recordQueue(*data, *pQueue, pInfo->queueFamilyIndex);
    }
}

// ---------------------------------------------------------------------------------------------------
// Proc address lookups

PFN_vkVoidFunction findDeviceHookAfterCheck(const char* name) {
#define EVR_HOOK(fn)                                                                                         \
    if (std::strcmp(name, "vk" #fn) == 0) {                                                                  \
        return reinterpret_cast<PFN_vkVoidFunction>(&fn);                                                    \
    }
    EVR_HOOK(DestroyDevice)
    EVR_HOOK(GetDeviceQueue)
    EVR_HOOK(GetDeviceQueue2)
#undef EVR_HOOK
    if (const PFN_vkVoidFunction swapchain = findSwapchainHook(name)) {
        return swapchain;
    }
    if (const PFN_vkVoidFunction stall = stall_watch::findHook(name)) { // chains to the shader dump's
        return stall;
    }
    const PFN_vkVoidFunction cpu = cpu_timing::findHook(name); // chains to the GPU timing's, UI's or dump's
    const PFN_vkVoidFunction timing = gpu_timing::findHook(name); // chains to the UI layer's where both hook
    const PFN_vkVoidFunction ui = ui_vulkan::findHook(name); // chains to the shader dump's where both hook
    const PFN_vkVoidFunction dump =
        shader_dump::findHook(name); // chains to the VRS experiment's where both hook
    return cpu ? cpu : timing ? timing : ui ? ui : dump ? dump : vrs_nv::findHook(name);
}

// The command buffer check (a Parallel Eye Rendering rig tool, off by default: then null) goes before every
// other hook of the same function, for the game's device only (`device` null: an instance lookup).
PFN_vkVoidFunction findDeviceHook(VkDevice device, const char* name) {
    const PFN_vkVoidFunction check = cb_check::findHook(device, name); // chains to findDeviceHookAfterCheck's
    return check ? check : findDeviceHookAfterCheck(name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* pName);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (std::strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddr);
    }
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    }
    if (std::strcmp(pName, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateInstance);
    }
    if (std::strcmp(pName, "vkDestroyInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&DestroyInstance);
    }
    if (std::strcmp(pName, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&CreateDevice);
    }
    if (PFN_vkVoidFunction hook = findSurfaceHook(instance, pName)) {
        return hook;
    }
    if (PFN_vkVoidFunction hook = findDeviceHook(VK_NULL_HANDLE, pName)) {
        return hook;
    }
    if (!instance) {
        return nullptr;
    }
    InstanceData* data = findInstance(instance);
    return data ? data->nextGetInstanceProcAddr(instance, pName) : nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* pName) {
    if (std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    }
    DeviceData* data = findDevice(device);
    if (!data) {
        return nullptr;
    }
    if (PFN_vkVoidFunction hook = findDeviceHook(device, pName)) {
        // Only hand out hooks for functions the device actually has (swapchain functions need the
        // extension).
        return data->nextGetDeviceProcAddr(device, pName) ? hook : nullptr;
    }
    return data->nextGetDeviceProcAddr(device, pName);
}

layer_gate::Decision gateDecision() {
    std::vector<wchar_t> path(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) {
            return layer_gate::Decision::WrongProcess;
        }
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    std::wstring enable;
    std::wstring disable;
    const bool hasEnable = readEnv(L"ETERNALVR_ENABLE_LAYER", enable);
    const bool hasDisable = readEnv(L"ETERNALVR_DISABLE_LAYER", disable);
    return layer_gate::decide(std::wstring_view(path.data(), path.size()),
                              hasEnable ? std::optional<std::wstring_view>(enable) : std::nullopt,
                              hasDisable ? std::optional<std::wstring_view>(disable) : std::nullopt);
}

} // namespace

void setPassThroughThread(bool passThrough) {
    t_passThrough = passThrough;
}

InstanceData* findInstanceData(void* dispatchable) {
    return dispatchable ? findInstance(dispatchable) : nullptr;
}

DeviceData* findDeviceData(void* dispatchable) {
    return dispatchable ? findDevice(dispatchable) : nullptr;
}

bool passThroughThread() {
    return t_passThrough;
}

bool processTerminating() {
    return g_processTerminating.load();
}

} // namespace evr::vkcore

// At process exit (lpReserved != nullptr) every other thread is already gone and the loader lock is
// held: nothing may be torn down then. The flag makes every teardown path a no-op.
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr) {
        evr::vkcore::g_processTerminating.store(true);
    }
    return TRUE;
}

// vk_layer.h declares this without dllexport, so it is exported through the linker.
#pragma comment(linker, "/EXPORT:vkNegotiateLoaderLayerInterfaceVersion")

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    using namespace evr::vkcore;
    const evr::layer_gate::Decision decision = gateDecision();
    if (decision != evr::layer_gate::Decision::Enable) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT ||
        pVersionStruct->loaderLayerInterfaceVersion < 2) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    logOpen();
    status::starting(); // after logOpen, which holds the log's lock while it runs
    // Single-player only (T-109): a multiplayer argument on the command line keeps the layer out.
    if (!mp_guard::screenCommandLine()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    EVR_LOG("EternalVR layer loaded (interface %u offered)", pVersionStruct->loaderLayerInterfaceVersion);
    logLoaderVersion();
    pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetInstanceProcAddr = &GetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr = &GetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
