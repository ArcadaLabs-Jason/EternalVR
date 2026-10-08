#include "vkcore/taa_ssr.hpp"

#include "stereo_seq/setting_follow.hpp"
#include "vkcore/log.hpp"
#include "vkcore/taa_hooks.hpp"

#include <atomic>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-taa";

stereo_seq::SsrHold g_hold;
std::atomic<bool> g_followed{false}; // g_hold.followed(), for the status file's writer on another thread

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
    const bool first = g_hold.followed() && !g_followed.exchange(g_hold.followed());
    if (t.changed || first) {
        EVR_LOG("%s: r_SSR held at %s from now on (the game's Reflections setting: %s)", kTag,
                g_hold.value().c_str(), g_hold.value() == "0" ? "off, Low" : "on, Medium or higher");
    }
    return t.value.empty() ? nullptr : g_hold.value().c_str();
}

void ssrRelease() {
    if (g_hold.held()) {
        g_hold.release();
        g_followed.store(false);
        EVR_LOG("%s: r_SSR no longer held: per-eye TAA failed closed", kTag);
    }
}

bool ssrFollowed() {
    return g_followed.load();
}

} // namespace evr::vkcore
