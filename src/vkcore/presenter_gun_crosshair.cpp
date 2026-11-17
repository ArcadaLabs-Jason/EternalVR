// The game's crosshair on the gun's ray (presenter_gun_crosshair.hpp).

#include "vkcore/presenter_gun_crosshair.hpp"

#include "ui_layer/backdrop.hpp"
#include "ui_layer/marker_boxes.hpp"
#include "vkcore/log.hpp"
#include "vkcore/marker_hooks.hpp"
#include "vkcore/presenter_types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

namespace evr::vkcore {

namespace {

// As the marker layer: an image the runtime still holds after this stays acquired and is waited on again at
// the next copy, which clears the crosshair from the HUD without moving it.
constexpr XrDuration kWaitTimeout = 20'000'000; // 20 ms
constexpr ULONGLONG kStatsMs = 10000;
// Copies the border test keeps whole that are logged one by one; later ones are only counted.
constexpr std::uint32_t kLoggedBorderCopies = 8;
// Runs of moves that end (a menu, a backdrop, the wheel, head aim) logged with their last copies' border
// tests.
constexpr std::uint32_t kLoggedMoveEnds = 6;

// The eight alphas of a border reading, as "a b c d e f g h".
std::string alphaText(const GunBorderProbe::Reading& r) {
    char text[64] = {};
    int at = 0;
    for (std::size_t p = 0;
         p < ui_layer::kBackdropProbePoints && at >= 0 && static_cast<std::size_t>(at) < sizeof(text); ++p) {
        at += std::snprintf(text + at, sizeof(text) - static_cast<std::size_t>(at), p ? " %u" : "%u",
                            static_cast<unsigned>(r[p * 4 + 3]));
    }
    return text;
}

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

std::vector<D3D12_RECT> toRects(const std::vector<ui_layer::PixelRect>& from) {
    std::vector<D3D12_RECT> out;
    out.reserve(from.size());
    for (const ui_layer::PixelRect& r : from) {
        out.push_back({static_cast<LONG>(r.x), static_cast<LONG>(r.y), static_cast<LONG>(r.x + r.width),
                       static_cast<LONG>(r.y + r.height)});
    }
    return out;
}

} // namespace

bool GunCrosshairLayer::create(const XrFunctions& xr,
                               XrSession session,
                               XrSwapchainSet& swapchains,
                               ID3D12Device* device,
                               const XrSwapchainSet::Images& uiImages,
                               std::uint32_t width,
                               std::uint32_t height,
                               std::int64_t format) {
    const ui_layer::PixelRect rect = ui_layer::gunCrosshairRect(width, height);
    if (rect.width == 0 || rect.height == 0) {
        EVR_LOG("ui: no rectangle for the game's crosshair in a %ux%u GUI target; it is held hidden under "
                "hand aim instead",
                width, height);
        return false;
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = format;
    info.sampleCount = 1;
    info.width = rect.width;
    info.height = rect.height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrSwapchain made = XR_NULL_HANDLE;
    const XrResult r = xr.xrCreateSwapchain(session, &info, &made);
    swapchains.destroy(xr, retired_); // the one before a rebuild goes once the new one is made
    if (XR_FAILED(r)) {
        EVR_LOG("ui: no swapchain for the game's crosshair on the gun's ray (XrResult %d); it is held hidden "
                "under hand aim instead",
                static_cast<int>(r));
        return false;
    }
    if (!swapchains.admit(xr, made, rect.width, rect.height, "the gun's crosshair", images_)) {
        EVR_LOG(
            "ui: no swapchain of %ux%u for the game's crosshair on the gun's ray; it is held hidden under "
            "hand aim instead",
            rect.width, rect.height);
        images_.clear();
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = static_cast<UINT>(uiImages.size() + images_.size());
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap_)))) {
        EVR_LOG("ui: no render target views for the game's crosshair on the gun's ray; it is held hidden "
                "under hand aim instead");
        swapchains.destroy(xr, made);
        images_.clear();
        return false;
    }
    swapchain_ = made;
    rect_ = rect;
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
        "ui: the game's crosshair goes on the gun's ray under hand aim: %ux%u at (%d, %d) of the %ux%u GUI "
        "target moves to its own quad (swapchain of %zu image(s)), the HUD's markers stay on the HUD",
        rect.width, rect.height, rect.x, rect.y, width, height, images_.size());
    return true;
}

