// The HUD's markers on their own quad (presenter_markers.hpp).

#include "vkcore/presenter_markers.hpp"

#include "common/pose.hpp"
#include "features/roomscale/eye_separation.hpp"
#include "ui_layer/marker_boxes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/marker_hooks.hpp"
#include "vkcore/presenter_hud_lock.hpp"
#include "vkcore/presenter_types.hpp"
#include "vkcore/room_scale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace evr::vkcore {

namespace {

// An image the runtime still holds after this is left acquired and waited on again at the next copy; that
// copy keeps its markers on the HUD meanwhile.
constexpr XrDuration kWaitTimeout = 20'000'000; // 20 ms
constexpr ULONGLONG kStatsMs = 10000;

void transition(ID3D12GraphicsCommandList* list,
                ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}

Pose toPose(const XrPosef& p) {
    return {Quat{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            Vec3{p.position.x, p.position.y, p.position.z}};
}

} // namespace

ui_layer::MarkerParallax markerParallax(const ui_layer::UiSettings& ui,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        float unitsPerMetre) {
    // The content fillUiQuad (presenter_ui.cpp) spreads over the quad's width.
    const ui_layer::PixelRect content =
        ui.wideCrop ? ui_layer::wideContentRect(width, height) : ui_layer::PixelRect{0, 0, width, height};
    ui_layer::MarkerParallax out;
    out.quadMetres = ui.distanceMetres;
    out.pixelsPerMetre = ui.widthMetres > 0.0f ? static_cast<float>(content.width) / ui.widthMetres : 0.0f;
    out.unitsPerMetre = unitsPerMetre;
    return out;
}

std::array<ui_layer::MarkerEye, 2> markerEyes(const ViewRecord* view) {
    std::array<ui_layer::MarkerEye, 2> out{};
    if (!view || !view->stereo || !view->showEyes) {
        return out;
    }
    const Pose fromHead = inverse(toPose(view->pose));
    const std::array<Vec3, 2> shown{transformPoint(fromHead, toPose(view->eyes[0].pose).position),
                                    transformPoint(fromHead, toPose(view->eyes[1].pose).position)};
    const std::array<Vec3, 2> rendered = roomscale::withSeparation(shown, roomScaleSettings().ipdMetres);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = {shown[i].x, rendered[i].x};
    }
    return out;
}

bool HudMarkerLayer::create(const XrFunctions& xr,
                            XrSession session,
                            XrSwapchainSet& swapchains,
                            ID3D12Device* device,
                            const XrSwapchainSet::Images& uiImages,
                            std::uint32_t width,
                            std::uint32_t height,
                            std::int64_t format) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = format;
    info.sampleCount = 1;
    info.width = width * 2; // the left eye's half, then the right's
    info.height = height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrSwapchain made = XR_NULL_HANDLE;
    const XrResult r = xr.xrCreateSwapchain(session, &info, &made);
    swapchains.destroy(xr, retired_); // the one before a rebuild goes once the new one is made
    if (XR_FAILED(r)) {
        EVR_LOG("ui: no HUD marker swapchain (XrResult %d); the whole HUD keeps the head pose its frame was "
                "rendered with",
                static_cast<int>(r));
        return false;
    }
    if (!swapchains.admit(xr, made, width * 2, height, "HUD markers", images_)) {
        EVR_LOG(
            "ui: no HUD marker swapchain of %ux%u; the whole HUD keeps the head pose its frame was rendered "
            "with",
            width * 2, height);
        images_.clear();
        return false;
    }
    swapchain_ = made;
    eyeWidth_ = static_cast<std::int32_t>(width);
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = static_cast<UINT>(uiImages.size() + images_.size());
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap_)))) {
        EVR_LOG(
            "ui: no render target views for the HUD markers; the whole HUD keeps the head pose its frame was "
            "rendered with");
        swapchains.destroy(xr, swapchain_);
        images_.clear();
        return false;
    }
    descriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle = heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC view{};
    view.Format = static_cast<DXGI_FORMAT>(format);
    view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    for (const auto& image : uiImages) {
        uiImages_.push_back(image.Get());
        device->CreateRenderTargetView(image.Get(), &view, handle);
        handle.ptr += descriptorSize_;
    }
    for (const auto& image : images_) {
        device->CreateRenderTargetView(image.Get(), &view, handle);
        handle.ptr += descriptorSize_;
    }
    EVR_LOG(
        "ui: HUD marker swapchain %ux%u, %zu image(s): the HUD's markers go on their own quads (one per eye, "
        "each marker moved for its target's distance) at the head pose their frame was rendered with, the "
        "HUD "
        "stays head-locked",
        width * 2, height, images_.size());
    return true;
}

