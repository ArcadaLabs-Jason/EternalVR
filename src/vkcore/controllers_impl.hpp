#pragma once

// State shared by the controller sources (controllers.hpp): input_xr.cpp (OpenXR actions and the
// snapshot), game_view_poses.cpp (the poses at the camera hook's time, aim smoothing), usercmd_hook.cpp
// (the mapper and the user command), aim_hooks.cpp (forced angles, hand aim, shots), viewmodel_hook.cpp
// (the weapon at the hand, weapon FOV), xinput_hook.cpp (virtual gamepad) and haptics_xr.cpp with
// rumble_hook.cpp (vibration).
//
// Threads: the XR worker (attach, sync, detach), the camera hook (beginGameView, aimAngles, endGameView),
// the game's user-command build (the user-command and angle hooks), the game's weapon code (fire and
// viewmodel hooks) and the pad sampler (XInput). Each piece of state names its lock.
//
// State marked "hook thread only" (the viewmodel's offset cache, the off hand's blend and trace state, and
// camera_anim_hook.cpp's episodes) is plain, unlocked: it assumes the game runs that hook's caller on one
// thread at a time. That is likely (each is one per-frame call on the player) but not shown, while the
// camera hook itself is seen on more than one of the game's worker threads (aim_hooks.cpp). If the game
// ever runs two at once, those caches need a lock or a per-thread copy.

#include "features/input/aim_smoothing.hpp"
#include "features/input/binding_watch.hpp"
#include "features/input/controller_bindings.hpp"
#include "features/input/controller_settings.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/cutscene_skip.hpp"
#include "features/input/forced_angles.hpp"
#include "features/input/game_input.hpp"
#include "features/input/haptics_policy.hpp"
#include "features/input/input_mapper.hpp"
#include "features/input/test_input.hpp"
#include "features/input/usercmd_injection.hpp"
#include "features/input/wheel_hand.hpp"
#include "features/input/wheel_mouse.hpp"
#include "features/input/xr_action_set.hpp"
#include "game/eternal/controller_data.hpp"
#include "game/eternal/weapon_offsets.hpp"
#include "vkcore/controllers.hpp"
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
#include <vector>

namespace evr::vkcore::controllers {

// A snapshot older than this is not used (the XR worker stopped or the session lost focus).
inline constexpr double kSnapshotStaleSeconds = 0.25;
// World hand poses older than this are not used by the fire and viewmodel hooks.
inline constexpr double kWorldStaleSeconds = 0.1;

// Every family's controller data, indexed by game::Controller.
using FamilyData = std::array<input::ControllerData, game::kControllerCount>;

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
    PFN_xrApplyHapticFeedback xrApplyHapticFeedback = nullptr;
    PFN_xrStopHapticFeedback xrStopHapticFeedback = nullptr;
    // Diagnostics only (input_watch.cpp): null when the runtime does not give them.
    PFN_xrEnumerateBoundSourcesForAction xrEnumerateBoundSourcesForAction = nullptr;
    PFN_xrGetInputSourceLocalizedName xrGetInputSourceLocalizedName = nullptr;

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

// What happened for the vibration since the XR worker's last sync (haptics_xr.cpp).
struct HapticEvents {
    bool fireHeld = false; // the fire action as the mapper last sent it (a level)
    LONGLONG fireQpc = 0;  // when; 0 for never
    std::array<bool, 2> punch{};
    bool capture = false;
    std::array<input::MenuTick, 2> menu{};
    input::GameRumble rumble; // the game's motors as its rumble hook last saw them (a level)
    LONGLONG rumbleQpc = 0;   // when; 0 for never
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
    bool offValid = false;
    xr_math::EyeRelativePose offGrip; // the off hand's grip (position and orientation)
    // The off-hand arm's IK (offhand_hook.cpp): the shoulder fixed to the head (relative to the eye) and
    // the elbow's bend direction (world), both in the head's yaw frame.
    Vec3 offShoulder;
    Vec3 offElbow;
    // The weapon arm's IK (weapon_arm.cpp): the same, on the weapon hand's side.
    Vec3 weaponShoulder;
    Vec3 weaponElbow;
    float unitsPerMetre = 1.0f;
};

// Where the viewmodel hook last placed the arms model (relative to the eye), for the off hand.
struct ModelPlacement {
    bool valid = false;
    LONGLONG qpc = 0;
    xr_math::EyeRelativePose pose;
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

