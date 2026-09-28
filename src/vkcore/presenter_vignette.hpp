#pragma once

// The comfort vignette on the headset (ETERNALVR_VIGNETTE, features/comfort/vignette.hpp): a quad layer
// locked to the head, big enough to cover every headset's view, that darkens the edges while the stick or
// the game moves the player.
//
// A quad has no alpha of its own, so the vignette is made in advance at kLevels amounts, each a one-image
// swapchain (kPixels square, 256 KiB each), and each frame shows the one nearest to the policy's amount, or
// none. XR worker only; the images are made on the first frame with the vignette on.

#include "features/comfort/vignette.hpp"
#include "ui_layer/ui_settings.hpp"

#include <windows.h>

#include <openxr/openxr.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace evr::vkcore {

struct XrFunctions;

class VignetteLayer {
public:
    static constexpr int kLevels = 8;
    static constexpr std::uint32_t kPixels = 256;
    // Makes a one-image swapchain holding `pixels` (width x height RGBA8, premultiplied); `what` names it in
    // the log (XrPresenter::Impl::createStaticImage).
    using MakeImage = std::function<bool(XrSwapchain& swapchain,
                                         const std::vector<std::uint8_t>& pixels,
                                         std::uint32_t width,
                                         std::uint32_t height,
                                         const char* what)>;

    // Every frame that shows the head-tracked game view: moves the amount with this frame's `motion` and
    // fills `quad` (head-locked in `viewSpace`) when there is a level to show. `allowed` is false while a
    // menu is up: nothing shows and the amount starts from clear afterwards. False when nothing should be
    // drawn (the vignette is off, clear, or its images could not be made).
    bool prepare(ui_layer::VignetteMode mode,
                 const comfort::VignetteMotion& motion,
                 bool allowed,
                 double nowSeconds,
                 XrSpace viewSpace,
                 const MakeImage& make,
                 XrCompositionLayerQuad& quad);
    // Every 10 s with the XR statistics: what was shown since the last line (nothing while off).
    void logStats(ui_layer::VignetteMode mode);
    void destroy(const XrFunctions& xr);

private:
    bool create(ui_layer::VignetteMode mode, const MakeImage& make);

    std::array<XrSwapchain, kLevels> swapchains_{};
    bool created_ = false;
    bool failed_ = false;
    comfort::VignettePolicy policy_;
    double lastSeconds_ = -1.0;
    // Since the last summary line.
    std::uint64_t frames_ = 0;
    std::uint64_t shown_ = 0;
    std::uint64_t heldBack_ = 0;   // frames with a menu up
    std::uint64_t gameFrames_ = 0; // frames the game moved the camera
    int maxLevel_ = 0;
    float maxTurn_ = 0.0f;
    float maxMove_ = 0.0f;
};

} // namespace evr::vkcore
