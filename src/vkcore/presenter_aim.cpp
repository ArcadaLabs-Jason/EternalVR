// Head aim (docs/VR_HEAD_TRACKED.md): the camera hook (presenter_head.cpp) turns the idPlayer's view angles
// to the head: the check that turns it on, the menus and the cameras the game drives, and the delta it
// writes.

#include "vkcore/presenter_impl.hpp"

#include "vkcore/camera_anim_hook.hpp"
#include "vkcore/controllers.hpp"
#include "vkcore/debug_commands.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/mp_guard.hpp"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <optional>

namespace evr::vkcore {

namespace {

// Whether the view under a menu faces a camera the game drives (aimWithHead), for the log's changes.
struct MenuCameraWatch {
    bool on = false;
    int lines = 0;
} g_menuCamera;

} // namespace

std::optional<xr_math::IdViewAxis>
XrPresenter::Impl::aimWithHead(std::byte* player, const xr_math::IdViewAxis& gameAxis, Quat headInIdTech) {
    aimBodyOwn = false; // set where head aim's own step gives the body (the end)
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
        const bool menu = menuOverGame();
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
    // (the owner's Quest 3, 2026-09-27: the view seemed to turn the wrong way in a tutorial popup). Under a
    // camera the game drives with the menu up (the Fortress's collectible viewer), the view faces the
    // camera's heading instead (xr_math::menuBodyYaw), except under a pickup's camera played on top of the
    // head (pickup_view.hpp), which keeps the player's heading as below.
    const xr_math::IdViewAxis angleAxis = xr_math::axisFromAngles({sample.view.pitch, sample.view.yaw, 0.0f});
    if (menuOverGame() && aimMenuBody) {
        if (aimMenuFrames++ == 0) {
            EVR_LOG(
                "aim: a menu is up: head aim holds the body yaw at %.1f and writes nothing until it closes",
                *aimMenuBody);
        }
        controllers::noteAimPaused();
        const xr_math::IdViewAxis& rendered = pickup.turn() ? angleAxis : gameAxis;
        const bool follow =
            pickup.followsCamera(); // a Still pickup's camera drives the view under a menu too
        const float yaw = xr_math::menuBodyYaw(aimState, *aimMenuBody, angleAxis, rendered, follow);
        const bool camera = xr_math::cameraDrivesView(follow, angleAxis, rendered);
        if (camera != g_menuCamera.on && g_menuCamera.lines++ < 40) {
            EVR_LOG("aim: under the menu the view %s (body yaw %.1f, held %.1f; game view yaw %.1f)",
                    camera ? "faces the camera the game drives" : "is the player's own again", yaw,
                    *aimMenuBody, xr_math::anglesFromAxis(gameAxis).yaw);
        }
        g_menuCamera.on = camera;
        return xr_math::axisFromAngles({0.0f, yaw, 0.0f});
    }

    // Only while the view is the player's own (not a cinematic or scripted camera). In a cutscene shown
    // around the player, with its cuts re-based, every frame is the camera's: a camera within 15 degrees of
    // the player's angles does not turn head aim back on mid-cutscene. A pickup's camera played on top of the
    // head (pickup_view.hpp) keeps the player's heading; shown with Still view it drives every view of it, as
    // a glory kill's camera does with follow the camera (glory_view.hpp).
    const bool cutsceneCamera = cutscene && !settings.cutsceneCinema && settings.cutsceneCutRebase;
    const bool takeover = pickup.followsCamera() || glory.drivesView();
    if (cutsceneCamera || (!pickup.turn() && xr_math::cameraDrivesView(takeover, angleAxis, gameAxis))) {
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
        return xr_math::axisFromAngles({0.0f, xr_math::cameraBodyYaw(aimState, cameraYaw), 0.0f});
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
    // After a glory kill shown steady or a pickup played on top of the head: the aim turns back to the view's
    // heading instead of the view to the aim (pickup_view.hpp; a stick turn in that window is overridden).
    const double now = qpcSeconds(qpcNow());
    if (const std::optional<float> back = turnBackYaw(glory, pickup, now)) {
        const float turn = xr_math::normalize180(*back - step.bodyYaw);
        if (std::fabs(turn) > 0.05f) {
            step.deltaYaw = xr_math::normalize180(step.deltaYaw + turn);
            step.bodyYaw = *back;
            noteTurnedBack(glory, pickup, now, turn);
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
    aimBodyOwn = true;
    return xr_math::axisFromAngles({0.0f, step.bodyYaw, 0.0f});
}

} // namespace evr::vkcore
