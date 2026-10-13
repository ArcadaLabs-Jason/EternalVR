// Adapter steering on two-GPU machines: the headset's LUID from the runtime, and the game's device list in
// the order adapter_order.hpp gives (adapter_steer.hpp).

#include "vkcore/adapter_steer.hpp"

#include "vkcore/adapter_order.hpp"
#include "vkcore/dispatch.hpp"
#include "vkcore/log.hpp"
#include "vkcore/surface_entry.hpp"
#include "vkcore/xr_presenter.hpp"

#include <windows.h>

#include <d3d12.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace evr::vkcore {

std::wstring moduleDirectory(); // xr_presenter.cpp

namespace adapter_steer {

namespace {

using adapter_order::Action;
using adapter_order::Luid;

// ---------------------------------------------------------------------------------------------------
// The headset's LUID, asked once per process.

struct Probe {
    std::optional<Luid> luid;
    std::string runtime;
    std::string why; // empty when the LUID was read
    double ms = 0.0;
};

double millisecondsSince(const LARGE_INTEGER& start) {
    LARGE_INTEGER now{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(now.QuadPart - start.QuadPart) * 1000.0 /
           static_cast<double>(frequency.QuadPart);
}

template <typename Fn>
Fn xrFunction(PFN_xrGetInstanceProcAddr gipa, XrInstance instance, const char* name) {
    PFN_xrVoidFunction fn = nullptr;
    gipa(instance, name, &fn);
    return reinterpret_cast<Fn>(fn);
}

std::string resultText(const char* call, XrResult r) {
    char text[96];
    std::snprintf(text, sizeof(text), "%s returned %d", call, static_cast<int>(r));
    return text;
}

// The runtime's adapter for a D3D12 app; `out.why` says why there is none.
void askRuntime(Probe& out) {
    const std::wstring path = moduleDirectory() + L"\\openxr_loader.dll";
    const HMODULE loader = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    const auto gipa =
        loader ? reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader, "xrGetInstanceProcAddr"))
               : nullptr;
    if (!gipa) {
        out.why = "the OpenXR loader could not be loaded";
        return;
    }
    const auto enumerate = xrFunction<PFN_xrEnumerateInstanceExtensionProperties>(
        gipa, XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties");
    const auto create = xrFunction<PFN_xrCreateInstance>(gipa, XR_NULL_HANDLE, "xrCreateInstance");
    std::uint32_t count = 0;
    XrResult r = enumerate && create ? enumerate(nullptr, 0, &count, nullptr) : XR_ERROR_FUNCTION_UNSUPPORTED;
    if (XR_FAILED(r)) {
        out.why = "no OpenXR runtime (" + resultText("xrEnumerateInstanceExtensionProperties", r) + ")";
        return;
    }
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    r = enumerate(nullptr, count, &count, extensions.data());
    const bool d3d12 =
        XR_SUCCEEDED(r) && std::any_of(extensions.begin(), extensions.end(), [](const auto& e) {
            return std::strcmp(e.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0;
        });
    if (!d3d12) {
        out.why = "the runtime does not offer " XR_KHR_D3D12_ENABLE_EXTENSION_NAME;
        return;
    }
    const char* const names[] = {XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(info.applicationInfo.applicationName, "EternalVR");
    info.applicationInfo.applicationVersion = 1;
    strcpy_s(info.applicationInfo.engineName, "idTech");
    info.applicationInfo.engineVersion = 7;
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = names;
    XrInstance instance = XR_NULL_HANDLE;
    r = create(&info, &instance);
    if (XR_FAILED(r)) {
        out.why = "the runtime would not start (" + resultText("xrCreateInstance", r) + ")";
        return;
    }
    const auto destroy = xrFunction<PFN_xrDestroyInstance>(gipa, instance, "xrDestroyInstance");
    const auto properties =
        xrFunction<PFN_xrGetInstanceProperties>(gipa, instance, "xrGetInstanceProperties");
    const auto getSystem = xrFunction<PFN_xrGetSystem>(gipa, instance, "xrGetSystem");
    const auto requirements = xrFunction<PFN_xrGetD3D12GraphicsRequirementsKHR>(
        gipa, instance, "xrGetD3D12GraphicsRequirementsKHR");
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    if (properties && XR_SUCCEEDED(properties(instance, &ip))) {
        out.runtime = ip.runtimeName;
    }
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    r = getSystem && requirements ? getSystem(instance, &systemInfo, &system) : XR_ERROR_FUNCTION_UNSUPPORTED;
    if (XR_FAILED(r)) {
        out.why = r == XR_ERROR_FORM_FACTOR_UNAVAILABLE ? "no headset yet (XR_ERROR_FORM_FACTOR_UNAVAILABLE)"
                                                        : resultText("xrGetSystem", r);
    } else {
        XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
        r = requirements(instance, system, &req);
        if (XR_FAILED(r)) {
            out.why = resultText("xrGetD3D12GraphicsRequirementsKHR", r);
        } else {
            Luid luid{};
            static_assert(sizeof(LUID) == sizeof(Luid));
            std::memcpy(luid.data(), &req.adapterLuid, sizeof(LUID));
            out.luid = luid;
        }
    }
    if (destroy) {
        destroy(instance);
    }
}

Probe probeOnce() {
    static std::optional<Probe> cached;
    if (!cached) {
        LARGE_INTEGER start{};
        QueryPerformanceCounter(&start);
        Probe probe;
        const bool wasPassThrough = passThroughThread();
        setPassThroughThread(true); // a VkInstance the runtime makes in here is not the game's
        askRuntime(probe);
        setPassThroughThread(wasPassThrough);
        probe.ms = millisecondsSince(start);
        cached = probe;
    }
    return *cached;
}

// ---------------------------------------------------------------------------------------------------
// The game's devices

enum class Mode { Off, On, Mismatch };

// ETERNALVR_ADAPTER_STEER (1/on, 0/off; off by default) and the rig's ETERNALVR_TEST_STEER (mismatch, or off
// as an alias of the switch at 0). `why` names what turned steering off.
Mode readMode(std::string& why) {
    std::wstring test;
    if (readEnv(L"ETERNALVR_TEST_STEER", test)) {
        if (_wcsicmp(test.c_str(), L"mismatch") == 0) {
            return Mode::Mismatch;
        }
        if (_wcsicmp(test.c_str(), L"off") == 0) {
            why = "ETERNALVR_TEST_STEER=off";
            return Mode::Off;
        }
        EVR_LOG("adapter steer: ETERNALVR_TEST_STEER='%ls' is not mismatch or off; ignored", test.c_str());
    }
    std::wstring value;
    if (readEnv(L"ETERNALVR_ADAPTER_STEER", value) &&
        (value == L"1" || _wcsicmp(value.c_str(), L"on") == 0)) {
        return Mode::On;
    }
    if (!value.empty() && value != L"0" && _wcsicmp(value.c_str(), L"off") != 0) {
        EVR_LOG("adapter steer: ETERNALVR_ADAPTER_STEER='%ls' is not 1, on, 0 or off; taken as off",
                value.c_str());
    }
    why = "the default; ETERNALVR_ADAPTER_STEER=1 turns it on";
    return Mode::Off;
}

// The player chose the device on the command line (Steam launch options, the launcher's extra arguments).
bool commandLineSetsIndex() {
    const wchar_t* line = GetCommandLineW();
    if (!line) {
        return false;
    }
    std::wstring lower(line);
    for (wchar_t& c : lower) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return lower.find(L"r_physicaldeviceindex") != std::wstring::npos;
}

struct Listed {
    std::vector<VkPhysicalDevice> handles;
    std::vector<adapter_order::Device> devices;
    std::vector<std::string> names;
};

adapter_order::Device describe(const InstanceData& inst, VkPhysicalDevice device, std::string& name) {
    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
    adapter_order::Device out;
    if (inst.vk.GetPhysicalDeviceProperties2) {
        inst.vk.GetPhysicalDeviceProperties2(device, &props);
        if (id.deviceLUIDValid == VK_TRUE) {
            Luid luid{};
            std::memcpy(luid.data(), id.deviceLUID, VK_LUID_SIZE);
            out.luid = luid;
        }
    } else {
        inst.vk.GetPhysicalDeviceProperties(device, &props.properties);
    }
    out.type = props.properties.deviceType;
    out.apiVersion = props.properties.apiVersion;
    name = props.properties.deviceName;
    return out;
}

VkResult list(const InstanceData& inst, Listed& out) {
    std::uint32_t count = 0;
    VkResult r = inst.vk.EnumeratePhysicalDevices(inst.instance, &count, nullptr);
    if (r != VK_SUCCESS) {
        return r;
    }
    out.handles.resize(count);
    r = inst.vk.EnumeratePhysicalDevices(inst.instance, &count, out.handles.data());
    if (r != VK_SUCCESS) {
        return r;
    }
    out.handles.resize(count);
    out.names.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        out.devices.push_back(describe(inst, out.handles[i], out.names[i]));
    }
    return VK_SUCCESS;
}

const char* typeName(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return "integrated";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return "discrete";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return "virtual";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return "cpu";
    default:
        return "other";
    }
}

std::string deviceText(const Listed& listed) {
    std::string text;
    for (std::size_t i = 0; i < listed.devices.size(); ++i) {
        const adapter_order::Device& d = listed.devices[i];
        text += (i ? "; #" : "#") + std::to_string(i) + " '" + listed.names[i] + "' " + typeName(d.type) +
                " api " + versionString(d.apiVersion) + " LUID " + adapter_order::luidText(d.luid);
    }
    return text;
}

std::string orderText(const std::vector<std::size_t>& order) {
    std::string text;
    for (const std::size_t i : order) {
        text += (text.empty() ? "#" : ", #") + std::to_string(i);
    }
    return text;
}

// What the layer does for one game instance.
struct Steer {
    bool decided = false;
    bool active = false; // Reordered or Filtered: the hooks hand out their own list
    Action action = Action::None;
    VkPhysicalDevice target = VK_NULL_HANDLE;
    std::optional<Luid> targetLuid;
    bool allowCut = false; // the rig's forced mismatch only
    std::string hmd;       // the headset's LUID and how it was had, for the steering line
    std::string logged;    // the last steering line, so a repeated enumeration logs nothing new
    bool groupsLogged = false;
};

std::mutex g_mutex;
auto& g_steers = *new std::unordered_map<VkInstance, Steer>; // never destroyed (see layer_entry.cpp)

void logOnce(Steer& steer, const std::string& line) {
    if (line != steer.logged) {
        steer.logged = line;
        EVR_LOG("%s", line.c_str());
    }
}

// The target's index in `listed` (by handle, else by LUID).
std::optional<std::size_t> targetIndex(const Steer& steer, const Listed& listed) {
    const auto it = std::find(listed.handles.begin(), listed.handles.end(), steer.target);
    if (it != listed.handles.end()) {
        return static_cast<std::size_t>(it - listed.handles.begin());
    }
    return steer.targetLuid ? adapter_order::findLuid(listed.devices, *steer.targetLuid) : std::nullopt;
}

adapter_order::Plan replan(Steer& steer, const Listed& listed) {
    const std::optional<std::size_t> target = targetIndex(steer, listed);
    const adapter_order::Plan plan = adapter_order::plan(listed.devices, target, steer.allowCut);
    std::string line = "adapter steer: " + std::to_string(listed.devices.size()) +
                       " Vulkan devices: " + deviceText(listed) + "; " + steer.hmd;
    if (!target) {
        line += "; the target device is gone: the list is left as it is";
    } else {
        line +=
            "; the game alone would pick #" + std::to_string(adapter_order::gamePick(listed.devices)) +
            "; target #" + std::to_string(*target) + ": " + adapter_order::actionName(plan.action) +
            (plan.action == Action::Reordered || plan.action == Action::Filtered
                 ? ", the game is given " + orderText(plan.order)
                 : std::string()) +
            (plan.action == Action::NotPickable
                 ? ": the runtime names a device the game's rule would not take even first (it takes the "
                   "first discrete GPU of api 1.1 or later); the list is left as it is and the LUID check "
                   "refuses VR when the session starts"
                 : std::string());
    }
    logOnce(steer, line);
    steer.action = plan.action;
    return plan;
}

std::string hmdText(const Probe& probe) {
    char ms[32];
    std::snprintf(ms, sizeof(ms), "%.0f ms", probe.ms);
    return "headset LUID " + adapter_order::luidText(probe.luid) +
           (probe.runtime.empty() ? std::string() : " (runtime '" + probe.runtime + "'") +
           (probe.runtime.empty() ? " (" : ", ") + "asked in " + ms + ")";
}

// The first enumeration of a game instance: whether and where to steer. Logs one line.
void decide(Steer& steer, const Listed& listed) {
    steer.decided = true;
    if (listed.devices.size() < 2) {
        EVR_LOG("adapter steer: %zu Vulkan device(s)%s%s: nothing to steer", listed.devices.size(),
                listed.devices.empty() ? "" : ": ", deviceText(listed).c_str());
        return;
    }
    const std::string prefix = "adapter steer: " + std::to_string(listed.devices.size()) +
                               " Vulkan devices: " + deviceText(listed) + "; ";
    std::string why;
    const Mode mode = readMode(why);
    if (mode == Mode::Off) {
        logOnce(steer,
                prefix + "steering off (" + why + "): the list is left as it is, the runtime is not asked");
        return;
    }
    if (commandLineSetsIndex()) {
        logOnce(steer,
                prefix + "the game's command line sets r_physicalDeviceIndex: the list is left as it is");
        return;
    }
    if (mode == Mode::On && !adapter_order::reorderCanMatter(listed.devices)) {
        logOnce(steer,
                prefix + "the game takes #" + std::to_string(adapter_order::gamePick(listed.devices)) +
                    " (its one discrete GPU of api 1.1 or later) in any order: the list is left as it is, "
                    "the runtime is not asked");
        return;
    }
    const Probe probe = probeOnce();
    if (!probe.luid) {
        logOnce(steer, prefix + hmdText(probe) + ": " + probe.why +
                           "; the list is left as it is (the LUID is checked when the session starts)");
        return;
    }
    const std::optional<std::size_t> target = mode == Mode::Mismatch
                                                  ? adapter_order::firstOther(listed.devices, *probe.luid)
                                                  : adapter_order::findLuid(listed.devices, *probe.luid);
    if (!target) {
        logOnce(steer, prefix + hmdText(probe) +
                           (mode == Mode::Mismatch ? ": no device without the headset's LUID"
                                                   : ": no device has the headset's LUID") +
                           "; the list is left as it is");
        return;
    }
    steer.target = listed.handles[*target];
    steer.targetLuid = listed.devices[*target].luid;
    steer.allowCut = mode == Mode::Mismatch;
    steer.hmd = hmdText(probe) + (mode == Mode::Mismatch ? "; forced mismatch (ETERNALVR_TEST_STEER=mismatch)"
                                                         : "; steering on (ETERNALVR_ADAPTER_STEER)");
    const adapter_order::Plan plan = replan(steer, listed);
    steer.active = plan.action == Action::Reordered || plan.action == Action::Filtered;
}

// Hands out `items` in `order` with the usual count and VK_INCOMPLETE rules.
template <typename T, typename Copy>
VkResult handOut(const std::vector<T>& items,
                 const std::vector<std::size_t>& order,
                 std::uint32_t* pCount,
                 T* pOut,
                 Copy copy) {
    if (!pOut) {
        *pCount = static_cast<std::uint32_t>(order.size());
        return VK_SUCCESS;
    }
    const std::uint32_t n = std::min(*pCount, static_cast<std::uint32_t>(order.size()));
    for (std::uint32_t i = 0; i < n; ++i) {
        copy(pOut[i], items[order[i]]);
    }
    *pCount = n;
    return n < order.size() ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL EnumeratePhysicalDevices(VkInstance instance,
                                                        std::uint32_t* pPhysicalDeviceCount,
                                                        VkPhysicalDevice* pPhysicalDevices) {
    InstanceData* inst = findInstanceData(instance);
    if (!inst) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!inst->isGame || passThroughThread()) {
        return inst->vk.EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    }
    std::lock_guard lock(g_mutex);
    Steer& steer = g_steers[instance];
    Listed listed;
    if (!steer.decided || steer.active) {
        if (list(*inst, listed) != VK_SUCCESS) {
            return inst->vk.EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
        }
        if (!steer.decided) {
            decide(steer, listed);
        }
    }
    if (!steer.active) {
        return inst->vk.EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    }
    const adapter_order::Plan plan = replan(steer, listed);
    return handOut(listed.handles, plan.order, pPhysicalDeviceCount, pPhysicalDevices,
                   [](VkPhysicalDevice& to, VkPhysicalDevice from) { to = from; });
}

PFN_vkEnumeratePhysicalDeviceGroups nextGroups(const InstanceData& inst) {
    return inst.vk.EnumeratePhysicalDeviceGroups
               ? inst.vk.EnumeratePhysicalDeviceGroups
               : reinterpret_cast<PFN_vkEnumeratePhysicalDeviceGroups>(
                     inst.nextGetInstanceProcAddr(inst.instance, "vkEnumeratePhysicalDeviceGroupsKHR"));
}

VKAPI_ATTR VkResult VKAPI_CALL EnumeratePhysicalDeviceGroups(VkInstance instance,
                                                             std::uint32_t* pGroupCount,
                                                             VkPhysicalDeviceGroupProperties* pGroups) {
    InstanceData* inst = findInstanceData(instance);
    const PFN_vkEnumeratePhysicalDeviceGroups next = inst ? nextGroups(*inst) : nullptr;
    if (!next) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!inst->isGame || passThroughThread()) {
        return next(instance, pGroupCount, pGroups);
    }
    std::lock_guard lock(g_mutex);
    Steer& steer = g_steers[instance];
    if (!steer.decided) {
        Listed listed;
        if (list(*inst, listed) != VK_SUCCESS) {
            return next(instance, pGroupCount, pGroups);
        }
        decide(steer, listed);
    }
    std::uint32_t count = 0;
    if (!steer.active || next(instance, &count, nullptr) != VK_SUCCESS) {
        return next(instance, pGroupCount, pGroups);
    }
    std::vector<VkPhysicalDeviceGroupProperties> groups(count,
                                                        {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES});
    if (next(instance, &count, groups.data()) != VK_SUCCESS) {
        return next(instance, pGroupCount, pGroups);
    }
    groups.resize(count);
    std::vector<bool> holds;
    for (const VkPhysicalDeviceGroupProperties& group : groups) {
        bool held = false;
        for (std::uint32_t i = 0; i < group.physicalDeviceCount; ++i) {
            std::string name;
            const adapter_order::Device d = describe(*inst, group.physicalDevices[i], name);
            held = held || group.physicalDevices[i] == steer.target || (d.luid && d.luid == steer.targetLuid);
        }
        holds.push_back(held);
    }
    const std::vector<std::size_t> order = adapter_order::groupOrder(holds, steer.action);
    if (!steer.groupsLogged) {
        steer.groupsLogged = true;
        EVR_LOG("adapter steer: %u device group(s); the game is given group(s) %s", count,
                orderText(order).c_str());
    }
    // The caller's sType and pNext stay; only the group's contents are written.
    return handOut(groups, order, pGroupCount, pGroups,
                   [](VkPhysicalDeviceGroupProperties& to, const VkPhysicalDeviceGroupProperties& from) {
                       to.physicalDeviceCount = from.physicalDeviceCount;
                       std::copy(std::begin(from.physicalDevices), std::end(from.physicalDevices),
                                 std::begin(to.physicalDevices));
                       to.subsetAllocation = from.subsetAllocation;
                   });
}

} // namespace

void onInstanceDestroyed(VkInstance instance) {
    std::lock_guard lock(g_mutex);
    g_steers.erase(instance);
}

PFN_vkVoidFunction findHook(VkInstance instance, const char* name) {
    PFN_vkVoidFunction hook = nullptr;
    if (std::strcmp(name, "vkEnumeratePhysicalDevices") == 0) {
        hook = reinterpret_cast<PFN_vkVoidFunction>(&EnumeratePhysicalDevices);
    } else if (std::strcmp(name, "vkEnumeratePhysicalDeviceGroups") == 0 ||
               std::strcmp(name, "vkEnumeratePhysicalDeviceGroupsKHR") == 0) {
        hook = reinterpret_cast<PFN_vkVoidFunction>(&EnumeratePhysicalDeviceGroups);
    } else {
        return nullptr;
    }
    InstanceData* data = instance ? findInstanceData(instance) : nullptr;
    if (!data || !data->nextGetInstanceProcAddr(instance, name)) {
        return nullptr;
    }
    return hook;
}

} // namespace adapter_steer

} // namespace evr::vkcore
