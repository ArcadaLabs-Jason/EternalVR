#pragma once

// A seated posture the player's walking contradicts (docs/VR_ROOMSCALE.md, Seated).
//
// Seated blocks body follow, and seated is decided from the floor's reading. A seated head reaches about
// 0.6 m across the floor from where the player sits, well under `kSeatedWalkMetres`; held further than
// that for `kSeatedWalkSeconds` the player is walking about the room, and the seated posture came from a
// floor the runtime got wrong (one it moved at a recenter while the player sat). The caller then switches
// to standing.

namespace evr::posture {

constexpr float kSeatedWalkMetres = 1.0f;
constexpr double kSeatedWalkSeconds = 2.0;

class SeatedWalk {
public:
    // One frame. `seatedBlocked`: body follow is blocked by a detected seated posture; `fromSeatMetres`:
    // how far the head is from where the seated body is, across the floor; `seconds`: monotonic. Returns
    // true on the frame the head has been further than `kSeatedWalkMetres` for `kSeatedWalkSeconds`, and
    // starts over. A nearer head, or a frame not blocked as seated, restarts the wait.
    bool update(bool seatedBlocked, float fromSeatMetres, double seconds);

    // When the head went far (seconds); negative while it is not.
    [[nodiscard]] double since() const { return since_; }
    void reset() { since_ = -1.0; }

private:
    double since_ = -1.0;
};

} // namespace evr::posture
