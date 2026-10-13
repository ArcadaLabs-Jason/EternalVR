#include "ui_layer/crosshair.hpp"

#include <cstddef>
#include <cwctype>

namespace evr::ui_layer {

namespace {

std::wstring_view trimmed(std::wstring_view s) {
    while (!s.empty() && std::iswspace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::iswspace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

bool sameNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

const char* crosshairModeName(CrosshairMode mode) {
    return mode == CrosshairMode::Mask ? "mask" : "hold";
}

std::optional<CrosshairMode> parseCrosshairMode(std::wstring_view text) {
    const std::wstring_view t = trimmed(text);
    if (sameNoCase(t, L"hold")) {
        return CrosshairMode::Hold;
    }
    if (sameNoCase(t, L"mask")) {
        return CrosshairMode::Mask;
    }
    return std::nullopt;
}

std::uint32_t nextHoldStreak(std::uint32_t streak, bool holding, bool hidden) {
    if (!holding || !hidden) {
        return 0;
    }
    return streak < kHoldConfirmPresents ? streak + 1 : streak;
}

bool holdConfirmed(std::uint32_t streak, std::uint64_t lastMs, std::uint64_t nowMs) {
    return streak >= kHoldConfirmPresents && (nowMs < lastMs || nowMs - lastMs <= kHoldStaleMs);
}

bool holdRequested(bool want, std::uint64_t requestMs, std::uint64_t nowMs, std::uint32_t presents) {
    const bool old = nowMs >= requestMs && nowMs - requestMs > kHoldStaleMs;
    return want && !(old && presents >= kHoldStalePresents);
}

ReticleWrite reticleWrite(bool requested, int reads, ReticleKeep& keep) {
    if (requested) {
        if (reads == kReticleStyleHidden) {
            return ReticleWrite::None;
        }
        if (!keep.kept) {
            keep.kept = true;
            keep.before = reads;
        }
        return ReticleWrite::Hide;
    }
    if (!keep.kept) {
        return ReticleWrite::None;
    }
    // The hold is over either way: given back now, or the cvar already reads another value (a cheat reset or
    // a checkpoint reload put one back before the hold ended), which stays and is not the layer's any more.
    keep.kept = false;
    return reads == kReticleStyleHidden ? ReticleWrite::Restore : ReticleWrite::None;
}

std::uint32_t nextRequestPresents(std::uint32_t presents) {
    return presents < kHoldStalePresents ? presents + 1 : presents;
}

bool wantsReticleHold(CrosshairMode mode, bool handAim) {
    return mode == CrosshairMode::Hold && handAim;
}

bool centreMasked(const CrosshairState& state) {
    if (!state.handAim || state.menu || state.backdrop) {
        return false;
    }
    return state.mode == CrosshairMode::Mask || !state.holdConfirmed;
}

bool HoldTally::note(bool holding, bool nowConfirmed, bool maskable) {
    if (!holding) {
        return false;
    }
    kept += nowConfirmed && maskable ? 1 : 0;
    if (nowConfirmed == confirmed) {
        return false;
    }
    confirmed = nowConfirmed;
    ++changes;
    return true;
}

} // namespace evr::ui_layer
