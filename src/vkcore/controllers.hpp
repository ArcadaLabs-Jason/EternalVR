#pragma once

// Motion controllers in the game (M5, docs/VR_CONTROLLERS.md, docs/rig-findings/input-aim.md).
//
// - OpenXR input (input_xr.cpp): the action sets of features/input/xr_action_set.hpp with the suggested
//   bindings of every controller data file, synced on the XR worker every frame into a snapshot, and the
//   controller poses located on the camera hook at the time the head was predicted for, the weapon hand's
//   aim smoothed there (game_view_poses.cpp).
// - The user command (usercmd_hook.cpp): buttons ORed and movement added where the game hands its command
//   to the user-command manager; turning added to the command generator's accumulated angles.
// - Hand aim (aim_hooks.cpp): under ETERNALVR_AIM=hand the view angles follow the weapon hand's ray
//   through head aim's closed loop (T-055), yielding while the game forces the view; shots start at the
//   hand and fly along its ray.
// - The viewmodel (viewmodel_hook.cpp): the game's arms and weapon at the controller with per-weapon
//   offsets (T-054), drawn with the headset's FOV.
// - The virtual gamepad (xinput_hook.cpp): the fallback when the user-command hooks cannot be installed.
// - Vibration (haptics_xr.cpp): pulses for fire, punches, the menu pointer, the capture and the game's
//   rumble, sent by the XR worker (features/input/haptics_policy.hpp).
//
// Everything is off unless ETERNALVR_CONTROLLERS=1. Every hook is installed only while the multiplayer
// guard is armed, and every callback asks mp_guard::allowsGameTouch() before it writes (mp-guard.md).

#include "common/pose.hpp"
#include "common/vector.hpp"
#include "features/input/capture_chord.hpp"
#include "features/input/controller_settings.hpp"
#include "features/input/controller_state.hpp"
#include "features/input/haptics_policy.hpp"
#include "game/eternal/game_action.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <openxr/openxr.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace evr::vkcore::controllers {

// The controller-profile extensions an instance was created with (interaction_profiles.hpp), and whether
// it is an OpenXR 1.1 instance.
struct ProfileSupport {
    std::vector<std::string> extensions;
    bool api11 = false;
};

// XR worker, before xrCreateInstance: the extensions of the controller profiles we have bindings for that
// the runtime offers (each one logged as enabled or not offered).
std::vector<std::string> profileExtensions(std::span<const XrExtensionProperties> offered);

struct XrContext {
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE; // the presenter's LOCAL space; poses are located in it
    ProfileSupport profiles;
};

// XR worker, once the session and LOCAL space exist: reads the settings, creates and attaches the action
// sets, suggests the bindings of every controller family whose profile the instance has (a profile whose
// extension is missing is skipped, and one the runtime refuses does not stop the others) and creates the
// pose spaces. False (logged) when controllers are off or anything fails; the game then runs with the
// keyboard, mouse and pad only.
bool attach(const XrContext& xr);

// XR worker, before the session or the LOCAL space is destroyed. Waits for the camera hook's locates.
void detach();

// XR worker, every frame of a running session, after xrWaitFrame: syncs the gameplay actions and
// publishes the controller snapshot the user command is built from.
void sync(XrTime predictedDisplayTime, bool focused);

// XR worker: the controllers as the last sync saw them (LOCAL, with scripted input laid over), or nullopt
// when controllers are off or the snapshot is stale. The menu pointer reads its rays from here.
std::optional<input::InputFrame> latestFrame();
// The capture chord's buttons for the runtime and the controller family in use (dashboard_pause.hpp), for
// the menu pointer's own chord.
input::CaptureButtons captureButtons();
// The weapon hand (the dominant hand: right unless ETERNALVR_HANDEDNESS says left).
input::Hand dominantHand();
// Which stick pans the Dossier's map (ETERNALVR_MAP_STICKS): the weapon hand's unless the player chose the
// other one.
input::MapSticks mapSticks();

