#pragma once

// State shared by the controller sources (controllers.hpp): input_xr.cpp (OpenXR actions and the
// snapshot), game_view_poses.cpp (the poses at the camera hook's time, aim smoothing), usercmd_hook.cpp
// (the mapper and the user command), aim_hooks.cpp (forced angles, hand aim, shots), viewmodel_hook.cpp
// (the weapon at the hand, weapon FOV) and xinput_hook.cpp (virtual gamepad).
//
// Threads: the XR worker (attach, sync, detach), the camera hook (beginGameView, aimAngles, endGameView),
// the game's user-command build (the user-command and angle hooks), the game's weapon code (fire and
// viewmodel hooks) and the pad sampler (XInput). Each piece of state names its lock.

#include "features/input/aim_smoothing.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/controller_settings.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/cutscene_skip.hpp"
#include "features/input/forced_angles.hpp"
#include "features/input/game_input.hpp"
#include "features/input/input_mapper.hpp"
#include "features/input/test_input.hpp"
#include "features/input/usercmd_injection.hpp"
#include "features/input/wheel_mouse.hpp"
#include "features/input/xr_action_set.hpp"
#include "game/eternal/controller_data.hpp"
#include "game/eternal/weapon_offsets.hpp"
#include "vkcore/player_aim.hpp"
#include "xr_math/weapon_pose.hpp"

#include <windows.h>

#include <openxr/openxr.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>

namespace evr::vkcore::controllers {

// A snapshot older than this is not used (the XR worker stopped or the session lost focus).
inline constexpr double kSnapshotStaleSeconds = 0.25;
// World hand poses older than this are not used by the fire and viewmodel hooks.
inline constexpr double kWorldStaleSeconds = 0.1;

struct XrInput {
    // Loaded in attach from the instance.
    PFN_xrStringToPath xrStringToPath = nullptr;
    PFN_xrPathToString xrPathToString = nullptr;
    PFN_xrCreateActionSet xrCreateActionSet = nullptr;
    PFN_xrDestroyActionSet xrDestroyActionSet = nullptr;
    PFN_xrCreateAction xrCreateAction = nullptr;
    PFN_xrSuggestInteractionProfileBindings xrSuggestInteractionProfileBindings = nullptr;
    PFN_xrAttachSessionActionSets xrAttachSessionActionSets = nullptr;
    PFN_xrCreateActionSpace xrCreateActionSpace = nullptr;
    PFN_xrCreateReferenceSpace xrCreateReferenceSpace = nullptr;
    PFN_xrSyncActions xrSyncActions = nullptr;
    PFN_xrGetActionStateBoolean xrGetActionStateBoolean = nullptr;
    PFN_xrGetActionStateFloat xrGetActionStateFloat = nullptr;
    PFN_xrGetActionStateVector2f xrGetActionStateVector2f = nullptr;
    PFN_xrGetCurrentInteractionProfile xrGetCurrentInteractionProfile = nullptr;
    PFN_xrLocateSpace xrLocateSpace = nullptr;
    PFN_xrDestroySpace xrDestroySpace = nullptr;
    PFN_xrResultToString xrResultToString = nullptr;

    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE; // the presenter's; never destroyed here
    XrSpace viewSpace = XR_NULL_HANDLE;  // ours
    std::array<XrActionSet, static_cast<std::size_t>(input::XrActionSetId::Count)> sets{};
    std::array<XrAction, input::kXrActionCount> actions{};
    std::array<XrPath, 2> handPaths{}; // indexed by input::Hand
    std::array<XrSpace, 2> aimSpaces{};
    std::array<XrSpace, 2> gripSpaces{};
};

// The controllers as the XR worker last synced them.
struct Snapshot {
    bool valid = false;
    LONGLONG qpc = 0;
    input::InputFrame frame; // buttons, sticks, aim poses and velocities, head (tracking space)
    game::Controller controller = game::Controller::OculusTouch;
};

// The controllers at the time the camera hook predicted the head for (tracking space).
struct GameViewPoses {
    bool valid = false;
    Pose head;
    std::array<bool, 2> aimValid{};
    std::array<Pose, 2> aim{};
    std::array<bool, 2> gripValid{};
    std::array<Pose, 2> grip{};
};

// The weapon hand relative to the game's eye (weapon_pose.hpp), for the fire and viewmodel hooks.
struct WorldHand {
    bool valid = false;
    LONGLONG qpc = 0;
    xr_math::EyeRelativePose aim;  // the aim ray: origin offset and axis (forward = the ray)
    xr_math::EyeRelativePose grip; // the grip position with the aim axis (the barrel follows the ray)
    float unitsPerMetre = 1.0f;
};

struct State {
    // Written once by settingsOnce() (any thread), read-only afterwards.
    std::once_flag settingsOnce;
    input::ControllerSettings settings;
    game::WeaponOffsetTable offsets;

