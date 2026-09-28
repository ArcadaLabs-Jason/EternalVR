// The comfort vignette layer (presenter_vignette.hpp).

#include "vkcore/presenter_vignette.hpp"

#include "vkcore/log.hpp"
#include "vkcore/presenter_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::vkcore {

namespace {

// The quad spans 75 degrees each way from the view axis at its edges' midpoints (more at its corners): past
// every headset's view, so no edge of it is ever seen. 5 m ahead, well behind the HUD (1.5 m) that is drawn
// over it, and far enough that both eyes see it centred on their own view (a quarter degree apart).
constexpr float kHalfDegrees = 75.0f;
constexpr float kDistance = 5.0f;
// A gap this long without a frame (the flat screen, a menu, a lost session) starts the amount from clear.
constexpr double kGapSeconds = 0.25;

const char* modeName(ui_layer::VignetteMode mode) {
    return mode == ui_layer::VignetteMode::Strong ? "strong" : "light";
}

const comfort::VignetteLook& lookFor(ui_layer::VignetteMode mode) {
    return mode == ui_layer::VignetteMode::Strong ? comfort::kStrongVignette : comfort::kLightVignette;
}

} // namespace

bool VignetteLayer::create(ui_layer::VignetteMode mode, const MakeImage& make) {
    const comfort::VignetteLook& look = lookFor(mode);
    for (int level = 1; level <= kLevels; ++level) {
        const float amount = static_cast<float>(level) / static_cast<float>(kLevels);
        const std::vector<std::uint8_t> pixels =
            comfort::vignetteImage(kPixels, comfort::vignetteShape(amount, look), kHalfDegrees);
        if (!make(swapchains_[static_cast<std::size_t>(level - 1)], pixels, kPixels, kPixels, "vignette")) {
            return false;
        }
    }
    const comfort::VignetteShape full = comfort::vignetteShape(1.0f, look);
    EVR_LOG("vignette: %s, %d level(s) of %ux%u ready; at full: clear to %.0f deg, dark from %.0f deg, "
            "opacity %.2f",
            modeName(mode), kLevels, kPixels, kPixels, full.clearDegrees, full.darkDegrees, full.opacity);
    return true;
}

bool VignetteLayer::prepare(ui_layer::VignetteMode mode,
                            const comfort::VignetteMotion& motion,
                            bool allowed,
                            double nowSeconds,
                            XrSpace viewSpace,
                            const MakeImage& make,
                            XrCompositionLayerQuad& quad) {
    if (mode == ui_layer::VignetteMode::Off || failed_) {
        return false;
    }
    if (!created_) {
        created_ = create(mode, make);
        failed_ = !created_;
        if (failed_) {
            EVR_LOG("vignette: the images could not be made; no vignette");
            return false;
        }
    }
    ++frames_;
    maxTurn_ =
        std::max(maxTurn_, std::isfinite(motion.turnDegreesPerSecond) ? motion.turnDegreesPerSecond : 0.0f);
    maxMove_ = std::max(maxMove_, std::isfinite(motion.moveMagnitude) ? motion.moveMagnitude : 0.0f);
    gameFrames_ += motion.gameMotion ? 1 : 0;
    const double dt = lastSeconds_ < 0.0 ? 0.0 : nowSeconds - lastSeconds_;
    lastSeconds_ = nowSeconds;
    if (!allowed || dt > kGapSeconds) {
        policy_.reset();
    }
    if (!allowed) {
        ++heldBack_;
        return false;
    }
    const int level = comfort::vignetteLevel(policy_.update(motion, dt), kLevels);
    if (level == 0) {
        return false;
    }
    ++shown_;
    maxLevel_ = std::max(maxLevel_, level);
    const float side = 2.0f * kDistance * std::tan(kHalfDegrees / 57.2957795f);
    quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    quad.space = viewSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = swapchains_[static_cast<std::size_t>(level - 1)];
    quad.subImage.imageRect = {{0, 0},
                               {static_cast<std::int32_t>(kPixels), static_cast<std::int32_t>(kPixels)}};
    quad.subImage.imageArrayIndex = 0;
    quad.pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -kDistance}};
    quad.size = {side, side};
    return true;
}

void VignetteLayer::logStats(ui_layer::VignetteMode mode) {
    if (mode == ui_layer::VignetteMode::Off) {
        return;
    }
    EVR_LOG(
        "vignette: %s, shown %llu of %llu frame(s), max level %d of %d; turn up to %.0f deg/s, move up to "
        "%.2f, game camera %llu frame(s), %llu frame(s) held back (menu)%s",
        modeName(mode), static_cast<unsigned long long>(shown_), static_cast<unsigned long long>(frames_),
        maxLevel_, kLevels, maxTurn_, maxMove_, static_cast<unsigned long long>(gameFrames_),
        static_cast<unsigned long long>(heldBack_), failed_ ? "; images failed" : "");
    frames_ = 0;
    shown_ = 0;
    heldBack_ = 0;
    gameFrames_ = 0;
    maxLevel_ = 0;
    maxTurn_ = 0.0f;
    maxMove_ = 0.0f;
}

void VignetteLayer::destroy(const XrFunctions& xr) {
    for (XrSwapchain& swapchain : swapchains_) {
        if (swapchain != XR_NULL_HANDLE) {
            xr.xrDestroySwapchain(swapchain);
            swapchain = XR_NULL_HANDLE;
        }
    }
    created_ = false;
    failed_ = false;
    policy_.reset();
    lastSeconds_ = -1.0;
}

} // namespace evr::vkcore
