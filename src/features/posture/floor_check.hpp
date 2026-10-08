#pragma once

// Whether the floor's reading can be trusted (docs/VR_ROOMSCALE.md, Posture and eye height).
//
// The head's height above the floor comes from the runtime's LOCAL_FLOOR or STAGE space, and a runtime
// can move that floor: SteamVR has put it at head height when it recentered. A reading no head can have
// (plausibleHeadHeight) is never used, and neither is a floor that moved:
//   - Across the runtime's recenter (LOCAL changes): the reading at the re-anchor is compared with the last
//     one before it, while the head stays where it was. More than `kFloorMoveMetres` apart, the floor moved,
//     and its readings are not used until one is within that of the reading before again (the runtime put
//     it back, or another recenter fixed it).
//   - When the floor space changes (a STAGE or LOCAL_FLOOR change; also for `kFloorWatchSeconds` after a
//     re-anchor that found no move, as a runtime may move its floor a moment after LOCAL): for
//     `kFloorWatchSeconds` the floor's own height in LOCAL (the head's LOCAL height less its height above
//     the floor, which the head moving does not change) is compared with the one before. More than
//     `kFloorMoveMetres` apart, with the reading itself stepping that much from one frame to the next, the
//     floor moved, and its readings are not used until the floor is back within that of where it was. A
//     step of the floor's LOCAL height without one of the reading is LOCAL moving (a late or unannounced
//     shift): the floor stayed, and the comparison starts again from there.
//   - The player's own recenter trusts the floor again (trustAgain()): it is how a player tells the mod
//     the floor is right, and walking while seated (seated_walk.hpp) corrects a wrong result.
//
// The caller feeds every reading to update(), says when the runtime announced a LOCAL change and, when it
// re-anchors after it, gives the reading then to afterSpaceChange(). Readings in between are not used:
// the runtime may have moved the floor already.

#include <optional>

namespace evr::posture {

// A head moves far less than this in the moment between the runtime's announcement and the re-anchor.
constexpr float kFloorMoveMetres = 0.30f;
// A floor space change may take effect a little after it is announced.
constexpr double kFloorWatchSeconds = 3.0;

class FloorCheck {
public:
    // One frame: the head's height above the floor (metres; nullopt: none, or the head is not tracked) and
    // in LOCAL, at `seconds` (monotonic). Returns the reading when it can be used.
    std::optional<float> update(std::optional<float> headAboveFloor, float headLocalY, double seconds);

    // The runtime announced a LOCAL change: until afterSpaceChange() no reading is used.
    void spaceChangePending();
    // The reading at the re-anchor after it (with the head's LOCAL height, at `seconds`). When it differs
    // from the last reading used by more than `kFloorMoveMetres`, marks the floor moved and returns how far
    // it moved (metres, + up: the head then reads that much lower). Otherwise the floor is watched from
    // here for `kFloorWatchSeconds`.
    std::optional<float>
    afterSpaceChange(std::optional<float> headAboveFloor, float headLocalY, double seconds);

    // The runtime announced a change of the floor space alone: the floor is watched for a move.
    void floorChangePending(double seconds);

    // A new session after a lost one: LOCAL may be elsewhere and the head may have moved, so nothing is
    // compared across it; a moved floor stays moved until its reading is back.
    void sessionRestarted();
    // The player's own recenter: the floor is used again from here.
    void trustAgain();

    [[nodiscard]] bool pending() const { return pending_; }
    // The floor moved and has not come back.
    [[nodiscard]] bool moved() const { return moved_; }
    // How far it moved (metres, + up), when it did.
    [[nodiscard]] float move() const { return move_; }

private:
    // LOCAL moved: the floor's LOCAL height no longer compares.
    void localMoved();

    std::optional<float> last_;       // the last reading used
    std::optional<float> lastFloorY_; // the floor's height in LOCAL then
    std::optional<float> prev_;       // the previous frame's reading, used or not
    std::optional<float> prevFloorY_; // the floor's height in LOCAL then
    bool pending_ = false;
    double watchUntil_ = -1.0; // watching the floor space after its change, until then
    float floorBefore_ = 0.0f; // the floor's height in LOCAL before that change
    bool moved_ = false;
    bool movedInLocal_ = false; // back when the floor's LOCAL height is (else when the reading is)
    float before_ = 0.0f;       // while moved: the reading before the move
    float move_ = 0.0f;
};

} // namespace evr::posture