void HudMarkerLayer::detach(const XrFunctions& xr, XrSwapchainSet& swapchains) {
    if (acquired_ >= 0 && waited_) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xr.xrReleaseSwapchainImage(swapchain_, &release);
    }
    if (swapchain_ != XR_NULL_HANDLE) {
        swapchains.destroy(xr, retired_);
        retired_ = swapchain_;
        swapchain_ = XR_NULL_HANDLE;
    }
    images_.clear();
    uiImages_.clear();
    heap_.Reset();
    acquired_ = -1;
    waited_ = false;
    pending_ = false;
    cut_ = false;
    shown_ = false;
}

void HudMarkerLayer::destroy(const XrFunctions& xr, XrSwapchainSet& swapchains) {
    detach(xr, swapchains);
    swapchains.destroy(xr, retired_);
}

void HudMarkerLayer::record(const XrFunctions& xr,
                            ID3D12GraphicsCommandList* list,
                            ID3D12Resource* source,
                            std::uint32_t uiIndex,
                            ID3D12Resource* ui,
                            const ui_layer::PixelRect& band,
                            const ui_layer::MarkerParallax& parallax,
                            const std::array<ui_layer::MarkerEye, 2>& eyes,
                            bool split) {
    cut_ = false;
    ++copies_;
    logStats();
    if (!ready() || !split || uiIndex >= uiImages_.size()) {
        return;
    }
    const std::vector<ui_layer::MarkerSpot> spots = markerSpots();
    const std::vector<ui_layer::PixelRect> boxes = ui_layer::markerBoxes(spots, band);
    if (boxes.empty()) {
        return;
    }
    const std::array<std::vector<ui_layer::MarkerCopy>, 2> copies{
        ui_layer::markerEyeCopies(spots, band, parallax, eyes[0]),
        ui_layer::markerEyeCopies(spots, band, parallax, eyes[1])};
    // OpenXR swapchain rules: an image whose wait timed out stays acquired and is waited on again next time.
    if (acquired_ < 0) {
        std::uint32_t index = 0;
        if (XR_FAILED(xr.xrAcquireSwapchainImage(swapchain_, nullptr, &index)) || index >= images_.size()) {
            ++notReady_;
            return;
        }
        acquired_ = index;
        waited_ = false;
    }
    if (!waited_) {
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kWaitTimeout;
        if (xr.xrWaitSwapchainImage(swapchain_, &wait) != XR_SUCCESS) {
            ++notReady_;
            return;
        }
        waited_ = true;
    }
    ID3D12Resource* markers = images_[static_cast<std::size_t>(acquired_)].Get();
    const D3D12_CPU_DESCRIPTOR_HANDLE start = heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE uiView = start;
    uiView.ptr += static_cast<SIZE_T>(uiIndex) * descriptorSize_;
    D3D12_CPU_DESCRIPTOR_HANDLE markerView = start;
    markerView.ptr += (uiImages_.size() + static_cast<SIZE_T>(acquired_)) * descriptorSize_;
    const float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // premultiplied: nothing drawn

    // The marker image: transparent, then in each eye's half the markers' boxes as the game drew them, each
    // moved for its target's distance.
    list->ClearRenderTargetView(markerView, transparent, 0, nullptr);
    transition(list, markers, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = markers;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = source;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    for (std::size_t eye = 0; eye < copies.size(); ++eye) {
        const std::int64_t base = static_cast<std::int64_t>(eye) * eyeWidth_;
        for (const ui_layer::MarkerCopy& c : copies[eye]) {
            const auto left = static_cast<UINT>(c.source.x);
            const auto top = static_cast<UINT>(c.source.y);
            const D3D12_BOX box{left, top, 0, left + c.source.width, top + c.source.height, 1};
            list->CopyTextureRegion(&dst, static_cast<UINT>(base + c.source.x + c.dx), top, 0, &src, &box);
        }
    }
    for (const ui_layer::MarkerCopy& c : copies[1]) {
        shiftSum_ += static_cast<float>(c.dx);
        shiftMax_ = std::max(shiftMax_, static_cast<float>(std::abs(c.dx)));
    }
    shifts_ += copies[1].size();
    transition(list, markers, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);

    std::vector<D3D12_RECT> rects;
    rects.reserve(boxes.size());
    for (const ui_layer::PixelRect& b : boxes) {
        rects.push_back({static_cast<LONG>(b.x), static_cast<LONG>(b.y), static_cast<LONG>(b.x + b.width),
                         static_cast<LONG>(b.y + b.height)});
    }

    // The UI image: the same boxes cleared out, so no marker shows twice.
    transition(list, ui, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->ClearRenderTargetView(uiView, transparent, static_cast<UINT>(rects.size()), rects.data());
    transition(list, ui, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);

    pending_ = true;
    cut_ = true;
    boxes_ += boxes.size();
    ++cuts_;
    if (!loggedFirst_) {
        loggedFirst_ = true;
        EVR_LOG(
            "ui: first HUD markers on their own quads (%zu box(es), the first %ux%u at (%d, %d); eyes shown "
            "%.1f and %.1f mm, rendered %.1f and %.1f mm from the head's centre, the quad %.2f m ahead at "
            "%.1f px/m, the first marker's target %.1f units ahead: moved %d and %d px)",
            boxes.size(), boxes[0].width, boxes[0].height, boxes[0].x, boxes[0].y,
            static_cast<double>(eyes[0].shownMetres) * 1000.0,
            static_cast<double>(eyes[1].shownMetres) * 1000.0,
            static_cast<double>(eyes[0].renderedMetres) * 1000.0,
            static_cast<double>(eyes[1].renderedMetres) * 1000.0, static_cast<double>(parallax.quadMetres),
            static_cast<double>(parallax.pixelsPerMetre),
            spots.empty() ? 0.0 : static_cast<double>(spots[0].ahead),
            copies[0].empty() ? 0 : copies[0][0].dx, copies[1].empty() ? 0 : copies[1][0].dx);
    }
}

void HudMarkerLayer::release(const XrFunctions& xr) {
    shown_ = cut_;
    if (!pending_) {
        return;
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr.xrReleaseSwapchainImage(swapchain_, &release);
    acquired_ = -1;
    waited_ = false;
    pending_ = false;
}

void HudMarkerLayer::fill(const XrCompositionLayerQuad& ui,
                          XrSpace viewSpace,
                          XrSpace localSpace,
                          const XrPosef& renderedHead,
                          std::array<XrCompositionLayerQuad, 2>& quads,
                          const XrCompositionLayerBaseHeader** layers,
                          std::uint32_t& count) const {
    if (!ready() || !shown_) {
        return;
    }
    for (std::size_t eye = 0; eye < quads.size(); ++eye) {
        XrCompositionLayerQuad& q = quads[eye];
        q = ui;
        q.subImage.swapchain = swapchain_;
        q.subImage.imageRect.offset.x += static_cast<std::int32_t>(eye) * eyeWidth_;
        q.eyeVisibility = eye == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
        lockToRenderedHead(q, viewSpace, localSpace, renderedHead);
        layers[count++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&q);
    }
}

void HudMarkerLayer::logStats() {
    const ULONGLONG now = GetTickCount64();
    if (!ready() || now - lastStatsTicks_ < kStatsMs) {
        return;
    }
    lastStatsTicks_ = now;
    EVR_LOG("ui: HUD markers: %llu GUI copies, %llu with markers on their own quads (%llu box(es); the right "
            "eye's "
            "copies moved %.1f px on average, %.1f at most), %llu kept on the HUD for want of a marker image",
            static_cast<unsigned long long>(copies_), static_cast<unsigned long long>(cuts_),
            static_cast<unsigned long long>(boxes_),
            shifts_ ? static_cast<double>(shiftSum_) / static_cast<double>(shifts_) : 0.0,
            static_cast<double>(shiftMax_), static_cast<unsigned long long>(notReady_));
    copies_ = cuts_ = boxes_ = notReady_ = shifts_ = 0;
    shiftSum_ = shiftMax_ = 0.0f;
}

} // namespace evr::vkcore
