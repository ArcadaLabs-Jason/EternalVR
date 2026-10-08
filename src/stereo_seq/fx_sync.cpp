#include "stereo_seq/fx_sync.hpp"

#include <cctype>
#include <string>

namespace evr::stereo_seq {

FxSyncMode fxSyncMode(std::string_view value) {
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
    if (lower == "1" || lower == "on" || lower == "true" || lower == "yes") {
        return FxSyncMode::On;
    }
    if (lower == "count") {
        return FxSyncMode::Count;
    }
    return FxSyncMode::Off;
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

FxAction fxGenerationFor(FxAction render, std::uint32_t stamp, std::uint32_t ringFrame) {
    switch (render) {
    case FxAction::UseEyeL:
        return stamp == ringFrame ? FxAction::UseEyeL : FxAction::Run;
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
