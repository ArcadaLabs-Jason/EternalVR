#pragma once

// The menu panel follows the player (docs/VR_MENUS.md): it stays world-locked while the head looks roughly
// its way, so a pointer aimed at it holds still, and is placed in front of the head again once the head has
// looked away from it (horizontally) by more than `awayDegrees` for `awaySeconds`. Turning away briefly, to
// look at something else in the room, leaves it where it is. Pure: the angle and the time come from the
// caller.

#include "common/pose.hpp"
#include "common/vector.hpp"

#include <optional>

namespace evr::menu {

struct PanelFollowTuning {
    float awayDegrees = 60.0f;
    double awaySeconds = 1.0;
};

// The horizontal angle in degrees (0 to 180) between where `head` faces and the direction from the head to
// `target`; 0 when either has no horizontal part (looking straight up or down, the target overhead).
float horizontalAngleTo(const Pose& head, Vec3 target);

class PanelFollow {
public:
    explicit PanelFollow(PanelFollowTuning tuning = {});

    // `angleDegrees`: horizontalAngleTo the panel's centre; `seconds`: any steady clock. True on the one
    // update the panel should be placed in front of the head again; the wait starts over after it.
    bool update(float angleDegrees, double seconds);
    // A new panel (placed, or the menu closed): nothing is waited for.
    void reset() { awaySince_.reset(); }

private:
    PanelFollowTuning tuning_;
    std::optional<double> awaySince_; // when the head last turned away
};

} // namespace evr::menu
