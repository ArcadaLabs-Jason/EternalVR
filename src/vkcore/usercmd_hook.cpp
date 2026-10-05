// Motion controllers, the user command (controllers.hpp, docs/rig-findings/input-aim.md sections 1.3
// and 1.4).
//
// Two mid-function hooks in the game's per-frame command build:
// - At the call of idUserCmdMgr::PutUserCmd (RVA 0x43E8DD in build 25216728): r8 is the finished command
//   (after the game's gameTime and button fix-up), ebx the local user, r14b set while the game suppresses
//   buttons. The mapper runs here once per command; its actions are ORed into the buttons and its move
//   added to the move axes. Keyboard, mouse and pad input are already in the command and stay.
// - At the generator's angle conversion (RVA 0x17FD3C0): rdi is the generator and [rdi+0x8D8] its
//   accumulated angles (pitch +4, yaw +8, degrees). The turn is added there, where mouse motion goes, so
//   it persists into every later command exactly like mouse motion.

#include "vkcore/controllers_impl.hpp"

#include "features/input/usercmd_motion.hpp"
#include "game/eternal/usercmd_buttons.hpp"
#include "vkcore/body_follow.hpp"
#include "vkcore/bug_capture.hpp"
#include "vkcore/demon_view.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/keep_active.hpp"
#include "vkcore/key_inject.hpp"
#include "vkcore/log.hpp"
#include "vkcore/menu_input.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"
#include "vkcore/room_scale.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace evr::vkcore::controllers {

