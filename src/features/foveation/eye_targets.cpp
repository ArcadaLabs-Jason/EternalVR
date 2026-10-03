#include "features/foveation/eye_targets.hpp"

namespace evr::foveation {

namespace {

// The game's smallest eye-space buffers are an eighth of the eye image on each side.
constexpr unsigned kLargestDivisorShift = 3;

bool dividesTo(std::uint32_t full, std::uint32_t part, std::uint32_t divisor) {
    return part == full / divisor || part == (full + divisor - 1) / divisor;
}

bool isPowerOfTwoSquare(TargetSize target) {
    return target.width == target.height && (target.width & (target.width - 1)) == 0;
}

// Whether one scale s in [1/2^kLargestDivisorShift, 1] gives `target` from `eye` on both sides, each side
// within one pixel: |target - s * eye| <= 1. Each side allows s in [(target - 1) / eye, (target + 1) / eye];
// the ranges and the bounds must overlap. Compared as cross products, exact in 64 bits.
bool scalesTo(TargetSize target, TargetSize eye) {
    const std::uint64_t tw = target.width;
    const std::uint64_t th = target.height;
    const std::uint64_t ew = eye.width;
    const std::uint64_t eh = eye.height;
    const std::uint64_t smallest = std::uint64_t{1} << kLargestDivisorShift;
    return tw <= ew && th <= eh && ew <= smallest * (tw + 1) && eh <= smallest * (th + 1) &&
           (tw - 1) * eh <= (th + 1) * ew && (th - 1) * ew <= (tw + 1) * eh;
}

} // namespace

bool isEyeSpaceTarget(TargetSize target, TargetSize eye) {
    if (eye.width == 0 || eye.height == 0 || target.width == 0 || target.height == 0) {
        return false;
    }
    for (unsigned shift = 0; shift <= kLargestDivisorShift; ++shift) {
        const std::uint32_t divisor = 1u << shift;
        if (dividesTo(eye.width, target.width, divisor) && dividesTo(eye.height, target.height, divisor)) {
            return true;
        }
    }
    // Shadow maps and look-up tables are power-of-two squares; near a square eye image one would pass the
    // scale test at a scale no eye-space buffer has.
    return !isPowerOfTwoSquare(target) && scalesTo(target, eye);
}

} // namespace evr::foveation