    // XR worker only: the interaction profile each hand last reported (empty for none, by input::Hand), and
    // whether the runtime binds our actions (input_watch.cpp; reset by attach).
    std::array<std::string, 2> handProfiles;
    input::BindingWatch bindingWatch;

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
    ModelPlacement model;
    // The game view's yaw in tracking space (radians, locomotion_direction.hpp convention), for the mapper.
    std::atomic<float> viewYawTracking{0.0f};

    // The mapper, under mapperMutex (user-command hook, or the pad sampler in XInput mode). The controller
    // data (suggested bindings and control maps per family) is written by attach under the same lock.
    std::mutex mapperMutex;
    FamilyData controllerData;
    std::unique_ptr<input::InputMapper> mapper;
    game::Controller mapperController = game::Controller::OculusTouch;
    bool mapperBroken = false; // the control map for mapperController has issues (reported once)
    input::ActionHold hold;
    bool menuHold = false; // a menu held gameplay input back at the last mapper run
    input::ViewDeltaQueue viewQueue;
    // The weapon wheel's pointer: the stick as the game's cursor motion while the wheel is held.
    input::WheelMouse wheelMouse;
    // Under ETERNALVR_WHEEL_SELECT=hand the weapon hand's turn is that pointer (created on first use).
    std::optional<input::WheelHand> wheelHand;
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
    // The turn rate and move magnitude at the last mapper run, after a menu held them back, and when
    // (artificialMotion).
    std::atomic<float> motionTurnRate{0.0f};
    std::atomic<float> motionMove{0.0f};
    std::atomic<LONGLONG> motionQpc{0};
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
    bool offhandHook = false;
    bool weaponArmHook = false; // the off-hand hook also bends the weapon arm (weapon_arm.cpp)
    bool xinputHook = false;
    bool rumbleHook = false;
    bool demonAimHook = false;
    bool facingHook = false;
    bool climbHook = false;
    bool promptHooks = false;
    std::atomic<bool> xinputActive{false}; // the virtual gamepad feeds the game
    PlayerAim player;                      // the idPlayer vtable check

    // Vibration: the events wait under hapticsMutex for the XR worker, which alone runs the policy (created
    // on its first sync) and keeps the summary's state.
    std::mutex hapticsMutex;
    HapticEvents hapticEvents;
    std::optional<input::HapticsPolicy> haptics;
    std::array<std::atomic<std::uint64_t>, input::kHapticSourceCount> hapticPulses{};
    std::atomic<std::uint64_t> hapticRefused{0};
    ULONGLONG hapticLogTicks = 0;
    std::uint64_t hapticLoggedTotal = 0;
    std::uint64_t hapticLoggedRefused = 0;

