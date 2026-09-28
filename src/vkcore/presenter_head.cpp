// Head tracking: the game's camera hook, head aim, cutscene handling, the render latch and the view
// each present carries (docs/VR_HEAD_TRACKED.md).

#include "vkcore/presenter_impl.hpp"

#include "vkcore/body_follow.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/head_sweep.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/menu_input.hpp"
#include "vkcore/mp_guard.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace evr::vkcore {

// ---------------------------------------------------------------------------------------------------
// Head tracking: the game's camera hook, the render latch and the view carried by each present

void XrPresenter::Impl::onGameView(std::byte* renderView, std::byte* player) {
    // Multiplayer guard (mp_guard.hpp): off, the game's view, aim and input are left exactly as it made
    // them, for the rest of the process.
    if (!mp_guard::allowsGameTouch()) {
        if (!loggedGuardOff) {
            loggedGuardOff = true;
            EVR_LOG("head: the multiplayer guard is %s; the game's view and aim are no longer changed",
                    mp_policy::toString(mp_guard::state()));
        }
        return;
    }
    trackCutscene(renderView[render_view::kInCutscene] != std::byte{0});
    cinemaView.onGameView(reinterpret_cast<float*>(renderView + render_view::kFovX),
                          cutscene && settings.cutsceneCinema, settings.cinemaAspect);
    if (!trackingReady.load(std::memory_order_acquire)) {
        return;
    }
    if (cutscene && settings.cutsceneCinema) {
        // The cutscene keeps the game's camera and view; without a head-tracked view its frames go to the
        // flat screen (kViewStaleSeconds), placed in front of the head when the cutscene started.
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
                    "%.3f %.3f); "
                    "frame left as the game made it",
                    axis[0], axis[1], axis[2], axis[3], axis[4], axis[5], axis[6], axis[7], axis[8]);
        }
        return;
    }
    ViewRecord record;
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
    const Quat headGame = roomHead.game.orientation;
    const Quat headId = xr_math::openXrToIdTech(headGame);
    controllers::setRoomFromLocal(roomHead.roomFromLocal);
    if (const auto weapon = controllers::beginGameView(record.poseTime, roomHead.room, player, cutscene)) {
        const Pose& a = weapon->used;
        record.weaponAimValid = true;
        record.weaponAim = {{a.orientation.x, a.orientation.y, a.orientation.z, a.orientation.w},
                            {a.position.x, a.position.y, a.position.z}};
        const Quat& q = weapon->tracked.orientation;
        record.weaponAimTracked = {q.x, q.y, q.z, q.w};
    }
    std::optional<xr_math::IdViewAxis> body;
    if (settings.headAim) {
        body = aimWithHead(player, gameAxis, headId);
    }
    if (!body) {
        body = xr_math::yawOnly(gameAxis);
    }
    if (!body) {
        ++gameViewsSkipped;
        return;
    }
    const xr_math::IdViewAxis view = xr_math::composeHeadAxis(*body, headId);
    room.noteBody(body->forward, body->left);

    const float gameFovX = fov[0];
    const float gameFovY = fov[1];
    xr_math::GameFov used{gameFovX, gameFovY};
    if (settings.setGameFov && targetFovValid.load(std::memory_order_acquire)) {
        used = {targetFovX.load(), targetFovY.load()};
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
    origin[0] += offset.x;
    origin[1] += offset.y;
    origin[2] += offset.z;
    fov[0] = used.fovX;
    fov[1] = used.fovY;
    controllers::endGameView(renderView, player, *body, eye, clear.validOffset, settings.unitsPerMetre);
    record.axis = written;
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

void XrPresenter::Impl::trackCutscene(bool inCutscene) {
    const ULONGLONG now = GetTickCount64();
    if (inCutscene != cutscene) {
        cutscene = inCutscene;
        cutsceneSince = now;
        ++cutsceneChanges;
        if (cutsceneChanges <= 20) {
            EVR_LOG("game: %s%s", inCutscene ? "cutscene starts" : "cutscene ends; the player's view is back",
                    inCutscene && settings.cutsceneCinema ? " (on the flat screen)" : "");
        }
        if (inCutscene && settings.cutsceneCinema) {
            replaceScreen.store(true, std::memory_order_release);
        }
    }
    // Without the automatic skip, holding the dash button skips by hand (controllers.hpp).
    controllers::noteSkippableCutscene(inCutscene && !settings.skipCinematics);
    if (!settings.skipCinematics) {
        return;
    }
    // Holds R (the game's skip key) while a cutscene plays: 2.5 s holds with short gaps, released as
    // soon as the cutscene ends.
    HWND window = gameWindow();
    if (skipHolding) {
        if (!inCutscene || now - skipHoldStart >= 2500) {
            injectKey(kSkipKey, false, window);
            skipHolding = false;
            skipReleased = now;
        }
    } else if (inCutscene && now - cutsceneSince >= 300 && now - skipReleased >= 400) {
        if (injectKey(kSkipKey, true, window)) {
            skipHolding = true;
            skipHoldStart = now;
            ++skipHolds;
            if (skipHolds <= 10) {
                EVR_LOG("game: holding the skip key (hold %llu)", static_cast<unsigned long long>(skipHolds));
            }
        }
    }
}

std::optional<xr_math::IdViewAxis>
XrPresenter::Impl::aimWithHead(std::byte* player, const xr_math::IdViewAxis& gameAxis, Quat headInIdTech) {
    if (aimPhase == AimPhase::Off) {
        return std::nullopt;
    }
    if (aimPhase == AimPhase::Unchecked) {
        aimPhase = playerAim.init() ? AimPhase::Verifying : AimPhase::Off;
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
    const PlayerAim::Sample sample = playerAim.read(player);
    if (!xr_math::plausible(sample.view) || !xr_math::plausible(sample.delta) ||
        !xr_math::plausible(sample.stateDelta) || !xr_math::plausible(sample.command)) {
        // Not a player state this layout describes (or one being torn down); never write from it.
        ++aimCameraFrames;
        return std::nullopt;
    }

    // Verification: the game's view angles must be its command angles plus one of the two deltas.
    if (aimPhase == AimPhase::Verifying) {
        const auto matches = [&](const xr_math::IdAngles& delta) {
            return std::fabs(xr_math::normalize180(sample.view.yaw - (sample.command.yaw + delta.yaw))) <
                       0.05f &&
                   std::fabs(xr_math::normalize180(sample.view.pitch -
                                                   (sample.command.pitch + delta.pitch))) < 0.05f;
        };
        ++aimChecks;
        aimPhysicsMatches += matches(sample.delta) ? 1 : 0;
        aimStateMatches += matches(sample.stateDelta) ? 1 : 0;
        if (aimChecks <= 3) {
            EVR_LOG("aim: check %d: view (%.2f %.2f) command (%.2f %.2f) delta (%.2f %.2f) state delta (%.2f "
                    "%.2f)",
                    aimChecks, sample.view.pitch, sample.view.yaw, sample.command.pitch, sample.command.yaw,
                    sample.delta.pitch, sample.delta.yaw, sample.stateDelta.pitch, sample.stateDelta.yaw);
        }
        if (aimChecks < 60) {
            return std::nullopt;
        }
        if (aimPhysicsMatches >= 54) {
            aimField = PlayerAim::DeltaField::Physics;
        } else if (aimStateMatches >= 54) {
            aimField = PlayerAim::DeltaField::State;
        } else {
            aimPhase = AimPhase::Off;
            EVR_LOG("aim: view angles = command + delta held in %d/%d (physics) and %d/%d (state) frames; "
                    "head aim "
                    "off, the game keeps its own aim",
                    aimPhysicsMatches, aimChecks, aimStateMatches, aimChecks);
            return std::nullopt;
        }
        aimPhase = AimPhase::Active;
        EVR_LOG("aim: head aim on through the %s deltaViewAngles (%d/%d frames matched)",
                aimField == PlayerAim::DeltaField::Physics ? "physics" : "state",
                aimField == PlayerAim::DeltaField::Physics ? aimPhysicsMatches : aimStateMatches, aimChecks);
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
        return xr_math::axisFromAngles({0.0f, *aimMenuBody, 0.0f});
    }

    // Only while the view is the player's own (not a cinematic or scripted camera).
    const xr_math::IdViewAxis angleAxis = xr_math::axisFromAngles({sample.view.pitch, sample.view.yaw, 0.0f});
    if (dot(angleAxis.forward, gameAxis.forward) < 0.966f) { // 15 degrees
        // A scripted camera (a glory kill's): nothing is written, and the view faces the camera's heading
        // while the head is where it was when the camera took over (not the camera plus the head's whole
        // yaw in the room, which faces backwards for a player turned round in the room).
        ++aimCameraFrames;
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
    const xr_math::IdAngles head = *target;
    // The game's own angles are command + delta: after a cutscene the game rewrites the delta after it
    // built the frame's view angles from the injected one, and scripted views ignore the delta, so the view
    // angles do not tell whether the injected head yaw is in them.
    const xr_math::IdAngles game{sample.command.pitch + current.pitch, sample.command.yaw + current.yaw,
                                 0.0f};
    if (!xr_math::plausible(head)) {
        return std::nullopt;
    }
    const xr_math::HeadAimStep step = xr_math::headAimStep(aimState, game, current.yaw, head, rewrote);
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

void XrPresenter::Impl::onRenderLatch(const std::byte* renderView, const float* previousProjection) {
    const auto* axis = reinterpret_cast<const float*>(renderView + render_view::kViewAxis);
    std::uint64_t seq = 0;
    XrFovf fov{};
    std::uint64_t newest = 0;
    {
        std::lock_guard lock(historyMutex);
        newest = latestSeq;
        for (std::size_t i = 0; i < kHistorySize && i < newest; ++i) {
            const ViewRecord& record = history[(newest - i) % kHistorySize];
            if (std::memcmp(record.axis.data(), axis, sizeof(record.axis)) == 0) {
                seq = record.seq;
                fov = record.fov;
                break;
            }
        }
    }
    if (seq == 0) {
        latchUnmatched.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    latchMatched.fetch_add(1, std::memory_order_relaxed);
    latchedSeq.store(seq, std::memory_order_release);
    if (loggedLatches.fetch_add(1, std::memory_order_relaxed) < 12) {
        EVR_LOG("latch: render view %p latched view %llu (newest %llu)", static_cast<const void*>(renderView),
                static_cast<unsigned long long>(seq), static_cast<unsigned long long>(newest));
    }
    const ULONGLONG ticks = GetTickCount64();
    ULONGLONG last = lastLatchStatsTicks.load(std::memory_order_relaxed);
    if (ticks - last >= 10000 && lastLatchStatsTicks.compare_exchange_strong(last, ticks)) {
        // The projection left from this view's previous render against the FOV we asked for, to check
        // that the renderer uses fov_x / fov_y as given (a mismatch means the headset shows the image
        // at the wrong size).
        const float wantX = 1.0f / std::tan(fov.angleRight);
        const float wantY = 1.0f / std::tan(fov.angleUp);
        EVR_LOG("latch: %llu matched, %llu other view(s); previous projection [0][0] %.4f [1][1] %.4f [0][2] "
                "%.4f "
                "[1][2] %.4f [2][0] %.4f [2][1] %.4f; expected %.4f / %.4f",
                static_cast<unsigned long long>(latchMatched.load()),
                static_cast<unsigned long long>(latchUnmatched.load()), previousProjection[0],
                previousProjection[5], previousProjection[2], previousProjection[6], previousProjection[8],
                previousProjection[9], wantX, wantY);
    }
}

bool XrPresenter::Impl::latestView(ViewRecord& out, std::uint64_t& gap) {
    std::lock_guard lock(historyMutex);
    gap = 0;
    if (latestSeq == 0) {
        return false;
    }
    // The view the render thread latched most recently is the frame being presented, if the latch
    // hook matched one; otherwise the newest game view.
    std::uint64_t seq = latchedSeq.load(std::memory_order_acquire);
    if (seq == 0 || seq > latestSeq || latestSeq - seq >= kHistorySize) {
        seq = latestSeq;
    }
    const ViewRecord& record = history[seq % kHistorySize];
    if (record.seq != seq || qpcSeconds(qpcNow() - record.locatedQpc) > kViewStaleSeconds) {
        return false;
    }
    out = record;
    gap = latestSeq - seq;
    return true;
}

} // namespace evr::vkcore
