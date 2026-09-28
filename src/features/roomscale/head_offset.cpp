#include "features/roomscale/head_offset.hpp"

#include "common/finite.hpp"

#include <algorithm>
#include <cmath>

namespace evr::roomscale {

HeadOffsetLimits sanitized(HeadOffsetLimits limits) {
    const HeadOffsetLimits defaults;
    limits.leanCapMetres = finiteInRangeOr(limits.leanCapMetres, 0.05f, 2.0f, defaults.leanCapMetres);
    limits.maxRiseMetres = finiteInRangeOr(limits.maxRiseMetres, 0.0f, 1.0f, defaults.maxRiseMetres);
    limits.minEyeMetres = finiteInRangeOr(limits.minEyeMetres, 0.1f, 1.5f, defaults.minEyeMetres);
    return limits;
}

HeadOffset headOffset(const HeadOffsetInput& input, const HeadOffsetLimits& rawLimits) {
    const HeadOffsetLimits limits = sanitized(rawLimits);
    HeadOffset out;
    const Vec3 head = input.roomHead;
    if (!std::isfinite(head.x) || !std::isfinite(head.y) || !std::isfinite(head.z)) {
        return out; // a glitched sample leaves the view on the game's eye
    }
    const float scale = finiteInRangeOr(input.unitsPerMetre, 0.01f, 100.0f, 1.0f);
    const float gameEye = finiteInRangeOr(input.gameEyeUnits, 0.0f, 100.0f, 1.657f) / scale;

    // Horizontal: the lean, capped.
    const float lean = std::sqrt(head.x * head.x + head.z * head.z);
    out.requestedLean = lean;
    float keep = 1.0f;
    if (lean > limits.leanCapMetres) {
        keep = limits.leanCapMetres / lean;
        out.leanClamped = true;
    }
    out.offset.x = head.x * keep;
    out.offset.z = head.z * keep;
    out.lean = lean * keep;

    // Vertical: from the game's eye to the head. Slayer height puts the anchored head at the game's eye;
    // Real height puts it at the player's own height above the floor.
    float base = 0.0f;
    if (input.height == HeightMode::Real && input.anchorAboveFloor &&
        finiteInRange(*input.anchorAboveFloor, 0.3f, 2.5f)) {
        base = *input.anchorAboveFloor - gameEye;
        out.realHeight = true;
    }
    const float lowest = limits.minEyeMetres - gameEye; // the eye never goes below minEye above the feet
    const float highest = base + limits.maxRiseMetres;
    const float wanted = base + head.y;
    out.offset.y = std::clamp(wanted, std::min(lowest, highest), highest);
    out.heightClamped = out.offset.y != wanted;
    return out;
}

} // namespace evr::roomscale
