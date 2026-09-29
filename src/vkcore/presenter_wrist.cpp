// The hand HUDs (docs/VR_HANDS_HUD.md).
//
// The wrist HUD (ETERNALVR_HUD=wrist): the captured GUI target's corner blocks (health, armor, ammo,
// equipment) and the crosshair's ability rings on small quads on the inside of the off hand's wrist, in its
// grip space, shown while that side faces the head and faded in and out.
//
// The weapon HUD (ETERNALVR_HUD=weapon): the ammo block (ammo, equipment, flame belch; health and armor too
// with ETERNALVR_WEAPON_HUD_VITALS=1) on a small quad above the back of the gun, in the weapon hand's aim
// space at the grip's position (the viewmodel's frame), shown while it faces the head, faded the same way.
//
// Either way the rest of the 16:9 band the UI quad shows (subtitles, prompts, boss and encounter bars,
// markers, damage) stays on the head-locked quad, cut into pieces around the blocks that moved. Every quad
// shows a sub-rectangle of the same UI swapchain image, so there is no extra copy.

#include "vkcore/presenter_wrist.hpp"

#include "ui_layer/weapon_hud.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/log.hpp"
#include "vkcore/presenter_types.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

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
// How many show / hide changes are logged.
constexpr std::uint32_t kLoggedChanges = 40;

} // namespace