// What aims now: the main aim (ETERNALVR_AIM), or while piloting a demon the demon's (input::demonAimSource).
input::AimSource activeAim();

// XR worker: the weapon hand's aim space (-Z along its pointing ray) while hand aim is on (activeAim), for a
// reticle on that ray; XR_NULL_HANDLE otherwise.
XrSpace weaponAimSpace();

// XR worker: the off hand's grip space while the controllers are attached, for the wrist HUD's quads (the
// runtime places them at display time, whatever the recenter transform); XR_NULL_HANDLE otherwise.
XrSpace offHandGripSpace();

// XR worker: the weapon hand's aim space while the controllers are attached, whatever the aim source, for
// the weapon HUD's quads (placed like the wrist's); XR_NULL_HANDLE otherwise.
XrSpace weaponHandAimSpace();

// XR worker, while the multiplayer guard is armed (head-tracked mode): installs the game hooks.
void installGameHooks();

// Camera hook, before beginGameView: the room transform (recenter, room_scale.hpp). Every controller and
// head pose the controllers locate is taken from LOCAL into room space with it, so the hands, the head and
// the game view share one space.
void setRoomFromLocal(const Pose& roomFromLocal);

// The weapon hand's aim ray at a game view's pose time, in LOCAL: as tracked, and as the gun, the shots
// and the reticle use it (smoothed, features/input/aim_smoothing.hpp).
struct WeaponAim {
    Pose tracked;
    Pose used;
};

// Camera hook, once per game frame after the head pose is known: the controllers at the same time, and
// whether the game forces the view this frame (`player` is the view's object, `cutscene` the game view's
// cutscene flag, `cameraAnimation` a hands animation moving the camera). Under hand aim, returns the weapon
// hand's aim ray for the view's record (the reticle); nullopt otherwise or while the hand is not tracked.
std::optional<WeaponAim> beginGameView(
    XrTime poseTime, const Pose& headTracking, const std::byte* player, bool cutscene, bool cameraAnimation);
// (headTracking is the head in room space.)

// Camera hook, every game frame: whether a cutscene plays that the player may skip by hand (the layer's
// automatic skip is off). While one does, holding the dash action holds the game's skip key
// (features/input/cutscene_skip.hpp).
void noteSkippableCutscene(bool playing);

// Camera hook, every game frame before endGameView: whether the frame is a cutscene shown around the player
// (ETERNALVR_CUTSCENES=immersive). Under ETERNALVR_CUTSCENE_ARMS=hidden (the default) the first-person arms
// are hidden meanwhile (hands_surfaces.hpp) and the game's own weapon FOV is kept for its hands model.
void noteImmersiveCutscene(bool playing);
// Any thread: whether that holds now (the last game frame was such a cutscene and the setting hides the
// arms).
bool cutsceneArmsHidden();

// Any thread: whether the controllers asked the game for a menu screen (the pause key, the Dossier or
// mission information; game::opensMenu) within the last `seconds`. A menu screen that comes up without
// that is one the game raised by itself (a tutorial or lore popup; docs/VR_MENUS.md).
bool menuRequestedWithin(double seconds);
// Any thread: the same for the Dossier (or the automap, one of its pages) alone.
bool dossierRequestedWithin(double seconds);

// XR worker (the menu pointer): a light tick on `hand` for the vibration (haptics_policy.hpp).
void noteMenuHaptic(input::Hand hand, input::MenuTick tick);
// Game thread (the rumble hook): the game's rumble motors this frame, 0..1 each.
void noteGameRumble(float low, float high);

// Any thread: the gameplay actions the controllers hold now, as the control map maps them before a menu
// holds them back (in a tutorial popup the menu router presses their keys). Empty while the controllers are
// off or the mapper has not run for a moment.
game::GameActionSet heldActions();

