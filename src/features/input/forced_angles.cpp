#include "features/input/forced_angles.hpp"

#include <algorithm>
#include <optional>
#include <string_view>

namespace evr::input {

const char* forcedReasonName(ForcedReason reason) {
    switch (reason) {
    case ForcedReason::None:
        return "none";
    case ForcedReason::SetViewAngles:
        return "forced view angles";
    case ForcedReason::Inhibit:
        return "view inhibited";
    case ForcedReason::Cutscene:
        return "cutscene";
    case ForcedReason::CameraAnimation:
        return "camera animation";
    case ForcedReason::WallClimb:
        return "climbable wall";
    case ForcedReason::Settling:
        return "settling after a forced view";
    }
    return "none";
}

bool aimsWithHead(ForcedReason reason) {
    return reason == ForcedReason::WallClimb;
}

ForcedAngleGate::ForcedAngleGate(int resumeFrames)
    : resumeFrames_(std::clamp(resumeFrames, 0, 600)), sinceSignal_(resumeFrames_ + 1) {}

bool ForcedAngleGate::update(const ForcedAngleSignals& signals) {
    ForcedReason now = ForcedReason::None;
    if (signals.cutscene) {
        now = ForcedReason::Cutscene;
    } else if (signals.foreignSetViewAngles && !signals.wallClimb) {
        // On the wall the game's calls set the view to the player's own angles (the climb animation's deltas
        // every tick, the let-go once): they do not take the view from the head.
        now = ForcedReason::SetViewAngles;
    } else if ((signals.inhibitFlags & kInhibitViewMask) != 0) {
        now = ForcedReason::Inhibit;
    } else if (signals.wallClimb) {
        // Before a camera animation: on the wall the aim behaves as head aim does, whatever the hands play.
        now = ForcedReason::WallClimb;
    } else if (signals.cameraAnimation) {
        now = ForcedReason::CameraAnimation;
    }
    const bool wasYielding = reason_ != ForcedReason::None;
    if (now != ForcedReason::None) {
        sinceSignal_ = 0;
        reason_ = now;
    } else if (sinceSignal_ < resumeFrames_) {
        ++sinceSignal_;
        reason_ = ForcedReason::Settling;
    } else {
        sinceSignal_ = resumeFrames_ + 1;
        reason_ = ForcedReason::None;
    }
    const bool yielding = reason_ != ForcedReason::None;
    if (yielding) {
        ++yielded_;
        if (!wasYielding) {
            ++episodes_;
        }
    }
    return yielding;
}

bool climbLookSwitch(std::wstring_view value) {
    const auto lower = [](wchar_t c) {
        return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    };
    const auto is = [&](std::wstring_view word) {
        return value.size() == word.size() &&
               std::equal(value.begin(), value.end(), word.begin(),
                          [&](wchar_t a, wchar_t b) { return lower(a) == lower(b); });
    };
    return !(is(L"0") || is(L"off"));
}

ClimbCvarAction climbCvarAction(bool wanted, bool touchAllowed, bool saved) {
    if (wanted && touchAllowed) {
        return ClimbCvarAction::Hold;
    }
    return saved ? ClimbCvarAction::Restore : ClimbCvarAction::None;
}

void SavedCvarValue::beforeWrite(int current) {
    if (!value_) {
        value_ = current;
    }
}

std::optional<int> SavedCvarValue::take() {
    const std::optional<int> value = value_;
    value_.reset();
    return value;
}

ClimbFrames::ClimbFrames(int holdFrames)
    : holdFrames_(std::clamp(holdFrames, 0, 600)), sinceTick_(holdFrames_ + 1) {}

bool ClimbFrames::update(std::uint32_t ticks) {
    const bool was = onWall_;
    if (ticks > 0) {
        sinceTick_ = 0;
        onWall_ = true;
    } else if (onWall_ && sinceTick_ < holdFrames_) {
        ++sinceTick_;
    } else {
        sinceTick_ = holdFrames_ + 1;
        onWall_ = false;
    }
    if (onWall_) {
        ++frames_;
        if (!was) {
            ++climbs_;
        }
    }
    return onWall_;
}

} // namespace evr::input
