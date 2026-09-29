// OpenXR start-up on the XR worker: loader, instance, system, the D3D12 device and session, and the XR
// swapchain. Session events: presenter_xr_events.cpp.

#include "vkcore/presenter_impl.hpp"

#include "features/input/dashboard_pause.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/status_file.hpp"
#include "vkcore/virtual_client.hpp"
#include "vkcore/vram_watch.hpp"
#include "vkcore/xr_runtime.hpp"
#include "xr_math/cinema_quad.hpp"
#include "xr_math/upright_space.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace {

VkFormat toVkFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return VK_FORMAT_R8G8B8A8_UNORM;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// Worker: OpenXR instance, system, D3D12 device, session and frame loop

const char* XrPresenter::Impl::xrText(XrResult result, char (&buffer)[XR_MAX_RESULT_STRING_SIZE]) const {
    if (xr.xrResultToString && instance && XR_SUCCEEDED(xr.xrResultToString(instance, result, buffer))) {
        return buffer;
    }
    std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XrResult %d", static_cast<int>(result));
    return buffer;
}

bool XrPresenter::Impl::loadOpenXr() {
    const std::wstring path = moduleDirectory() + L"\\openxr_loader.dll";
    xrLoader = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!xrLoader) {
        status::flat("the OpenXR loader bundled with the mod could not be loaded (reinstall EternalVR)");
        EVR_LOG("xr: cannot load %ls (error %lu)", path.c_str(), GetLastError());
        return false;
    }
    xr.xrGetInstanceProcAddr =
        reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(xrLoader, "xrGetInstanceProcAddr"));
    if (!xr.xrGetInstanceProcAddr) {
        EVR_LOG("xr: openxr_loader.dll has no xrGetInstanceProcAddr");
        return false;
    }
    xr.xrGetInstanceProcAddr(
        XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
        reinterpret_cast<PFN_xrVoidFunction*>(&xr.xrEnumerateInstanceExtensionProperties));
    xr.xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance",
                             reinterpret_cast<PFN_xrVoidFunction*>(&xr.xrCreateInstance));
    EVR_LOG("xr: loaded %ls", path.c_str());
    return xr.xrEnumerateInstanceExtensionProperties && xr.xrCreateInstance;
}

