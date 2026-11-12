#pragma once

// A pickup animation's camera in the headset (ETERNALVR_PICKUP_CAMERA; the options are in
// features/comfort/pickup_camera.hpp, the rig's measurement in docs/rig-findings/camera-animations.md section
// 6). The camera hook calls frame() once per head-tracked game view before head aim, and builds the view with
// the turn on top of the head (headWithGameCamera). Camera hook thread only.

#include "common/quat.hpp"
#include "common/vector.hpp"
#include "features/comfort/glory_kill.hpp"
#include "features/comfort/pickup_camera.hpp"
#include "vkcore/camera_anim_hook.hpp"
#include "xr_math/camera_turn.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <cstddef>
#include <optional>

namespace evr::vkcore {

class PickupView {
public:
    explicit PickupView(comfort::PickupCamera camera = comfort::PickupCamera::Still);

    [[nodiscard]] comfort::PickupCamera camera() const { return camera_; }

    // Each head-tracked game view, before head aim: whether a pickup's sync animation runs for `player` (the
    // view's object), with the forced-view gate, whether a cutscene plays around the player (no pickup is
    // played then: its camera is the cutscene's), whether a menu is up (the game paused) and the game's
    // camera. Under Still nothing is tracked and the clock is not read.
    void frame(const std::byte* player, bool cutscene, bool menu, const xr_math::IdViewAxis& gameAxis);

    // Play: the turn that goes on top of the head this game view (xr_math::CameraTurn: the camera's motion
    // since the pickup took the view, then easing out after it); nullopt when there is none. While there is
    // one, head aim keeps the player's own heading (no scripted-camera yaw follow, also under a menu).
    [[nodiscard]] const std::optional<xr_math::IdAngles>& turn() const { return turn_; }

private:
    comfort::PickupCamera camera_;
    comfort::GloryEpisode episode_;
    xr_math::CameraTurn follow_;
    std::optional<xr_math::IdAngles> turn_;
    unsigned long long frames_ = 0; // game views of the running pickup
};

// The head (id Tech axes, in the body frame) the view and the eyes are built from this game view with the
// game's camera on top of it: a pickup's turn (PickupView::turn), else the hands animation's (`anim`,
// ETERNALVR_CAMERA_ANIMATIONS=1); nullopt: the head as it is. Aim and the body follow the head without it.
// `headAimed`: head aim gave the body (the player's own heading); without it the body is the game's yaw,
// which already holds the camera's, so a pickup's turn goes on without its yaw (xr_math::turnOnBody). Also
// traces the view written (camera_anim::traceView): the game's eye, before the head's room offset.
std::optional<Quat> headWithGameCamera(const PickupView& pickup,
                                       const camera_anim::Frame& anim,
                                       const xr_math::IdViewAxis& body,
                                       Quat headInIdTech,
                                       const xr_math::IdViewAxis& gameAxis,
                                       bool headAimed,
                                       bool forcedView,
                                       const Vec3& eye);

} // namespace evr::vkcore
