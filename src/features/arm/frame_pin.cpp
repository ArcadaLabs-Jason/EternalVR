// One placement of the arms model per game frame (frame_pin.hpp).

#include "features/arm/frame_pin.hpp"

#include <cmath>
#include <cstddef>

namespace evr::arm {

bool placesAhead(std::optional<std::int32_t> frame, std::optional<std::int32_t> placedIn) {
    return frame && placedIn && *placedIn != *frame;
}

} // namespace evr::arm

namespace evr::arm {

namespace {

bool near(Vec3 a, Vec3 b, float tolerance) {
    return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance &&
           std::fabs(a.z - b.z) <= tolerance;
}

} // namespace

bool samePlacement(const xr_math::EyeRelativePose& a, const xr_math::EyeRelativePose& b) {
    constexpr float kTolerance = 1e-5f;
    return near(a.offset, b.offset, kTolerance) && near(a.axis.forward, b.axis.forward, kTolerance) &&
           near(a.axis.left, b.axis.left, kTolerance) && near(a.axis.up, b.axis.up, kTolerance);
}

PinCheck checkPin(const PinInputs& pinned,
                  const xr_math::EyeRelativePose& pinnedPlacement,
                  const PinInputs& own,
                  const xr_math::EyeRelativePose& ownPlacement) {
    if (samePlacement(pinnedPlacement, ownPlacement)) {
        return PinCheck::Same;
    }
    if (pinned.item != own.item) {
        return PinCheck::Item;
    }
    if (pinned.worldStamp != own.worldStamp) {
        return PinCheck::Hands;
    }
    if (pinned.leftItem != own.leftItem || pinned.leftWeight != own.leftWeight ||
        !near(pinned.leftWrist, own.leftWrist, 1e-6f)) {
        return PinCheck::LeftGlove;
    }
    return PinCheck::Placement;
}

const char* pinCheckName(PinCheck check) {
    switch (check) {
    case PinCheck::Same:
        return "the same";
    case PinCheck::Item:
        return "another held item";
    case PinCheck::Hands:
        return "other hands";
    case PinCheck::LeftGlove:
        return "the left glove moved on the gun";
    case PinCheck::Placement:
        return "other placement";
    case PinCheck::NotPlaced:
        return "not placed";
    }
    return "?";
}

void PinTally::note(PinCheck check) {
    ++counts_[static_cast<std::size_t>(check)];
    ++total_;
}

std::string PinTally::line() const {
    std::string out =
        std::to_string(total_) + " frame(s) the arms were posed under the frame's own placement: ";
    for (std::size_t i = 0; i < 6; ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += pinCheckName(static_cast<PinCheck>(i));
        out += ' ';
        out += std::to_string(counts_[i]);
    }
    return out;
}

} // namespace evr::arm