void XrPresenter::Impl::makeLocalUpright() {
    std::wstring value;
    if (readEnv(L"ETERNALVR_XR_UPRIGHT", value) && (value == L"0" || _wcsicmp(value.c_str(), L"off") == 0)) {
        EVR_LOG("xr: LOCAL used as the runtime gives it (ETERNALVR_XR_UPRIGHT=0)");
        return;
    }
    PFN_xrConvertWin32PerformanceCounterToTimeKHR toTime = nullptr;
    if (perfCounterTime) {
        xr.xrGetInstanceProcAddr(instance, "xrConvertWin32PerformanceCounterToTimeKHR",
                                 reinterpret_cast<PFN_xrVoidFunction*>(&toTime));
    }
    if (!toTime) {
        EVR_LOG("xr: LOCAL not checked for being upright (the runtime has no %s)",
                XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME);
        return;
    }
    XrReferenceSpaceCreateInfo stageInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    stageInfo.poseInReferenceSpace.orientation.w = 1.0f;
    stageInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    XrSpace stage = XR_NULL_HANDLE;
    if (XR_FAILED(xr.xrCreateReferenceSpace(session, &stageInfo, &stage))) {
        EVR_LOG("xr: LOCAL not checked for being upright (no STAGE space)");
        return;
    }
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    XrTime time = 0;
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    const bool located = XR_SUCCEEDED(toTime(instance, &counter, &time)) &&
                         XR_SUCCEEDED(xr.xrLocateSpace(localSpace, stage, time, &location)) &&
                         (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0 &&
                         (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
    if (!located) {
        xr.xrDestroySpace(stage);
        EVR_LOG("xr: LOCAL not checked for being upright (the runtime could not place it in STAGE yet)");
        return;
    }
    const XrQuaternionf& o = location.pose.orientation;
    const xr_math::Quaternion localInStage{o.x, o.y, o.z, o.w};
    const std::optional<xr_math::Quaternion> heading = xr_math::uprightReplacement(localInStage);
    if (!heading) {
        xr.xrDestroySpace(stage);
        EVR_LOG("xr: LOCAL is upright (up axis %.2f)", xr_math::upCosine(localInStage));
        return;
    }
    XrReferenceSpaceCreateInfo upright{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    upright.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    upright.poseInReferenceSpace.position = location.pose.position;
    upright.poseInReferenceSpace.orientation = {heading->x, heading->y, heading->z, heading->w};
    XrSpace replacement = XR_NULL_HANDLE;
    xr.xrDestroySpace(stage);
    if (XR_FAILED(xr.xrCreateReferenceSpace(session, &upright, &replacement))) {
        EVR_LOG("xr: LOCAL is not upright (up axis %.2f) and no replacement could be made; the view may be "
                "tilted",
                xr_math::upCosine(localInStage));
        return;
    }
    xr.xrDestroySpace(localSpace);
    localSpace = replacement;
    EVR_LOG(
        "xr: the runtime's LOCAL is not upright (orientation in STAGE %.3f %.3f %.3f %.3f, up axis %.2f); "
        "using an upright space from STAGE at its origin (%.2f %.2f %.2f) with its heading "
        "(ETERNALVR_XR_UPRIGHT=0 keeps LOCAL)",
        o.x, o.y, o.z, o.w, xr_math::upCosine(localInStage), location.pose.position.x,
        location.pose.position.y, location.pose.position.z);
}

bool XrPresenter::Impl::createXrInstance(bool quiet) {
    std::uint32_t count = 0;
    if (quiet && XR_FAILED(xr.xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr))) {
        return false; // no runtime to ask yet
    }
    EVR_XR_CHECK(xr.xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr));
    std::vector<XrExtensionProperties> props(count, {XR_TYPE_EXTENSION_PROPERTIES});
    EVR_XR_CHECK(xr.xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data()));
    const bool hasD3D12 = std::any_of(props.begin(), props.end(), [](const XrExtensionProperties& p) {
        return std::strcmp(p.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0;
    });
    if (!hasD3D12) {
        if (!quiet) {
            status::flat(
                "the headset runtime does not support Direct3D 12 sharing (pick another OpenXR runtime)");
            EVR_LOG("xr: the runtime does not offer %s; VR off", XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
        }
        return false;
    }
    // The controller profiles' extensions the runtime offers; a profile without its extension gets no
    // bindings (controllers.hpp).
    controllerProfiles.extensions = controllers::profileExtensions(props);
    std::vector<const char*> extensions{XR_KHR_D3D12_ENABLE_EXTENSION_NAME};
    // Optional: turns a Windows performance counter into an XrTime, so LOCAL can be checked for being
    // upright before the first frame (makeLocalUpright).
    perfCounterTime = std::any_of(props.begin(), props.end(), [](const XrExtensionProperties& p) {
        return std::strcmp(p.extensionName, XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME) ==
               0;
    });
    if (perfCounterTime) {
        extensions.push_back(XR_KHR_WIN32_CONVERT_PERFORMANCE_COUNTER_TIME_EXTENSION_NAME);
    }
    // Optional: a colour scale on quad layers, for the wrist HUD's fade (docs/VR_HANDS_HUD.md).
    wrist.colorScaleBias = std::any_of(props.begin(), props.end(), [](const XrExtensionProperties& p) {
        return std::strcmp(p.extensionName, XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME) == 0;
    });
    if (wrist.colorScaleBias) {
        extensions.push_back(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);
    }
    for (const std::string& name : controllerProfiles.extensions) {
        extensions.push_back(name.c_str());
    }
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(info.applicationInfo.applicationName, "EternalVR");
    info.applicationInfo.applicationVersion = 1;
    strcpy_s(info.applicationInfo.engineName, "idTech");
    info.applicationInfo.engineVersion = 7;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.enabledExtensionNames = extensions.data();
    info.applicationInfo.apiVersion = XR_API_VERSION_1_1;
    XrResult r = xr.xrCreateInstance(&info, &instance);
    if (r == XR_ERROR_API_VERSION_UNSUPPORTED) {
        EVR_LOG("xr: OpenXR 1.1 unsupported; falling back to 1.0");
        info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        r = xr.xrCreateInstance(&info, &instance);
    }
    if (XR_FAILED(r) && extensions.size() > 1) { // an extension listed but refused: the core profiles only
        if (!quiet) {
            EVR_LOG("xr: xrCreateInstance failed (%d); retrying without the optional extensions",
                    static_cast<int>(r));
        }
        controllerProfiles.extensions.clear();
        perfCounterTime = false;
        wrist.colorScaleBias = false;
        info.enabledExtensionCount = 1;
        r = xr.xrCreateInstance(&info, &instance);
    }
    controllerProfiles.api11 = info.applicationInfo.apiVersion == XR_API_VERSION_1_1;
    if (XR_FAILED(r)) {
        if (!quiet) {
            status::flat("the headset runtime would not start (is it installed and running?)");
            EVR_LOG("xr: xrCreateInstance failed: %d", static_cast<int>(r));
        }
        return false;
    }
#define EVR_XR_LOAD(name)                                                                                    \
    xr.xrGetInstanceProcAddr(instance, #name, reinterpret_cast<PFN_xrVoidFunction*>(&xr.name));              \
    if (!xr.name) {                                                                                          \
        EVR_LOG("xr: missing %s", #name);                                                                    \
        return false;                                                                                        \
    }
    EVR_XR_FUNCTIONS(EVR_XR_LOAD)
#undef EVR_XR_LOAD
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    xr.xrGetInstanceProperties(instance, &ip);
    EVR_LOG("xr: runtime '%s' %u.%u.%u, api %s", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
            XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion),
            info.applicationInfo.apiVersion == XR_API_VERSION_1_1 ? "1.1" : "1.0");
    setXrRuntimeName(ip.runtimeName);
    // The menu pointer's capture chord drops the trigger click of the same buttons the mapper chords.
    menuChord = input::CaptureChord(input::kTriggerThresholds, input::captureButtonsFor(xrRuntimeName()));
    return true;
}

bool XrPresenter::Impl::waitForSystem() {
    XrSystemGetInfo info{XR_TYPE_SYSTEM_GET_INFO};
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    for (int attempt = 0; !stop.load(); ++attempt) {
        const XrResult r = xr.xrGetSystem(instance, &info, &systemId);
        if (XR_SUCCEEDED(r)) {
            XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
            xr.xrGetSystemProperties(instance, systemId, &sp);
            EVR_LOG("xr: system '%s', max swapchain %ux%u", sp.systemName,
                    sp.graphicsProperties.maxSwapchainImageWidth,
                    sp.graphicsProperties.maxSwapchainImageHeight);
            maxSwapchainWidth = sp.graphicsProperties.maxSwapchainImageWidth;
            maxSwapchainHeight = sp.graphicsProperties.maxSwapchainImageHeight;
            reportViewLimits();
            awaitRenderSize();
            std::lock_guard lock(mutex);
            gameExtent = requestedExtent;
            gameFormat = requestedFormat;
            setRingExtent(gameExtent);
            return true;
        }
        if (r != XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            char text[XR_MAX_RESULT_STRING_SIZE];
            status::flat("the headset runtime reported an error (see the log)");
            EVR_LOG("xr: xrGetSystem failed: %s; VR off", xrText(r, text));
            return false;
        }
        if (attempt % 10 == 0) {
            status::waiting(
                "no headset found yet: connect the headset and start its runtime (the mod keeps trying)");
            EVR_LOG("xr: headset not available (XR_ERROR_FORM_FACTOR_UNAVAILABLE); retrying every second");
        }
        Sleep(1000);
    }
    return false;
}

void XrPresenter::Impl::reportViewLimits() {
    if (!virtual_client::wanted()) {
        return;
    }
    std::uint32_t count = 0;
    if (XR_FAILED(xr.xrEnumerateViewConfigurationViews(instance, systemId, viewConfig, 0, &count, nullptr)) ||
        count == 0) {
        EVR_LOG("size: the runtime lists no views; the render size waits");
        return;
    }
    std::vector<XrViewConfigurationView> views(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (XR_FAILED(xr.xrEnumerateViewConfigurationViews(instance, systemId, viewConfig, count, &count,
                                                       views.data()))) {
        return;
    }
    render_size::ViewLimits limits;
    limits.maxImageRect = {UINT32_MAX, UINT32_MAX};
    for (const XrViewConfigurationView& v : views) {
        limits.recommended.width = std::max(limits.recommended.width, v.recommendedImageRectWidth);
        limits.recommended.height = std::max(limits.recommended.height, v.recommendedImageRectHeight);
        limits.maxImageRect.width = std::min(limits.maxImageRect.width, v.maxImageRectWidth);
        limits.maxImageRect.height = std::min(limits.maxImageRect.height, v.maxImageRectHeight);
    }
    limits.maxSwapchain = {maxSwapchainWidth, maxSwapchainHeight};
    {
        std::lock_guard lock(mutex);
        limits.eyesSideBySide = ringEyes == 2 ? 2 : 1;
    }
    virtual_client::onViewLimits(limits);
}

void XrPresenter::Impl::awaitRenderSize() {
    // The render size switches on (or changes) once the runtime's views are known: the game then makes a
    // swapchain of that size. Building the ring for the old one would only rebuild it a moment later, and
    // every rebuild destroys XR swapchains (the OpenXR Simulator mixes up the images of the swapchains made
    // after one is destroyed), so the ring waits a little for the new size.
    const std::optional<render_size::Extent> size = virtual_client::activeExtent();
    if (!size) {
        return;
    }
    const ULONGLONG start = GetTickCount64();
    while (!stop.load() && GetTickCount64() - start < kRenderSizeWaitMs) {
        {
            std::lock_guard lock(mutex);
            if (requestedExtent.width == size->width && requestedExtent.height == size->height) {
                EVR_LOG("size: the game's swapchain has the render size %ux%u after %llu ms", size->width,
                        size->height, static_cast<unsigned long long>(GetTickCount64() - start));
                return;
            }
        }
        Sleep(10);
    }
    EVR_LOG("size: the game's swapchain did not reach %ux%u within %llu ms; the ring follows it later",
            size->width, size->height, static_cast<unsigned long long>(kRenderSizeWaitMs));
}

void XrPresenter::Impl::logD3dHealth() {
    if (d3dInfoQueue) {
        const UINT64 stored = d3dInfoQueue->GetNumStoredMessages();
        for (UINT64 i = 0; i < stored && d3dMessagesLogged < 40; ++i) {
            SIZE_T size = 0;
            d3dInfoQueue->GetMessage(i, nullptr, &size);
            std::vector<char> buffer(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
            if (size && SUCCEEDED(d3dInfoQueue->GetMessage(i, message, &size)) &&
                message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
                ++d3dMessagesLogged;
                EVR_LOG("d3d12 debug: %.*s", static_cast<int>(message->DescriptionByteLength),
                        message->pDescription);
            }
        }
        d3dInfoQueue->ClearStoredMessages();
    }
    if (d3dDevice && !loggedDeviceRemoved) {
        const HRESULT reason = d3dDevice->GetDeviceRemovedReason();
        if (FAILED(reason)) {
            loggedDeviceRemoved = true;
            EVR_LOG("d3d12: the presenter's device was removed: 0x%08lx", static_cast<unsigned long>(reason));
        }
    }
}

bool XrPresenter::Impl::createD3D12AndSession() {
    XrGraphicsRequirementsD3D12KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    EVR_XR_CHECK(xr.xrGetD3D12GraphicsRequirementsKHR(instance, systemId, &req));
    EVR_LOG("xr: runtime adapter LUID %08x:%08x, min feature level 0x%x",
            static_cast<unsigned>(req.adapterLuid.HighPart), static_cast<unsigned>(req.adapterLuid.LowPart),
            static_cast<unsigned>(req.minFeatureLevel));
    if (!gameLuidValid || std::memcmp(&req.adapterLuid, gameLuid, sizeof(LUID)) != 0) {
        std::uint32_t low = 0;
        std::int32_t high = 0;
        std::memcpy(&low, gameLuid, 4);
        std::memcpy(&high, gameLuid + 4, 4);
        status::flat("the headset runtime uses a different graphics card than the game (laptop with two "
                     "GPUs: run the game on the same GPU as the headset)");
        EVR_LOG("xr: LUID mismatch: the game's device is %08x:%08x%s; VR off (T-111)",
                static_cast<unsigned>(high), low, gameLuidValid ? "" : " (invalid)");
        return false;
    }

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        status::flat("Direct3D 12 could not start (update Windows and the graphics driver)");
        EVR_LOG("d3d12: CreateDXGIFactory1 failed");
        return false;
    }
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapterByLuid(req.adapterLuid, IID_PPV_ARGS(&adapter)))) {
        EVR_LOG("d3d12: no adapter with the runtime's LUID");
        return false;
    }
    DXGI_ADAPTER_DESC1 desc{};
    adapter->GetDesc1(&desc);
    // ETERNALVR_D3D12_DEBUG=1: the D3D12 debug layer on the presenter's device; its errors go to the log.
    std::wstring debugText;
    const bool debugLayer = readEnv(L"ETERNALVR_D3D12_DEBUG", debugText) && debugText == L"1";
    if (debugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            EVR_LOG("d3d12: debug layer on (ETERNALVR_D3D12_DEBUG)");
        }
    }
    HRESULT hr = D3D12CreateDevice(adapter.Get(), req.minFeatureLevel, IID_PPV_ARGS(&d3dDevice));
    if (SUCCEEDED(hr) && debugLayer) {
        d3dDevice.As(&d3dInfoQueue);
    }
    if (FAILED(hr)) {
        status::flat("Direct3D 12 could not start on the game's graphics card (update the graphics driver)");
        EVR_LOG("d3d12: D3D12CreateDevice failed: 0x%08lx", hr);
        return false;
    }
    EVR_LOG("d3d12: device on '%ls'", desc.Description);
    vram::watch(adapter.Get()); // the stall line and the 10 s vram line read its memory budget
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&d3dQueue))) ||
        FAILED(
            d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&d3dAllocator))) ||
        FAILED(d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, d3dAllocator.Get(), nullptr,
                                            IID_PPV_ARGS(&d3dList))) ||
        FAILED(d3dList->Close()) ||
        FAILED(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copyFence)))) {
        status::flat("Direct3D 12 could not start (update the graphics driver)");
        EVR_LOG("d3d12: queue, command list or fence creation failed");
        return false;
    }
    copyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return createSession();
}

