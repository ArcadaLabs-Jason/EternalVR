#include "features/roomscale/follow_test_steps.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace evr::roomscale {

Vec3 testStepAxis(const TestSteps& steps) {
    return steps.sideways ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{0.0f, 0.0f, -1.0f};
}

TestStepPhase testStepAt(const TestSteps& steps, double seconds) {
    TestStepPhase out;
    out.legs = static_cast<int>(steps.metres.size()) * 2;
    if (out.legs == 0 || !(seconds >= 0.0) || !std::isfinite(seconds)) {
        return out;
    }
    const double hold = std::max(0.1, static_cast<double>(steps.holdSeconds));
    const double leg = std::floor(seconds / hold);
    out.leg = static_cast<int>(std::min(leg, 1e9));
    out.started = static_cast<double>(out.leg) * hold;
    const int index = out.leg % out.legs;
    const float metres = steps.metres[static_cast<std::size_t>(index / 2)];
    const bool outward = index % 2 == 0;
    out.offset = outward ? testStepAxis(steps) * metres : Vec3{};
    out.asked = outward ? metres : -metres;
    if (steps.commands) {
        out.offset = {};
        out.command = outward ? static_cast<int>(std::lround(metres)) : 0;
    }
    return out;
}

void StepProbe::begin(int leg, float asked, Vec3 axis, double seconds, double holdSeconds) {
    active_ = true;
    half_ = seconds + 0.5 * holdSeconds;
    halfTaken_ = false;
    halfAlong_ = 0.0f;
    halfSeconds_ = seconds;
    axis_ = axis;
    moved_ = {};
    start_ = seconds;
    last_ = seconds;
    report_ = {};
    report_.leg = leg;
    report_.asked = asked;
}

void StepProbe::add(Vec3 displacement, Vec3 absorbed, double seconds, float gap) {
    if (!active_) {
        return;
    }
    if (gap >= 0.0f) {
        if (report_.gapStart < 0.0f) {
            report_.gapStart = gap;
        }
        report_.gapLeft = gap;
        if (report_.closeSeconds < 0.0 && gap <= kCloseMetres) {
            report_.closeSeconds = seconds - start_;
        }
    }
    const float sense = report_.asked < 0.0f ? -1.0f : 1.0f;
    moved_ = moved_ + Vec3{displacement.x, 0.0f, displacement.z};
    last_ = seconds;
    const float along = dot(moved_, axis_) * sense;
    const Vec3 off = moved_ - axis_ * dot(moved_, axis_);
    report_.along = along;
    report_.across = std::max(report_.across, length(off));
    report_.peak = std::max(report_.peak, along);
    report_.absorbed += dot(absorbed, axis_) * sense;
    if (report_.reachSeconds < 0.0 && along >= 0.9f * std::fabs(report_.asked)) {
        report_.reachSeconds = seconds - start_;
    }
    if (report_.firstMotion < 0.0 && length(moved_) >= 0.002f) {
        report_.firstMotion = seconds - start_;
    }
    if (!halfTaken_ && seconds >= half_) {
        halfTaken_ = true;
        halfAlong_ = along;
        halfSeconds_ = seconds;
    }
}

StepReport StepProbe::report() const {
    StepReport out = report_;
    out.seconds = last_ - start_;
    if (halfTaken_ && last_ > halfSeconds_) {
        out.speed = static_cast<float>((report_.along - halfAlong_) / (last_ - halfSeconds_));
    }
    return out;
}

} // namespace evr::roomscale
