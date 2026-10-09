#pragma once

// The holds that follow the player's own game settings in Route S (docs/VR_STEREO.md, ETERNALVR_STEREO_SSR
// and ETERNALVR_STEREO_SSDO): r_SSR while per-eye TAA runs (vkcore/taa_ssr.hpp) and r_SSDO
// (vkcore/runtime_cvars.cpp). The game writes r_SSR 0 and r_SSDO 0 on every render while r_TAASafeMode is not
// 0 (0x1C6FCC0); each hold writes over that knock-on, at the value the launcher passed until the game's own
// setting runs, then at that setting's value.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace evr::stereo_seq {

// r_SSR held while per-eye TAA runs. The game's Reflections setting is told by the
// r_raytracedReflectionsTemporalUpscaleQuality it writes with r_SSR (reflectionsSsrCvar): a value from 1 to 3
// read on a tick, before the layer writes it back to 0, means the setting ran since the last tick. This needs
// the cvar at 0 from the start: the launcher's command line sets
// +r_raytracedReflectionsTemporalUpscaleQuality 0 (forced-cvars.txt; the rig's launch-ht.ps1 too), since its
// default 1 would read as Medium or higher until the profile's load.
class SsrHold {
public:
    // The first stereo tick with per-eye TAA on: ETERNALVR_STEREO_SSR's value (stereoSsrCvar). False when it
    // holds nothing.
    bool start(std::string_view setting);

    struct Tick {
        std::string_view value; // the r_SSR to hold; empty when not held
        bool changed = false;   // the game's Reflections setting changed the value held this tick
    };
    // Each stereo tick with per-eye TAA on: the upscale quality as read this tick (-1 when not located).
    Tick tick(int upscaleQuality);

    // Per-eye TAA failed closed: nothing held from now on.
    void release();

    [[nodiscard]] bool held() const { return held_; }
    // Not ETERNALVR_STEREO_SSR=off.
    [[nodiscard]] bool follows() const { return follows_; }
    // Held, following the game's setting, and that setting has run since start: the value held is the
    // player's own (the launcher's restore keeps the r_SSR the game saves only then).
    [[nodiscard]] bool followed() const { return held_ && follows_ && seen_; }
    [[nodiscard]] const std::string& value() const { return value_; }

private:
    std::string value_;
    bool held_ = false;
    bool follows_ = false;
    bool seen_ = false;
};

// The r_SSDO to hold: the game's Directional Occlusion setting's value once it ran (`menuChoice` 0 or 1, -1
// before), else `held` (ETERNALVR_STEREO_SSDO's).
std::string_view ssdoHoldValue(std::string_view held, int menuChoice);

// The status file's lines for one of these holds (vkcore/status_file.hpp), `name` "ssr" or "ssdo", when
// `value` is not `reported` (the last one written): with 0 or 1 (held at the player's own setting, at that
// value) <name>_value=<value> then <name>_follow=1; with -1 <name>_follow=0 alone. The value comes first, so
// a file read between the two writes never pairs a new follow with an old value. Empty when unchanged.
std::vector<std::pair<std::string, std::string>> followFields(std::string_view name, int value, int reported);

} // namespace evr::stereo_seq
