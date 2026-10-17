#pragma once

// The hand HUDs on the XR worker (docs/VR_HANDS_HUD.md): the UI quad split into its head-locked pieces and
// the quads on the off hand's wrist (ETERNALVR_HUD=wrist) or above the gun in the weapon hand
// (ETERNALVR_HUD=weapon), and in every HUD mode the weapon wheel straight ahead while it is up
// (ui_layer/weapon_wheel.hpp); presenter_wrist.cpp. XR worker only.

#include "common/pose.hpp"
#include "features/input/controller_state.hpp"
#include "ui_layer/ui_settings.hpp"
#include "ui_layer/weapon_wheel.hpp"
#include "ui_layer/wrist_hud.hpp"

#include <windows.h>

#include <openxr/openxr.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace evr::vkcore {

class WristHud {
public:
    // The head-locked pieces (2 in practice, 5 with the wheel centred), the hand quads (3 on the wrist, 2 on
    // the gun) and the centred wheel that replace the UI quad. With the projection, the vignette, the reticle
    // and the fade a frame then has at most 13 layers (OpenXR guarantees 16).
    static constexpr std::uint32_t kMaxQuads = 10;

    // XR_KHR_composition_layer_color_scale_bias is enabled (the quads fade); set at instance creation.
    bool colorScaleBias = false;

    // In wrist or weapon mode, or while the weapon wheel is centred, the UI quad `ui` (showing part of a
    // `width` x `height` GUI target) split into its head-locked pieces, the hand quads and the wheel's quad,
    // written to `out` (at most kMaxQuads); 0 when the whole UI quad stays as it is (panel mode or no
    // controllers, and no wheel). `wheelInImage`: the frame of the GUI image on the quad had the weapon wheel
    // up (ViewRecord::weaponWheel). The quads written point into this object (the fade), so they are valid
    // until the next call.
    std::uint32_t fill(const ui_layer::UiSettings& settings,
                       std::uint32_t width,
                       std::uint32_t height,
                       const XrCompositionLayerQuad& ui,
                       bool wheelInImage,
                       XrCompositionLayerQuad* out);

private:
    // The hand quads after the `n` head-locked pieces in `out`; returns the new count.
    std::uint32_t addHandQuads(const ui_layer::UiSettings& settings,
                               std::uint32_t width,
                               std::uint32_t height,
                               const input::InputFrame& frame,
                               XrSpace space,
                               const XrCompositionLayerQuad& ui,
                               std::uint32_t n,
                               XrCompositionLayerQuad* out);
    // The centred wheel's starts and ends in the log (the first kLoggedChanges of them).
    void logWheel(const ui_layer::WheelCentring::Step& step,
                  const ui_layer::UiSettings& settings,
                  const std::optional<ui_layer::CentredWheel>& wheel,
                  std::uint32_t pieces);
    // The fade's target this frame: whether the hand faces the head (and updates the angles for the log).
    bool wristWanted(const input::InputFrame& frame, const ui_layer::WristSettings& ws, bool left);
    bool weaponWanted(const input::InputFrame& frame, const ui_layer::UiSettings& settings, bool left);
    // Appends `quads` (poses in `space`) to `out` from `n`, faded by `alpha`; returns the new count.
    std::uint32_t addQuads(const std::vector<ui_layer::WristQuad>& quads,
                           XrSpace space,
                           float alpha,
                           const XrCompositionLayerQuad& ui,
                           std::uint32_t n,
                           XrCompositionLayerQuad* out);
    void logStats(const ui_layer::UiSettings& settings);

    ui_layer::WristFacing facing_;
    ui_layer::WristFade fade_;
    LONGLONG lastQpc_ = 0;
    std::uint64_t frames_ = 0;      // frames in wrist or weapon mode
    std::uint64_t shownFrames_ = 0; // of those, with the hand quads shown
    bool logged_ = false;
    bool loggedFallback_ = false;
    bool loggedNoAbilities_ = false;
    float lastDegrees_ = 180.0f; // the last facing and gaze angles, for the log
    float lastGaze_ = 180.0f;
    bool lastWant_ = false; // the fade's last target, for the log of its changes
    std::uint32_t changesLogged_ = 0;
    // Weapon mode: the gun's frame in the weapon hand's aim space (the grip's position there), from the last
    // snapshot with both poses tracked.
    std::optional<Pose> weaponInAim_;
    std::array<XrCompositionLayerColorScaleBiasKHR, kMaxQuads> bias_{};
    // The weapon wheel straight ahead (every HUD mode).
    ui_layer::WheelCentring wheel_;
    double wheelSince_ = 0.0;       // when the wheel was last centred
    std::uint64_t wheelFrames_ = 0; // headset frames with the wheel centred, this time
    std::uint32_t wheelChangesLogged_ = 0;
};

} // namespace evr::vkcore
