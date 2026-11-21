#pragma once

// Teleports of the player (docs/rig-findings/camera-takeovers.md row 12): the game puts the player somewhere
// else in one game frame (a teleporter or portal, the Slayer Gate's arena, the way back after a fall into a
// pit, a debug teleport) and may set a new heading with it, which the view takes at once, as a snap turn.
// Nothing here changes what the player sees; it tells the log when a teleport happens and how far the view
// turned, whatever the settings (body follow and bHaptics have teleport lines of their own, each only while
// it is on).
//
// A teleport is a jump of the player's origin between two head-tracked game views no more than kGapSeconds
// apart that the player physics' own velocity does not explain: the origin's move less the velocity's (the
// mean of the two views' velocities over the time between them) is longer than kMetres and faster than
// kSpeed. The player's own motion is in the velocity, however fast: a booster's throw is 75 m/s (e1m2), and a
// fall into one of Exultia's pits went down 2.7 m a game frame at 160 m/s (rig, 2026-11-17); a teleport
// leaves the velocity as it was or sets it to 0 (the debug teleports and the game's way back after a fall).
// Without a velocity the move itself is taken. While the game drives the view (a forced view, a kill's or a
// pickup's sync, a cutscene: `takeover`) the jump must be longer than kTakeoverMetres: a glory kill's sync
// snaps the player 3 to 4 m onto the demon in one game frame (rig, 2026-11-18, glory-follow and
// glory-follow-head), and a long session's hundreds of kills would use up the log's lines. The game's way
// back after a fall comes with a forced view and was 20 to 50 m on the rig (a shorter one is not logged); a
// debug `teleport` onto a pickup 1184 m away was. Views further apart start over: a load or a loading screen
// between them is not measured. The view's turn is the body yaw's from the view before the jump to
// kSettleViews views after it (a heading the game sets a game frame late counts), with the largest step
// between two of those views. Pure: positions in game units (metres), yaw in degrees, time in seconds on any
// steady clock.

#include "common/vector.hpp"

#include <optional>

namespace evr::comfort {

struct Teleport {
    Vec3 from;
    Vec3 to;
    float metres = 0.0f;      // the jump
    float seconds = 0.0f;     // between the two views it fell between
    float turn = 0.0f;        // the body yaw's turn from the view before the jump to the last view counted
    float largestStep = 0.0f; // the largest turn between two consecutive views of those (signed)
    int views = 0;            // the views after the jump counted in the turn
};

class TeleportWatch {
public:
    static constexpr float kMetres = 2.0f;
    static constexpr float kTakeoverMetres = 10.0f;
    static constexpr float kSpeed = 100.0f;
    static constexpr double kGapSeconds = 0.25;
    static constexpr int kSettleViews = 5;

    // One head-tracked game view at `seconds`: the player's origin and the player physics' velocity (game
    // units per second; nullopt when they could not be read), the body yaw the view shows and whether the
    // game drives the view in it (`takeover`). Returns a teleport once kSettleViews views after it are in, or
    // earlier when another teleport or a gap ends its count.
    std::optional<Teleport> update(double seconds,
                                   std::optional<Vec3> origin,
                                   std::optional<Vec3> velocity,
                                   float bodyYaw,
                                   bool takeover = false);

    // The last update's view is a teleport's jump (reported kSettleViews views later).
    bool startedLastUpdate() const { return started_; }

private:
    std::optional<Teleport> finish();

    bool hasLast_ = false;
    double lastSeconds_ = 0.0;
    Vec3 lastOrigin_;
    float lastYaw_ = 0.0f;
    bool lastOriginKnown_ = false;
    std::optional<Vec3> lastVelocity_;
    bool pending_ = false;
    bool started_ = false;
    Teleport teleport_;
    float startYaw_ = 0.0f; // the body yaw in the view before the jump
};

} // namespace evr::comfort
