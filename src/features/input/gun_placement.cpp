#include "features/input/gun_placement.hpp"

#include <cmath>

namespace evr::input {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

GunPlacement gunPlacement(AimSource aim, bool fistsHeld, ActionAimSource meleeAim) {
    if (aim == AimSource::Head) {
        return GunPlacement::Head;
    }
    if (aim == AimSource::Hand && fistsHeld && meleeAim != ActionAimSource::Same) {
        return GunPlacement::GameView;
    }
    return GunPlacement::Hand;
}

std::optional<Vec3> headGunOrigin(Vec3 gameOrigin,
                                  const HeadOffsetNote& note,
                                  std::int64_t notBefore,
                                  const ViewClockRead& views) {
    if (!note.valid || note.written < notBefore || !views.current(note.written) || !finite(note.offset) ||
        !finite(gameOrigin)) {
        return std::nullopt;
    }
    const Vec3 moved = gameOrigin + note.offset;
    if (!finite(moved)) {
        return std::nullopt;
    }
    return moved;
}

} // namespace evr::input
