#include "stereo_seq/setting_follow.hpp"

#include "stereo_seq/seq_settings.hpp"

#include <optional>

namespace evr::stereo_seq {

bool SsrHold::start(std::string_view setting) {
    const std::optional<CvarExpectation> c = stereoSsrCvar(setting);
    held_ = c.has_value();
    follows_ = held_ && stereoSsrFollowsGame(setting);
    seen_ = false;
    value_ = held_ ? std::string(c->value) : std::string();
    return held_;
}

SsrHold::Tick SsrHold::tick(int upscaleQuality) {
    Tick t;
    if (!held_) {
        return t;
    }
    if (follows_) {
        if (const std::optional<CvarExpectation> c = reflectionsSsrCvar(upscaleQuality)) {
            seen_ = true;
            t.changed = c->value != value_;
            value_ = std::string(c->value);
        }
    }
    t.value = value_;
    return t;
}

void SsrHold::release() {
    held_ = false;
}

std::string_view ssdoHoldValue(std::string_view held, int menuChoice) {
    if (menuChoice == 0) {
        return "0";
    }
    if (menuChoice == 1) {
        return "1";
    }
    return held;
}

} // namespace evr::stereo_seq
