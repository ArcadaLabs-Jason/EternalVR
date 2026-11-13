#pragma once

// A pickup camera's turn on top of the head-tracked view (comfort/pickup_camera.hpp, docs/rig-findings/
// camera-animations.md section 6).
//
// A pickup's sync animation drives the game's camera for a few seconds. In the flat game the camera cuts to
// the animation's own start in one frame (the Praetor token's: from the player's 15 degrees down to 5.6
// degrees), then moves smoothly (down to 11.4 degrees over 1.3 s, up to 2.2 degrees over 0.45 s), and the
// game puts the player's view angles where it ended. On the head-tracked view the head is where the view
// starts, so the part played is the camera's smooth motion since it took the view: its turn from one game
// frame to the next, summed. A step larger than CameraTurnTiming::cutDegrees in one frame is a cut (the
// start's, for one) and is left out, so the view never jumps; and when the pickup ends, what is left of the
// turn eases out instead of snapping back. A step over CameraTurnTiming::hugeDegrees is always a cut; below
// that only a lone step is: one that comes out of a nearly still camera and is followed by one; a fast motion
// is played (a gore nest's heart pull turns the camera 6 degrees in one frame at 60 frames a second, then 4,
// section 7 there; a slower frame rate makes every step of a motion larger). So each step is played a game
// frame late, once the next one is known: the turn follows the camera's motion step for step, one frame
// behind, and a fast step is never doubled up.

#include "xr_math/head_aim.hpp"
#include "xr_math/head_view.hpp"

#include <optional>

namespace evr::xr_math {

struct CameraTurnTiming {
    // A turn of more than this in any angle from one game frame to the next is a cut, not motion (the token's
    // smooth motion is under 0.7 degrees a frame; its start cuts 9.4 degrees).
    float cutDegrees = 5.0f;
    // Each angle of the turn is held within this.
    float limitDegrees = 45.0f;
    // After the pickup the turn left eases to none over this (a smoothstep).
    double releaseSeconds = 0.3;
    // A step over cutDegrees is a cut only if the steps before and after it are both under this share of it;
    // otherwise it is motion and played. On the rig the cuts' larger neighbour was at most 0.05 of them (the
    // token's 9.4 degrees: 0 before, 0.1 after; a gore nest's 47 to 178: 0 before, 2 to 3 after), a gore
    // nest's heart pull 0.4 to 0.7 of its largest step (3.0, 6.7, 2.8 with one frame twice as long).
    float cutShare = 0.25f;
    // A step over this is a cut whatever its neighbours (a cut spread over two frames, 100 then 80 degrees,
    // or one next to a fast motion): no camera motion measured on the rig came near it (the heart pull's
    // largest step 6.7 at about 60 frames a second, so about 13 at 30).
    float hugeDegrees = 20.0f;
};

class CameraTurn {
public:
    explicit CameraTurn(CameraTurnTiming timing = {});

    // One game frame: whether the camera is followed now (a pickup runs), the game's camera and a monotonic
    // clock (seconds). Returns the turn to add on top of the head this frame (addCameraAnim's angles): the
    // summed motion while followed, then easing out; nullopt once nothing is left. A camera with angles that
    // are not finite adds nothing; a clock that is not finite ends a release at once.
    std::optional<IdAngles> update(bool follow, const IdViewAxis& camera, double seconds);

    // The cuts left out (counted a frame after them) and the largest turn (each angle)
    // since the camera was last followed from none.
    [[nodiscard]] unsigned cuts() const { return cuts_; }
    [[nodiscard]] const IdAngles& largest() const { return largest_; }

private:
    void play(const IdAngles& step);
    // Plays held_ (if any) or leaves it out as a cut, from the largest angle of the steps around it.
    void settle(float after);