std::uint32_t WristHud::fill(const ui_layer::UiSettings& settings,
                             std::uint32_t width,
                             std::uint32_t height,
                             const XrCompositionLayerQuad& ui,
                             XrCompositionLayerQuad* out) {
    if (settings.hud == ui_layer::HudMode::Panel) {
        return 0;
    }
    const bool weapon = settings.hud == ui_layer::HudMode::Weapon;
    // Without controllers there is no hand: the whole HUD stays on the head-locked quad.
    const std::optional<input::InputFrame> frame = controllers::latestFrame();
    const XrSpace space = weapon ? controllers::weaponHandAimSpace() : controllers::offHandGripSpace();
    if (!frame || space == XR_NULL_HANDLE) {
        if (!loggedFallback_) {
            loggedFallback_ = true;
            EVR_LOG("ui: %s HUD: no controllers (or no %s space); the HUD stays on the panel",
                    ui_layer::hudModeName(settings.hud), weapon ? "weapon-hand aim" : "off-hand grip");
        }
        return 0;
    }
    // The part of the target the UI quad shows (the 16:9 band, ETERNALVR_UI_CROP), in pixels.
    const XrRect2Di& shownRect = ui.subImage.imageRect;
    const ui_layer::PixelRect shown{shownRect.offset.x, shownRect.offset.y,
                                    static_cast<std::uint32_t>(shownRect.extent.width),
                                    static_cast<std::uint32_t>(shownRect.extent.height)};
    const std::vector<ui_layer::WristBlock> moved =
        weapon
            ? ui_layer::weaponMovedBlocks(settings.weapon)
            : std::vector<ui_layer::WristBlock>{ui_layer::WristBlock::Vitals, ui_layer::WristBlock::Weapon};
    std::uint32_t n = 0;
    for (const ui_layer::PixelRect& r : ui_layer::headLockedPieces(shown, width, height, moved)) {
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
    // The wrist is on the off hand, the gun in the weapon hand (the dominant hand).
    const bool rightWeapon = controllers::dominantHand() == input::Hand::Right;
    const bool left = weapon ? !rightWeapon : rightWeapon;
    const input::HandState& hand = left ? frame->left : frame->right;
    ui_layer::WristSettings ws =
        weapon ? ui_layer::weaponShowSettings(settings.weapon, settings.wrist) : settings.wrist;
    // Under hand aim the UI copy leaves the centre of the target out (the game's crosshair and the ability
    // rings around it, presenter_ui.cpp), so an abilities quad would be empty: it is left out then.
    if (!weapon && ws.abilities && controllers::weaponAimSpace() != XR_NULL_HANDLE) {
        ws.abilities = false;
        if (!loggedNoAbilities_) {
            loggedNoAbilities_ = true;
            EVR_LOG("ui: wrist HUD: hand aim masks the crosshair and the ability rings; no abilities quad");
        }
    }
    // Hidden while a glory kill / cutscene owns the arms (the gun leaves the hand).
    const bool want = !controllers::forcedView() &&
                      (weapon ? weaponWanted(*frame, settings, left) : wristWanted(*frame, ws, left));
    if (!want) {
        facing_.reset();
    }
    if (want != lastWant_) {
        lastWant_ = want;
        // The first changes, for rig runs that script the hands.
        if (changesLogged_ < kLoggedChanges) {
            ++changesLogged_;
            EVR_LOG("ui: %s HUD %s (facing %.0f deg, gaze %.0f deg%s)", ui_layer::hudModeName(settings.hud),
                    want ? "showing" : "hiding", lastDegrees_, lastGaze_,
                    controllers::forcedView() ? ", forced view" : "");
        }
    }
    const float alpha = fade_.update(want, dt, ws);
    ++frames_;
    logStats(settings);
    // Without the colour scale extension there is no fade: the quads switch at the ramp's midpoint.
    const bool tracked = weapon ? hand.poseValid : hand.gripValid;
    const bool visible = tracked && (colorScaleBias ? alpha > 0.0f : alpha >= 0.5f);
    if (!visible) {
        return n;
    }
    ++shownFrames_;
    if (!logged_) {
        logged_ = true;
        EVR_LOG("ui: %s HUD shown for the first time (%s hand, facing %.0f deg, gaze %.0f deg)",
                ui_layer::hudModeName(settings.hud), left ? "left" : "right", lastDegrees_, lastGaze_);
    }
    // Laid out around the hand's own space (identity for the grip; the gun's frame in the aim space): the
    // poses are then relative to it and the runtime places them at display time.
    const std::vector<ui_layer::WristQuad> quads =
        weapon ? ui_layer::layoutWeaponQuads(weaponInAim_.value_or(Pose::identity()), width, height,
                                             settings.weapon, left)
               : ui_layer::layoutWristQuads(Pose::identity(), width, height, ws, left);
    return addQuads(quads, space, alpha, ui, n, out);
}

bool WristHud::wristWanted(const input::InputFrame& frame, const ui_layer::WristSettings& ws, bool left) {
    const input::HandState& hand = left ? frame.left : frame.right;
    // The facing test in room space (hand and head both), the quads in the grip space.
    if (!hand.gripValid || !frame.head.poseValid) {
        return false;
    }
    lastDegrees_ = ui_layer::wristFacingDegrees(hand.gripPose, frame.head.pose, ws, left);
    lastGaze_ = ui_layer::wristGazeDegrees(hand.gripPose, frame.head.pose, ws, left);
    return facing_.update(lastDegrees_, lastGaze_, ws);
}

bool WristHud::weaponWanted(const input::InputFrame& frame, const ui_layer::UiSettings& settings, bool left) {
    const input::HandState& hand = left ? frame.left : frame.right;
    if (!hand.poseValid || !frame.head.poseValid) {
        return false;
    }
    // The gun's frame (grip position, aim orientation) in room space for the facing test, and relative to
    // the aim for the quads. Both poses come from one snapshot, so their offset is the controller's own.
    const Pose gun = ui_layer::weaponFrame(hand.aimPose, hand.gripPose, hand.gripValid);
    if (hand.gripValid || !weaponInAim_) {
        weaponInAim_ = compose(inverse(hand.aimPose), gun);
    }
    lastDegrees_ = ui_layer::weaponFacingDegrees(gun, frame.head.pose, settings.weapon, left);
    lastGaze_ = 0.0f; // no gaze test on the gun
    return facing_.update(lastDegrees_, lastGaze_,
                          ui_layer::weaponShowSettings(settings.weapon, settings.wrist));
}

std::uint32_t WristHud::addQuads(const std::vector<ui_layer::WristQuad>& quads,
                                 XrSpace space,
                                 float alpha,
                                 const XrCompositionLayerQuad& ui,
                                 std::uint32_t n,
                                 XrCompositionLayerQuad* out) {
    for (const ui_layer::WristQuad& w : quads) {
        if (n == kMaxQuads) {
            break;
        }
        XrCompositionLayerQuad q = ui;
        q.next = nullptr;
        q.space = space;
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
    if (settings.hud == ui_layer::HudMode::Panel || frames_ == 0 || frames_ % 900 != 0) {
        return;
    }
    const char* mode = ui_layer::hudModeName(settings.hud);
    EVR_LOG("ui: %s HUD: %llu frame(s) in %s mode, %llu with the %s shown; last facing %.0f deg, gaze %.0f "
            "deg; fade %s",
            mode, static_cast<unsigned long long>(frames_), mode,
            static_cast<unsigned long long>(shownFrames_),
            settings.hud == ui_layer::HudMode::Weapon ? "gun's panel" : "wrist", lastDegrees_, lastGaze_,
            colorScaleBias ? "on" : "off (no colour scale extension)");
}

} // namespace evr::vkcore
