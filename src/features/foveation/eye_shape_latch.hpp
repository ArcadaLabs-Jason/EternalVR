#pragma once

// The eye shape foveation's rate images are made for (vrs_nv.hpp's noteEye): one eye's FOV and orientation in
// the head, from frames whose eyes passed the presenter's plausibility checks. It used to be the first one
// seen for the whole process, while the runtime's FOV can change during a session (features/tracking/
// fov_watch.hpp). So:
//
// - The first shape is taken at once.
// - A different one (an angle of the FOV, or the orientation, off by more than kChangeDegrees) is taken once
//   kStableNotes notes in a row agree on it (within kChangeDegrees of the first of them): a shape that comes
//   and goes is never taken.
// - At most kMaxChanges changes in a process (each one makes new rate images); later ones keep the shape.

#include "common/quat.hpp"
#include "xr_math/fov.hpp"

#include <cstdint>
#include <optional>

namespace evr::foveation {

struct EyeShape {
    xr_math::Fov fov;
    Quat orientation;
};

// Every FOV angle within `degrees` of the other's, and the orientations within `degrees` of each other.
bool sameShape(const EyeShape& a, const EyeShape& b, float degrees);

class EyeShapeLatch {
public:
    static constexpr float kChangeDegrees = 1.0f;
    static constexpr int kStableNotes = 90;
    static constexpr int kMaxChanges = 8;

    enum class Note : std::uint8_t {
        First,   // the first shape: taken
        Same,    // the shape taken
        Waiting, // a different shape, not taken until it holds
        Changed, // a different shape held kStableNotes notes: taken
        Capped,  // a different shape held, but kMaxChanges were taken already: kept as it is
    };

    Note note(const EyeShape& shape);

    [[nodiscard]] const std::optional<EyeShape>& shape() const { return shape_; }
    [[nodiscard]] int changes() const { return changes_; }

private:
    std::optional<EyeShape> shape_;
    std::optional<EyeShape> candidate_;
    int candidateNotes_ = 0;
    int changes_ = 0;
};

} // namespace evr::foveation