namespace {

constexpr const char* kTag = "controllers";

// input-aim.md 1.3 (f): the button fix-up and the PutUserCmd call; the hook goes on the call (+0x2C).
constexpr const char* kPutUserCmdSignature =
    "45 84 F6 4C 8D 44 24 60 8B D7 48 8B 08 48 8B 44 24 70 48 89 4C 24 60 B9 00 00 00 00 48 0F 45 C1 49 8D "
    "8D 58 21 00 00 48 89 44 24 70 E8 ?? ?? ?? ??";
constexpr std::size_t kPutUserCmdCall = 0x2C;

// input-aim.md 1.3 (d): mov rcx,[rdi+0x8D8], then the pitch, yaw and roll conversions.
constexpr const char* kAngleSignature =
    "48 8B 8F D8 08 00 00 F3 0F 10 15 ?? ?? ?? ?? 0F 28 B4 24 90 00 00 00 4C "
    "8B B4 24 A8 00 00 00 F3 0F 10 41 04 4C 8B AC 24 B0 00 00 00";
constexpr std::size_t kGeneratorAngles = 0x8D8;
constexpr std::size_t kGeneratorUser =
    0x8D0; // the local user being built (mov [rdi+0x8D0], esi at the build's start)
constexpr std::size_t kAccumulatedPitch = 4;
constexpr std::size_t kAccumulatedYaw = 8;

// idUserCmd (type info, 0x98 bytes).
constexpr std::size_t kCmdButtons = 0x10;
constexpr std::size_t kCmdForward = 0x18;
constexpr std::size_t kCmdRight = 0x19;
constexpr std::size_t kCmdUp = 0x1A;

// The accumulated pitch the game keeps (it clamps the view itself; this only keeps our additions sane).
constexpr float kPitchLimit = 89.0f;
// The held actions the mapper published are taken as released when it has not run for this long (the game
// builds a command every tick, popups included).
constexpr double kHeldActionsStaleSeconds = 0.25;

std::atomic<bool> g_buttonsAndMove{false};
std::atomic<std::uint64_t> g_loggedCommands{0};
// The action lines: every one for the first stretch of play (the weapon-swap protocol checks each switch
// against a marker), then one a minute; ETERNALVR_CONTROLLERS_TRACE logs them all.
LogCap g_actionLines{500};

void sendPause(State& s, bool down) {
    if (down == s.pauseKeyDown) {
        return;
    }
    const auto key = game::keyForAction(game::GameAction::Pause);
    if (key && injectKey(*key, down, gameWindow())) {
        s.pauseKeyDown = down;
    } else if (!down) {
        s.pauseKeyDown = false;
    }
}

// A tutorial or lore popup suppresses the command's buttons and waits for a key (Space or E), so while
// the game suppresses buttons jump sends Space and melee/use sends E; each is released when the button
// is, when the suppression ends or when the controllers go stale.
void sendPopupKey(bool& keyDown, std::uint8_t key, bool down) {
    if (down == keyDown) {
        return;
    }
    if (injectKey(key, down, gameWindow())) {
        keyDown = down;
        static std::atomic<int> logged{0};
        if (down && logged.fetch_add(1) < 6) {
            EVR_LOG("%s: popup key %s sent (the game suppresses buttons)", kTag,
                    key == VK_SPACE ? "Space" : "E");
        }
    } else if (!down) {
        keyDown = false;
    }
}

// A cutscene skipped by hand (cutscene_skip.hpp): the skip key follows the dash action while a cutscene
// plays, and goes up with the button or the cutscene.
void sendCutsceneSkip(State& s, bool dash) {
    const bool cutscene = s.skippableCutscene.load(std::memory_order_relaxed);
    const input::CutsceneSkipOutput out = s.cutsceneSkip.update(cutscene, dash);
    if (out.keyDown == s.skipKeyDown) {
        return;
    }
    if (injectKey(input::kCutsceneSkipKey, out.keyDown, gameWindow())) {
        s.skipKeyDown = out.keyDown;
        if (out.firstHold) {
            EVR_LOG("%s: dash held in a cutscene: holding the skip key", kTag);
        }
    } else if (!out.keyDown) {
        s.skipKeyDown = false;
    }
}

// The weapon wheel (wheel_mouse.hpp, docs/rig-findings/menus.md section 4): the game's wheel selects with its
// menu cursor, so while the wheel button is held (and the wheel has had time to open) the pointer stick
// becomes relative mouse motion through the game's raw input. `held` false ends it.
void sendWheelPointer(State& s, bool held, input::Axis2 pointer, float dt) {
    const input::WheelMouseOutput w = s.wheelMouse.update(held, pointer, dt);
    if (w.opened) {
        if (settings().wheelSelect == input::WheelSelect::Hand) {
            EVR_LOG(
                "%s: the weapon wheel is up: the weapon hand moves the game's wheel cursor (%.0f px to the "
                "rim at a %.0f deg turn); let go of the wheel's stick or button to pick",
                kTag, s.wheelMouse.settings().reachPixels, settings().wheelHandDegrees);
        } else {
            EVR_LOG(
                "%s: the weapon wheel is up: the stick moves the game's wheel cursor (%.0f px to the rim); "
                "let go of the stick to pick",
                kTag, s.wheelMouse.settings().reachPixels);
        }
    }
    if (w.move) {
        const bool sent = injectMouse(w.dx, w.dy, 0, 0, gameWindow());
        if (w.directionChanged || !sent) {
            EVR_LOG("%s: weapon wheel: pointing %s (motion %d, %d)%s", kTag,
                    input::wheelDirectionName(w.pointed), w.dx, w.dy, sent ? "" : "; not delivered");
        }
    }
    if (w.released) {
        EVR_LOG("%s: weapon wheel released after %u motion(s): the game picks the highlighted weapon", kTag,
                w.moves);
    }
}

// Body follow (body_follow.hpp): the move toward the head, added to the finished command only when
// nothing else moves the player in it.
void addBodyFollow(std::byte* cmd, bool suppressed) {
    std::uint64_t buttons = 0;
    std::int8_t forward = 0;
    std::int8_t right = 0;
    std::int8_t up = 0;
    if (!safeRead(cmd + kCmdButtons, buttons) || !safeRead(cmd + kCmdForward, forward) ||
        !safeRead(cmd + kCmdRight, right) || !safeRead(cmd + kCmdUp, up)) {
        return;
    }
    const bool moving = forward != 0 || right != 0;
    const bool jumpOrDash =
        up != 0 || (buttons & (game::usercmd_button::kMoveUp | game::usercmd_button::kDash)) != 0;
    // Not while piloting a demon: body follow steers the Slayer's body toward the head.
    const bool gameplay = !suppressed && !menu_input::suppressGameplay() && !pilotingDemon();
    const input::MoveAxes move = body_follow::commandMove(
        moving, jumpOrDash, gameplay, state().viewYawTracking.load(std::memory_order_relaxed));
    if (move == input::MoveAxes{} || !mp_guard::allowsGameTouch()) {
        return;
    }
    const std::int8_t newForward = input::addMoveAxis(forward, move.forward);
    const std::int8_t newRight = input::addMoveAxis(right, move.right);
    safeCopy(cmd + kCmdForward, &newForward, sizeof(newForward));
    safeCopy(cmd + kCmdRight, &newRight, sizeof(newRight));
}

void onPutUserCmd(const HookRegisters& regs) {
    if (!g_buttonsAndMove || !mp_guard::allowsGameTouch()) {
        return;
    }
    if ((regs.rbx & 0xFFFFFFFFu) != 0) {
        return; // local user 0 only: single player
    }
    State& s = state();
    s.commands.fetch_add(1, std::memory_order_relaxed);
    const MappedInput mapped = runMapper();
    auto* cmd = reinterpret_cast<std::byte*>(regs.r8);
    const bool suppressed = (regs.r14 & 0xFFu) != 0;
    // The pause key goes out even while the game suppresses buttons: that is how its menu is closed. Every
    // key the controllers hold goes up when their input goes stale.
    {
        std::lock_guard lock(s.mapperMutex);
        sendPause(s, mapped.live && game::contains(mapped.actions, game::GameAction::Pause));
        sendCutsceneSkip(s, mapped.live && game::contains(mapped.actions, game::GameAction::Dash));
        const bool popup = mapped.live && suppressed;
        sendPopupKey(s.popupSpaceDown, VK_SPACE,
                     popup && game::contains(mapped.actions, game::GameAction::Jump));
        sendPopupKey(s.popupUseDown, 'E', popup && game::contains(mapped.actions, game::GameAction::Melee));
    }
    if (!mapped.live) {
        addBodyFollow(cmd, suppressed);
        return;
    }
    if (settings().trace) {
        // The game's own command as built from the keyboard, mouse and pad: which bits and axes a key sets.
        static std::uint64_t lastRaw = 0;
        std::uint8_t raw[0x20] = {};
        if (safeCopy(raw, cmd, sizeof(raw))) {
            std::uint64_t b = 0;
            std::memcpy(&b, raw + kCmdButtons, sizeof(b));
            const std::uint64_t key = b ^ (static_cast<std::uint64_t>(raw[0x18]) << 8) ^
                                      (static_cast<std::uint64_t>(raw[0x19]) << 16) ^
                                      (static_cast<std::uint64_t>(raw[0x1A]) << 24);
            static std::int16_t lastYaw = 0;
            std::int16_t angles[3] = {};
            std::memcpy(angles, raw + 0x1C, sizeof(angles));
            if (key != lastRaw || angles[1] != lastYaw) {
                lastRaw = key;
                lastYaw = angles[1];
                EVR_LOG("%s: trace: game command angles %d %d", kTag, angles[0], angles[1]);
                EVR_LOG("%s: trace: game command buttons 0x%llx move %d %d up %d", kTag,
                        static_cast<unsigned long long>(b), static_cast<std::int8_t>(raw[0x18]),
                        static_cast<std::int8_t>(raw[0x19]), static_cast<std::int8_t>(raw[0x1A]));
            }
        }
    }
    const std::uint64_t injected = pilotedDemonButtons(mapped.actions, game::usercmdButtons(mapped.actions));
    const input::MoveAxes move = input::quantizeMove(mapped.input.move, input::kMaxMoveAxis);
    std::uint64_t buttons = 0;
    std::int8_t forward = 0;
    std::int8_t right = 0;
    std::int8_t up = 0;
    if (!safeRead(cmd + kCmdButtons, buttons) || !safeRead(cmd + kCmdForward, forward) ||
        !safeRead(cmd + kCmdRight, right) || !safeRead(cmd + kCmdUp, up)) {
        return;
    }
    const std::uint64_t newButtons = input::mergeButtons(buttons, injected, suppressed);
    const std::int8_t newForward = suppressed ? forward : input::addMoveAxis(forward, move.forward);
    const std::int8_t newRight = suppressed ? right : input::addMoveAxis(right, move.right);
    const std::int8_t newUp = suppressed ? up : input::addMoveAxis(up, game::usercmdUpMove(mapped.actions));
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    safeCopy(cmd + kCmdButtons, &newButtons, sizeof(newButtons));
    safeCopy(cmd + kCmdForward, &newForward, sizeof(newForward));
    safeCopy(cmd + kCmdRight, &newRight, sizeof(newRight));
    safeCopy(cmd + kCmdUp, &newUp, sizeof(newUp));
    addBodyFollow(cmd, suppressed);
    if (injected != 0 || move.forward != 0 || move.right != 0) {
        s.commandsWithInput.fetch_add(1, std::memory_order_relaxed);
        if (g_loggedCommands.fetch_add(1) < 12) {
            EVR_LOG("%s: command: buttons 0x%llx | 0x%llx -> 0x%llx%s, move (%d %d) + (%d %d)", kTag,
                    static_cast<unsigned long long>(buttons), static_cast<unsigned long long>(injected),
                    static_cast<unsigned long long>(newButtons),
                    suppressed ? " (suppressed by the game)" : "", forward, right, move.forward, move.right);
        }
    }
}

void onAngles(const HookRegisters& regs) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    // The generator builds every local user's command in turn; the turn belongs to user 0's (the rig showed
    // the first build to reach this point is user 1's).
    const auto* generator = reinterpret_cast<const std::byte*>(regs.rdi);
    std::int32_t user = -1;
    if (!safeRead(generator + kGeneratorUser, user) || user != 0) {
        return;
    }
    State& s = state();
    static std::atomic<std::uint64_t> calls{0};
    const std::uint64_t call = calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (settings().trace && (call == 1 || call % 2000 == 0)) {
        EVR_LOG("%s: trace: angle conversion call %llu", kTag, static_cast<unsigned long long>(call));
    }
    input::ViewDelta delta;
    {
        std::lock_guard lock(s.mapperMutex);
        delta = s.viewQueue.drain();
    }
    if (delta.yaw == 0.0f && delta.pitch == 0.0f) {
        return;
    }
    std::byte* angles = nullptr;
    float pitch = 0.0f;
    float yaw = 0.0f;
    if (!safeRead(generator + kGeneratorAngles, angles) || !angles ||
        !safeRead(angles + kAccumulatedPitch, pitch) || !safeRead(angles + kAccumulatedYaw, yaw) ||
        !std::isfinite(pitch) || !std::isfinite(yaw)) {
        return;
    }
    const float newYaw = input::addAccumulatedYaw(yaw, delta.yaw);
    const float newPitch = std::clamp(pitch + delta.pitch, -kPitchLimit, kPitchLimit);
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    safeCopy(angles + kAccumulatedYaw, &newYaw, sizeof(newYaw));
    if (delta.pitch != 0.0f) {
        safeCopy(angles + kAccumulatedPitch, &newPitch, sizeof(newPitch));
    }
    const std::uint64_t applied = s.turnsApplied.fetch_add(1, std::memory_order_relaxed) + 1;
    if (settings().trace && (applied <= 5 || applied % 50 == 0)) {
        EVR_LOG("%s: trace: turn %llu (user %d): yaw %.3f + %.3f -> %.3f, pitch %.3f + %.3f", kTag,
                static_cast<unsigned long long>(applied), user, yaw, delta.yaw, newYaw, pitch, delta.pitch);
    }
}

