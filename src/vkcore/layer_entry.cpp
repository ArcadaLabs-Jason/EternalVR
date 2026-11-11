// Vulkan loader entry points and dispatch for the EternalVR layer (ARCHITECTURE sections 4 and 6).
//
// The layer gates itself to the game (T-079), adds the interop extensions and the timeline feature to
// the game's device (T-082), tracks the game's swapchain (T-111: the game's device is the one that
// creates it) and hands presents to the XR presenter, which copies the presented image into a ring
// shared with its own D3D12 device and shows it in the headset (head-tracked or on a flat screen).

#include "common/finishes.hpp"
#include "platform/layer_gate/layer_gate.hpp"
#include "vkcore/adapter_steer.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/hook_guard.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/layer_entry_impl.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/profile_hooks.hpp"
#include "vkcore/ssdo_menu_hook.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/stereo_present.hpp"
#include "vkcore/surface_entry.hpp"
#include "vkcore/taa_hooks.hpp"
#include "vkcore/view_slots.hpp"
#include "vkcore/virtual_client.hpp"
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

namespace layer_entry_detail {

// Allocated once and never destroyed. The game usually exits without vkDestroyDevice, and destructors
// run from DLL_PROCESS_DETACH would tear the presenter down under the loader lock after its threads
// are gone (see processTerminating).
std::shared_mutex& g_mapMutex = *new std::shared_mutex;
auto& g_instances = *new std::unordered_map<DispatchKey, std::unique_ptr<InstanceData>>;
auto& g_devices = *new std::unordered_map<DispatchKey, std::unique_ptr<DeviceData>>;

// Set on threads that run our own OpenXR calls, so a VkInstance a runtime creates from inside them
// passes straight through (T-082).
thread_local bool t_passThrough = false;

} // namespace layer_entry_detail

using namespace layer_entry_detail;

namespace {

std::atomic<bool> g_processTerminating{false};

// The loader's entry points catch what the layer's own work throws (hook_guard.hpp).
HookGuard g_createInstanceGuard("vkCreateInstance");
// One per install, so the log names the one that threw.
HookGuard g_mpGuardInstall("the multiplayer guard's install (vkCreateInstance)");
HookGuard g_viewSlotsInstall("Parallel Eye Rendering's early install (vkCreateInstance)");
HookGuard g_taaInstall("per-eye TAA's early install (vkCreateInstance)");
HookGuard g_ssdoMenuInstall("the SSDO menu hook's install (vkCreateInstance)");
HookGuard g_profileInstall("the profile hooks' install (vkCreateInstance)");
HookGuard g_instanceProcAddrGuard("a vkGetInstanceProcAddr lookup");
HookGuard g_deviceProcAddrGuard("a vkGetDeviceProcAddr lookup");
HookGuard g_negotiateGuard("vkNegotiateLoaderLayerInterfaceVersion");

// ---------------------------------------------------------------------------------------------------
// Instance

// How far the game's vkCreateInstance got when the layer's part of it threw (CreateInstance).
struct InstanceProgress {
    VkLayerInstanceCreateInfo* link = nullptr;
    VkLayerInstanceLink* savedLink = nullptr;
    PFN_vkGetInstanceProcAddr nextGipa = nullptr;
    PFN_vkCreateInstance nextCreate = nullptr;
    bool created = false; // the next layer made the instance and it is not recorded yet
};

// The next layer's functions the layer calls, with the KHR aliases for a 1.0 instance.
void loadInstanceFunctions(InstanceData& data) {
    const VkInstance instance = data.instance;
    const PFN_vkGetInstanceProcAddr nextGipa = data.nextGetInstanceProcAddr;
#define EVR_LOAD_INSTANCE_FN(name)                                                                           \
    data.vk.name = reinterpret_cast<PFN_vk##name>(nextGipa(instance, "vk" #name));
    EVR_INSTANCE_FUNCTIONS(EVR_LOAD_INSTANCE_FN)
#undef EVR_LOAD_INSTANCE_FN
    data.nextSurfaceCapabilities2 = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(
        nextGipa(instance, "vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
    if (!data.vk.GetPhysicalDeviceProperties2) {
        data.vk.GetPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
            nextGipa(instance, "vkGetPhysicalDeviceProperties2KHR"));
    }
    if (!data.vk.GetPhysicalDeviceFeatures2) {
        data.vk.GetPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            nextGipa(instance, "vkGetPhysicalDeviceFeatures2KHR"));
    }
    if (!data.vk.GetPhysicalDeviceImageFormatProperties2) {
        data.vk.GetPhysicalDeviceImageFormatProperties2 =
            reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
                nextGipa(instance, "vkGetPhysicalDeviceImageFormatProperties2KHR"));
    }
    if (!data.vk.GetPhysicalDeviceExternalSemaphoreProperties) {
        data.vk.GetPhysicalDeviceExternalSemaphoreProperties =
            reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(
                nextGipa(instance, "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"));
    }
}