    // OpenXR handles: created and destroyed by the XR worker under the unique lock; the camera hook
    // locates under the shared lock.
    std::shared_mutex xrMutex;
    XrInput xr;
    std::atomic<bool> attached{false};

    std::mutex snapshotMutex;
    Snapshot snapshot;

    // The recenter transform from LOCAL into room space (setRoomFromLocal), applied by every locate.
    std::mutex roomMutex;
    Pose roomFromLocal;

    // Scripted input (ETERNALVR_TEST_INPUT): re-read by the camera hook when the file changes, read by the
    // worker's sync, the camera hook and the mapper.
    std::mutex testMutex;
    std::optional<input::TestInput> test;
    ULONGLONG testFileTime = 0;   // refreshTestInput only (under its own lock)
    std::uint64_t testChecks = 0; // refreshTestInput only

    // The weapon hand's aim smoothing (ETERNALVR_AIM_SMOOTHING; none when 0): created by settingsOnce,
    // then used by the camera hook only (beginGameView).
    std::optional<input::OneEuroRotation> aimFilter;

    // Camera hook writes, fire and viewmodel hooks read.
    std::mutex viewMutex;
    GameViewPoses poses;
    WorldHand world;
    // The game view's yaw in tracking space (radians, locomotion_direction.hpp convention), for the mapper.
    std::atomic<float> viewYawTracking{0.0f};

    // The mapper, under mapperMutex (user-command hook, or the pad sampler in XInput mode). The controller
    // data (suggested bindings and control maps per family) is written by attach under the same lock.
    std::mutex mapperMutex;
    std::array<input::ControllerData, 2> controllerData; // indexed by game::Controller
    std::unique_ptr<input::InputMapper> mapper;
    game::Controller mapperController = game::Controller::OculusTouch;
    bool mapperBroken = false; // the control map for mapperController has issues (reported once)
    input::ActionHold hold;
    input::ViewDeltaQueue viewQueue;
    // The weapon wheel's pointer: the stick as the game's cursor motion while the wheel is held.
    input::WheelMouse wheelMouse;
    LONGLONG lastMapQpc = 0;
    bool pauseKeyDown = false;
    // Keys sent while the game suppresses buttons (tutorial and lore popups read keys, not commands).
    bool popupSpaceDown = false;
    bool popupUseDown = false;
    // When the controllers last asked for a menu screen (menuRequestedWithin); 0 for never.
    std::atomic<LONGLONG> menuRequestQpc{0};
    std::atomic<LONGLONG> dossierRequestQpc{0}; // the same for the Dossier (dossierRequestedWithin)
    // The actions the controllers held at the last mapper run, before a menu held them back, and when
    // (heldActions).
    std::atomic<std::uint64_t> heldActionBits{0};
    std::atomic<LONGLONG> heldActionsQpc{0};
    // Skipping a cutscene by hand: the camera hook's flag, and the skip key's state (mapperMutex).
    std::atomic<bool> skippableCutscene{false};
    input::CutsceneSkip cutsceneSkip;
    bool skipKeyDown = false;
    input::GameInput lastInput;
    game::GameActionSet lastSent;

