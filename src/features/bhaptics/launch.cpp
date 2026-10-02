// LaunchFilter, the launch's curve, and BodyHaptics::launch: the curve as frames on both sides of the vest.

#include "features/bhaptics/launch.hpp"

#include "features/bhaptics/body_haptics.hpp"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace evr::bhaptics {

const char* launchSourceName(LaunchSource source) {
    return source == LaunchSource::Booster ? "booster" : "jump pad";
}

bool LaunchFilter::note(double seconds) {
    if (!std::isfinite(seconds)) {
        return false;
    }
    const bool fresh = !seen_ || std::fabs(seconds - lastTouch_) >= kLaunchRepeatSeconds;
    seen_ = true;
    lastTouch_ = seconds;
    return fresh;
}

void LaunchFilter::reset() {
    seen_ = false;
    lastTouch_ = 0.0;
}

const LaunchStep* launchStepAt(double sinceStart) {
    if (!std::isfinite(sinceStart) || sinceStart < 0.0 || sinceStart >= kLaunchSeconds) {
        return nullptr;
    }
    const auto index = static_cast<std::size_t>(sinceStart / kLaunchStepSeconds);
    return index < kLaunchSteps.size() ? &kLaunchSteps[index] : nullptr;
}

void BodyHaptics::launch(std::vector<Frame>& out, double seconds) {
    if (seconds >= launchUntil_) {
        return;
    }
    // Each step once; a late update plays the step under way, not the ones it missed.
    const LaunchStep* step = launchStepAt(seconds - launchStart_);
    const std::size_t index =
        step ? static_cast<std::size_t>(step - kLaunchSteps.data()) : kLaunchSteps.size();
    if (index >= kLaunchSteps.size() || index < launchNextStep_) {
        return;
    }
    launchNextStep_ = index + 1;
    for (const Device side : {Device::VestFront, Device::VestBack}) {
        std::vector<Dot> dots;
        for (int wearerColumn = 0; wearerColumn < kVestColumns; ++wearerColumn) {
            dots.push_back(
                {static_cast<std::uint8_t>((kVestRows - 1) * kVestColumns + vestColumn(side, wearerColumn)),
                 scaled(step->bottom)});
            if (step->above > 0.0f) {
                dots.push_back({static_cast<std::uint8_t>((kVestRows - 2) * kVestColumns +
                                                          vestColumn(side, wearerColumn)),
                                scaled(step->above)});
            }
        }
        add(out, Effect::Launch, side, kLaunchMillis, std::move(dots));
    }
}

} // namespace evr::bhaptics
