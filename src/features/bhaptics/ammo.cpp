#include "features/bhaptics/ammo.hpp"

#include <algorithm>
#include <cmath>

namespace evr::bhaptics {

bool isAmmoPickup(std::string_view useComponentClass, std::string_view itemClass) {
    if (useComponentClass == ".?AVidDeclProp_UniversalAmmoComponent@@") {
        return true;
    }
    return useComponentClass == ".?AVidDeclProp_ItemPickupComponent@@" && itemClass == ".?AVidDeclAmmo@@";
}

AmmoWave::AmmoWave(int rows) : rows_(std::max(1, rows)) {}

bool AmmoWave::pickUp(double seconds) {
    if (!std::isfinite(seconds)) {
        return false;
    }
    if (started_ && seconds >= start_ && seconds - start_ < kAmmoMergeSeconds) {
        return false;
    }
    active_ = true;
    started_ = true;
    start_ = seconds;
    done_ = -1;
    return true;
}

std::optional<AmmoStep> AmmoWave::step(double seconds) {
    if (!active_) {
        return std::nullopt;
    }
    const int i = static_cast<int>(std::max(0.0, seconds - start_) / kAmmoRowSeconds);
    if (i >= rows_) {
        active_ = false;
        return std::nullopt;
    }
    if (i <= done_) {
        return std::nullopt;
    }
    done_ = i;
    AmmoStep s;
    s.row = i;
    s.trailRow = i - 1;
    s.intensity = kAmmoIntensity;
    s.millis = static_cast<int>(std::lround(kAmmoRowSeconds * 1000.0)) + kAmmoOverlapMillis;
    return s;
}

void AmmoWave::stop() {
    active_ = false;
    started_ = false;
}

} // namespace evr::bhaptics