// Any thread: whether the game forced the view in the last game frame or within the gate's resume delay (a
// glory kill, the Meathook pull, a melee lunge, a scripted camera; forced_angles.hpp). False while the
// controllers are off.
bool forcedView();

// Any thread: whether the weapon wheel is up with only its own inhibit bits, so only the aim yields and
// forcedView() is false (forced_angles.hpp). The game skips its view update meanwhile.
bool wheelView();
// Any thread: the sync entity of the animation `player` (the idPlayer) is in, or null: idPlayer::
// savedSyncEntity, else idPlayer::syncMaster (docs/BHAPTICS.md). A pickup's animation is a sync too.
const std::byte* syncEntity(const std::byte* player);
// Camera hook: whether `player` (the view's object) is the idPlayer and a glory kill runs: a sync entity
// that is not a pickup's animation (comfort::isKillSync). False for any other object, and while the
// controllers are off.
bool syncKillActive(const std::byte* player);

// Writes back the game's own values of the wall-climb cvars the layer held (climb_hook.cpp), once; nothing
// if it wrote none or gave them back already. The camera hook when the multiplayer guard has stopped game
// touches (the hold itself no longer runs then), and the presenter's shutdown after the camera hook's last
// callback. `why` goes into the log line.
void restoreClimbCvars(const char* why);
// Writes back the game's own weaponWheel_slowTimeScale when the thumb-rest wheel holds it (its slowdown
// turned off, rest_wheel.cpp); nothing otherwise. The same callers as restoreClimbCvars; `why` goes into the
// log line.
void restoreWheelSlowdown(const char* why);

// Any thread: the artificial motion of the last mapper run, for the comfort vignette: the turn rate (degrees
// per second, smooth or snap, either direction) and the move stick's magnitude after its response (0 to 1).
// Zero while a menu holds the controllers back, while they are off or when the mapper has not run for a
// moment.
struct ArtificialMotion {
    float turnDegreesPerSecond = 0.0f;
    float move = 0.0f;
};
ArtificialMotion artificialMotion();

// Camera hook, inside head aim: the angles the view follows. The head's own, or under hand aim the weapon
// hand's ray (the head's or the off hand's while a melee or equipment press asks for it,
// action_aim_hook.cpp); nullopt when hand aim yields this frame (forced view) and nothing may be written.
std::optional<xr_math::IdAngles> aimAngles(const xr_math::IdAngles& head);
// Camera hook, inside head aim, once the angles aimAngles gave are written into the game: a melee press
// waiting for them may go out with the next command.
void noteAimWritten();
// Camera hook, inside head aim, on a frame it writes nothing although hand aim does not yield (a menu or
// popup is up, a scripted camera): a melee press waiting for the view goes out at once.
void noteAimPaused();

// Camera hook, while piloting a demon (demon_aim.cpp): the angles the demon aims at, the head's or the weapon
// hand's ray (input::demonAimSource), whatever the game's view is doing: the demon's update runs through a
// forced view every tick.
xr_math::IdAngles demonAimAngles(const xr_math::IdAngles& head);

// Camera hook: the forward direction of the view it wrote (engine world axes), for the look-at triggers
// (facing_hook.cpp), which test where the head looks rather than where the gun points.
void noteViewForward(float x, float y);

// Camera hook, after the view is written: the weapon hand's pose in the world for the viewmodel and the
// shots, and the weapon FOV. `eye` is the game's view origin before the head's offset was added.
void endGameView(std::byte* renderView,
                 const std::byte* player,
                 const xr_math::IdViewAxis& body,
                 Vec3 eye,
                 Vec3 headOffset,
                 float unitsPerMetre);

// Camera hook, after endGameView: the weapon hand's aim ray in the game's world, the start (the game's eye
// `eye` plus the hand's offset, as hand-origin shots start) and its unit direction; nullopt without one.
struct WorldRay {
    Vec3 origin;
    Vec3 direction;
};
std::optional<WorldRay> weaponRayInWorld(Vec3 eye);

} // namespace evr::vkcore::controllers
