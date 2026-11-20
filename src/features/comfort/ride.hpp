#pragma once

// Rides the game moves the player on (the comfort vignette, vignette.hpp): a tram, an elevator, a moving
// platform. The player stands still and the world goes by, which is motion the player sees but does not make
// with the body, as with the stick. One general rule, no list of maps: on the ground, the player's body moves
// at least startSpeed faster than the player's own velocity explains, for startSeconds. The carried speed is
// CarriedSpeed's: the body's displacement over a short window against the player physics' own velocity over
// it (vkcore/game_motion.cpp reads both; a mover carries the body without the physics' velocity, rig
// 2026-11-17). What the rule leaves out, and why:
// - the player's own motion: running, strafing, dashes, jumps and knockback are the physics' own velocity,
//   and a body that falls short of it (a wall, a corner, a stuck spot, slow motion) is never carried; on the
//   rig running, double jumps, dashes, jump pad throws and a glory-kill fight gave no ride, and running into
//   a wall, a corner and the weapon wheel's slow motion are checked by tools/rig/qa (motion-negative);
// - falls, the Meathook's pull, monkey bars and swing poles are off the ground; a jump during a ride
//   (shorter than airSeconds) keeps it;
// - a cutscene (on the screen already), a climb, swimming, a piloted demon and death are excluded by the
//   caller: they end a ride at once and RideWatch starts the window over, so a glide under one counts for
//   nothing after it;
// - while the player walks, only level carrying counts (stairs lift the body with no upward velocity of its
//   own), so a lift going up or down under a walking player is not a ride until the player stands.
// The amount follows the carried speed on the move stick's scale, so a ride shows what walking that fast
// with the stick would show. Pure: time in seconds on any steady clock, speeds in game units (metres) per
// second.

#include "common/vector.hpp"

#include <cstddef>
#include <cstdint>

namespace evr::comfort {

// How fast something other than the player's own motion carries the body, over the samples of the last
// window (at least kMinWindowSeconds old at its far end, kMaxWindowSeconds at most): the body's mean velocity
// over it against the mean of the player's own. While the player's own level speed is above kOwnWalkSpeed,
// the level speed by which the body outruns its own (never less than 0, the vertical left out for stairs);
// below it, the body's velocity less its own without the part that only falls short of the own velocity
// (above kOwnGuardSpeed). A window spans several frames, so the render's two views of one game tick and
// frame-time jitter do not count. A step longer than kTeleportMetres between two samples (a teleport, a
// respawn, a checkpoint load) or a gap of more than kMaxWindowSeconds starts over. Positions in game units
// (metres), velocities in units per second (the id Tech axes: z up).
class CarriedSpeed {
public:
    static constexpr double kMinWindowSeconds = 0.2;
    static constexpr double kMaxWindowSeconds = 0.4;
    static constexpr float kTeleportMetres = 20.0f;
    static constexpr float kOwnWalkSpeed = 1.0f;
    static constexpr float kOwnGuardSpeed = 0.5f;
    static constexpr std::size_t kSamples = 64;

    // One sample at `seconds`; returns the carried speed, or a negative value while the window is too short
    // (just started, after a teleport or a gap). Non-finite values start over.
    float add(double seconds, Vec3 origin, Vec3 ownVelocity);
    void reset() { count_ = 0; }

private:
    struct Sample {
        double t = 0.0;
        Vec3 origin;
        Vec3 velocity;
    };
    Sample samples_[kSamples]{};
    std::size_t head_ = 0;  // where the next sample goes
    std::size_t count_ = 0; // samples held
};

struct RideTiming {
    float startSpeed = 1.0f;   // the body is carried at least this fast ...
    float startSeconds = 0.5f; // ... this long with the player on it: a ride starts
    float stopSpeed = 0.5f;    // slower than this ...
    float stopSeconds = 0.4f;  // ... this long: it ends
    float airSeconds = 1.0f;   // off the ground this long during a ride (a jump is shorter): it ends
    // The amount: 0 at amountStartSpeed, 1 from fullSpeed (the move stick's 0.1 and 0.7 of the run speed,
    // pm_runspeed 9.525 m/s).
    float amountStartSpeed = 1.0f;
    float fullSpeed = 6.5f;
};

// One game frame as the reads saw it.
struct RideSample {
    bool valid = false;        // the player and its physics were read
    bool excluded = false;     // a cutscene, a climb, swimming, a piloted demon, death
    bool onGround = false;     // the player's feet are on the ground
    float carriedSpeed = 0.0f; // CarriedSpeed::add's (0 when it has none yet)
};

enum class RideChange : std::uint8_t { None, Started, Ended };

enum class RideEnd : std::uint8_t {
    None,
    Stopped,   // the carrying slowed down
    OffGround, // the player left it for longer than a jump
    Excluded,  // a cutscene, a climb, swimming, a piloted demon, death
    Lost,      // the reads failed, or the frames stopped (a load, a menu, the flat screen)
};

const char* rideEndName(RideEnd end);

class RideDetector {
public:
    explicit RideDetector(RideTiming timing = {});

    // One game frame at `now`. More than kMaxFrameGapSeconds since the last one (or a clock going back)
    // ends a ride as lost and starts over.
    RideChange update(const RideSample& sample, double now);
    [[nodiscard]] bool riding() const { return riding_; }
    // 0 to 1 for the vignette while riding (by the latest carried speed on the ground, held through a jump),
    // else 0.
    [[nodiscard]] float amount() const;
    // The ride under way, or the one that just ended: when the carrying started, its speed then
    // and its fastest since.
    [[nodiscard]] double rideStart() const { return rideStart_; }
    [[nodiscard]] float startSpeed() const { return startSpeed_; }
    [[nodiscard]] float peakSpeed() const { return peakSpeed_; }
    [[nodiscard]] RideEnd lastEnd() const { return lastEnd_; }
    void reset();

    static constexpr double kMaxFrameGapSeconds = 0.5;

private:
    RideChange end(RideEnd why);

    RideTiming timing_;
    bool haveLast_ = false;
    double last_ = 0.0;
    bool riding_ = false;
    double fastSince_ = -1.0; // not riding: carried fast since then
    double slowSince_ = -1.0; // riding: carried slowly since then
    double airSince_ = -1.0;  // riding: off the ground since then
    float speed_ = 0.0f;      // riding: the latest carried speed on the ground
    float firstSpeed_ = 0.0f; // the carried speed when fastSince_ was set
    double rideStart_ = 0.0;
    float startSpeed_ = 0.0f;
    float peakSpeed_ = 0.0f;
    RideEnd lastEnd_ = RideEnd::None;
};

// The rule from the reads of each game frame: CarriedSpeed into RideDetector, the window started over on a
// lost reading and for every excluded frame (so motion under a cutscene, a death, a climb, a swim or a
// piloted demon counts for nothing once it ends: a whole new window must fill first).
struct RideFrame {
    bool valid = false;
    bool excluded = false;
    bool onGround = false;
    Vec3 origin;
    Vec3 ownVelocity;
};

class RideWatch {
public:
    explicit RideWatch(RideTiming timing = {}) : ride_(timing) {}

    RideChange update(const RideFrame& frame, double now);
    [[nodiscard]] const RideDetector& ride() const { return ride_; }
    // The carried speed of the last frame (0 while it has none).
    [[nodiscard]] float carried() const { return carried_; }
    void reset();

private:
    CarriedSpeed speed_;
    RideDetector ride_;
    float carried_ = 0.0f;
};

} // namespace evr::comfort