bool XrPresenter::Impl::createSession() {
    XrGraphicsBindingD3D12KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    binding.device = d3dDevice.Get();
    binding.queue = d3dQueue.Get();
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO, &binding};
    sessionInfo.systemId = systemId;
    EVR_XR_CHECK(xr.xrCreateSession(instance, &sessionInfo, &session));

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    EVR_XR_CHECK(xr.xrCreateReferenceSpace(session, &spaceInfo, &localSpace));
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    EVR_XR_CHECK(xr.xrCreateReferenceSpace(session, &spaceInfo, &viewSpace));
    makeLocalUpright(); // before anything else takes localSpace
    controllers::attach({xr.xrGetInstanceProcAddr, instance, session, localSpace, controllerProfiles});

    std::uint32_t viewCount = 0;
    EVR_XR_CHECK(
        xr.xrEnumerateViewConfigurationViews(instance, systemId, viewConfig, 0, &viewCount, nullptr));
    std::vector<XrViewConfigurationView> configViews(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    EVR_XR_CHECK(xr.xrEnumerateViewConfigurationViews(instance, systemId, viewConfig, viewCount, &viewCount,
                                                      configViews.data()));
    for (std::uint32_t i = 0; i < viewCount; ++i) {
        EVR_LOG("xr: view %u recommended %ux%u (max %ux%u)", i, configViews[i].recommendedImageRectWidth,
                configViews[i].recommendedImageRectHeight, configViews[i].maxImageRectWidth,
                configViews[i].maxImageRectHeight);
    }

    // sRGB swapchain whose D3D12 images are typeless: the game's bytes are already sRGB-encoded and
    // are copied unchanged (T-080). The ring uses the matching UNORM format so the copy is a plain
    // CopyTextureRegion; Vulkan's blit reorders channels if the game's format differs.
    std::uint32_t count = 0;
    EVR_XR_CHECK(xr.xrEnumerateSwapchainFormats(session, 0, &count, nullptr));
    xrFormats.resize(count);
    EVR_XR_CHECK(xr.xrEnumerateSwapchainFormats(session, count, &count, xrFormats.data()));
    std::string list;
    for (std::int64_t f : xrFormats) {
        list += std::to_string(f) + " ";
    }
    EVR_LOG("xr: swapchain formats: %s", list.c_str());
    if (!createXrSwapchain()) {
        return false;
    }
    createRoomObjects();
    return true;
}

