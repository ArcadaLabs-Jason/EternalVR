// Head tracking: the game's camera hook and cutscene handling (docs/VR_HEAD_TRACKED.md). Head aim is in
// presenter_aim.cpp; the render latch and the view each present carries are in presenter_latch.cpp.

#include "vkcore/presenter_fov.hpp"
#include "vkcore/presenter_impl.hpp"

#include "vkcore/body_follow.hpp"
#include "vkcore/body_jump_watch.hpp"
#include "vkcore/camera_anim_hook.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/debug_commands.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/head_sweep.hpp"
#include "vkcore/menu_input.hpp"
#include "vkcore/menu_model_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/pose_guards.hpp"
#include "vkcore/reticle_depth.hpp"
#include "vkcore/stall_watch.hpp"
#include "xr_math/camera_anim.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace evr::vkcore {

// ---------------------------------------------------------------------------------------------------
// Head tracking: the game's camera hook

void XrPresenter::Impl::onGameView(std::byte* renderView, std::byte* player) {
    // Multiplayer guard (mp_guard.hpp): off, the game's view, aim and input stay as it made them for the
    // rest of the process, and the wall-climb cvars go back to the game's own values (climb_hook.cpp).
    if (!mp_guard::allowsGameTouch()) {
        if (!loggedGuardOff) {
            loggedGuardOff = true;
            EVR_LOG("head: the multiplayer guard is %s; the game's view and aim are no longer changed",
                    mp_policy::toString(mp_guard::state()));
            controllers::restoreClimbCvars("the multiplayer guard stopped game touches");
            controllers::restoreWheelSlowdown("the multiplayer guard stopped game touches");
            controllers::restoreZoomLook("the multiplayer guard stopped game touches");
            controllers::noteImmersiveCutscene(false); // the arms and the weapon FOV as in play
        }
        return;
    }
    // Cutscenes and glory kills on the flat screen, and the frames before tracking is ready, end here.
    if (flatScreenGameView(*this, renderView, player)) {
        return;
    }
    auto* fov = reinterpret_cast<float*>(renderView + render_view::kFovX);
    auto* origin = reinterpret_cast<float*>(renderView + render_view::kViewOrigin);
    auto* axis = reinterpret_cast<float*>(renderView + render_view::kViewAxis);
    const xr_math::IdViewAxis gameAxis{
        {axis[0], axis[1], axis[2]}, {axis[3], axis[4], axis[5]}, {axis[6], axis[7], axis[8]}};
    if (!xr_math::isOrthonormal(gameAxis, 1e-2f)) {
        ++gameViewsSkipped;
        if (!loggedBadAxis) {
            loggedBadAxis = true;
            EVR_LOG("head: the game's view axis is not orthonormal (%.3f %.3f %.3f / %.3f %.3f %.3f / %.3f "
                    "%.3f %.3f); frame left as the game made it",
                    axis[0], axis[1], axis[2], axis[3], axis[4], axis[5], axis[6], axis[7], axis[8]);
        }
        return;
    }
    // The hands animation's camera (camera_anim_hook.hpp): off the game's view for the body and head aim, and
    // (ETERNALVR_CAMERA_ANIMATIONS=1) back on top of the head below.
    const camera_anim::Frame anim = camera_anim::frame(player, gameAxis, cutscene, controllers::forcedView());
    pickup.frame(player, cutscene, menuOverGame(), gameAxis, {origin[0], origin[1], origin[2]},
                 settings.unitsPerMetre);
    ViewRecord record;
    record.gameOriginValid = true;
    record.gameOrigin = {origin[0], origin[1], origin[2]};
    record.gameYawDegrees = std::atan2(gameAxis.forward.y, gameAxis.forward.x) * 57.29578f;
    bool positionValid = false;
    std::optional<float> headAboveFloor;
    {
        std::shared_lock lock(spaceMutex);
        if (!trackingReady.load(std::memory_order_acquire)) {
            return;
        }
        record.poseTime = nextDisplayTime.load();
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        const XrResult r = xr.xrLocateSpace(viewSpace, localSpace, record.poseTime, &location);
        record.locatedQpc = qpcNow();
        constexpr XrSpaceLocationFlags needed = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (XR_FAILED(r) || (location.locationFlags & needed) != needed) {
            ++gameViewsSkipped;
            return;
        }
        record.pose = location.pose;
        positionValid = (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
        if (!positionValid) {
            record.pose.position = {0.0f, 0.0f, 0.0f};
        }
        if (floorSpace != XR_NULL_HANDLE) {
            XrSpaceLocation floor{XR_TYPE_SPACE_LOCATION};
            if (XR_SUCCEEDED(xr.xrLocateSpace(viewSpace, floorSpace, record.poseTime, &floor)) &&
                (floor.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                std::isfinite(floor.pose.position.y)) {
                headAboveFloor = floor.pose.position.y;
            }
        }
    }
    // Everything written into the game's view below derives from this pose: never from a non-finite one.
    const XrPosef& p = record.pose;
    if (!std::isfinite(p.orientation.x) || !std::isfinite(p.orientation.y) ||
        !std::isfinite(p.orientation.z) || !std::isfinite(p.orientation.w) || !std::isfinite(p.position.x) ||
        !std::isfinite(p.position.y) || !std::isfinite(p.position.z)) {
        ++gameViewsSkipped;
        return;
    }
    // A position no head can reach from the last good one is held, and that frame's floor reading (taken with
    // it) is not used (pose_guards.hpp): a jump never reaches body follow, the lean cap or the posture.
    if (Vec3 position{p.position.x, p.position.y, p.position.z};
        positionValid && pose_guards::check(pose_guards::Slot::Head, position, record.poseTime)) {
        record.pose.position = {position.x, position.y, position.z};
        headAboveFloor.reset();
    }
    Quat headXr = normalize(Quat{record.pose.orientation.x, record.pose.orientation.y,
                                 record.pose.orientation.z, record.pose.orientation.w});
    if (settings.swayPeriod > 0.0f) {
        headXr = normalize(xr_math::headSwayAround(settings.swayBaseYaw, settings.swayBasePitch,
                                                   settings.swayYaw, settings.swayPitch, settings.swayPeriod,
                                                   qpcSeconds(record.locatedQpc)) *
                           headXr);
    }
    record.pose.orientation = {headXr.x, headXr.y, headXr.z, headXr.w};
    // The compositor keeps the LOCAL pose; the game is given the head in room space (recenter, room-scale).
    RoomScale::Input roomIn;
    roomIn.localHead = {headXr, {p.position.x, p.position.y, p.position.z}};
    roomIn.positionValid = positionValid;
    roomIn.seconds = qpcSeconds(record.locatedQpc);
    roomIn.focused = sessionFocused.load(std::memory_order_relaxed);
    roomIn.headAboveFloor = headAboveFloor;
    roomIn.unitsPerMetre = settings.unitsPerMetre;
    // The game driving the view (the forced view is last frame's gate): body follow stops, and the head's
    // offset eases out so the camera stays on the game's animated eye.
    roomIn.cutscene = cutscene;
    roomIn.forcedView = controllers::forcedView();
    if (roomScaleSettings().follow.enabled) {
        // Body follow: where the body is now, and what stops it.
        roomIn.bodyOrigin =
            body_follow::playerOrigin(player, {origin[0], origin[1], origin[2]}, settings.unitsPerMetre);
        roomIn.menu = menu_input::suppressGameplay();
    }
    const RoomScale::Head roomHead = room.head(roomIn);
    Quat headGame = roomHead.game.orientation;
    Quat headId = xr_math::openXrToIdTech(headGame);
    controllers::setRoomFromLocal(roomHead.roomFromLocal);
    if (const auto weapon =
            controllers::beginGameView(record.poseTime, roomHead.room, player, cutscene, anim.active)) {
        const Pose& a = weapon->used;
        record.weaponAimValid = true;
        record.weaponAim = {{a.orientation.x, a.orientation.y, a.orientation.z, a.orientation.w},
                            {a.position.x, a.position.y, a.position.z}};
        const Quat& q = weapon->tracked.orientation;
        record.weaponAimTracked = {q.x, q.y, q.z, q.w};
        record.offHandValid = weapon->offValid;
        const Pose& g = weapon->offGrip;
        record.offHandGrip = {{g.orientation.x, g.orientation.y, g.orientation.z, g.orientation.w},
                              {g.position.x, g.position.y, g.position.z}};
        const Quat& o = weapon->offAim.orientation;
        record.offHandAim = {o.x, o.y, o.z, o.w};
        record.weaponAimTwoHanded = weapon->twoHanded;
    }
    // The weapon wheel in this frame, from the forced-view gate beginGameView just updated.
    record.weaponWheel = controllers::wheelView();
    rebaseAfterCutscene(headId); // the first frame after a cutscene shown around the player
    std::optional<xr_math::IdViewAxis> body;
    const std::uint64_t menuViews = aimMenuFrames; // head aim's menu views so far: did it hold this one?
    if (settings.headAim) {
        body = aimWithHead(player, pickup.followed(anim.gameAxis), headId); // a pickup's lone flick left out
    } else if (const std::optional<xr_math::IdAngles>& own = pickup.viewAngles()) {
        // View aim: the forced view's log and the trace get the player's own angles as under head aim, and
        // ETERNALVR_DEBUG_COMMANDS counts from the first view of play (no head aim check to wait for).
        camera_anim::noteForcedView(controllers::forcedView(), anim.gameAxis, *own);
        if (!cutscene && !roomIn.forcedView && !menuOverGame() && mp_guard::inPlayMap()) {
            markPlayerInMap();
        }
    }
    const bool headAimed = body.has_value();
    if (!body) {
        // The game's yaw (view aim, or head aim not on), with a pickup camera's flick left out.
        body = xr_math::yawOnly(pickup.withoutFlick(anim.gameAxis));
    }
    if (!body) {
        ++gameViewsSkipped;
        return;
    }
    if (const auto kept = keptBodyYaw(glory, pickup, headAimed, qpcSeconds(qpcNow()))) {
        body = xr_math::axisFromAngles({0.0f, *kept, 0.0f}); // a glory kill or pickup keeps its heading
    }
    // A cutscene shown around the player: its cuts keep the new shot's forward where the head looks.
    const float bodyYaw = xr_math::anglesFromAxis(*body).yaw;
    const float cutYaw =
        cutsceneBodyYaw(bodyYaw, anim.gameAxis, headId, renderView[render_view::kCameraCut] != std::byte{0});
    if (cutYaw != bodyYaw) {
        body = xr_math::axisFromAngles({0.0f, cutYaw, 0.0f});
    }
    glory.noteBody(xr_math::anglesFromAxis(*body).yaw);
    pickup.noteBody(xr_math::anglesFromAxis(*body).yaw, headAimed && aimBodyOwn, aimMenuFrames != menuViews);
    watchBodyYaw(xr_math::anglesFromAxis(*body).yaw, headAimed, roomIn.forcedView, cutscene, menuOverGame());
    // The game drives the view: last frame's gate, this frame's (beginGameView updated it), a kill's or a
    // pickup's camera. A sync's snap onto a demon is no teleport (comfort::TeleportWatch).
    const bool driven =
        roomIn.forcedView || controllers::forcedView() || glory.drivesView() || pickup.followsCamera();
    watchTeleport(player, qpcSeconds(qpcNow()), xr_math::anglesFromAxis(*body).yaw, driven, cutscene);
    if (camera_anim::tracing()) {
        camera_anim::noteTraceAim({xr_math::anglesFromAxis(*body).yaw, xr_math::headAngles(headId),
                                   aimState.injected, xr_math::viewHeadYaw(aimState), menuOverGame(),
                                   !cutscene && controllers::syncPickupActive(player), headAimed});
    }
    // Aim and the body above follow the head; the view and the eyes turn by the game's camera on top of it (a
    // pickup's turn or the hands animation's, pickup_view.hpp).
    if (const std::optional<Quat> turned =
            headWithGameCamera(pickup, anim, *body, headId, gameAxis, headAimed, roomIn.forcedView,
                               {origin[0], origin[1], origin[2]})) {
        headId = *turned;
        headGame = xr_math::idTechToOpenXr(headId);
    }
    const xr_math::IdViewAxis view = xr_math::composeHeadAxis(*body, headId);
    room.noteBody(player, body->forward, body->left);

    const float gameFovX = fov[0];
    const float gameFovY = fov[1];
    xr_math::GameFov used{gameFovX, gameFovY};
    if (const auto target = unpackGameFov(targetFov.load(std::memory_order_acquire));
        target && settings.setGameFov) {
        used = *target;
    }
    const auto xrFov = xr_math::fovFromGame(used);
    if (!xrFov) {
        ++gameViewsSkipped;
        return;
    }
    record.fov = {xrFov->angleLeft, xrFov->angleRight, xrFov->angleUp, xrFov->angleDown};

    Vec3 offset{};
    if (settings.headPosition) {
        offset = xr_math::headOffsetInWorld(*body, roomHead.offset.offset, settings.unitsPerMetre);
    }
    const Vec3 eye{origin[0], origin[1], origin[2]};
    glory.noteEye(eye, settings.unitsPerMetre);
    // A glory kill shown steady: the view also stays where the eye was before it (glory_view.hpp, T-180).
    const std::optional<Vec3> held = glory.steadyEye(qpcSeconds(qpcNow()));
    if (held) {
        offset = offset + (*held - eye);
    }
    // The head swept from the game's eye: its depth in geometry fades the view, and shots start from the
    // first contact (T-062). Not to a held place: the player stood there.
    std::optional<float> hit;
    if (roomScaleSettings().collision && !cutscene && !held &&
        length(offset) > 0.01f * settings.unitsPerMetre) {
        hit = sweepHead(player, eye, eye + offset,
                        length(offset) + roomscale::kLateHeadSlackMetres * settings.unitsPerMetre);
    }
    const roomscale::ClearanceStep clear = room.clearance(offset, hit, settings.unitsPerMetre);
    const std::array<float, 9> written{view.forward.x, view.forward.y, view.forward.z,
                                       view.left.x,    view.left.y,    view.left.z,
                                       view.up.x,      view.up.y,      view.up.z};
    if (!mp_guard::allowsGameTouch()) {
        return; // the guard went off during this frame: the view stays the game's
    }
    std::memcpy(axis, written.data(), sizeof(written));
    controllers::noteViewForward(view.forward.x, view.forward.y);
    render_view::moveViewOrigin(renderView, offset.x, offset.y, offset.z);
    fov[0] = used.fovX;
    fov[1] = used.fovY;
    // The head in LOCAL and in the world, for a menu's 3D model on the panel (menu_model_hook.hpp).
    menu_model::noteHead({{headXr, {p.position.x, p.position.y, p.position.z}},
                          {origin[0], origin[1], origin[2]},
                          view.forward,
                          view.left,
                          view.up,
                          settings.unitsPerMetre},
                         gameFovX);
    controllers::endGameView(renderView, player, *body, eye, clear.validOffset, settings.unitsPerMetre);
    if (controllers::pilotingDemon()) {
        controllers::notePilotView(*body, offset, view); // the demon's models stay on this view
    }
    if (settings.ui.reticle && record.weaponAimValid) {
        record.weaponAimHitMetres = reticleHitMetres(player, eye, settings.unitsPerMetre);
    }
    record.axis = written;
    record.cutscene = cutscene;
    record.cutsceneArms = controllers::cutsceneArmsHidden();
    if (settings.stereo.enabled) {
        prepareEyes(record, *body, headGame);
        if (settings.stereo.testWeaponFov > 0.0f) {
            // Experiment E3: an extreme weapon FOV shows whether the hands and gun use it.
            auto* weapon = reinterpret_cast<float*>(renderView + stereo_view_fields::kWeaponFovX);
            weapon[0] = settings.stereo.testWeaponFov;
            weapon[1] = settings.stereo.testWeaponFov;
        }
    }

    {
        std::lock_guard lock(historyMutex);
        record.seq = ++latestSeq;
        history[record.seq % kHistorySize] = record;
    }
    ++gameViews;
    gameTicks.fetch_add(1, std::memory_order_relaxed);
    stall_watch::onGameTick(); // stalls in play are told from loading screens and menus by the ticks

    if (!loggedFirstGameView) {
        loggedFirstGameView = true;
        EVR_LOG("head: first head-tracked game view (renderView_t %p): game axis fwd (%.3f %.3f %.3f) left "
                "(%.3f %.3f "
                "%.3f) up (%.3f %.3f %.3f), origin (%.2f %.2f %.2f), fov %.2f x %.2f",
                static_cast<void*>(renderView), gameAxis.forward.x, gameAxis.forward.y, gameAxis.forward.z,
                gameAxis.left.x, gameAxis.left.y, gameAxis.left.z, gameAxis.up.x, gameAxis.up.y,
                gameAxis.up.z, origin[0] - offset.x, origin[1] - offset.y, origin[2] - offset.z, gameFovX,
                gameFovY);
        EVR_LOG(
            "head: head (LOCAL) orientation (%.3f %.3f %.3f %.3f) position (%.3f %.3f %.3f); new fwd (%.3f "
            "%.3f "
            "%.3f) up (%.3f %.3f %.3f), fov %.2f x %.2f, offset (%.3f %.3f %.3f) at %.2f unit(s) per metre",
            headXr.x, headXr.y, headXr.z, headXr.w, record.pose.position.x, record.pose.position.y,
            record.pose.position.z, view.forward.x, view.forward.y, view.forward.z, view.up.x, view.up.y,
            view.up.z, used.fovX, used.fovY, offset.x, offset.y, offset.z, settings.unitsPerMetre);
    }
    const ULONGLONG ticks = GetTickCount64();
    if (ticks - lastHookStatsTicks >= 10000) {
        lastHookStatsTicks = ticks;
        EVR_LOG(
            "head: %llu head-tracked game view(s), %llu left unchanged; game fov %.2f x %.2f -> %.2f x %.2f; "
            "head fwd (%.2f %.2f %.2f)",
            static_cast<unsigned long long>(gameViews), static_cast<unsigned long long>(gameViewsSkipped),
            gameFovX, gameFovY, used.fovX, used.fovY, view.forward.x, view.forward.y, view.forward.z);
    }
}

} // namespace evr::vkcore
