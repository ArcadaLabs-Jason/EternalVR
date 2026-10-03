// The hand-aim reticle and the other static images on quads (the menu pointer's dot and beam).

#include "vkcore/fence_wait.hpp"
#include "vkcore/presenter_impl.hpp"

#include "common/quat.hpp"
#include "common/vector.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/reticle_depth.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace evr::vkcore {

// ---- Worker: the reticle under hand aim ------------------------------------------------------------

namespace {
constexpr std::uint32_t kReticlePixels = 64;
// As the UI swapchain: RGBA8 bytes, sRGB-encoded already, copied unchanged into an sRGB image.
constexpr DXGI_FORMAT kStaticDxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kStaticSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
// How often the dot's distance is logged (XR thread only).
constexpr double kDotLogSeconds = 10.0;
double g_nextDotLog = 0.0;
} // namespace

bool XrPresenter::Impl::createStaticImage(XrSwapchain& swapchain,
                                          const std::vector<std::uint8_t>& pixels,
                                          std::uint32_t width,
                                          std::uint32_t height,
                                          const char* what) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT; // one image, written once
    info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = kStaticSwapchainFormat;
    info.sampleCount = 1;
    info.width = width;
    info.height = height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    EVR_XR_CHECK(xr.xrCreateSwapchain(session, &info, &swapchain));
    std::uint32_t count = 0;
    EVR_XR_CHECK(xr.xrEnumerateSwapchainImages(swapchain, 0, &count, nullptr));
    std::vector<XrSwapchainImageD3D12KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    EVR_XR_CHECK(xr.xrEnumerateSwapchainImages(swapchain, count, &count,
                                               reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())));
    std::uint32_t index = 0;
    EVR_XR_CHECK(xr.xrAcquireSwapchainImage(swapchain, nullptr, &index));
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = kSwapchainWaitTimeout;
    EVR_XR_CHECK(xr.xrWaitSwapchainImage(swapchain, &wait));
    ID3D12Resource* target = images[index].texture;

    // The pixels go through an upload buffer (rows 256-byte aligned) on a list of its own, waited for once.
    const UINT pitch =
        (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<UINT64>(pitch) * height;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    void* mapped = nullptr;
    if (FAILED(d3dDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                  IID_PPV_ARGS(&upload))) ||
        FAILED(upload->Map(0, nullptr, &mapped)) ||
        FAILED(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&list)))) {
        EVR_LOG("ui: the %s's upload could not be made; no %s", what, what);
        return false;
    }
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(static_cast<std::uint8_t*>(mapped) + static_cast<std::size_t>(y) * pitch,
                    pixels.data() + static_cast<std::size_t>(y) * width * 4,
                    static_cast<std::size_t>(width) * 4);
    }
    upload->Unmap(0, nullptr);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = target;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint = {kStaticDxgiFormat, width, height, 1, pitch};
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    list->Close();
    ID3D12CommandList* lists[] = {list.Get()};
    d3dQueue->ExecuteCommandLists(1, lists);
    d3dQueue->Signal(copyFence.Get(), ++copyFenceValue);
    if (!waitFence(copyFence.Get(), copyFenceValue, copyEvent, 2000)) {
        // The queue may still use the buffer and the list: keep them alive rather than free them.
        upload->AddRef();
        list->AddRef();
        allocator->AddRef();
        EVR_LOG("ui: the %s's upload did not finish; no %s", what, what);
        return false;
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    EVR_XR_CHECK(xr.xrReleaseSwapchainImage(swapchain, &release));
    return true;
}

bool XrPresenter::Impl::createReticle() {
    if (!createStaticImage(reticleSwapchain, ui_layer::reticleImage(kReticlePixels), kReticlePixels,
                           kReticlePixels, "reticle")) {
        return false;
    }
    EVR_LOG("ui: reticle image ready (the hand-aim dot at %.1f m, %.2f deg; the menu pointer's dot)",
            settings.ui.reticleDistanceMetres, settings.ui.reticleDegrees);
    return true;
}

bool XrPresenter::Impl::fillReticleQuad(XrCompositionLayerQuad& quad) {
    if (!settings.ui.reticle || reticleFailed) {
        return false;
    }
    const XrSpace aim = controllers::weaponAimSpace();
    if (aim == XR_NULL_HANDLE) {
        return false;
    }
    if (!reticleSwapchain && !createReticle()) {
        reticleFailed = true;
        return false;
    }
    // Where the ray meets the world (reticle_depth.hpp), else at the set distance; the same angular size.
    const bool traced = shownHasView && shownView.weaponAimValid && shownView.weaponAimHitMetres > 0.0f;
    const float d = traced ? shownView.weaponAimHitMetres : settings.ui.reticleDistanceMetres;
    const float side = ui_layer::reticleSideMetres(d, settings.ui.reticleDegrees);
    if (const double now = qpcSeconds(qpcNow()); now >= g_nextDotLog) {
        g_nextDotLog = now + kDotLogSeconds;
        EVR_LOG("ui: the aim dot at %.1f m (%s)", d,
                !traced                    ? "the set distance"
                : d >= kReticleReachMetres ? "the ray is clear"
                                           : "where the ray meets the world");
    }
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = reticleSwapchain;
    quad.subImage.imageRect = {
        {0, 0}, {static_cast<std::int32_t>(kReticlePixels), static_cast<std::int32_t>(kReticlePixels)}};
    quad.subImage.imageArrayIndex = 0;
    quad.size = {side, side};
    // On the ray (-Z of the aim pose), facing back along it toward the hand and the eyes.
    if (shownHasView && shownView.weaponAimValid) {
        // The ray the shown frame's gun and shots were made with (smoothed, at the view's pose time), placed
        // in LOCAL like the frame itself, so the dot stays on the barrel's line and where the shots go.
        const XrPosef& ray = shownView.weaponAim;
        const Quat q{ray.orientation.x, ray.orientation.y, ray.orientation.z, ray.orientation.w};
        const Vec3 point =
            rotate(q, Vec3{0.0f, 0.0f, -d}) + Vec3{ray.position.x, ray.position.y, ray.position.z};
        quad.space = localSpace;
        quad.pose = {ray.orientation, {point.x, point.y, point.z}};
        return true;
    }
    // No ray with the shown frame (hand tracking lost): the hand's own space, as tracked.
    quad.space = aim;
    quad.pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -d}};
    return true;
}

} // namespace evr::vkcore