void XrPresenter::Impl::createRoomObjects() {
    if (settings.mode != Mode::HeadTracked) {
        return;
    }
    std::uint32_t count = 0;
    std::vector<XrReferenceSpaceType> types;
    if (XR_SUCCEEDED(xr.xrEnumerateReferenceSpaces(session, 0, &count, nullptr)) && count > 0) {
        types.resize(count);
        if (XR_FAILED(xr.xrEnumerateReferenceSpaces(session, count, &count, types.data()))) {
            types.clear();
        }
    }
    const auto has = [&types](XrReferenceSpaceType type) {
        return std::find(types.begin(), types.end(), type) != types.end();
    };
    const XrReferenceSpaceType floor =
        has(XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR) ? XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR
        : has(XR_REFERENCE_SPACE_TYPE_STAGE)     ? XR_REFERENCE_SPACE_TYPE_STAGE
                                                 : XR_REFERENCE_SPACE_TYPE_MAX_ENUM;
    if (floor != XR_REFERENCE_SPACE_TYPE_MAX_ENUM) {
        XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        info.poseInReferenceSpace.orientation.w = 1.0f;
        info.referenceSpaceType = floor;
        std::unique_lock lock(spaceMutex);
        if (XR_FAILED(xr.xrCreateReferenceSpace(session, &info, &floorSpace))) {
            floorSpace = XR_NULL_HANDLE;
        }
    }
    EVR_LOG("room: floor space %s", floorSpace == XR_NULL_HANDLE ? "none (posture from overrides only)"
                                    : floor == XR_REFERENCE_SPACE_TYPE_STAGE ? "STAGE"
                                                                             : "LOCAL_FLOOR");
    if (roomScaleSettings().fade) {
        fadeLayer.create(xr, session, d3dDevice.Get(), xrSwapchainFormat);
    }
}