bool hookAt(const GameImage& image, const char* name, const std::byte* at, MidHookCallback callback) {
    std::string error;
    if (!installMidHook(const_cast<std::byte*>(at), callback, error)) {
        EVR_LOG("%s: %s hook at RVA 0x%X failed: %s", kTag, name, image.rva(at), error.c_str());
        return false;
    }
    EVR_LOG("%s: %s hook at RVA 0x%X", kTag, name, image.rva(at));
    return true;
}

} // namespace

void noteSkippableCutscene(bool playing) {
    state().skippableCutscene.store(playing, std::memory_order_relaxed);
}

bool menuRequestedWithin(double seconds) {
    const LONGLONG at = state().menuRequestQpc.load(std::memory_order_relaxed);
    return at != 0 && secondsSince(at) <= seconds;
}

bool dossierRequestedWithin(double seconds) {
    const LONGLONG at = state().dossierRequestQpc.load(std::memory_order_relaxed);
    return at != 0 && secondsSince(at) <= seconds;
}

game::GameActionSet heldActions() {
    const State& s = state();
    const LONGLONG at = s.heldActionsQpc.load(std::memory_order_relaxed);
    if (at == 0 || secondsSince(at) > kHeldActionsStaleSeconds) {
        return {};
    }
    return game::GameActionSet(s.heldActionBits.load(std::memory_order_relaxed));
}

