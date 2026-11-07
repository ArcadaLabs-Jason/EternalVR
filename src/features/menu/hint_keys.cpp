#include "features/menu/hint_keys.hpp"

namespace evr::menu {

std::optional<std::size_t> hintShortcut(std::int32_t slot, std::int32_t gameKey, std::string_view label) {
    for (std::size_t i = 0; i < kHintShortcuts.size(); ++i) {
        const HintShortcut& s = kHintShortcuts[i];
        if (s.slot == slot && s.gameKey == gameKey && (s.label.empty() || s.label == label)) {
            return i;
        }
    }
    return std::nullopt;
}

bool hintShortcutWantsLabel(std::int32_t slot, std::int32_t gameKey) {
    for (const HintShortcut& s : kHintShortcuts) {
        if (s.slot == slot && s.gameKey == gameKey && !s.label.empty()) {
            return true;
        }
    }
    return false;
}

double hintSinceShown(std::uint64_t now, std::uint64_t at) {
    if (at == 0) {
        return -1.0;
    }
    return now > at ? static_cast<double>(now - at) * 1e-6 : 0.0;
}

bool MenuPauseHold::update(double seconds, bool taken, bool held) {
    if (held && (taken || press_)) {
        press_ = true;
    } else if (!held && press_) {
        press_ = false;
        releasedAt_ = seconds;
    }
    const bool recent = releasedAt_ >= 0.0 && seconds - releasedAt_ < afterRelease_;
    return taken || press_ || recent;
}

bool HintKeys::shown(std::size_t shortcut, const HintKeysInput& in) const {
    const double since = shortcut < in.sinceShown.size() ? in.sinceShown[shortcut] : -1.0;
    if (!in.allowed || !in.menuUp || since < 0.0 || since >= tuning_.shownFresh) {
        return false;
    }
    // One shortcut has a place: one whose press is still held (latched until let go), else the one shown most
    // recently (as recently: the first in the table).
    for (std::size_t other = 0; other < kHintShortcuts.size(); ++other) {
        if (other == shortcut || kHintShortcuts[other].button != kHintShortcuts[shortcut].button) {
            continue;
        }
        if (latched(other)) {
            return false;
        }
        const double otherSince = in.sinceShown[other];
        if (!latched(shortcut) && otherSince >= 0.0 &&
            (otherSince < since || (otherSince == since && other < shortcut))) {
            return false;
        }
    }
    return true;
}

HintKeysOutput HintKeys::update(const HintKeysInput& in) {
    HintKeysOutput out;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        Button& b = buttons_[i];
        const bool shownNow = shown(i, in);
        const std::uint8_t key = kHintShortcuts[i].virtualKey;
        if (in.held[i] && !b.held && shownNow && !b.keyDown) {
            out.keys.push_back({key, true});
            b.keyDown = true;
            b.pressed = true;
            b.releaseWanted = false;
            b.downAt = in.seconds;
        }
        if (!in.held[i]) {
            b.pressed = false;
        }
        if (b.keyDown && (!in.held[i] || !in.allowed)) {
            b.releaseWanted = true;
        }
        if (b.keyDown && b.releaseWanted && in.seconds - b.downAt >= tuning_.minHold) {
            out.keys.push_back({key, false});
            b.keyDown = false;
            b.releaseWanted = false;
        }
        out.taken[i] = shownNow || b.pressed || b.keyDown;
        b.held = in.held[i];
    }
    return out;
}

std::vector<HintKey> HintKeys::release() {
    std::vector<HintKey> keys;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        Button& b = buttons_[i];
        if (b.keyDown) {
            keys.push_back({kHintShortcuts[i].virtualKey, false});
        }
        b.keyDown = false;
        b.releaseWanted = false;
    }
    return keys;
}

} // namespace evr::menu
