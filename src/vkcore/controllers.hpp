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
//
// Everything is off unless ETERNALVR_CONTROLLERS=1. Every hook is installed only while the multiplayer
// guard is armed, and every callback asks mp_guard::allowsGameTouch() before it writes (mp-guard.md).

#include "common/pose.hpp"
#include "common/vector.hpp"
#include "features/input/controller_state.hpp"
#include "game/eternal/game_action.hpp"
#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <openxr/openxr.h>

#include <cstddef>
#include <optional>

namespace evr::vkcore::controllers {

struct XrContext {
    PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE; // the presenter's LOCAL space; poses are located in it
};

// XR worker, once the session and LOCAL space exist: reads the settings, creates and attaches the action
// sets, suggests every controller family's bindings and creates the pose spaces. False (logged) when
// controllers are off or anything fails; the game then runs with the keyboard, mouse and pad only.
bool attach(const XrContext& xr);

// XR worker, before the session or the LOCAL space is destroyed. Waits for the camera hook's locates.
void detach();

// XR worker, every frame of a running session, after xrWaitFrame: syncs the gameplay actions and
// publishes the controller snapshot the user command is built from.
void sync(XrTime predictedDisplayTime, bool focused);

// XR worker: the controllers as the last sync saw them (LOCAL, with scripted input laid over), or nullopt
// when controllers are off or the snapshot is stale. The menu pointer reads its rays from here.
std::optional<input::InputFrame> latestFrame();
// The weapon hand (the dominant hand: right unless ETERNALVR_HANDEDNESS says left).
input::Hand dominantHand();

// XR worker: the weapon hand's aim space (-Z along its pointing ray) while hand aim is on, for a reticle
// on that ray; XR_NULL_HANDLE otherwise.
XrSpace weaponAimSpace();

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
// cutscene flag). Under hand aim, returns the weapon hand's aim ray for the view's record (the reticle);
// nullopt otherwise or while the hand is not tracked.
std::optional<WeaponAim>
beginGameView(XrTime poseTime, const Pose& headTracking, const std::byte* player, bool cutscene);
// (headTracking is the head in room space.)

// Camera hook, every game frame: whether a cutscene plays that the player may skip by hand (the layer's
// automatic skip is off). While one does, holding the dash action holds the game's skip key
// (features/input/cutscene_skip.hpp).
void noteSkippableCutscene(bool playing);

// Any thread: whether the controllers asked the game for a menu screen (the pause key, the Dossier or
// mission information; game::opensMenu) within the last `seconds`. A menu screen that comes up without
// that is one the game raised by itself (a tutorial or lore popup; docs/VR_MENUS.md).
bool menuRequestedWithin(double seconds);
// Any thread: the same for the Dossier (or the automap, one of its pages) alone.
bool dossierRequestedWithin(double seconds);

// Any thread: the gameplay actions the controllers hold now, as the control map maps them before a menu
// holds them back (in a tutorial popup the menu router presses their keys). Empty while the controllers are
// off or the mapper has not run for a moment.
game::GameActionSet heldActions();

// Any thread: whether the game forced the view in the last game frame or within the gate's resume delay (a
// glory kill, the Meathook pull, a melee lunge, a scripted camera; forced_angles.hpp). False while the
// controllers are off.
bool forcedView();

// Camera hook, inside head aim: the angles the view follows. The head's own, or under hand aim the weapon
// hand's ray; nullopt when hand aim yields this frame (forced view) and nothing may be written.
std::optional<xr_math::IdAngles> aimAngles(const xr_math::IdAngles& head);

// Camera hook, after the view is written: the weapon hand's pose in the world for the viewmodel and the
// shots, and the weapon FOV. `eye` is the game's view origin before the head's offset was added.
void endGameView(std::byte* renderView,
                 const std::byte* player,
                 const xr_math::IdViewAxis& body,
                 Vec3 eye,
                 Vec3 headOffset,
                 float unitsPerMetre);

} // namespace evr::vkcore::controllers
