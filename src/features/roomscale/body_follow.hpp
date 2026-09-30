#pragma once

// Room-scale body follow (T-062, docs/VR_ROOMSCALE.md "Body follow").
//
// When the player walks in the room, the head leaves the body; beyond a deadzone the body is walked
// toward the head through the game's own movement (a forward/side request in the user command), so the
// game's collision stops it at walls. The loop is closed on what the body really did: each game frame the
// player's origin displacement, turned into room axes, shifts the room anchor by the part that brought the
// body toward the head, so the head's offset shrinks by exactly what the body followed. Whatever the body
// could not follow (a wall) stays as head offset, under the lean cap and the fade.
//
// Anything that moves the body on its own (the stick, a jump or a fall, a dash, the Meathook, a teleport,
// a cutscene, a forced view) turns follow off for that frame and a short time after; the body's motion is
// then not taken into the anchor, so nothing accumulates: the view simply rides with the body as it does
// without body follow.
//
// Room axes (room_anchor.hpp): +X right, +Y up, -Z forward, metres before world scale.

#include "common/vector.hpp"

#include <cstdint>
#include <optional>

namespace evr::roomscale {

// The game's response to a move command is not linear (measured on the rig, docs/VR_ROOMSCALE.md "Body
// follow"): up to about 30 of 127 nothing moves, from about 35 to 65 the body creeps at 0.1 to 0.2 m/s,
// from about 75 to 95 it walks at 2.1 to 2.7 m/s, and above about 100 it runs (8 to 9.3 m/s). No command
// gives the speeds in between, so the controller pulses: it walks while the gap left after braking is
// larger than the deadzone, lets the body coast when it would reach the head, and creeps the rest of the
// way. The body also coasts after a command ends: about its speed times 0.1 s (measured: 0.08 m from
// 0.8 m/s, 0.15 m from 2.2 m/s, 0.88 m from 9.3 m/s), which the pulses allow for.
struct BodyFollowSettings {
    bool enabled = true;
    float deadzoneMetres = 0.04f; // following starts beyond this gap and stops within 40 % of it
    int walkCommand = 85;         // a move value (of 127) in the game's walk tier
    int creepCommand = 60;        // a move value in its creep tier
    float coastSeconds = 0.1f;    // the body goes on for its speed times this after the command stops
    float maxSpeed = 3.0f;        // metres per second: the fastest body follow moves the body (walk tier)
};

// Values that are not finite and sane (deadzone 0.01 to 0.5 m, walk 30 to 127, creep 20 to 127, coast 0 to
// 0.5 s, speed 0.5 to 5 m/s) fall back to the defaults one by one.
BodyFollowSettings sanitized(BodyFollowSettings settings);

// A move in the room frame as a fraction of a full command (+right, +forward; length at most 1).
struct FollowMove {
    float right = 0.0f;
    float forward = 0.0f;

    friend constexpr bool operator==(FollowMove, FollowMove) = default;
};

enum class FollowTier : std::uint8_t {
    None,  // not following
    Coast, // following, but the body is still moving fast enough to reach the head: no command
    Creep,
    Walk,
};

struct FollowRequest {
    FollowMove move;
    FollowTier tier = FollowTier::None;
    bool engaged = false;
};

// The controller: the request that closes the horizontal gap `gapRoom` (the head's position relative to
// the body, room axes) given the body's speed toward the head `speedToward` (room metres per second).
// Starts beyond the deadzone, stops within 40 % of it (`wasEngaged` carries the state).
FollowRequest followRequest(Vec3 gapRoom, float speedToward, bool wasEngaged, const BodyFollowSettings& s);

// A world displacement (id Tech axes, game units) in room axes (metres), through the body frame the head
// offset is composed with (headOffsetInWorld): `bodyForward` and `bodyLeft` are its id Tech rows.
Vec3 displacementInRoom(Vec3 worldUnits, Vec3 bodyForward, Vec3 bodyLeft, float unitsPerMetre);

enum class FollowBlock : std::uint8_t {
    None,
    Off,         // ETERNALVR_BODY_FOLLOW=0, or the user-command hook is not installed
    NotAnchored, // the room is not anchored yet (LOCAL as the runtime set it)
    Seated,
    NoOrigin, // the player's origin could not be read this frame, or frames stopped for a while
    Teleport, // the origin jumped more than `kTeleportMetres` in one frame
    Menu,     // a menu or the pause menu drives the controllers
    Cutscene, // a cutscene or the game forcing the view (glory kill, Meathook pull, scripted camera)
    Stick,    // the stick or the keyboard moves the player
    JumpOrDash,
    Airborne, // the origin moves vertically faster than walking on stairs or slopes
    Fast,     // the origin moves faster than body follow asks for (a dash, the Meathook, a knockback)
    Settling, // none of the above now, but one held within the resume delay
    Count,
};

const char* followBlockName(FollowBlock block);

// Something other than the player's walking moves the body (the stick, a jump or dash, a fall, the
// Meathook, a teleport, a cutscene or glory kill, a menu, and the resume delay after them): follow cannot
// close the gap meanwhile, so the head's walk past the lean cap is taken into the room (the view rides with
// the body, as without body follow) instead of fading the view.
bool followBlockRidesWithBody(FollowBlock block);

// A jump this large in one frame is a teleport (a checkpoint, a level change), never a step.
inline constexpr float kTeleportMetres = 1.0f;
// Vertical origin speed above which the player counts as in the air (the grounded flag is not read).
inline constexpr float kAirborneSpeed = 1.5f;
// Follow resumes this long after the last blocking frame.
inline constexpr double kResumeSeconds = 0.3;

struct FollowTick {
    double seconds = 0.0;
    Vec3 gapRoom;                     // the head relative to the body before this frame's shift (room)
    std::optional<Vec3> displacement; // the origin's move since the last frame, room axes, metres
    float unitsPerMetre = 1.0f;
    bool commanded = false; // a follow move went into a user command within the resume delay
    // What blocks follow this frame (FollowBlock gives the priority).
    bool hookInstalled = true;
    bool anchored = true;
    bool seated = false;
    bool menu = false;
    bool cutscene = false;
    bool forcedView = false;
    bool stick = false;
    bool jumpOrDash = false;
};

struct FollowStep {
    Vec3 absorbed; // room metres to move the anchor by (the head's room position moves by -absorbed)
    FollowRequest request;
    FollowBlock block = FollowBlock::None;
    FollowBlock cause = FollowBlock::None; // this frame's own block (None while settling)
    float gap = 0.0f;                      // the gap the request was made for (room metres)
    float speedToward = 0.0f;              // the body's smoothed speed toward the head (m/s)
};

const char* followTierName(FollowTier tier);

class BodyFollow {
public:
    explicit BodyFollow(const BodyFollowSettings& settings = {});

    FollowStep update(const FollowTick& tick);
    void reset();

    [[nodiscard]] const BodyFollowSettings& settings() const { return settings_; }

private:
    BodyFollowSettings settings_;
    double lastSeconds_ = -1.0;
    double blockedUntil_ = -1.0;
    bool engaged_ = false;
    Vec3 velocity_; // the body's horizontal velocity, smoothed (room metres per second)
};

} // namespace evr::roomscale
