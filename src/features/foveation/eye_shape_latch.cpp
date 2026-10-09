#include "features/foveation/eye_shape_latch.hpp"

#include <algorithm>
#include <cmath>

namespace evr::foveation {

namespace {

constexpr float kRadians = 1.0f / 57.29578f;

bool within(float a, float b, float radians) {
    return std::fabs(a - b) <= radians;
}

} // namespace

bool sameShape(const EyeShape& a, const EyeShape& b, float degrees) {
    const float radians = degrees * kRadians;
    if (!within(a.fov.angleLeft, b.fov.angleLeft, radians) ||
        !within(a.fov.angleRight, b.fov.angleRight, radians) ||
        !within(a.fov.angleUp, b.fov.angleUp, radians) ||
        !within(a.fov.angleDown, b.fov.angleDown, radians)) {
        return false;
    }
    // The angle between the orientations: 2 acos |q1 . q2| for unit quaternions.
    const Quat& p = a.orientation;
    const Quat& q = b.orientation;
    const float d = std::min(1.0f, std::fabs(p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w));
    return std::isfinite(d) && 2.0f * std::acos(d) <= radians;
}

EyeShapeLatch::Note EyeShapeLatch::note(const EyeShape& shape) {
    if (!shape_) {
        shape_ = shape;
        return Note::First;
    }
    if (sameShape(*shape_, shape, kChangeDegrees)) {
        candidate_.reset();
        candidateNotes_ = 0;
        return Note::Same;
    }
    if (candidate_ && sameShape(*candidate_, shape, kChangeDegrees)) {
        ++candidateNotes_;
    } else {
        candidate_ = shape;
        candidateNotes_ = 1;
    }
    if (candidateNotes_ < kStableNotes) {
        return Note::Waiting;
    }
    candidateNotes_ = 0;
    if (changes_ >= kMaxChanges) {
        candidate_.reset();
        return Note::Capped;
    }
    shape_ = *candidate_;
    candidate_.reset();
    ++changes_;
    return Note::Changed;
}

} // namespace evr::foveation
