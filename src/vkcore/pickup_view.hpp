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
#include "vkcore/glory_view.hpp"
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
    // played then: its camera is the cutscene's), whether a menu is up (the game paused), the game's camera
    // and its eye (world units, with the world scale). Under Still only the camera's heading from the
    // player's view angles is tracked during a pickup (followed()).
    void frame(const std::byte* player,
               bool cutscene,
               bool menu,
               const xr_math::IdViewAxis& gameAxis,
               const Vec3& eye,
               float unitsPerMetre);

    // Play: the turn that goes on top of the head this game view (xr_math::CameraTurn: the camera's motion
    // since the pickup took the view, then easing out after it); nullopt when there is none. While there is
    // one, head aim keeps the player's own heading (no scripted-camera yaw follow, also under a menu).
    [[nodiscard]] const std::optional<xr_math::IdAngles>& turn() const { return turn_; }

    // Still: the game's camera as head aim's 15-degree follow sees it in this game view: during a pickup a
    // flick of its heading is left out (xr_math::CameraHeadingHold: the axis turned back to the heading it
    // had from the player's view angles before the flick); otherwise `gameAxis` itself.
    [[nodiscard]] xr_math::IdViewAxis followed(const xr_math::IdViewAxis& gameAxis) const;
    // Play: whether this game view's forced view (`forcedNow`, the gate after its update) is the game's own
    // of the set that ended the last pickup (comfort::EndSetWindow).
    [[nodiscard]] bool endSetView(bool forcedNow, double seconds) const;

    // Play: the heading the body keeps while the pickup runs (comfort::PickupHeading: the player's own before
    // it, so the game's turn of the player's angles to its camera as it ends does not turn the view); after
    // it, for a short window, the heading head aim turns the game's aim back to. nullopt otherwise.
    [[nodiscard]] std::optional<float> heldYaw() const { return heading_.held(); }
    [[nodiscard]] std::optional<float> restoreYaw(double seconds) const { return heading_.restore(seconds); }
    // Each head-tracked game view, after the body is built: its yaw, and whether it is the player's own
    // heading (head aim's step gave it: not a camera the game drives, a forced view or a menu's hold).
    void noteBody(float yaw, bool own) { heading_.noteBody(yaw, own); }
    // Head aim turned the game's aim back by `degrees` to restoreYaw (logged for the first pickups).
    void noteRestored(float degrees);

private:
    void
    stillFrame(const std::byte* player, bool pickup, const xr_math::IdViewAxis& gameAxis, double seconds);

    comfort::PickupCamera camera_;
    comfort::GloryEpisode episode_;
    comfort::PickupHeading heading_;
    bool loggedRestore_ = false;
    xr_math::CameraTurn follow_;
    std::optional<xr_math::IdAngles> turn_;
    xr_math::CameraHeadingHold hold_;
    float heldTurn_ = 0.0f;        // Still: the yaw followed() turns the game's camera by this view (degrees)
    unsigned long long holds_ = 0; // Still: holds started (logged for the first)
    comfort::EndSetWindow endSet_;
    unsigned long long frames_ = 0; // game views of the running pickup
};

// The body yaw this game view is built on when a glory kill shown steady or a pickup's camera played on top
// of the head keeps its heading (`headAimed`: head aim gave the body, the player's own heading); nullopt: the
// body as head aim or the game gave it. The game's own forced view of the set that ends a pickup without a
// forced view of its own (the Sentinel Crystal's; PickupView::endSetView) keeps the restore heading too
// (comfort::keptHeading).
std::optional<float>
keptBodyYaw(const GloryKills& glory, const PickupView& pickup, bool headAimed, double seconds);

// Just after a glory kill shown steady or such a pickup: the heading head aim turns the game's aim back to
// (the view never turned away from it, while the game's aim ended where the kill or the pickup's camera left
// it); nullopt otherwise. noteTurnedBack logs a turn with the one it came from.
std::optional<float> turnBackYaw(const GloryKills& glory, const PickupView& pickup, double seconds);
void noteTurnedBack(GloryKills& glory, PickupView& pickup, double seconds, float degrees);

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
