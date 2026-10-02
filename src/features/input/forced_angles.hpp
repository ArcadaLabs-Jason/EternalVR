#pragma once

// When hand aim must leave the game's view angles alone (T-055, docs/rig-findings/input-aim.md section 2,
// "Forced-angle states").
//
// The game forces the view during sync and glory kills, the meathook pull, melee lunges, wall-climb
// release, scripted and photo cameras and more. Every one of those goes through idPlayer::SetViewAngles
// from a caller other than the per-tick view update, or sets the player's view inhibit flags, or runs
// as a cutscene. A hands animation that moves the camera counts too while the layer plays its rotation
// (xr_math/camera_anim.hpp): the hands play in front of the game's camera, not at the controller. While any
// of these holds, and for a short time after, the layer sends no aim delta; fighting a forced view would jerk
// the camera and could push the player out of the animation.
//
// A climbable wall is the one state that yields everything but the aim: with the wall-climb cvars the layer
// holds (kClimbLookCvars) the view there is the player's own, and it follows the head under hand aim too, so
// the wall-climb mechanic's jump goes where the player looks (climb_hook.cpp). The viewmodel, the shots and
// the off hand leave the game alone as for any forced view. A foreign SetViewAngles does not end that state:
// on the wall the game calls it every tick to apply the climb animation's deltas to the player (returning
// to RVA 0x138F33B in Steam build 25216728) and once when the player lets go (DisconnectFromWall, 0x13B4677),
// both with the player's own angles. A cutscene and the view inhibit bits still take the view from the head.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace evr::input {

// idPlayer::inhibitFlags bits that stop the game's own view update: VIEW (0x8) and VIEW_ONCE (0x100).
inline constexpr std::uint32_t kInhibitViewMask = 0x108;

struct ForcedAngleSignals {
    bool foreignSetViewAngles = false; // a SetViewAngles call not from the per-tick update since last frame
                                       // (ignored on a climbable wall)
    std::uint32_t inhibitFlags = 0;    // idPlayer::inhibitFlags
    bool cutscene = false;             // renderView_t::inCutscene
    bool cameraAnimation = false;      // a hands animation moves the camera
    bool wallClimb = false;            // on a climbable wall whose view is the player's own (ClimbFrames)
};

enum class ForcedReason : std::uint8_t {
    None,
    SetViewAngles,
    Inhibit,
    Cutscene,
    CameraAnimation,
    WallClimb, // yields all but the aim, which follows the head (aimsWithHead)
    Settling,  // none holds now, but one did within the resume delay
};

const char* forcedReasonName(ForcedReason reason);

// True for the reason under which hand aim aims with the head instead of sending nothing.
bool aimsWithHead(ForcedReason reason);

class ForcedAngleGate {
public:
    // `resumeFrames`: game frames to keep yielding after the last signal (the game can apply a forced
    // angle one tick after the call that set it).
    explicit ForcedAngleGate(int resumeFrames = 2);

    // One game frame. True: yield (send no aim delta this frame, or the head's under aimsWithHead).
    bool update(const ForcedAngleSignals& signals);

    [[nodiscard]] ForcedReason reason() const { return reason_; }
    // Frames that yielded, and how many separate forced episodes began.
    [[nodiscard]] std::uint64_t yieldedFrames() const { return yielded_; }
    [[nodiscard]] std::uint64_t episodes() const { return episodes_; }

private:
    int resumeFrames_;
    int sinceSignal_;
    ForcedReason reason_ = ForcedReason::None;
    std::uint64_t yielded_ = 0;
    std::uint64_t episodes_ = 0;
};

// The cvars held while the view on a climbable wall is the player's own: the wall-climb mechanic then leaves
// the view to the player's own update (no view inhibit bit, no SetViewAngles every tick) and does not push
// the view out of its dead zone below the eye (which it would do with SetViewAngles).
struct ClimbCvar {
    std::string_view name;
    std::string_view value;
};
inline constexpr std::array<ClimbCvar, 2> kClimbLookCvars{{
    {"wallclimb_takeoverViewAngles", "0"},
    {"wallclimb_deadZone_enable", "0"},
}};

// ETERNALVR_CLIMB_LOOK: on unless it is "0" or "off" (any case); unset or empty is on.
bool climbLookSwitch(std::wstring_view value);

// What the layer does with the climb-look cvars in a frame.
enum class ClimbCvarAction : std::uint8_t {
    None,
    Hold,    // write kClimbLookCvars where the game's value differs
    Restore, // write back the game's own values saved before the first write
};

// `wanted`: the switch is on and the layer drives the view; `touchAllowed`: the multiplayer guard lets the
// layer touch the game; `saved`: game values were saved before a write and not yet given back. Hold while
// both allow; once either stops (the guard trips, and never resets) give the game's values back once;
// otherwise nothing.
ClimbCvarAction climbCvarAction(bool wanted, bool touchAllowed, bool saved);

// The game's own value of one held cvar: saved before the layer's first write, handed back once.
class SavedCvarValue {
public:
    // Before the layer writes over `current`: saves it unless a value is already saved (a later write, after
    // the game put its own value back, keeps the first one).
    void beforeWrite(int current);
    // The saved value, once: it is cleared, so a second call gives nothing.
    std::optional<int> take();
    [[nodiscard]] bool saved() const { return value_.has_value(); }

private:
    std::optional<int> value_;
};

// Whether the player is on a climbable wall, from the wall-climb mechanic's steps that run only while the
// player is on one (one per game tick). A frame with no tick since the last (a render between two ticks)
// keeps the answer for `holdFrames` frames.
class ClimbFrames {
public:
    explicit ClimbFrames(int holdFrames = 3);

    // One game frame; `ticks` is the number of on-wall steps since the last frame. True: on a wall.
    bool update(std::uint32_t ticks);

    [[nodiscard]] bool onWall() const { return onWall_; }
    // Frames on a wall, and how many separate climbs began.
    [[nodiscard]] std::uint64_t frames() const { return frames_; }
    [[nodiscard]] std::uint64_t climbs() const { return climbs_; }

private:
    int holdFrames_;
    int sinceTick_;
    bool onWall_ = false;
    std::uint64_t frames_ = 0;
    std::uint64_t climbs_ = 0;
};

} // namespace evr::input