VkResult createInstance(const VkInstanceCreateInfo* pCreateInfo,
                        const VkAllocationCallbacks* pAllocator,
                        VkInstance* pInstance,
                        InstanceProgress& progress) {
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
    progress = {link, savedLink, nextGipa, nextCreate, false};

    const VkApplicationInfo* app = pCreateInfo->pApplicationInfo;
    const char* appName = app && app->pApplicationName ? app->pApplicationName : "";
    const char* engineName = app && app->pEngineName ? app->pEngineName : "";
    const std::uint32_t apiVersion = app && app->apiVersion ? app->apiVersion : VK_API_VERSION_1_0;
    const bool isGame =
        !t_passThrough && std::strcmp(appName, "DOOMEternal") == 0 && std::strcmp(engineName, "idTech") == 0;
    // The app's own layers: the loader passes the app's create info on, so implicit layers never show.
    EVR_LOG("vkCreateInstance: app '%s' engine '%s' api %s, %u extension(s), layers asked for: %s%s", appName,
            engineName, versionString(apiVersion).c_str(), pCreateInfo->enabledExtensionCount,
            nameList(pCreateInfo->enabledLayerCount, pCreateInfo->ppEnabledLayerNames, 8).c_str(),
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
        progress.created = result == VK_SUCCESS;
    }
    if (result != VK_SUCCESS && augmented) {
        EVR_LOG("vkCreateInstance with added extensions failed (%d); retrying the game's own create info",
                result);
        link->u.pLayerInfo = savedLink;
        result = nextCreate(pCreateInfo, pAllocator, pInstance);
        progress.created = result == VK_SUCCESS;
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
    if (isGame && !data->isGame) {
        // The game's own instance, but one the mod cannot use: no presenter, so the game runs flat.
        status::flat("the game's Vulkan instance could not be set up for the headset (details in the log)");
    }
    data->surfaceMaintenance1 = data->isGame && surfaceMaintenance && result == VK_SUCCESS;
    data->surfaceMaintenance1Khr = data->surfaceMaintenance1 && surfaceKhr;
    data->surfaceMaintenance1Ext = data->surfaceMaintenance1 && surfaceExt;
    loadInstanceFunctions(*data);

    // The multiplayer guard goes in before the game can reach its menus: every later feature asks it. An
    // install that throws leaves its own feature off and the others still go in; without the guard (unarmed)
    // every feature stays off.
    if (data->isGame) {
        if (!g_mpGuardInstall.run([] { mp_guard::install(); })) {
            finishes(
                [] { status::flat("the multiplayer guard could not be installed (details in the log)"); });
        }
        // Parallel Eye Rendering: before the renderer sizes its views.
        g_viewSlotsInstall.run([] { installViewSlotsEarly(); });
        // Per-eye TAA: eye R's images are built with the device context, after this.
        g_taaInstall.run([] { installTaaEarly(); });
        // Route S's r_SSDO hold follows the game's setting from the profile's load.
        g_ssdoMenuInstall.run([] { installSsdoMenuHook(); });
        // Held cvars stay out of the player's profile.bin, from the profile's load.
        g_profileInstall.run([] { installProfileHooks(); });
    }
    std::unique_lock lock(g_mapMutex);
    g_instances[keyOf(*pInstance)] = std::move(data);
    progress.created = false; // recorded
    return VK_SUCCESS;
}

// After the layer's part of vkCreateInstance threw: the instance the game asked for, made now from its own
// create info if it was not made yet, recorded to pass through (no presenter: the game runs flat). When not
// even that record can be made, the instance is destroyed and the game gets VK_ERROR_OUT_OF_HOST_MEMORY, a
// result vkCreateInstance may return.
VkResult passThroughInstance(const VkInstanceCreateInfo* pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             VkInstance* pInstance,
                             const InstanceProgress& progress) {
    if (!progress.nextCreate) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!progress.created) {
        progress.link->u.pLayerInfo = progress.savedLink;
        const VkResult result = progress.nextCreate(pCreateInfo, pAllocator, pInstance);
        if (result != VK_SUCCESS) {
            EVR_LOG("vkCreateInstance with the game's own create info failed: %d", result);
            return result;
        }
    }
    const VkApplicationInfo* app = pCreateInfo->pApplicationInfo;
    const bool recorded = finishes([&] {
        auto data = std::make_unique<InstanceData>();
        data->instance = *pInstance;
        data->nextGetInstanceProcAddr = progress.nextGipa;
        data->apiVersion = app && app->apiVersion ? app->apiVersion : VK_API_VERSION_1_0;
        loadInstanceFunctions(*data);
        std::unique_lock lock(g_mapMutex);
        g_instances[keyOf(*pInstance)] = std::move(data);
    });
    if (!recorded) {
        if (const auto destroy =
                reinterpret_cast<PFN_vkDestroyInstance>(progress.nextGipa(*pInstance, "vkDestroyInstance"))) {
            destroy(*pInstance, pAllocator);
        }
        EVR_LOG(
            "vkCreateInstance: no memory to record the instance; destroyed (VK_ERROR_OUT_OF_HOST_MEMORY)");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    EVR_LOG("vkCreateInstance: the instance passes through the layer (no VR on it)");
    const bool game = !t_passThrough && app && app->pApplicationName && app->pEngineName &&
                      std::strcmp(app->pApplicationName, "DOOMEternal") == 0 &&
                      std::strcmp(app->pEngineName, "idTech") == 0;
    if (game) {
        finishes([] { status::flat("the game's Vulkan instance could not be set up for the headset"); });
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkInstance* pInstance) {
    InstanceProgress progress;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    if (g_createInstanceGuard.run(
            [&] { result = createInstance(pCreateInfo, pAllocator, pInstance, progress); })) {
        return result;
    }
    return passThroughInstance(pCreateInfo, pAllocator, pInstance, progress);
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    InstanceData* data = findInstance(instance);
    if (!data) {
        return;
    }
    const PFN_vkDestroyInstance destroy = data->vk.DestroyInstance;
    const bool game = data->isGame;
    destroy(instance, pAllocator);
    if (game) { // only the game's instances have a steering decision (never one made inside a runtime call)
        adapter_steer::onInstanceDestroyed(instance);
    }
    std::unique_lock lock(g_mapMutex);
    g_instances.erase(keyOf(instance));
}

// ---------------------------------------------------------------------------------------------------
// Proc address lookups

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* pName);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* pName);

