// The head-collision fade layer (presenter_fade.hpp).

#include "vkcore/presenter_fade.hpp"

#include "vkcore/presenter_impl.hpp"

namespace evr::vkcore {

namespace {

constexpr std::uint32_t kSize = 16;
// Head-locked, 0.25 m ahead and 4 m wide: it covers about 83 degrees each way from the view axis, more than
// any headset shows.
constexpr float kDistance = 0.25f;
constexpr float kWidth = 4.0f;
constexpr XrDuration kWaitTimeout = 20'000'000; // 20 ms

} // namespace

bool FadeLayer::create(const XrFunctions& xr, XrSession session, ID3D12Device* device, std::int64_t format) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = format;
    info.sampleCount = 1;
    info.width = kSize;
    info.height = kSize;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrResult r = xr.xrCreateSwapchain(session, &info, &swapchain_);
    if (XR_FAILED(r)) {
        EVR_LOG("room: fade swapchain not created (XrResult %d); no fade", static_cast<int>(r));
        swapchain_ = XR_NULL_HANDLE;
        return false;
    }
    std::uint32_t count = 0;
    xr.xrEnumerateSwapchainImages(swapchain_, 0, &count, nullptr);
    std::vector<XrSwapchainImageD3D12KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
    r = xr.xrEnumerateSwapchainImages(swapchain_, count, &count,
                                      reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = count;
    if (XR_FAILED(r) || count == 0 || FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap_))) ||
        FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr,
                                         IID_PPV_ARGS(&list_))) ||
        FAILED(list_->Close()) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        EVR_LOG("room: fade layer objects not created; no fade");
        destroy(xr);
        return false;
    }
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    descriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC view{};
    view.Format = static_cast<DXGI_FORMAT>(format);
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (const auto& image : images) {
        images_.push_back(image.texture);
        device->CreateRenderTargetView(image.texture, &view, handle);
        handle.ptr += descriptorSize_;
    }
    EVR_LOG("room: fade layer ready (%u image(s), format %lld)", count, static_cast<long long>(format));
    return true;
}

bool FadeLayer::prepare(const XrFunctions& xr,
                        ID3D12CommandQueue* queue,
                        float alpha,
                        XrSpace viewSpace,
                        XrCompositionLayerQuad& quad) {
    if (!ready() || !(alpha > 0.002f)) {
        return false;
    }
    // The previous clear must be done before its allocator is reused.
    if (fence_->GetCompletedValue() < fenceValue_) {
        fence_->SetEventOnCompletion(fenceValue_, event_);
        if (WaitForSingleObject(event_, 50) != WAIT_OBJECT_0) {
            return false;
        }
    }
    // OpenXR swapchain rules: an image whose wait timed out stays acquired and is waited on again next
    // time; it is released only after a successful wait.
    if (acquired_ < 0) {
        std::uint32_t index = 0;
        if (XR_FAILED(xr.xrAcquireSwapchainImage(swapchain_, nullptr, &index)) || index >= images_.size()) {
            return false;
        }
        acquired_ = index;
        waited_ = false;
    }
    if (!waited_) {
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kWaitTimeout;
        if (xr.xrWaitSwapchainImage(swapchain_, &wait) != XR_SUCCESS) {
            return false; // this frame shows no fade
        }
        waited_ = true;
    }
    const auto index = static_cast<std::uint32_t>(acquired_);
    allocator_->Reset();
    list_->Reset(allocator_.Get(), nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * descriptorSize_;
    const float color[4] = {0.0f, 0.0f, 0.0f, alpha > 1.0f ? 1.0f : alpha};
    list_->ClearRenderTargetView(handle, color, 0, nullptr);
    list_->Close();
    ID3D12CommandList* lists[] = {list_.Get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence_.Get(), ++fenceValue_);
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr.xrReleaseSwapchainImage(swapchain_, &release);
    acquired_ = -1;

    quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    // Black premultiplied by alpha is still black: out = dst * (1 - alpha).
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space = viewSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = swapchain_;
    quad.subImage.imageRect = {{0, 0}, {static_cast<std::int32_t>(kSize), static_cast<std::int32_t>(kSize)}};
    quad.subImage.imageArrayIndex = 0;
    quad.pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -kDistance}};
    quad.size = {kWidth, kWidth};
    if (++frames_ == 1) {
        EVR_LOG("room: first fade frame (alpha %.2f)", alpha);
    }
    return true;
}

void FadeLayer::destroy(const XrFunctions& xr) {
    if (fence_ && event_ && fence_->GetCompletedValue() < fenceValue_) {
        fence_->SetEventOnCompletion(fenceValue_, event_);
        WaitForSingleObject(event_, 500);
    }
    if (swapchain_ != XR_NULL_HANDLE) {
        xr.xrDestroySwapchain(swapchain_);
        swapchain_ = XR_NULL_HANDLE;
    }
    acquired_ = -1;
    images_.clear();
    list_.Reset();
    allocator_.Reset();
    heap_.Reset();
    fence_.Reset();
    if (event_) {
        CloseHandle(event_);
        event_ = nullptr;
    }
}

} // namespace evr::vkcore
