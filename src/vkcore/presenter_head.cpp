// Head tracking: the game's camera hook, head aim and cutscene handling (docs/VR_HEAD_TRACKED.md). The render
// latch and the view each present carries are in presenter_latch.cpp.

#include "vkcore/presenter_fov.hpp"
#include "vkcore/presenter_impl.hpp"

#include "vkcore/body_follow.hpp"
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

namespace {

// While a glory kill is shown as a fade, the camera hook holds the view black this far ahead, each frame.
constexpr double kGloryBlackRefreshSeconds = 0.1;

// The body yaw turning more than 90 degrees in one game frame (a stick turn never does): logged with what
// drove the view, to find the rare half turn seen once in a seated glory kill (2026-09-28; camera hook only).
struct BodyJumpWatch {
    std::optional<float> lastYaw;
    std::uint64_t jumps = 0;
};
BodyJumpWatch g_bodyJump;

void watchBodyYaw(float yaw, bool headAimed, bool forcedView, bool cutscene, bool menuUp) {
    if (g_bodyJump.lastYaw) {
        const float turn = xr_math::normalize180(yaw - *g_bodyJump.lastYaw);
        if (std::fabs(turn) > 90.0f && ++g_bodyJump.jumps <= 20) {
            EVR_LOG("aim: the body turned %.1f deg in one game frame (%.1f -> %.1f); %s, forced view %s, "
                    "cutscene %s, menu %s (jump %llu)",
                    turn, *g_bodyJump.lastYaw, yaw, headAimed ? "head aim" : "the game's yaw",
                    forcedView ? "yes" : "no", cutscene ? "yes" : "no", menuUp ? "yes" : "no",
                    static_cast<unsigned long long>(g_bodyJump.jumps));
        }
    }
    g_bodyJump.lastYaw = yaw;
}

} // namespace

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
            controllers::noteImmersiveCutscene(false); // the arms and the weapon FOV as in play
        }
        return;
    }
    trackCutscene(renderView[render_view::kInCutscene] != std::byte{0});
    cutscene_eyes::noteGameView(renderView, cutscene);
    // Glory kills (glory_view.hpp): on the flat screen, faded out, or with a steady heading (below).
    const double now = qpcSeconds(qpcNow());
    const bool menu = menuUp.load(std::memory_order_relaxed);
    if (glory.frame(player, controllers::forcedView(), menu, now).started && glory.onScreen()) {
        replaceScreen.store(true, std::memory_order_release);
    }
    if (glory.black()) {
        room.holdBlack(now + kGloryBlackRefreshSeconds);
    }
    const bool flatScreen = (cutscene && settings.cutsceneCinema) || glory.onScreen();
    cinemaView.onGameView(reinterpret_cast<float*>(renderView + render_view::kFovX), flatScreen,
                          settings.cinemaAspect);
    if (!trackingReady.load(std::memory_order_acquire)) {
        return;
    }
    if (flatScreen) {
        // The cutscene (or glory kill) keeps the game's camera and view; without a head-tracked view its
        // frames go to the flat screen (kViewStaleSeconds, or at once for a glory kill: GloryKills::flat),
        // placed in front of the head when it started.
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
        const Quat sway = xr_math::headTurn(settings.swayBaseYaw, 0.0f) *
                          xr_math::headSway(settings.swayYaw, settings.swayPitch, settings.swayPeriod,
                                            qpcSeconds(record.locatedQpc));
        headXr = normalize(sway * headXr);
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
    }
    rebaseAfterCutscene(headId); // the first frame after a cutscene shown around the player
    std::optional<xr_math::IdViewAxis> body;
    if (settings.headAim) {
        body = aimWithHead(player, anim.gameAxis, headId);
    }
    const bool headAimed = body.has_value();
    if (!body) {
        body = xr_math::yawOnly(anim.gameAxis);
    }
    if (!body) {
        ++gameViewsSkipped;
        return;
    }
    if (const std::optional<float> steady = glory.steadyYaw()) {
        // A glory kill shown steady: the view stays on the kill's eye with the heading it had before it.
        body = xr_math::axisFromAngles({0.0f, *steady, 0.0f});
    }
    // A cutscene shown around the player: its cuts keep the new shot's forward where the head looks.
    const float bodyYaw = xr_math::anglesFromAxis(*body).yaw;
    const float cutYaw =
        cutsceneBodyYaw(bodyYaw, anim.gameAxis, headId, renderView[render_view::kCameraCut] != std::byte{0});
    if (cutYaw != bodyYaw) {
        body = xr_math::axisFromAngles({0.0f, cutYaw, 0.0f});
    }
    glory.noteBody(xr_math::anglesFromAxis(*body).yaw);
    watchBodyYaw(xr_math::anglesFromAxis(*body).yaw, headAimed, roomIn.forcedView, cutscene,
                 menuUp.load(std::memory_order_relaxed));
    if (anim.active) {
        // Aim and the body above follow the head; the view and the eyes turn by the animation on top of it.
        headId = xr_math::headWithCameraAnim(*body, headId, anim.applied);
        headGame = xr_math::idTechToOpenXr(headId);
    }
    const xr_math::IdViewAxis view = xr_math::composeHeadAxis(*body, headId);
    room.noteBody(body->forward, body->left);

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
    // The head swept from the game's eye: its depth in geometry fades the view, and shots start from the
    // first contact (T-062).
    const Vec3 eye{origin[0], origin[1], origin[2]};
    std::optional<float> hit;
    if (roomScaleSettings().collision && !cutscene && length(offset) > 0.01f * settings.unitsPerMetre) {
        hit = sweepHead(player, eye, eye + offset, 0.16f * settings.unitsPerMetre);
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

std::optional<xr_math::IdViewAxis>
XrPresenter::Impl::aimWithHead(std::byte* player, const xr_math::IdViewAxis& gameAxis, Quat headInIdTech) {
    if (aimPhase == AimPhase::Off) {
        const ULONGLONG ticks = GetTickCount64();
        if (ticks - lastAimStatsTicks >= 10000) {
            lastAimStatsTicks = ticks;
            if (aimCheck.gaveUp()) {
                EVR_LOG("aim: head aim off for this session after %d tries of the view angles check; the "
                        "game keeps its own aim",
                        aimCheck.tryNumber());
            } else {
                EVR_LOG("aim: head aim off for this session (the player layout was not found); the game "
                        "keeps its own aim");
            }
        }
        return std::nullopt;
    }
    if (aimPhase == AimPhase::Unchecked) {
        aimPhase = playerAim.init() ? AimPhase::Verifying : AimPhase::Off;
        lastAimStatsTicks = GetTickCount64(); // the first statistics line comes 10 s on
        if (aimPhase == AimPhase::Off) {
            return std::nullopt;
        }
    }
    if (!playerAim.isPlayer(player)) {
        if (!loggedNotPlayer) {
            loggedNotPlayer = true;
            const std::byte* vtable = nullptr;
            if (player) {
                std::memcpy(&vtable, player, sizeof(vtable));
            }
            EVR_LOG(
                "aim: the view's object %p (vtable %p) is not the idPlayer; this view keeps the game's aim",
                static_cast<void*>(player), static_cast<const void*>(vtable));
        }
        return std::nullopt;
    }
    if (controllers::pilotingDemon()) {
        // Piloting a demon (demon_view.hpp): the idPlayer's own angles stay where the Slayer stood, so
        // nothing is written to them. The demon aims where the head looks, or under the demon's hand aim
        // where the weapon hand points (demon_aim.cpp): the view is built on the body yaw it aimed from;
        // without it, on the demon's camera yaw.
        ++aimCameraFrames;
        const xr_math::IdAngles aim = controllers::demonAimAngles(xr_math::headAngles(headInIdTech));
        controllers::notePilotAim(aim.pitch, aim.yaw);
        if (const std::optional<controllers::PilotAim> piloted = controllers::pilotAim()) {
            return xr_math::axisFromAngles({0.0f, piloted->bodyYaw, 0.0f});
        }
        return std::nullopt;
    }
    const PlayerAim::Sample sample = playerAim.read(player);
    if (!xr_math::plausible(sample.view) || !xr_math::plausible(sample.delta) ||
        !xr_math::plausible(sample.stateDelta) || !xr_math::plausible(sample.command)) {
        // Not a player state this layout describes (or one being torn down); never write from it.
        ++aimCameraFrames;
        return std::nullopt;
    }
    // What moved the camera in a forced view (a pickup, a glory kill): logged when it ends.
    camera_anim::noteForcedView(controllers::forcedView(), gameAxis, sample.view);

    // Verification (xr_math/aim_check.hpp): the game's view angles must be its command angles plus one of
    // the two deltas. Frames the game drives are not counted, and a failed try runs again later.
    if (aimPhase == AimPhase::Verifying) {
        using Event = xr_math::AimCheck::Event;
        constexpr int kTries = xr_math::AimCheck::kRetries + 1;
        // The weapon wheel too: the game skips its view update while it is up.
        const bool forced = controllers::forcedView() || controllers::wheelView();
        const bool menu = menuUp.load(std::memory_order_relaxed);
        const xr_math::AimCheck::Step check = aimCheck.update(sample.view, sample.command, sample.delta,
                                                              sample.stateDelta, cutscene || forced || menu);
        const int tryNumber = aimCheck.tryNumber();
        const bool mismatch = check.counted && !check.physicsMatch && !check.stateMatch;
        if (check.counted && (aimCheck.checks() <= 3 || (mismatch && aimCheck.mismatches() <= 10))) {
            EVR_LOG("aim: try %d check %d: view (%.2f %.2f) command (%.2f %.2f) delta (%.2f %.2f) state "
                    "delta (%.2f %.2f)%s",
                    tryNumber, aimCheck.checks(), sample.view.pitch, sample.view.yaw, sample.command.pitch,
                    sample.command.yaw, sample.delta.pitch, sample.delta.yaw, sample.stateDelta.pitch,
                    sample.stateDelta.yaw, mismatch ? "; matches neither delta" : "");
        } else if (check.event == Event::Skipped && aimCheck.skipped() == 1) {
            EVR_LOG("aim: try %d paused after %d check(s) while the game drives the view (cutscene %s, "
                    "forced view %s, menu %s); those frames are not counted",
                    tryNumber, aimCheck.checks(), cutscene ? "yes" : "no", forced ? "yes" : "no",
                    menu ? "yes" : "no");
        } else if (check.event == Event::Retry) {
            EVR_LOG("aim: checking the view angles again (try %d of %d) after %d frames of the player's own "
                    "view",
                    tryNumber, kTries, xr_math::AimCheck::kSettleFrames * (tryNumber - 1));
        }
        const ULONGLONG ticks = GetTickCount64();
        const bool ended = check.event == Event::Passed || check.event == Event::GaveUp;
        if (!ended && ticks - lastAimStatsTicks >= 10000) {
            lastAimStatsTicks = ticks;
            EVR_LOG("aim: head aim not on yet: try %d of %d, %d frame(s) checked, %d not counted%s",
                    tryNumber, kTries, aimCheck.checks(), aimCheck.skipped(),
                    check.event == Event::Waiting ? "; the try failed, waiting for the player's view" : "");
        }
        if (check.event == Event::Failed || check.event == Event::GaveUp) {
            EVR_LOG("aim: view angles = command + delta held in %d/%d (physics) and %d/%d (state) frames, %d "
                    "frame(s) in a cutscene, forced view or menu not counted (try %d of %d); %s",
                    aimCheck.physicsMatches(), aimCheck.checks(), aimCheck.stateMatches(), aimCheck.checks(),
                    aimCheck.skipped(), tryNumber, kTries,
                    check.event == Event::GaveUp ? "head aim off for this session, the game keeps its own aim"
                                                 : "checking again once the player has their own view");
            if (check.event == Event::GaveUp) {
                aimPhase = AimPhase::Off;
            }
        }
        if (check.event != Event::Passed) {
            return std::nullopt;
        }
        aimField = aimCheck.physics() ? PlayerAim::DeltaField::Physics : PlayerAim::DeltaField::State;
        aimPhase = AimPhase::Active;
        markPlayerInMap(); // ETERNALVR_DEBUG_COMMANDS counts from here
        EVR_LOG("aim: head aim on through the %s deltaViewAngles (%d/%d frames matched, %d not counted; try "
                "%d of %d)",
                aimCheck.physics() ? "physics" : "state",
                aimCheck.physics() ? aimCheck.physicsMatches() : aimCheck.stateMatches(), aimCheck.checks(),
                aimCheck.skipped(), tryNumber, kTries);
    }

    // A menu or popup over the game (the pause menu, the Dossier, a tutorial popup): the game holds its view
    // and keeps rewriting the delta, and the hand that aims points at the panel. Nothing is written and the
    // body keeps the yaw it had when the menu came up, so the world stays put while the head looks around
    // (the owner's Quest 3, 2026-09-27: the view seemed to turn the wrong way in a tutorial popup).
    if (menuUp.load(std::memory_order_relaxed) && aimMenuBody) {
        if (aimMenuFrames++ == 0) {
            EVR_LOG(
                "aim: a menu is up: head aim holds the body yaw at %.1f and writes nothing until it closes",
                *aimMenuBody);
        }
        controllers::noteAimPaused();
        return xr_math::axisFromAngles({0.0f, *aimMenuBody, 0.0f});
    }

    // Only while the view is the player's own (not a cinematic or scripted camera).
    const xr_math::IdViewAxis angleAxis = xr_math::axisFromAngles({sample.view.pitch, sample.view.yaw, 0.0f});
    // In a cutscene shown around the player, with its cuts re-based, every frame is the camera's: a camera
    // within 15 degrees of the player's angles does not turn head aim back on mid-cutscene.
    const bool cutsceneCamera = cutscene && !settings.cutsceneCinema && settings.cutsceneCutRebase;
    if (cutsceneCamera || dot(angleAxis.forward, gameAxis.forward) < 0.966f) { // 15 degrees
        // A scripted camera (a glory kill's): nothing is written, and the view faces the camera's heading
        // while the head is where it was when the camera took over (not the camera plus the head's whole
        // yaw in the room, which faces backwards for a player turned round in the room).
        ++aimCameraFrames;
        controllers::noteAimPaused();
        const std::optional<xr_math::IdViewAxis> camera = xr_math::yawOnly(gameAxis);
        if (!camera) {
            return std::nullopt;
        }
        const float cameraYaw = xr_math::anglesFromAxis(*camera).yaw;
        return xr_math::axisFromAngles(
            {0.0f, xr_math::drivenBodyYaw(aimState, cameraYaw, 0.0f, false), 0.0f});
    }
    const xr_math::IdAngles current =
        aimField == PlayerAim::DeltaField::Physics ? sample.delta : sample.stateDelta;
    const bool rewrote =
        aimWritten && (std::fabs(xr_math::normalize180(current.yaw - aimLastDelta.yaw)) > 0.01f ||
                       std::fabs(xr_math::normalize180(current.pitch - aimLastDelta.pitch)) > 0.01f);
    if (rewrote) {
        ++aimRewrites;
    }
    // The head's angles, or the weapon hand's under hand aim (nullopt: hand aim yields to a forced view).
    const std::optional<xr_math::IdAngles> target = controllers::aimAngles(xr_math::headAngles(headInIdTech));
    if (!target) {
        // Nothing is written. The game's yaw holds the aim injected before the forced view (a value the game
        // set itself re-aims that view), so the body is its yaw without that: the view faces where the game
        // points it, and the camera does not jump by the head's or the hand's yaw.
        ++aimCameraFrames;
        return xr_math::axisFromAngles(
            {0.0f, xr_math::drivenBodyYaw(aimState, sample.command.yaw + current.yaw, current.yaw, rewrote),
             0.0f});
    }
    // The view keeps following the other delta, not the one head aim writes: head aim moves there (public
    // issue #22); this frame writes nothing.
    const xr_math::IdAngles other =
        aimField == PlayerAim::DeltaField::Physics ? sample.stateDelta : sample.delta;
    if (!rewrote && aimCheck.watchField(sample.view, sample.command, current, other)) {
        aimField = aimCheck.physics() ? PlayerAim::DeltaField::Physics : PlayerAim::DeltaField::State;
        aimWritten = false;
        EVR_LOG(
            "aim: the view follows the %s deltaViewAngles, not the one head aim wrote: head aim writes that "
            "one now (%d of %d)",
            aimCheck.physics() ? "physics" : "state", aimCheck.fieldSwitches(),
            xr_math::AimCheck::kFieldSwitches);
        return std::nullopt;
    }
    const xr_math::IdAngles head = *target;
    // The game's own angles are command + delta: after a cutscene the game rewrites the delta after it
    // built the frame's view angles from the injected one, and scripted views ignore the delta, so the view
    // angles do not tell whether the injected head yaw is in them.
    const xr_math::IdAngles game{sample.command.pitch + current.pitch, sample.command.yaw + current.yaw,
                                 0.0f};
    if (!xr_math::plausible(head)) {
        return std::nullopt;
    }
    xr_math::HeadAimStep step = xr_math::headAimStep(aimState, game, current.yaw, head, rewrote);
    if (const std::optional<float> steady = glory.restoreYaw(qpcSeconds(qpcNow()))) {
        // After a glory kill shown steady the view still has the heading it kept, while the game's aim
        // ended where the kill left it: the aim turns back to the view instead of the view to the aim.
        const float turn = xr_math::normalize180(*steady - step.bodyYaw);
        if (std::fabs(turn) > 0.05f) {
            step.deltaYaw = xr_math::normalize180(step.deltaYaw + turn);
            step.bodyYaw = *steady;
            glory.noteRestored(turn);
        }
    }
    if (step.restoredWrite) {
        ++aimRestores;
    }
    if (rewrote && aimRewrites <= 3) {
        EVR_LOG("aim: rewrite %llu: view (%.2f %.2f) command (%.2f %.2f) delta (%.2f %.2f) written (%.2f "
                "%.2f) head "
                "(%.2f %.2f) -> body %.2f%s",
                static_cast<unsigned long long>(aimRewrites), sample.view.pitch, sample.view.yaw,
                sample.command.pitch, sample.command.yaw, current.pitch, current.yaw, aimLastDelta.pitch,
                aimLastDelta.yaw, head.pitch, head.yaw, step.bodyYaw,
                step.restoredWrite ? " (a value head aim wrote)" : "");
    }
    if (!mp_guard::allowsGameTouch()) {
        return std::nullopt; // the guard went off during this frame: the player's angles stay the game's
    }
    aimLastDelta = playerAim.addDelta(player, aimField, step.deltaPitch, step.deltaYaw);
    controllers::noteAimWritten();
    xr_math::noteWritten(aimState, aimLastDelta.yaw);
    aimWritten = true;
    aimMenuBody = step.bodyYaw; // held while a menu is up (above)
    ++aimFrames;
    const ULONGLONG ticks = GetTickCount64();
    if (ticks - lastAimStatsTicks >= 10000) {
        lastAimStatsTicks = ticks;
        EVR_LOG("aim: %llu frame(s) aimed by the head, %llu on another camera, %llu delta rewrite(s) by the "
                "game (%llu "
                "back to a value head aim wrote); body yaw %.1f, head yaw %.1f pitch %.1f, game view pitch "
                "%.1f yaw %.1f",
                static_cast<unsigned long long>(aimFrames), static_cast<unsigned long long>(aimCameraFrames),
                static_cast<unsigned long long>(aimRewrites), static_cast<unsigned long long>(aimRestores),
                step.bodyYaw, head.yaw, head.pitch, sample.view.pitch, sample.view.yaw);
    }
    return xr_math::axisFromAngles({0.0f, step.bodyYaw, 0.0f});
}

} // namespace evr::vkcore