    // Statistics.
    std::atomic<std::uint64_t> syncs{0};
    std::atomic<std::uint64_t> commands{0};
    std::atomic<std::uint64_t> commandsWithInput{0};
    std::atomic<std::uint64_t> turnsApplied{0};
    std::atomic<std::uint64_t> shots{0};
    std::atomic<std::uint64_t> shotsRewritten{0};
    std::atomic<std::uint64_t> shotsOverHalfDegree{0};
    std::atomic<std::uint64_t> viewmodelWrites{0};
    std::atomic<std::uint64_t> offhandWrites{0};
    std::atomic<std::uint64_t> padReads{0};
};

State& state();

// The text of an OpenXR result, for logs.
const char* xrText(XrResult result, char (&buffer)[XR_MAX_RESULT_STRING_SIZE]);

// Returns false from the calling function, logged with the file's kTag, when an OpenXR call fails.
#define EVR_XR_TRY(call)                                                                                     \
    do {                                                                                                     \
        const XrResult evr_r = (call);                                                                       \
        if (XR_FAILED(evr_r)) {                                                                              \
            char evr_text[XR_MAX_RESULT_STRING_SIZE];                                                        \
            EVR_LOG("%s: %s failed: %s", kTag, #call, xrText(evr_r, evr_text));                              \
            return false;                                                                                    \
        }                                                                                                    \
    } while (0)

// Controller families (input_profiles.cpp). Every family's data: the built-in files, with a player's file
// (ETERNALVR_CONTROLLER_DATA, a file or every *.toml in a folder) in place of the family whose profile it
// names.
FamilyData loadControllerData(const input::ControllerSettings& settings);
// Suggests the bindings of every family whose profile the instance has, and logs which were suggested and
// which skipped. True when at least one profile was accepted.
bool suggestAllBindings(XrInput& xr, const ProfileSupport& support, const FamilyData& data);
// The family whose control map is used, from the profiles the runtime reports for both hands
// (controller_family.hpp; `current` when neither has one we have data for). The profile of each hand is kept
// in handProfiles and logged when it changes, a profile without data once. XR worker only.
game::Controller currentFamily(const XrInput& xr, State& s, game::Controller current);

// Whether the runtime binds our actions (input_watch.cpp). XR worker, inside sync under the shared xrMutex:
// feeds the watch what this sync saw (before any scripted input; the time and the profiles are filled in)
// and lists the bound sources, or warns, when it asks.
void watchBindings(const XrInput& xr, State& s, input::BindingWatchSample sample);

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

// idPlayer::syncMaster's object [inferred]: never changed in headset sessions (docs/BHAPTICS.md), so only a
// fallback for idPlayer::savedSyncEntity's object, the sync entity of the animation the player is in for its
// whole length (a glory kill's `syncmelee/<demon>`, a pickup's `interact/...`).
inline constexpr std::size_t kPlayerSyncMaster = 0x7DA8 + 8;
inline constexpr std::size_t kPlayerSavedSync = 0x8420 + 8;
// idEntity::entityDef: the entity's decl (its name through itemDeclName).
inline constexpr std::size_t kEntityDef = 0xA8;

// The hand holding the weapon for the configured handedness.
input::Hand weaponHand();

// Runs the mapper once for `now` and returns the actions to send (held for the game, ActionHold) and
// the input; queues the turn for the angle hook and sends the weapon wheel's pointer as the game's cursor
// motion. Empty when the snapshot is stale.
struct MappedInput {
    bool live = false;
    game::GameActionSet actions;
    input::GameInput input;
    input::Axis2 turnStick; // the turn stick as the mapper read it (the pad's look when turning has no hook)
};
MappedInput runMapper();

// The mapper for `controller` (control_map.cpp), built from the family's control map when there is none or it
// is for another family, which also gives the game's prompts the buttons' names; false when the map has
// issues (reported once, when it was built). The caller holds mapperMutex. `when` is added to the log line.
bool ensureMapper(State& s, game::Controller controller, const char* when);
// XR worker, once the runtime reports a controller: builds the control map ahead of the mapper's first run,
// so the title screen's and the main menu's prompts name the buttons too (they build no user commands).
void prepareControlMap(game::Controller controller);

// The whole of a small text file named by a setting (UTF-8 path), or nullopt.
std::optional<std::string> readTextFile(const std::string& utf8Path);
// The names of the files directly inside a folder (UTF-8 path; no subfolders), or nullopt when the path is
// not a folder.
std::optional<std::vector<std::string>> folderFileNames(const std::string& utf8Path);

// Camera hook and XR worker: re-reads the ETERNALVR_TEST_INPUT file when it has changed (cheap; call every
// frame).
void refreshTestInput();
// The scripted input in force, if any (any thread).
std::optional<input::TestInput> testInput();

// Camera hook: feeds this frame's forced-view signals to the gate and sets `yielding`. `wallClimb`: on a
// climbable wall whose view is the player's own (climbFrame).
void updateForcedView(const std::byte* player, bool cutscene, bool cameraAnimation, bool wallClimb);

// The local player's foreign SetViewAngles calls since the last take (aim_hooks.cpp): how many, how many
// turned the view (pitch or yaw left the player's angles), the largest turn in degrees and its caller's RVA.
struct ForeignViewWrites {
    std::uint32_t writes = 0;
    std::uint32_t turns = 0;
    float largestTurnDegrees = 0.0f;
    std::uintptr_t largestTurnCaller = 0;
};
// Camera hook, once a game frame (climbFrame): takes and clears the counts.
ForeignViewWrites takeForeignViewWrites();

// Climbable walls (climb_hook.cpp). Camera hook, every game frame whether or not the controllers are
// attached: holds the wall-climb cvars and returns whether the player is on a climbable wall with the view
// the player's own (false with ETERNALVR_CLIMB_LOOK=0, under aim view, or without the hook).
bool climbFrame();
// Camera hook: the gate had hand aim aim with the head this frame (the counters).
void noteClimbAim(bool headAims);

// Vibration (haptics_xr.cpp). The mapper, after a menu held its actions back: the actions sent and the
// command's punch and capture.
void noteMapperHaptics(const game::GameActionSet& sent, const input::GameInput& input);
// XR worker, inside sync under the shared xrMutex: runs the policy on the events and sends its pulses (none
// while the session is not focused).
void updateHaptics(const XrInput& xr, bool focused);

// The held item (viewmodel_hook.cpp): the decl in `hands`' right hand (idHands::rightItem), or null, and a
// decl's name ("weapon/player/shotgun"), or empty.
const std::byte* heldItemDecl(const std::byte* hands);
std::string itemDeclName(const std::byte* decl);

// bHaptics (bhaptics_game.cpp, docs/BHAPTICS.md). With the game hooks: starts its thread once when
// ETERNALVR_BHAPTICS=1. The fire hook: a shot of the local player's `hands`. The camera hook, once a game
// frame: the player's health, armor, hits and sync kill. The portal hooks (bhaptics_portal.cpp, installed by
// startBhaptics): the player went through a portal, a pad or a level exit. The launch hooks
// (bhaptics_launch.cpp, the same): a jump pad or a booster launched the player (once per launch, already
// filtered). All only note; nothing waits on the network.
void startBhaptics();
void noteBhapticsShot(const std::byte* hands);
void noteBhapticsFrame(const std::byte* player);
void noteBhapticsPortal();
void noteBhapticsLaunch();
bool installBhapticsPortalHooks();
bool installBhapticsLaunchHooks();

// Installers (each logs what it did); `image` checks were made by the caller.
bool installUserCmdHooks(bool buttonsAndMove, bool& angleInstalled);
bool installAimHooks(bool& fireInstalled);
bool installViewmodelHook();
// The hook on the game's left-hand modifier, which also bends the weapon arm (`weaponArmInstalled`).
bool installOffhandHook(bool& weaponArmInstalled);
bool installXInputHook();
bool installRumbleHook();
bool installFacingHook();
bool installClimbHook();
bool installPromptHooks();

// The game's prompts name the buttons of `controller` under `profile` from now on (prompt_hooks.cpp). Called
// under mapperMutex whenever the mapper is built (ensureMapper); nothing changes when the texts are the same.
void publishPromptLabels(const input::BindingProfile& profile, game::Controller controller);

} // namespace evr::vkcore::controllers