PFN_vkVoidFunction instanceProcAddr(VkInstance instance, const char* pName) {
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
    if (PFN_vkVoidFunction hook = adapter_steer::findHook(instance, pName)) { // two-GPU machines
        return hook;
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

PFN_vkVoidFunction deviceProcAddr(VkDevice device, const char* pName) {
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

// A hook's lookup can throw the first time a feature reads its setting (its environment variable, a thread
// it starts); that feature's function is then not hooked and the next layer's goes to the game. The layer's
// own lifecycle hooks (findLifecycleHook) are still handed out, so a create is never hooked while its destroy
// is not.
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* pName) {
    PFN_vkVoidFunction function = nullptr;
    if (g_instanceProcAddrGuard.run([&] { function = instanceProcAddr(instance, pName); })) {
        return function;
    }
    InstanceData* data = instance ? findInstance(instance) : nullptr;
    if (!data) {
        return nullptr;
    }
    const PFN_vkVoidFunction next = data->nextGetInstanceProcAddr(instance, pName);
    const PFN_vkVoidFunction lifecycle = next ? findLifecycleHook(pName) : nullptr;
    return lifecycle ? lifecycle : next;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* pName) {
    PFN_vkVoidFunction function = nullptr;
    if (g_deviceProcAddrGuard.run([&] { function = deviceProcAddr(device, pName); })) {
        return function;
    }
    DeviceData* data = findDevice(device);
    if (!data) {
        return nullptr;
    }
    const PFN_vkVoidFunction next = data->nextGetDeviceProcAddr(device, pName);
    const PFN_vkVoidFunction lifecycle = next ? findLifecycleHook(pName) : nullptr;
    return lifecycle ? lifecycle : next;
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

VkResult negotiate(VkNegotiateLayerInterface* pVersionStruct, bool& started) {
    const layer_gate::Decision decision = gateDecision();
    if (decision != layer_gate::Decision::Enable) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!pVersionStruct || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT ||
        pVersionStruct->loaderLayerInterfaceVersion < 2) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    logOpen();
    status::starting(); // after logOpen, which holds the log's lock while it runs
    started = true;
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

// The gate, the log and the multiplayer screen allocate: when one throws the layer stays out of the process
// and the game runs without it (the multiplayer screen is never skipped).
extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    using namespace evr::vkcore;
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    bool started = false; // the status file reads "starting"
    if (!g_negotiateGuard.run([&] { result = negotiate(pVersionStruct, started); }) && started) {
        evr::finishes(
            [] { status::flat("the mod could not start (details in the log); the game runs without it"); });
    }
    return result;
}
