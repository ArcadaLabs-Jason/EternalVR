#pragma once

// The game's crosshair on the gun's ray under hand aim (ETERNALVR_UI_CROSSHAIR=gun, opt-in, with the aim dot
// on; ui_layer/gun_crosshair.hpp, ui_layer/crosshair.hpp).
//
// With each GUI copy into the UI swapchain that the present hook marked for it (UiImage::moveCentre: hand
// aim, no menu, backdrop or weapon wheel), that the frame showing it draws the quad for (GunCopy::frameShows:
// the same test as presenter_frame.cpp's), and whose own border shows no full-screen layer (GunCopy::border,
// gun_border_probe.hpp), the crosshair's rectangle of the GUI image is copied into a small swapchain of its
// own, less the HUD markers' boxes, and cleared out of the UI image, less those boxes; the presenter then
// shows that image on a quad on the weapon ray at the aim dot's distance, at the angle the rectangle spans on
// the HUD (presenter_reticle.cpp, fillGunCrosshairQuad). Any other copy is left whole. When the small image
// is not ready (the runtime still holds it), the rectangle is still cleared from the UI image: no crosshair
// at the head's centre, and none on the gun for that copy. Only copies, clears and one quad: no shader, no
// game memory. XR worker only, like the marker layer (presenter_markers.hpp), whose swapchain handling it
// follows.
//
// Known, not fixed: when a UI swapchain rebuild cannot make this layer's swapchain, the copies the present
// hook marked before it learnt that (uiGunReady; at most the ring's few slots in flight) are left whole, so
// the game's crosshair shows at the head's centre for those frames before the hold takes over.

#include "ui_layer/gun_crosshair.hpp"
#include "ui_layer/ui_settings.hpp"
#include "vkcore/gun_border_probe.hpp"
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

// One GUI copy, as the worker sees it.
struct GunCopy {
    bool wanted = false;     // the present hook marked it (UiImage::moveCentre)
    bool frameShows = false; // the frame that shows it draws the crosshair's quad
    bool borderRead = false; // its border test was recorded and read
    GunBorderProbe::Reading border{};
};

class GunCrosshairLayer {
public:
    // Once the UI swapchain exists (its images `uiImages`, of a `width` x `height` GUI target, `format`): the
    // crosshair's swapchain (ui_layer::gunCrosshairRect's size) and render target views of both swapchains'
    // images. False (logged): no quad; the presenter keeps the crosshair hold instead
    // (ui_layer::effectiveCrosshairMode).
    bool create(const XrFunctions& xr,
                XrSession session,
                XrSwapchainSet& swapchains,
                ID3D12Device* device,
                const XrSwapchainSet::Images& uiImages,
                std::uint32_t width,
                std::uint32_t height,
                std::int64_t format);
    // With the UI swapchain detached (a rebuild): set aside, destroyed once the next one is made (or at
    // destroy).
    void detach(const XrFunctions& xr, XrSwapchainSet& swapchains);
    void destroy(const XrFunctions& xr, XrSwapchainSet& swapchains);
    [[nodiscard]] bool ready() const { return swapchain_ != XR_NULL_HANDLE; }
    // The rectangle of the GUI target that moves.
    [[nodiscard]] const ui_layer::PixelRect& rect() const { return rect_; }

    // With the GUI copy recorded on `list` into UI image `uiIndex` (`ui`, in COPY_DEST) from `source` (in
    // COPY_SOURCE): when `copy` is wanted, shown and its border clear, the rectangle as above, the markers'
    // boxes in the band `band` kept on the UI image. The UI image is left in COPY_DEST. Nothing at all while
    // the layer is not ready (another mode, or no swapchain).
    void record(const XrFunctions& xr,
                ID3D12GraphicsCommandList* list,
                ID3D12Resource* source,
                std::uint32_t uiIndex,
                ID3D12Resource* ui,
                const ui_layer::PixelRect& band,
                const GunCopy& copy);
    // With the UI image released after its copy: the crosshair image too; the quad follows what that copy
    // did (the runtime shows the released images).
    void release(const XrFunctions& xr);
    // The UI image released last had its crosshair moved onto an image of this layer: there is a quad to
    // show.
    [[nodiscard]] bool shown() const { return ready() && shown_; }
    // The quad: `size` metres (ui_layer::gunCrosshairSize), at `pose` in `space`.
    void fill(XrCompositionLayerQuad& quad,
              XrSpace space,
              const XrPosef& pose,
              const ui_layer::GunCrosshairSize& size) const;

private:
    bool acquire(const XrFunctions& xr);
    void logStats();

    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    XrSwapchain retired_ = XR_NULL_HANDLE; // detached, destroyed after the next create
    XrSwapchainSet::Images images_;
    ui_layer::PixelRect rect_;
    std::vector<ID3D12Resource*> uiImages_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_; // the UI images' views, then the crosshair's
    UINT descriptorSize_ = 0;
    std::int64_t acquired_ = -1;
    bool waited_ = false;
    bool pending_ = false; // the acquired image has a copy recorded
    bool moved_ = false;   // the copy recorded last moved the crosshair onto it
    bool shown_ = false;   // ... the one released last
    bool loggedFirst_ = false;
    std::uint32_t loggedBorder_ = 0;
    bool loggedDrawnBorder_ = false;
    // The border tests of the last copies that reached the move, for the line when moves stop.
    static constexpr std::uint32_t kRecentBorders = 3;
    struct RecentBorder {
        bool read = false;
        GunBorderProbe::Reading reading{};
    };
    std::array<RecentBorder, kRecentBorders> recentBorders_{};
    std::uint32_t recentNext_ = 0, recentMoves_ = 0, loggedMoveEnds_ = 0;
    std::uint64_t copies_ = 0, moves_ = 0, withMarkers_ = 0, notReady_ = 0, keptBorder_ = 0, keptFrame_ = 0;
    ULONGLONG lastStatsTicks_ = 0;
};

} // namespace evr::vkcore
