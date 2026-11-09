// Where the free off hand's glove was drawn (drawn_wrist.hpp).

#include "features/arm/drawn_wrist.hpp"

namespace evr::arm {

Vec3 placedPoint(Vec3 eye, const xr_math::EyeRelativePose& pose, Vec3 point) {
    const xr_math::IdViewAxis& a = pose.axis;
    return eye + pose.offset + a.forward * point.x + a.left * point.y + a.up * point.z;
}

std::optional<DrawnWrist> drawnWrist(const PosedWrist& posed,
                                     std::span<const FramePlacement> placements,
                                     const ReadWrist& read,
                                     float unitsPerMetre) {
    if (!(unitsPerMetre > 0.0f) || read.frame != posed.frame + 1) {
        return std::nullopt;
    }
    for (const FramePlacement& p : placements) {
        if (p.frame != read.frame) {
            continue;
        }
        const float metres = 1.0f / unitsPerMetre;
        return DrawnWrist{placedPoint(p.eye, p.pose, read.wrist) * metres,
                          placedPoint(posed.eye, posed.placement, posed.wrist) * metres,
                          (posed.snapshotSeconds - posed.placementSnapshotSeconds) * 1000.0};
    }
    return std::nullopt;
}

} // namespace evr::arm
