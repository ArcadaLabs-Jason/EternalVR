#pragma once

// Head aim: the headset drives the game's own view angles, so aim, crosshair and weapon follow the head.
//
// id Tech angles (idAngles) are degrees: yaw turns counter-clockwise about +Z starting from +X, pitch is
// positive looking DOWN, roll turns about the forward axis. The game's view angles are the user
// command's angles plus a delta (deltaViewAngles); head aim adds to that delta each game frame.
//
// Yaw: the mouse keeps turning the body. The game's yaw holds body + the head yaw injected last frame,
// so body = game yaw - last injected head yaw, and the next injection moves the game to body + head.
// Pitch: owned by the head. Each frame the game's pitch is moved to the head's pitch.
// When the game rewrites the delta itself, the rewritten value decides how much head yaw it holds. After a
// cutscene the game holds the delta at a snapshot for a few seconds, and that snapshot is a value head aim
// wrote (with the head yaw injected then), so the values written recently are remembered with the head yaw
// each carried. Any other value (a glory kill, a teleport, a scripted view) is the game re-aiming the view
// the player had: it is taken to hold the head yaw injected last, so the view faces where the game put it
// and the body keeps its heading in the room. Taking it to hold none turned the view by the head's whole
// yaw in the room (a standing player turned round in the room ended every glory kill facing backwards).

#include "common/quat.hpp"
#include "xr_math/head_view.hpp"

#include <array>
#include <cstddef>

namespace evr::xr_math {

struct IdAngles {
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
};

// Wraps degrees into [-180, 180).
float normalize180(float degrees);

// True when all three angles are finite and within +-1e6 degrees. Angles read from game memory are
// checked with this before anything derived from them is written back.
bool plausible(const IdAngles& angles);

// idAngles::ToMat3: rows forward, left, up.
IdViewAxis axisFromAngles(const IdAngles& angles);

// The yaw and pitch of an axis's forward vector, with roll from its up vector. Looking straight up or
// down gives yaw 0.
IdAngles anglesFromAxis(const IdViewAxis& axis);

// The head's yaw and pitch (degrees, id Tech convention) from its orientation in id Tech axes; roll 0.
IdAngles headAngles(Quat headInIdTech);

struct HeadAimState {
    bool injected = false;    // a head yaw is currently part of the game's yaw
    float injectedYaw = 0.0f; // that head yaw

