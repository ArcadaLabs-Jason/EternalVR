#pragma once

// The HUD's world-attached markers on their own quad (docs/rig-findings/hud-markers.md 5b; T-178, refines
// T-174).
//
// The GUI image holds both the HUD, which should stay locked to the view, and the objective markers, which
// the game draws for the head pose the frame was rendered with (marker_hooks.hpp) and which should stay on
// their targets as the eye images do. One quad cannot do both: in VIEW the markers moved off their targets by
// the head's turn until the frame was shown, and at the rendered pose (presenter_hud_lock.hpp) the whole HUD
// stood still in the world for a refresh and jumped with each new frame whenever the game ran under the
// headset's rate. So with each GUI copy the markers' boxes (ui_layer/marker_boxes.hpp) are copied into a
// swapchain of their own and cleared out of the UI image: the UI quad stays in VIEW and the marker quad, the
// same quad over the other image, goes to the rendered head pose in LOCAL.
//
// Each eye has its own half of the marker image and its own quad (T-186): every marker is copied into an
// eye's half moved by the parallax of its target's distance (ui_layer::markerEyeCopies), so both eyes meet on
// the target instead of on the HUD's quad.
//
// With the panel HUD only: the wrist and weapon HUDs' quads keep the rendered pose for the whole HUD (their
// cuts and the markers' boxes are not reconciled), as do menus' panels (no markers there). XR worker only.

#include "ui_layer/marker_boxes.hpp"
#include "ui_layer/ui_settings.hpp"
#include "vkcore/presenter_swapchains.hpp"

#include <windows.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <openxr/openxr.h>

#include <array>
#include <cstdint>
#include <vector>

namespace evr::vkcore {

struct XrFunctions;
struct ViewRecord;

// The UI quad's parallax for the markers (ui_layer::MarkerParallax) with a GUI target of `width` x `height`:
// its distance, the pixels per metre across the content it shows, the world's units per metre.
ui_layer::MarkerParallax markerParallax(const ui_layer::UiSettings& ui,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        float unitsPerMetre);

// The eyes the markers of a GUI image rendered with `view` (nullptr: not known) are moved for: each eye's
// offset to the right of the head's centre as its half of the frame is shown (presenter_stereo.cpp
// fillProjectionViews) and as the game rendered it (ETERNALVR_IPD moves the rendered eyes,
// roomscale::withSeparation). A frame shown from the head's pose (mono, or not a pair) moves nothing.
std::array<ui_layer::MarkerEye, 2> markerEyes(const ViewRecord* view);
struct ViewRecord;

// The UI quad's parallax for the markers (ui_layer::MarkerParallax) with a GUI target of `width` x `height`:
// its distance, the pixels per metre across the content it shows, the world's units per metre.
ui_layer::MarkerParallax markerParallax(const ui_layer::UiSettings& ui,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        float unitsPerMetre);

// The eyes the markers of a GUI image rendered with `view` (nullptr: not known) are moved for: each eye's
// offset to the right of the head's centre as its half of the frame is shown (presenter_stereo.cpp
// fillProjectionViews) and as the game rendered it (ETERNALVR_IPD moves the rendered eyes,
// roomscale::withSeparation). A frame shown from the head's pose (mono, or not a pair) moves nothing.
std::array<ui_layer::MarkerEye, 2> markerEyes(const ViewRecord* view);

class HudMarkerLayer {
public:
    // Once the UI swapchain exists (its images `uiImages`, `width` x `height`, `format`): the marker
    // swapchain, twice as wide (the left eye's half, then the right's), and render target views of both
    // swapchains' images. False (logged): no marker layer, the whole HUD keeps the rendered head pose.
    bool create(const XrFunctions& xr,
                XrSession session,
                XrSwapchainSet& swapchains,
                ID3D12Device* device,
                const XrSwapchainSet::Images& uiImages,
                std::uint32_t width,
                std::uint32_t height,
                std::int64_t format);
    // With the UI swapchain detached (a rebuild): the marker swapchain is set aside and destroyed once the
    // next one is made (or at destroy), as the presenter does with its own (presenter_ring.cpp).
    void detach(const XrFunctions& xr, XrSwapchainSet& swapchains);
    void destroy(const XrFunctions& xr, XrSwapchainSet& swapchains);
    [[nodiscard]] bool ready() const { return swapchain_ != XR_NULL_HANDLE; }

    // With the GUI copy recorded on `list` into UI image `uiIndex` (`ui`, in COPY_DEST) from `source` (in
    // COPY_SOURCE), when the panel HUD is up (`split`): copies the boxes of the markers on the HUD's frame
    // now (marker_hooks.hpp) in the band `band` into each eye's half of the marker image, moved for
    // `parallax` and that eye (`eyes`, left then right; zero offsets for a mono frame), and clears them out
    // of the UI image, which is left in COPY_DEST. Without
    // markers, or without a marker image ready, nothing is cut: the UI image keeps them and no marker quad is
    // shown for this copy.
    void record(const XrFunctions& xr,
                ID3D12GraphicsCommandList* list,
                ID3D12Resource* source,
                std::uint32_t uiIndex,
                ID3D12Resource* ui,
                const ui_layer::PixelRect& band,
                const ui_layer::MarkerParallax& parallax,
                const std::array<ui_layer::MarkerEye, 2>& eyes,
                bool split);
    // With the UI image released after its copy: the marker image too, and the quad follows what that copy
    // did (the runtime shows the released images).
    void release(const XrFunctions& xr);
    // When the UI image last released had markers cut out: the marker quads for `ui` (the UI quad, in
    // `viewSpace`), the left eye's then the right's, at `renderedHead` in `localSpace`, written to `quads`
    // (which the caller keeps until the frame ends) and added to `layers` at `count`. Nothing otherwise.
    void fill(const XrCompositionLayerQuad& ui,
              XrSpace viewSpace,
              XrSpace localSpace,
              const XrPosef& renderedHead,
              std::array<XrCompositionLayerQuad, 2>& quads,
              const XrCompositionLayerBaseHeader** layers,
              std::uint32_t& count) const;

private:
    void logStats();

    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    XrSwapchain retired_ = XR_NULL_HANDLE; // detached, destroyed after the next create
    XrSwapchainSet::Images images_;
    std::int32_t eyeWidth_ = 0; // the UI target's width: the right eye's half starts there
    std::vector<ID3D12Resource*> uiImages_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_; // the UI images' views, then the marker images'
    UINT descriptorSize_ = 0;
    std::int64_t acquired_ = -1;
    bool waited_ = false;
    bool pending_ = false; // the acquired marker image has a copy recorded
    bool cut_ = false;     // the copy recorded last took markers out of the UI image
    bool shown_ = false;   // ... the one released last
    bool loggedFirst_ = false;
    std::uint64_t copies_ = 0, cuts_ = 0, boxes_ = 0, notReady_ = 0, shifts_ = 0;
    float shiftSum_ = 0.0f; // the right eye's shifts (pixels) over the stats period, for their mean
    float shiftMax_ = 0.0f;
    ULONGLONG lastStatsTicks_ = 0;
};

} // namespace evr::vkcore