ArtificialMotion artificialMotion() {
    const State& s = state();
    const LONGLONG at = s.motionQpc.load(std::memory_order_relaxed);
    if (at == 0 || secondsSince(at) > kHeldActionsStaleSeconds) {
        return {};
    }
    return {s.motionTurnRate.load(std::memory_order_relaxed), s.motionMove.load(std::memory_order_relaxed)};
}

MappedInput runMapper() {
    State& s = state();
    const input::ControllerSettings& cfg = settings();
    std::lock_guard lock(s.mapperMutex);
    const LONGLONG now = nowQpc();
    const float dt = s.lastMapQpc ? static_cast<float>(secondsSince(s.lastMapQpc)) : 0.0f;
    s.lastMapQpc = now;
    Snapshot snapshot;
    {
        std::lock_guard snapshotLock(s.snapshotMutex);
        snapshot = s.snapshot;
    }
    MappedInput out;
    const bool stale = !snapshot.valid || secondsSince(snapshot.qpc) > kSnapshotStaleSeconds;
    if (stale && s.attached.load(std::memory_order_acquire)) {
        // Scripted input does not depend on the XR worker's frame loop: a runtime that stalls the worker
        // (OpenXR-Simulator's window does, for seconds at a time) must not stall a rig test.
        if (const auto test = testInput()) {
            input::InputFrame frame;
            {
                std::lock_guard viewLock(s.viewMutex);
                frame.head.poseValid = s.poses.valid;
                frame.head.pose = s.poses.head;
            }
            input::applyTestInput(*test, frame);
            const game::Controller controller = snapshot.controller;
            snapshot = {};
            snapshot.valid = true;
            snapshot.qpc = now;
            snapshot.frame = frame;
            snapshot.controller = controller;
        }
    }
    if (!s.attached.load(std::memory_order_acquire) || !snapshot.valid ||
        secondsSince(snapshot.qpc) > kSnapshotStaleSeconds) {
        // Nothing is held across a gap: the next live frame starts from released buttons.
        s.mapper.reset();
        s.hold.reset();
        resetActionAim(s);
        s.viewQueue.drain();
        sendWheelPointer(s, false, {}, 0.0f);
        s.wheelHand.reset();
        return out;
    }
    if (!ensureMapper(s, snapshot.controller, "")) {
        return out;
    }
    input::MapperContext context;
    context.viewYawRadians = s.viewYawTracking.load(std::memory_order_relaxed);
    context.posture = cfg.seated ? posture::Posture::Seated : roomPosture();
    const bool menuHold = menu_input::suppressGameplay();
    context.menuHold = menuHold;
    out.input = s.mapper->update(snapshot.frame, context, dt);
    if (s.menuHold && !menuHold) {
        // A tap that fired in the menu is not carried out of it by the minimum hold (menu_release_latch.hpp).
        s.hold.reset();
        EVR_LOG("%s: gameplay input back on: presses begun in the menu are dropped%s", kTag,
                s.mapper->heldFromMenu() ? "; a control still held stays out of the game until let go" : "");
    }
    s.menuHold = menuHold;
    if (out.input.capture) {
        bug_capture::request(); // the capture chord (capture_chord.hpp): the in-headset capture, in a menu
                                // too
    }
    // The recenter binding is the layer's own: a long press re-anchors the room (docs/VR_ROOMSCALE.md).
    noteRecenterBinding(game::contains(out.input.down, game::GameAction::Recenter), cfg.holdSeconds);
    out.turnStick = s.mapper->turnStick();
    // A melee or equipment press that aims with the head or the off hand waits for the view (action_aim.hpp).
    out.actions = s.hold.update(aimActions(s, out.input.down, menuHold, dt), dt);
    out.live = true;
    if (cfg.wheelSelect == input::WheelSelect::Hand) {
        // The weapon hand points at the wheel; the stick or button only holds it (wheel_hand.hpp).
        if (!s.wheelHand) {
            s.wheelHand.emplace(cfg.wheelHandDegrees);
        }
        const input::HandState& hand = snapshot.frame.hand(weaponHand());
        out.input.wheelPointer =
            s.wheelHand->update(game::contains(out.actions, game::GameAction::WeaponWheel), hand.poseValid,
                                hand.aimPose.orientation);
    }
    // Published before a menu holds them back: in a tutorial popup the menu router presses their keys.
    s.heldActionBits.store(out.actions.to_ullong(), std::memory_order_relaxed);
    s.heldActionsQpc.store(now, std::memory_order_relaxed);
    if (menuHold) {
        // A menu is up (or a control is still held from one): the controllers drive the menu through the
        // pointer (docs/VR_MENUS.md). Only the pause key goes through, so the Menu button still closes it.
        const bool pause = game::contains(out.actions, game::GameAction::Pause);
        out.actions.reset();
        if (pause) {
            game::add(out.actions, game::GameAction::Pause);
        }
        out.input.move = {};
        out.input.turnDegrees = 0.0f;
        out.input.wheelPointer = {};
        out.input.punch = {};
        out.input.down = out.actions;
        out.turnStick = {};
    } else if (out.input.thrown || out.input.swung || out.input.handsJumped) {
        // Its action is logged below with every other action sent (arm_gestures.hpp, hands_jump.hpp).
        const char* jump = "";
        if (out.input.handsJumped) {
            jump = context.posture == posture::Posture::Seated ? " hands-up jump (seated height)"
                                                               : " hands-up jump";
        }
        EVR_LOG("%s: gesture:%s%s%s", kTag, out.input.thrown ? " throw" : "",
                out.input.swung ? " overhead swing" : "", jump);
    }
    s.viewQueue.add({0.0f, out.input.turnDegrees});
    // The comfort vignette follows the motion sent (artificialMotion); the mapper counted at most this step.
    const float mapDt = std::min(dt, input::kMaxFrameSeconds);
    s.motionTurnRate.store(mapDt > 0.0f ? std::fabs(out.input.turnDegrees) / mapDt : 0.0f,
                           std::memory_order_relaxed);
    s.motionMove.store(std::min(1.0f, input::magnitude(out.input.move)), std::memory_order_relaxed);
    s.motionQpc.store(now, std::memory_order_relaxed);
    if (cfg.trace && out.input.turnDegrees != 0.0f) {
        static std::uint64_t turns = 0;
        if (++turns <= 5 || turns % 50 == 0) {
            EVR_LOG("%s: trace: mapper turn %.3f deg (%llu)", kTag, out.input.turnDegrees,
                    static_cast<unsigned long long>(turns));
        }
    }
    // The virtual gamepad carries the wheel pointer on its right stick instead.
    sendWheelPointer(s, game::contains(out.actions, game::GameAction::WeaponWheel) && !s.xinputActive.load(),
                     out.input.wheelPointer, std::min(dt, input::kMaxFrameSeconds));
    const game::GameActionSet changed = out.actions ^ s.lastSent;
    if (changed.any()) {
        for (std::size_t i = 0; i < game::kGameActionCount; ++i) {
            if (changed.test(i) && out.actions.test(i)) {
                const auto action = static_cast<game::GameAction>(i);
                if (game::opensMenu(action)) {
                    s.menuRequestQpc.store(now, std::memory_order_relaxed);
                }
                if (action == game::GameAction::Dossier || action == game::GameAction::Automap) {
                    s.dossierRequestQpc.store(now, std::memory_order_relaxed);
                }
                std::uint64_t skipped = 0;
                if (cfg.trace || g_actionLines.due(GetTickCount64(), skipped)) {
                    char note[48] = "";
                    if (skipped != 0) {
                        std::snprintf(note, sizeof(note), " (%llu more not logged)",
                                      static_cast<unsigned long long>(skipped));
                    }
                    EVR_LOG("%s: action %s%s", kTag, std::string(game::gameActionName(action)).c_str(), note);
                }
            }
        }
    }
    noteMapperHaptics(out.actions, out.input);
    s.lastSent = out.actions;
    s.lastInput = out.input;
    return out;
}

bool installUserCmdHooks(bool buttonsAndMove, bool& angleInstalled) {
    GameImage image;
    if (!locateGameImage(image, kTag)) {
        angleInstalled = false;
        return false;
    }
    bool commandInstalled = false;
    if (buttonsAndMove) {
        if (const std::byte* site = findUnique(image, kTag, "PutUserCmd call", kPutUserCmdSignature)) {
            g_buttonsAndMove = true;
            commandInstalled = hookAt(image, "user command", site + kPutUserCmdCall, &onPutUserCmd);
            g_buttonsAndMove = commandInstalled;
            body_follow::setCommandHook(commandInstalled);
        }
    }
    angleInstalled = false;
    if (const std::byte* site = findUnique(image, kTag, "angle conversion", kAngleSignature)) {
        angleInstalled = hookAt(image, "turn", site, &onAngles);
    }
    return commandInstalled;
}

} // namespace evr::vkcore::controllers