    struct Written {
        float deltaYaw = 0.0f;    // a deltaViewAngles yaw head aim wrote
        float injectedYaw = 0.0f; // the head yaw it carried
    };
    std::array<Written, 32> written{}; // the most recent writes, a ring
    std::size_t writtenCount = 0;
    bool hasRestored = false; // the last rewrite matched `restored`
    Written restored{};
    bool restoredIsOurs = false; // `restored` is a value head aim wrote, not one of the game's own
};

struct HeadAimStep {
    float bodyYaw = 0.0f;       // the game's yaw without the head
    float deltaYaw = 0.0f;      // add to deltaViewAngles.yaw
    float deltaPitch = 0.0f;    // add to deltaViewAngles.pitch
    bool restoredWrite = false; // the game rewrote the delta to a value head aim had written
};

// One game frame. `game` is the game's own angles as its delta now stands: the user command's angles plus
// deltaViewAngles (not the frame's view angles, which the game can build before it rewrites the delta,
// or from a scripted source); `deltaYaw` that delta's yaw; `head` the head's angles; `gameRewrote` is
// true when the game replaced the delta since the last injection. pitchLimit clamps the target.
HeadAimStep headAimStep(HeadAimState& state,
                        const IdAngles& game,
                        float deltaYaw,
                        const IdAngles& head,
                        bool gameRewrote,
                        float pitchLimit = 89.0f);

// The head yaw the game's yaw holds now, for a frame head aim does not write (a forced view): what
// headAimStep would subtract, without changing the state. `deltaYaw` and `gameRewrote` as for headAimStep.
// A value of the game's own holds what the delta held before it (viewHeadYaw).
float headYawHeld(const HeadAimState& state, float deltaYaw, bool gameRewrote);

// The head yaw the player's view angles held in the last frame head aim aimed: the one the delta the game
// held then carries (a value head aim wrote earlier, which the game put back, or its own), else the head yaw
// injected last. While the game keeps putting a delta back, head aim's writes never reach the view, so the
// head yaw injected last is not in it: in gamepad mode Use at a Sentinel Crystal put back a delta written
// with the head 8 degrees from where it was when the menu came up (rig, 2026-11-12, run pa9).
float viewHeadYaw(const HeadAimState& state);

// The body yaw under a scripted camera, which starts from the player's view angles and ignores the delta:
// the camera's heading less the head yaw those angles held (viewHeadYaw).
float cameraBodyYaw(const HeadAimState& state, float cameraYaw);

// The body yaw of a view the game drives (a forced view's angles or a scripted camera's heading): that yaw
// without the head yaw it holds. The head-tracked view (body + head) then faces where the game points it
// while the head is where it was when the game took over, and turns with the head from there.
float drivenBodyYaw(const HeadAimState& state, float gameYaw, float deltaYaw, bool gameRewrote);

// True when the frame's view is a camera the game drives, not the player's own view angles: its forward is
// more than 15 degrees from theirs (a glory kill's camera, the Fortress's collectible viewer).
bool drivenCamera(const IdViewAxis& playerView, const IdViewAxis& rendered);

// Head aim's choice for one view: the game drives it (the view faces the camera's heading, nothing is
// written) for a camera past drivenCamera's 15 degrees, and for every view of a pickup's camera shown with
// Still view
// (`pickupFollow`). Decided per view by the 15 degrees alone, a pickup's camera that swings across them
// turned the view to its heading and back: under head aim the player's view angles are the head's, and the
// Sentinel Crystal's camera, 121 to 160 degrees of yaw about the player's 130, took the view 9.5 degrees in
// one frame as it passed the line and 8.6 back (rig, 2026-11-12, run pa3). Under hand aim the hand's angles
// were far enough from that camera for it to drive every view of the pickup.
bool cameraDrivesView(bool pickupFollow, const IdViewAxis& playerView, const IdViewAxis& rendered);

// The body yaw while a menu or popup is up and head aim writes nothing. Over the player's own view (the pause
// menu, the Dossier, a tutorial popup) it is `heldYaw`, the body yaw from when the menu came up, so the world
// stays put while the head looks around. Under a camera the game drives with the menu up (the Fortress's
// collectible viewer: its orbit camera faces the toy or album from a side of the game's choosing, 90 degrees
// from the player's heading for the toys' close-up), it is the camera's heading without the head yaw the
// player's view angles hold (cameraBodyYaw): the view faces what the camera shows while the head is where it
// was when the menu came up.
// `pickupFollow`: a pickup's camera shown with Still view, which drives every view (cameraDrivesView).
float menuBodyYaw(const HeadAimState& state,
                  float heldYaw,
                  const IdViewAxis& playerView,
                  const IdViewAxis& rendered,
                  bool pickupFollow = false);

// Records the delta yaw just written (after the step's deltaYaw was added) with the head yaw it carries.
void noteWritten(HeadAimState& state, float writtenDeltaYaw);

// A test head motion in OpenXR axes: yaw (about +Y, positive turns left) and pitch (about +X, positive
// looks up), each `amplitude * sin(2 pi t / period)` degrees; applied as sway * head. Identity when the
// period is not positive.
Quat headSway(float yawDegrees, float pitchDegrees, float periodSeconds, double seconds);

// A constant test head turn in the same axes (the sway's value at its quarter-period peak).
Quat headTurn(float yawDegrees, float pitchDegrees);

// ETERNALVR_TEST_HEAD_SWAY's whole turn: the sway around a held view of `baseYaw` and `basePitch` (degrees,
// as headTurn): yaw base * sway * pitch base, so a yaw sway turns about the vertical with the head held
// tilted. Applied as turn * head.
Quat headSwayAround(float baseYaw,
                    float basePitch,
                    float yawDegrees,
                    float pitchDegrees,
                    float periodSeconds,
                    double seconds);

} // namespace evr::xr_math
