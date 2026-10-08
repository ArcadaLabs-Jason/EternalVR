#include "features/roomscale/driven_offset.hpp"

#include <algorithm>
#include <cmath>

namespace evr::roomscale {

namespace {

bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

bool DrivenViewOffset::rebase(Vec3 offset) {
    if (!driven_ || !finite(offset)) {
        return false;
    }
    const Vec3 jump = offset - lastOffset_;
    base_ = base_ + jump;
    held_ = held_ + jump;
    return true;
}

Vec3 DrivenViewOffset::update(Vec3 offset, bool driven, double seconds) {
    if (!finite(offset)) {
        return offset; // head_offset.hpp never gives one; nothing here is kept from it
    }
    // A long gap (menus, loading) or time going backwards eases by at most a tenth of a second.
    const double dt = last_ < 0.0 || !std::isfinite(seconds) ? 0.0 : std::clamp(seconds - last_, 0.0, 0.1);
    if (std::isfinite(seconds)) {
        last_ = seconds;
    }
    lastOffset_ = offset;
    began_ = driven && !driven_;
    if (began_) {
        base_ = offset;
    }
    driven_ = driven;
    const Vec3 target = driven ? base_ : Vec3{};
    const float k = static_cast<float>(1.0 - std::exp(-dt / kDrivenEaseSeconds));
    held_ = held_ + (target - held_) * k;
    return offset - held_;
}

} // namespace evr::roomscale
