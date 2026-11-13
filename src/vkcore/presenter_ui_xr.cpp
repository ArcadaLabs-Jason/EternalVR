// The UI layer's XR side (presenter_ui.cpp has the shared GUI images and the present hook's copy into them):
// on the XR worker, the UI swapchain, the GUI image's D3D12 copy into it and the head-locked quad that shows
// it.

#include "vkcore/presenter_impl.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace evr::vkcore {

namespace {

// The UI swapchain is sRGB: the GUI target's display-encoded bytes go into it unchanged (presenter_ui.cpp).
constexpr DXGI_FORMAT kUiSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

} // namespace

// ---- Worker: the UI swapchain, its copy and the quad ------------------------------------------------

bool XrPresenter::Impl::createUiSwapchain() {
    if (std::find(xrFormats.begin(), xrFormats.end(), static_cast<std::int64_t>(kUiSwapchainFormat)) ==
        xrFormats.end()) {
        EVR_LOG("ui: the runtime offers no R8G8B8A8 sRGB swapchain; UI layer off");
        return false;
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = kUiSwapchainFormat;
    info.sampleCount = 1;
    info.width = uiExtent.width;
    info.height = uiExtent.height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrSwapchain made = XR_NULL_HANDLE;
    EVR_XR_CHECK(xr.xrCreateSwapchain(session, &info, &made));
    // Only a new swapchain of its own with images of the GUI's size (presenter_swapchains.hpp); a swapchain
    // that is another one in use is neither used nor destroyed.
    if (!xrSwapchains.admit(xr, made, uiExtent.width, uiExtent.height, "UI", uiXrImages)) {
        EVR_LOG("ui: no UI swapchain of %ux%u; UI layer off", uiExtent.width, uiExtent.height);
        return false;
    }
    uiSwapchain = made;
    EVR_LOG("ui: UI swapchain %ux%u, %zu image(s)", uiExtent.width, uiExtent.height, uiXrImages.size());
    if (settings.ui.hud == ui_layer::HudMode::Panel && settings.ui.front) {
        hudMarkers.create(xr, session, xrSwapchains, d3dDevice.Get(), uiXrImages, uiExtent.width,
                          uiExtent.height, kUiSwapchainFormat);
    }
    if (settings.ui.removeWash) {
        const char* why = "";
        if (uiWash.create(d3dDevice.Get(), why)) {
            EVR_LOG("ui: the full-screen additive wash (hit and low-health vignette) is removed from the HUD "
                    "quad");
        } else {
            EVR_LOG("ui: wash filter off (%s); the GUI image is copied as the game drew it", why);
        }
    }
    return true;
}

bool XrPresenter::Impl::acquireUiXrImage() {
    if (!uiReady || uiFailed) {
        return false;
    }
    if (!uiSwapchain && !createUiSwapchain()) {
        uiFailed = true;
        return false;
    }
    if (uiAcquired < 0) {
        std::uint32_t index = 0;
        if (XR_FAILED(xr.xrAcquireSwapchainImage(uiSwapchain, nullptr, &index))) {
            return false;
        }
        uiAcquired = index;
        uiAcquiredWaited = false;
    }
    if (!uiAcquiredWaited) {
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = kSwapchainWaitTimeout;
        if (timedCall(inRuntime.waitImage, xr.xrWaitSwapchainImage, uiSwapchain, &wait) != XR_SUCCESS) {
            return false; // a timeout keeps the image acquired for the next frame (T-081)
        }
        uiAcquiredWaited = true;
    }
    return true;
}

void XrPresenter::Impl::recordUiXrCopy(RingSlot& slot) {
    ID3D12Resource* target = uiXrImages[static_cast<std::size_t>(uiAcquired)].Get();
    std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = slot.ui.resource.Get();
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    // The HUD (not a menu, whose panel shows the same image) without the full-screen additive wash: the
    // filtered image is copied instead of the shared one (ui_wash.hpp).
    ID3D12Resource* filtered =
        !menuUp.load(std::memory_order_relaxed)
            ? uiWash.record(d3dList.Get(), slot.ui.resource.Get(), uiExtent.width, uiExtent.height)
            : nullptr;
    d3dList->ResourceBarrier(filtered ? 1u : static_cast<UINT>(barriers.size()),
                             filtered ? &barriers[1] : barriers.data());
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = target;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = filtered ? filtered : slot.ui.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    d3dList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    // The HUD's markers onto their own image, outside menus, each eye's moved for its target's distance
    // (presenter_markers.hpp).
    hudMarkers.record(xr, d3dList.Get(), src.pResource, static_cast<std::uint32_t>(uiAcquired), target,
                      ui_layer::wideContentRect(uiExtent.width, uiExtent.height),
                      markerParallax(settings.ui, uiExtent.width, uiExtent.height, settings.unitsPerMetre),
                      markerEyes(slot.hasView ? &slot.view : nullptr),
                      !menuUp.load(std::memory_order_relaxed));
    for (auto& b : barriers) {
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    }
    d3dList->ResourceBarrier(filtered ? 1u : static_cast<UINT>(barriers.size()),
                             filtered ? &barriers[1] : barriers.data());
    if (filtered) {
        uiWash.afterCopy(d3dList.Get());
        if (!uiWashLogged) {
            uiWashLogged = true;
            EVR_LOG("ui: first GUI image filtered for the wash");
        }
    }
    uiCopyPending = true;
}

void XrPresenter::Impl::finishUiXrCopy() {
    uiImageQpc = qpcNow(); // with the game image's copy (completeCopy)
    uiWithImage = uiCopyPending;
    if (!uiCopyPending) {
        return;
    }
    uiCopyPending = false;
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr.xrReleaseSwapchainImage(uiSwapchain, &release);
    hudMarkers.release(xr);
    uiAcquired = -1;
    uiAcquiredWaited = false;
    if (!uiHasImage) {
        EVR_LOG("ui: first GUI image on the quad");
    }
    uiHasImage = true;
    uiShownQpc = qpcNow();
    ++uiXrCopies;
}

void XrPresenter::Impl::noteUiGap(LONGLONG imageBefore, bool newImage, bool shown) {
    constexpr std::uint32_t kGapLines = 20;
    const LONGLONG now = qpcNow();
    const double sinceImage = imageBefore != 0 ? qpcSeconds(now - imageBefore) : 0.0;
    const double guiAge = uiHasImage ? qpcSeconds(now - uiShownQpc) : -1.0;
    const std::optional<ui_layer::GuiGapTally::Gap> gap = uiGaps.onFrame(newImage, sinceImage, guiAge, shown);
    if (!gap || uiGapLines >= kGapLines) {
        return;
    }
    ++uiGapLines;
    EVR_LOG("ui: a %.0f ms gap in the game's images: the GUI quad in %u headset frame(s) past %.0f ms (its "
            "image up to %.0f ms old), left out of %u%s",
            gap->gapMs, gap->shown, ui_layer::kGuiFreshSeconds * 1000.0, gap->lastShownAgeMs, gap->left,
            uiGapLines == kGapLines ? "; later gaps are not logged" : "");
}

bool XrPresenter::Impl::uiFresh() const {
    return uiHasImage && uiSwapchain &&
           ui_layer::guiImageShown(qpcSeconds(qpcNow() - uiShownQpc), uiWithImage);
}

bool XrPresenter::Impl::fillUiQuad(XrCompositionLayerQuad& quad) {
    // Only a fresh GUI image: a capture that stopped (menus, a failed check) hides the quad, and the
    // composite comes back with it. A game that is late keeps the quad up with the image the headset repeats,
    // for up to a second (ui_layer/gui_hold.hpp).
    if (!uiFresh()) {
        return false;
    }
    // The 16:9 band the game's HUD and menus are laid out in (the rows above and below it on a near-square
    // eye image are empty, as are the columns beside it on an image wider than 16:9).
    const ui_layer::PixelRect content = settings.ui.wideCrop
                                            ? ui_layer::wideContentRect(uiExtent.width, uiExtent.height)
                                            : ui_layer::PixelRect{0, 0, uiExtent.width, uiExtent.height};
    const auto size = ui_layer::panelSize(settings.ui.widthMetres, content.width, content.height);
    if (!size) {
        return false;
    }
    // Premultiplied alpha, as the game draws the target (OpenXR's default without the unpremultiplied bit).
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space = viewSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = uiSwapchain;
    quad.subImage.imageRect = {
        {content.x, content.y},
        {static_cast<std::int32_t>(content.width), static_cast<std::int32_t>(content.height)}};
    quad.subImage.imageArrayIndex = 0;
    quad.pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, settings.ui.offsetYMetres, -settings.ui.distanceMetres}};
    quad.size = {size->width, size->height};
    return true;
}

XrSwapchain XrPresenter::Impl::detachUiSwapchain() {
    const XrSwapchain swapchain = uiSwapchain;
    uiSwapchain = XR_NULL_HANDLE;
    uiXrImages.clear();
    uiAcquired = -1;
    uiAcquiredWaited = false;
    uiCopyPending = false;
    uiHasImage = false;
    uiWithImage = false;
    uiGaps = ui_layer::GuiGapTally{}; // a gap across a new swapchain is not one the GUI quad could bridge
    uiFailed = false;
    uiWash.destroy();
    hudMarkers.detach(xr, xrSwapchains);
    return swapchain;
}

void XrPresenter::Impl::destroyUiXrObjects() {
    XrSwapchain ui = detachUiSwapchain();
    xrSwapchains.destroy(xr, ui);
    hudMarkers.destroy(xr, xrSwapchains);
    xrSwapchains.destroy(xr, reticleSwapchain);
    reticleFailed = false;
    destroyMenuXrObjects();
}

} // namespace evr::vkcore