bool XrPresenter::Impl::createXrSwapchain() {
    const std::vector<std::int64_t>& formats = xrFormats;
    std::uint32_t count = 0;
    struct Choice {
        DXGI_FORMAT swapchain;
        DXGI_FORMAT ring;
    };
    // The ring format that matches the game's swapchain comes first, so presents are plain copies
    // (a blit needs a graphics queue, and the game presents from its compute queue in levels).
    const bool gameIsRgba = gameFormat == VK_FORMAT_R8G8B8A8_UNORM || gameFormat == VK_FORMAT_R8G8B8A8_SRGB;
    const Choice bgra{DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM};
    const Choice rgba{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM};
    const Choice choices[] = {gameIsRgba ? rgba : bgra, gameIsRgba ? bgra : rgba};
    const Choice* chosen = nullptr;
    for (const Choice& c : choices) {
        if (std::find(formats.begin(), formats.end(), static_cast<std::int64_t>(c.swapchain)) !=
            formats.end()) {
            chosen = &c;
            break;
        }
    }
    if (!chosen) {
        status::flat("the headset runtime offers no usable image format");
        EVR_LOG("xr: no 8-bit sRGB swapchain format offered; VR off");
        return false;
    }
    ringFormat = toVkFormat(chosen->ring);
    xrSwapchainFormat = static_cast<std::int64_t>(chosen->swapchain);
    XrSwapchainCreateInfo scInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    scInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    scInfo.format = chosen->swapchain;
    scInfo.sampleCount = 1;
    scInfo.width = ringExtent.width;
    scInfo.height = ringExtent.height;
    scInfo.faceCount = 1;
    scInfo.arraySize = 1;
    scInfo.mipCount = 1;
    EVR_XR_CHECK(xr.xrCreateSwapchain(session, &scInfo, &xrSwapchain));
    EVR_XR_CHECK(xr.xrEnumerateSwapchainImages(xrSwapchain, 0, &count, nullptr));
    std::vector<XrSwapchainImageD3D12KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    EVR_XR_CHECK(xr.xrEnumerateSwapchainImages(xrSwapchain, count, &count,
                                               reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
    for (const auto& image : images) {
        xrImages.push_back(image.texture);
    }
    // A copy into an image of another size fails to record, and executing it would remove the device: a
    // runtime that hands out such images (the OpenXR Simulator does for a swapchain made after one was
    // destroyed) gets no swapchain from us.
    for (std::size_t i = 0; i < xrImages.size(); ++i) {
        const D3D12_RESOURCE_DESC d = xrImages[i]->GetDesc();
        if (d.Width != ringExtent.width || d.Height != ringExtent.height) {
            EVR_LOG("xr: the runtime's swapchain image %zu is %llux%u, not %ux%u; swapchain refused", i,
                    static_cast<unsigned long long>(d.Width), d.Height, ringExtent.width, ringExtent.height);
            xr.xrDestroySwapchain(xrSwapchain);
            xrSwapchain = XR_NULL_HANDLE;
            xrImages.clear();
            return false;
        }
    }
    if (const auto size = xr_math::cinemaQuadSize(eyeExtent.width, eyeExtent.height, kScreenWidthMetres)) {
        quadSize = {size->width, size->height};
    }
    cinemaView.setImage(eyeExtent.width, eyeExtent.height);
    EVR_LOG("xr: swapchain %ux%u (%u eye image(s) of %ux%u) format %d, %u image(s); screen %.2f x %.2f m at "
            "%.1f m",
            ringExtent.width, ringExtent.height, ringEyes, eyeExtent.width, eyeExtent.height,
            static_cast<int>(chosen->swapchain), count, quadSize.width, quadSize.height,
            kScreenDistanceMetres);
    return true;
}

} // namespace evr::vkcore
