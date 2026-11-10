#include "stereo_seq/fx_sync.hpp"

#include <cctype>
#include <string>

namespace evr::stereo_seq {

namespace {

// The value trimmed and in lower case.
std::string normalized(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    std::string lower;
    for (const char c : value) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower;
}

} // namespace

FxSyncMode fxSyncMode(std::string_view value) {
    const std::string lower = normalized(value);
    if (lower == "0" || lower == "off" || lower == "false" || lower == "no") {
        return FxSyncMode::Off;
    }
    if (lower == "count") {
        return FxSyncMode::Count;
    }
    return FxSyncMode::On;
}

bool fxGpuStages(std::string_view value) {
    const std::string lower = normalized(value);
    return !(lower == "0" || lower == "off" || lower == "false" || lower == "no");
}

bool fxRtMask(std::string_view value) {
    return fxGpuStages(value); // the same switch values
}

FxAction fxActionFor(FxSyncMode mode, Eye eye, bool gameTouchAllowed) {
    if (mode == FxSyncMode::Off || eye != Eye::Right || !gameTouchAllowed) {
        return FxAction::Run;
    }
    return mode == FxSyncMode::Count ? FxAction::RunCounted : FxAction::UseEyeL;
}

FxAction fxPoolResetFor(std::optional<FxAction> ringAdvance) {
    return ringAdvance.value_or(FxAction::Run);
}

FxAction fxRtMaskClearFor(FxAction render, bool worldHasRing) {
    if (!worldHasRing || render == FxAction::BindGpuThenRun) {
        return FxAction::Run;
    }
    return render;
}

FxAction fxGenerationFor(FxAction render, std::uint32_t stamp, std::uint32_t ringFrame, bool gpuStaged) {
    switch (render) {
    case FxAction::UseEyeL:
        if (stamp != ringFrame) {
            return FxAction::Run;
        }
        return gpuStaged ? FxAction::BindGpuThenRun : FxAction::UseEyeL;
    case FxAction::RunCounted:
        return stamp == ringFrame - 1 ? FxAction::RunCounted : FxAction::Run;
    default:
        return FxAction::Run;
    }
}

std::int32_t fxPreviousSlot(std::int32_t index) {
    return (index + 2) % 3;
}

} // namespace evr::stereo_seq