    CameraTurnTiming timing_;
    std::optional<IdAngles> last_; // the camera's angles last frame while followed
    std::optional<IdAngles> held_; // last frame's step: played (or left out) once the next one is known
    float before_ = 0.0f;          // the largest angle of the step settled before it (0 after a cut)
    IdAngles turn_;
    bool releasing_ = false;
    IdAngles releaseFrom_;
    double releaseStart_ = 0.0;
    unsigned cuts_ = 0;
    IdAngles largest_;
};

// The part of a camera turn that goes on top of the head: all of it on a body that keeps the player's own
// heading; without its yaw on a body that already faces the camera's heading (the view built on the game's
// yaw: head aim off, not checked yet, or not the player), which would turn the view by the camera's yaw
// twice.
IdAngles turnOnBody(const IdAngles& turn, bool bodyOwnHeading);

// An axis turned about the world's up (+Z) by `degrees` (counter-clockwise from above, as id Tech's yaw): its
// yaw grows by that much, its pitch and roll stay.
IdViewAxis turnYaw(const IdViewAxis& axis, float degrees);

// Still (the default for pickups): the heading of a pickup's camera that the 15-degree follow turns the view
// to, without a flick. The Sentinel Crystal's sync shows its camera about 10 degrees off for one game frame,
// then back (docs/rig-findings/camera-animations.md section 7): followed, the view turned by it and back.
//
// It works on the camera's yaw from the player's own view angles (the offset), and starts a hold only in a
// view whose view angles moved less than kAnglesStillDegrees since the last (the crystal's stay put through
// its sync): a snap turn, a fast hand or a head turn that moves the view angles never starts one, and ends
// one. A hold starts on a jump of the offset out of a nearly still camera: over kStepDegrees and over
// kStepRate degrees a second over the frame's time (at most kMaxStepSeconds of it: after a stall the
// threshold stops growing), so a fast frame rate's smaller steps of the same motion never start one, and over
// kFromStillShare times the step before it (a steady motion's steps are alike, so it never starts one). The
// offset then stays where it was until the camera comes back within kStepDegrees of it (a flick: never seen,
// also one that comes back over two frames), goes on the way it jumped by more than kOnwardDegrees (a motion
// that started fast: followed a view late), or for kMaxHeldViews views and at most kMaxHeldSeconds (a cut
// that stays, a gore nest's to the nest's side: followed that late). Outside a pickup the views only keep the
// last offset, so a pickup's first view can start a hold. After kMaxPickupSeconds of one pickup (longer than
// any measured; a sync flag that never clears) nothing more is held until it ends: a gap of over
// kPickupGapSeconds without one.
class CameraHeadingHold {
public:
    static constexpr float kStepDegrees = 5.0f;
    // A gore nest's swing reaches 4.7 degrees a frame at about 60 frames a second (280 a second) for a frame,
    // the crystal's 3 (180 a second); the crystal's flick is 9.2 to 10.5 degrees in one frame. At 30 frames a
    // second this is 8 degrees: a flick of 9.2 still starts a hold, and the heart pull's start (8.5 in the
    // 30-frame sampling) does when the steps before it are under a quarter of it: then it is followed a frame
    // late.
    static constexpr float kStepRate = 240.0f;
    static constexpr double kMaxStepSeconds = 0.25;
    static constexpr float kFromStillShare = 4.0f;
    static constexpr float kAnglesStillDegrees = 0.5f;
    static constexpr float kOnwardDegrees = 1.0f;
    static constexpr int kMaxHeldViews = 2;
    static constexpr double kMaxHeldSeconds = 0.1;
    static constexpr double kMaxPickupSeconds = 10.0;
    static constexpr double kPickupGapSeconds = 0.5;

    // One game view: the camera's yaw, the player's view angles' yaw (nullopt: not read, which ends a hold
    // and keeps nothing), a monotonic clock (seconds) and whether a pickup runs (nothing is held outside
    // one). Returns the yaw (degrees) to turn the camera by for the follow this view: 0 but while a hold
    // runs. Values that are not finite end a hold and keep nothing.
    float update(float cameraYaw, std::optional<float> anglesYaw, double seconds, bool pickup);
    // Whether the last update held the offset, and whether it started that hold.
    [[nodiscard]] bool held() const { return heldViews_ > 0; }
    [[nodiscard]] bool started() const { return started_; }
    // Set on the view the running pickup passed kMaxPickupSeconds (once per pickup).
    [[nodiscard]] bool timedOut() const { return timedOut_; }

private:
    void forget();

    std::optional<float> last_;       // the offset last view
    std::optional<float> lastAngles_; // the view angles' yaw last view
    float lastStep_ = 0.0f;           // the offset's change the view before
    double lastSeconds_ = 0.0;
    float base_ = 0.0f;      // the offset a hold keeps
    float startStep_ = 0.0f; // the jump that started it
    int heldViews_ = 0;      // views of the running hold
    double heldSince_ = 0.0;
    double pickupSince_ = -1.0; // when the running pickup started (-1: none)
    double pickupLast_ = -1.0;  // its last view
    bool expired_ = false;      // the running pickup passed kMaxPickupSeconds
    bool started_ = false;
    bool timedOut_ = false;
};

} // namespace evr::xr_math