void GunCrosshairLayer::detach(const XrFunctions& xr, XrSwapchainSet& swapchains) {
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
    moved_ = false;
    shown_ = false;
}

void GunCrosshairLayer::destroy(const XrFunctions& xr, XrSwapchainSet& swapchains) {
    detach(xr, swapchains);
    swapchains.destroy(xr, retired_);
}

bool GunCrosshairLayer::acquire(const XrFunctions& xr) {
    // OpenXR swapchain rules: an image whose wait timed out stays acquired and is waited on again next time.
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
            return false;
        }
        waited_ = true;
    }
    return true;
}

void GunCrosshairLayer::record(const XrFunctions& xr,
                               ID3D12GraphicsCommandList* list,
                               ID3D12Resource* source,
                               std::uint32_t uiIndex,
                               ID3D12Resource* ui,
                               const ui_layer::PixelRect& band,
                               const GunCopy& copy) {
    moved_ = false;
    if (!ready()) {
        return; // another mode, or no swapchain: the copy as in hold mode, nothing counted
    }
    ++copies_;
    logStats();
    if (copy.borderRead && !loggedDrawnBorder_) {
        // The test reads each copy's own pixels: the first reading that holds anything shows it working.
        for (std::size_t p = 0; p < ui_layer::kBackdropProbePoints; ++p) {
            if (copy.border[p * 4 + 3] != 0) {
                loggedDrawnBorder_ = true;
                EVR_LOG(
                    "ui: the gun's crosshair's border test: first reading with something drawn around the "
                    "rectangle (alpha %s), a copy %s",
                    alphaText(copy.border).c_str(),
                    copy.wanted ? "marked for the move"
                                : "not marked for the move (a menu, a backdrop, the wheel)");
                break;
            }
        }
    }
    if (!copy.wanted || uiIndex >= uiImages_.size()) {
        if (recentMoves_ > 0 && loggedMoveEnds_ < kLoggedMoveEnds) {
            // The copies just before a menu, a backdrop or the wheel took over: what their borders read shows
            // whether a full-screen layer was already coming in when the present hook stopped the move.
            ++loggedMoveEnds_;
            std::string last;
            const std::uint32_t n = std::min<std::uint32_t>(recentMoves_, kRecentBorders);
            for (std::uint32_t i = n; i > 0; --i) {
                const RecentBorder& b = recentBorders_[(recentNext_ + kRecentBorders - i) % kRecentBorders];
                last += b.read ? " [" + alphaText(b.reading) + "]" : " [not read]";
            }
            EVR_LOG("ui: the gun's crosshair: moves stop after %u copies; the border tests of the last %u "
                    "(oldest "
                    "first):%s%s",
                    recentMoves_, n, last.c_str(),
                    loggedMoveEnds_ == kLoggedMoveEnds ? " (the last such line)" : "");
        }
        recentMoves_ = 0;
        return;
    }
    if (!copy.frameShows) {
        ++keptFrame_; // a menu panel or a frame on the screen shows this copy: no quad, so no hole either
        return;
    }
    recentBorders_[recentNext_] = {copy.borderRead, copy.border};
    recentNext_ = (recentNext_ + 1) % kRecentBorders;
    ++recentMoves_;
    if (copy.borderRead && ui_layer::readingShowsBackdrop(copy.border)) {
        // A full-screen layer the backdrop test has not seen yet (the copy was marked without one).
        ++keptBorder_;
        if (loggedBorder_ < kLoggedBorderCopies) {
            ++loggedBorder_;
            EVR_LOG(
                "ui: the gun's crosshair: a GUI copy kept whole, a full-screen layer around its rectangle "
                "(alpha %s) before the backdrop test saw it%s",
                alphaText(copy.border).c_str(),
                loggedBorder_ == kLoggedBorderCopies ? " (the last such line)" : "");
        }
        return;
    }
    const std::vector<ui_layer::PixelRect> boxes = ui_layer::markerBoxes(markerSpots(), band);
    const ui_layer::GunCrosshairCuts cuts = ui_layer::gunCrosshairCuts(rect_, boxes);
    const D3D12_CPU_DESCRIPTOR_HANDLE start = heap_->GetCPUDescriptorHandleForHeapStart();
    const float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // premultiplied: nothing drawn
    if (acquire(xr)) {
        // The crosshair's image: the rectangle as the game drew it, the markers' boxes in it cleared.
        ID3D12Resource* image = images_[static_cast<std::size_t>(acquired_)].Get();
        D3D12_CPU_DESCRIPTOR_HANDLE imageView = start;
        imageView.ptr += (uiImages_.size() + static_cast<SIZE_T>(acquired_)) * descriptorSize_;
        transition(list, image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = image;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = source;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        const auto left = static_cast<UINT>(rect_.x);
        const auto top = static_cast<UINT>(rect_.y);
        const D3D12_BOX box{left, top, 0, left + rect_.width, top + rect_.height, 1};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
        transition(list, image, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (!cuts.moved.empty()) {
            const std::vector<D3D12_RECT> rects = toRects(cuts.moved);
            list->ClearRenderTargetView(imageView, transparent, static_cast<UINT>(rects.size()),
                                        rects.data());
            ++withMarkers_;
        }
        pending_ = true;
        moved_ = true;
        ++moves_;
    } else {
        ++notReady_;
    }
    // The UI image: the rectangle cleared out, the markers' boxes kept (the marker layer, when it runs, has
    // already taken them to their own quads).
    if (!cuts.hud.empty()) {
        D3D12_CPU_DESCRIPTOR_HANDLE uiView = start;
        uiView.ptr += static_cast<SIZE_T>(uiIndex) * descriptorSize_;
        const std::vector<D3D12_RECT> rects = toRects(cuts.hud);
        transition(list, ui, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ClearRenderTargetView(uiView, transparent, static_cast<UINT>(rects.size()), rects.data());
        transition(list, ui, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    }
    if (moved_ && !loggedFirst_) {
        loggedFirst_ = true;
        EVR_LOG(
            "ui: first GUI copy with the game's crosshair moved to the gun's ray (%zu marker box(es) kept "
            "on the HUD)",
            cuts.moved.size());
    }
}

void GunCrosshairLayer::release(const XrFunctions& xr) {
    shown_ = moved_;
    if (!pending_) {
        return;
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr.xrReleaseSwapchainImage(swapchain_, &release);
    acquired_ = -1;
    waited_ = false;
    pending_ = false;
}

void GunCrosshairLayer::fill(XrCompositionLayerQuad& quad,
                             XrSpace space,
                             const XrPosef& pose,
                             const ui_layer::GunCrosshairSize& size) const {
    // Premultiplied alpha, as the GUI target (presenter_ui_xr.cpp, fillUiQuad).
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = swapchain_;
    quad.subImage.imageRect = {
        {0, 0}, {static_cast<std::int32_t>(rect_.width), static_cast<std::int32_t>(rect_.height)}};
    quad.subImage.imageArrayIndex = 0;
    quad.space = space;
    quad.pose = pose;
    quad.size = {size.width, size.height};
}

void GunCrosshairLayer::logStats() {
    const ULONGLONG now = GetTickCount64();
    if (!ready() || now - lastStatsTicks_ < kStatsMs) {
        return;
    }
    lastStatsTicks_ = now;
    EVR_LOG("ui: the gun's crosshair: %llu GUI copies, %llu with the crosshair moved to the gun's ray (%llu "
            "with markers kept on the HUD), %llu cleared from the HUD without a crosshair image (the runtime "
            "held it); kept whole: %llu for a full-screen layer around the rectangle (the border test), %llu "
            "for a frame without the quad (a menu panel, the screen)",
            static_cast<unsigned long long>(copies_), static_cast<unsigned long long>(moves_),
            static_cast<unsigned long long>(withMarkers_), static_cast<unsigned long long>(notReady_),
            static_cast<unsigned long long>(keptBorder_), static_cast<unsigned long long>(keptFrame_));
    copies_ = moves_ = withMarkers_ = notReady_ = keptBorder_ = keptFrame_ = 0;
}

} // namespace evr::vkcore
