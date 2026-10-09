#include "vkcore/taa_ssr.hpp"

#include "stereo_seq/setting_follow.hpp"
#include "vkcore/log.hpp"
#include "vkcore/taa_hooks.hpp"

#include <atomic>
#include <cstdlib>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-taa";

stereo_seq::SsrHold g_hold;
// The r_SSR held while g_hold.followed(), else -1: for the status file's writer on another thread.
std::atomic<int> g_followed{-1};

} // namespace

void ssrDecide(bool perEye, bool located) {
    const std::string& setting = taaSsrSetting();
    if (!g_hold.start(setting)) {
        EVR_LOG("%s: r_SSR is left as the game has it (ETERNALVR_STEREO_SSR=%s)", kTag, setting.c_str());
    } else if (!perEye) {
        g_hold.release();
        EVR_LOG("%s: r_SSR is not held: per-eye TAA failed closed", kTag);
    } else if (!located) {
        g_hold.release();
        EVR_LOG("%s: r_SSR not located; left as the game has it", kTag);
    } else {
        EVR_LOG("%s: r_SSR held at %s (ETERNALVR_STEREO_SSR=%s; per-eye TAA; %s)", kTag,
                g_hold.value().c_str(), setting.empty() ? "unset" : setting.c_str(),
                g_hold.follows() ? "follows the game's Reflections setting" : "whatever the game's setting");
    }
}

const char* ssrHoldTick(int upscaleQuality) {
    const stereo_seq::SsrHold::Tick t = g_hold.tick(upscaleQuality);
    const int followed = g_hold.followed() ? std::atoi(g_hold.value().c_str()) : -1;
    const bool first = followed >= 0 && g_followed.exchange(followed) < 0;
    if (t.changed || first) {
        EVR_LOG("%s: r_SSR held at %s from now on (the game's Reflections setting: %s)", kTag,
                g_hold.value().c_str(), g_hold.value() == "0" ? "off, Low" : "on, Medium or higher");
    }
    return t.value.empty() ? nullptr : g_hold.value().c_str();
}

void ssrRelease() {
    if (g_hold.held()) {
        g_hold.release();
        g_followed.store(-1);
        EVR_LOG("%s: r_SSR no longer held: per-eye TAA failed closed", kTag);
    }
}

int ssrFollowedValue() {
    return g_followed.load();
}

} // namespace evr::vkcore
