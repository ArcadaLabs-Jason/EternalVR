// Adapter steering's runtime query (adapter_steer_probe.hpp, docs/rig-findings/adapter-steer.md section 2):
// the headset's LUID from a short-lived OpenXR instance, asked once per process.

#include "vkcore/adapter_steer_probe.hpp"

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
#include <optional>
#include <string>
#include <vector>

namespace evr::vkcore {

std::wstring moduleDirectory(); // xr_presenter.cpp

namespace adapter_steer::detail {

namespace {

using adapter_order::Luid;

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

// This thread runs the layer's own OpenXR calls until the scope ends, however it ends (an exception
// included): a VkInstance the runtime makes in them is not the game's.
struct PassThroughScope {
    bool was = passThroughThread();
    PassThroughScope() { setPassThroughThread(true); }
    ~PassThroughScope() { setPassThroughThread(was); }
    PassThroughScope(const PassThroughScope&) = delete;
    PassThroughScope& operator=(const PassThroughScope&) = delete;
};

} // namespace

Probe probeOnce() {
    static std::optional<Probe> cached;
    if (!cached) {
        LARGE_INTEGER start{};
        QueryPerformanceCounter(&start);
        Probe probe;
        {
            const PassThroughScope ownCalls;
            askRuntime(probe);
        }
        probe.ms = millisecondsSince(start);
        cached = probe;
    }
    return *cached;
}

} // namespace adapter_steer::detail

} // namespace evr::vkcore
