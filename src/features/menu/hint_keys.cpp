#include "features/menu/hint_keys.hpp"

namespace evr::menu {

std::optional<std::size_t> hintShortcut(std::int32_t slot, std::int32_t gameKey) {
    for (std::size_t i = 0; i < kHintShortcuts.size(); ++i) {
        if (kHintShortcuts[i].slot == slot && kHintShortcuts[i].gameKey == gameKey) {
            return i;
        }
    }
    return std::nullopt;
}

bool HintKeys::shown(std::size_t shortcut, const HintKeysInput& in) const {
    const double since = shortcut < in.sinceShown.size() ? in.sinceShown[shortcut] : -1.0;
    return in.allowed && in.menuUp && since >= 0.0 && since < tuning_.shownFresh;
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
