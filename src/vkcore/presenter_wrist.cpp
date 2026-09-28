// The wrist HUD (ETERNALVR_HUD=wrist, docs/VR_HANDS_HUD.md): the captured GUI target's corner blocks
// (health, armor, ammo, equipment) and the crosshair's ability rings on small quads on the inside of the
// off hand's wrist, in its grip space, shown while that side faces the head and faded in and out; the rest
// of the 16:9 band the UI quad shows (subtitles, prompts, boss and encounter bars, markers, damage) stays on
// the head-locked quad, cut into pieces around the corner blocks. Every quad shows a sub-rectangle of the
// same UI swapchain image, so there is no extra copy.

#include "vkcore/presenter_wrist.hpp"

#include "vkcore/controllers.hpp"
#include "vkcore/log.hpp"
#include "vkcore/presenter_types.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace evr::vkcore {

namespace {

XrPosef toXrPose(const Pose& p) {
    return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
            {p.position.x, p.position.y, p.position.z}};
}

XrRect2Di toXrRect(const ui_layer::PixelRect& r) {
    return {{r.x, r.y}, {static_cast<std::int32_t>(r.width), static_cast<std::int32_t>(r.height)}};
}

// Frames longer than this (a stall, the first frame) count as this long for the fade.
constexpr float kMaxFadeStep = 0.1f;

} // namespace

std::uint32_t WristHud::fill(const ui_layer::UiSettings& settings,
                             std::uint32_t width,
                             std::uint32_t height,
                             const XrCompositionLayerQuad& ui,
                             XrCompositionLayerQuad* out) {
    if (settings.hud != ui_layer::HudMode::Wrist) {
        return 0;
    }
    // Without controllers there is no wrist: the whole HUD stays on the head-locked quad.
    const std::optional<input::InputFrame> frame = controllers::latestFrame();
    const XrSpace grip = controllers::offHandGripSpace();
    if (!frame || grip == XR_NULL_HANDLE) {
        if (!loggedFallback_) {
            loggedFallback_ = true;
            EVR_LOG("ui: wrist HUD: no controllers (or no off-hand grip space); the HUD stays on the panel");
        }
        return 0;
    }
    // The part of the target the UI quad shows (the 16:9 band, ETERNALVR_UI_CROP), in pixels.
    const XrRect2Di& shownRect = ui.subImage.imageRect;
    const ui_layer::PixelRect shown{shownRect.offset.x, shownRect.offset.y,
                                    static_cast<std::uint32_t>(shownRect.extent.width),
                                    static_cast<std::uint32_t>(shownRect.extent.height)};
    std::uint32_t n = 0;
    for (const ui_layer::PixelRect& r : ui_layer::headLockedPieces(shown, width, height)) {
        if (n == kMaxQuads - 3) {
            break;
        }
        const ui_layer::PanelPiece piece = ui_layer::panelPiece(r, shown, ui.size.width);
        XrCompositionLayerQuad q = ui;
        q.subImage.imageRect = toXrRect(r);
        q.pose.position.x += piece.centreX;
        q.pose.position.y += piece.centreY;
        q.size = {piece.width, piece.height};
        out[n++] = q;
    }
    if (n == 0) {
        return 0;
    }

    const LONGLONG now = qpcNow();
    const float dt = lastQpc_ ? std::min(kMaxFadeStep, static_cast<float>(qpcSeconds(now - lastQpc_))) : 0.0f;
    lastQpc_ = now;
    ui_layer::WristSettings ws = settings.wrist;
    // Under hand aim the UI copy leaves the centre of the target out (the game's crosshair and the ability
    // rings around it, presenter_ui.cpp), so an abilities quad would be empty: it is left out then.
    if (ws.abilities && controllers::weaponAimSpace() != XR_NULL_HANDLE) {
        ws.abilities = false;
        if (!loggedNoAbilities_) {
            loggedNoAbilities_ = true;
            EVR_LOG("ui: wrist HUD: hand aim masks the crosshair and the ability rings; no abilities quad");
        }
    }
    const bool left = controllers::dominantHand() == input::Hand::Right;
    const input::HandState& hand = left ? frame->left : frame->right;
    bool want = false;
    // The facing test in room space (hand and head both), the quads in the grip space.
    if (hand.gripValid && frame->head.poseValid && !controllers::forcedView()) {
        const float degrees = ui_layer::wristFacingDegrees(hand.gripPose, frame->head.pose, ws, left);
        const float gaze = ui_layer::wristGazeDegrees(hand.gripPose, frame->head.pose, ws, left);
        want = facing_.update(degrees, gaze, ws);
        lastDegrees_ = degrees;
        lastGaze_ = gaze;
    } else {
        facing_.reset(); // untracked, or a glory kill / cutscene owns the arms
    }
    const float alpha = fade_.update(want, dt, ws);
    ++frames_;
    logStats(settings);
    // Without the colour scale extension there is no fade: the quads switch at the ramp's midpoint.
    const bool visible = hand.gripValid && (colorScaleBias ? alpha > 0.0f : alpha >= 0.5f);
    if (!visible) {
        return n;
    }
    ++shownFrames_;
    if (!logged_) {
        logged_ = true;
        EVR_LOG("ui: wrist HUD shown for the first time (%s hand, facing %.0f deg, gaze %.0f deg)",
                left ? "left" : "right", lastDegrees_, lastGaze_);
    }
    // Laid out around an identity hand: the poses are then relative to the grip space.
    for (const ui_layer::WristQuad& w :
         ui_layer::layoutWristQuads(Pose::identity(), width, height, ws, left)) {
        if (n == kMaxQuads) {
            break;
        }
        XrCompositionLayerQuad q = ui;
        q.next = nullptr;
        q.space = grip;
        q.subImage.imageRect = toXrRect(w.rect);
        q.pose = toXrPose(w.pose);
        q.size = {w.width, w.height};
        if (colorScaleBias) {
            // The GUI is premultiplied: scaling all four channels fades it.
            XrCompositionLayerColorScaleBiasKHR& bias = bias_[n];
            bias = {XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
            bias.colorScale = {alpha, alpha, alpha, alpha};
            bias.colorBias = {0.0f, 0.0f, 0.0f, 0.0f};
            q.next = &bias;
        }
        out[n++] = q;
    }
    return n;
}

void WristHud::logStats(const ui_layer::UiSettings& settings) {
    if (settings.hud != ui_layer::HudMode::Wrist || frames_ == 0 || frames_ % 900 != 0) {
        return;
    }
    EVR_LOG("ui: wrist HUD: %llu frame(s) in wrist mode, %llu with the wrist shown; last facing %.0f deg, "
            "gaze %.0f deg; fade %s",
            static_cast<unsigned long long>(frames_), static_cast<unsigned long long>(shownFrames_),
            lastDegrees_, lastGaze_, colorScaleBias ? "on" : "off (no colour scale extension)");
}

} // namespace evr::vkcore