    // Forced angles: the SetViewAngles hook counts, the camera hook's gate decides (camera hook only).
    std::atomic<std::uint32_t> foreignSetViewAngles{0};
    input::ForcedAngleGate gate;
    std::atomic<bool> yielding{false};
    input::ForcedReason lastReason = input::ForcedReason::None;
    // The last usable hand aim angles and when (camera hook only), held briefly through tracking loss.
    xr_math::IdAngles lastHandAngles;
    LONGLONG lastHandQpc = 0;

    // Which game hooks are in place (installGameHooks, then read-only).
    bool userCmdHook = false;
    bool angleHook = false;
    bool setViewAnglesHook = false;
    bool fireHook = false;
    bool viewmodelHook = false;
    bool xinputHook = false;
    std::atomic<bool> xinputActive{false}; // the virtual gamepad feeds the game
    PlayerAim player;                      // the idPlayer vtable check

    // Statistics.
    std::atomic<std::uint64_t> syncs{0};
    std::atomic<std::uint64_t> commands{0};
    std::atomic<std::uint64_t> commandsWithInput{0};
    std::atomic<std::uint64_t> turnsApplied{0};
    std::atomic<std::uint64_t> shots{0};
    std::atomic<std::uint64_t> shotsRewritten{0};
    std::atomic<std::uint64_t> shotsOverHalfDegree{0};
    std::atomic<std::uint64_t> viewmodelWrites{0};
    std::atomic<std::uint64_t> padReads{0};
};

State& state();

// The settings and the offset table, read once from the environment on first use.
const input::ControllerSettings& settings();

// Locates `space` in room space (LOCAL under the recenter transform): true with both orientation and
// position valid. The caller holds xrMutex (shared at least). `room`: the room transform to use (the
// current one when null).
bool locate(const XrInput& xr,
            XrSpace space,
            XrTime time,
            Pose& out,
            Vec3* velocity = nullptr,
            bool* velocityValid = nullptr,
            const Pose* room = nullptr);

// Seconds since a QueryPerformanceCounter value.
double secondsSince(LONGLONG qpc);
LONGLONG nowQpc();

// Reads or writes game memory that may have been freed or changed; false instead of a crash.
bool safeCopy(void* destination, const void* source, std::size_t size);

template <typename T>
bool safeRead(const std::byte* at, T& value) {
    return at && safeCopy(&value, at, sizeof(T));
}

// True when `object` is the idPlayer, with its vtable read safely (the pointer came from game memory).
bool isPlayerSafe(const std::byte* object);

// The hand holding the weapon for the configured handedness.
input::Hand weaponHand();

// Runs the mapper once for `now` and returns the actions to send (held for the game, ActionHold) and
// the input; queues the turn for the angle hook and sends the weapon wheel's pointer as the game's cursor
// motion. Empty when the snapshot is stale.
struct MappedInput {
    bool live = false;
    game::GameActionSet actions;
    input::GameInput input;
    input::Axis2 turnStick; // the raw turn stick (the virtual gamepad's look when turning has no hook)
};
MappedInput runMapper();

// The whole of a small text file named by a setting (UTF-8 path), or nullopt.
std::optional<std::string> readTextFile(const std::string& utf8Path);

// Camera hook and XR worker: re-reads the ETERNALVR_TEST_INPUT file when it has changed (cheap; call every
// frame).
void refreshTestInput();
// The scripted input in force, if any (any thread).
std::optional<input::TestInput> testInput();

// Camera hook: feeds this frame's forced-view signals to the gate and sets `yielding`.
void updateForcedView(const std::byte* player, bool cutscene);

// Installers (each logs what it did); `image` checks were made by the caller.
bool installUserCmdHooks(bool buttonsAndMove, bool& angleInstalled);
bool installAimHooks(bool& fireInstalled);
bool installViewmodelHook();
bool installXInputHook();

} // namespace evr::vkcore::controllers
