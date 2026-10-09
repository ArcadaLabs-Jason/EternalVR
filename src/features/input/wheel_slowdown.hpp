#pragma once

// The game's slowdown under the thumb-rest wheel (ETERNALVR_THUMBREST_SLOWDOWN, rest_wheel.hpp).
//
// The game slows time while its weapon wheel is open (weaponWheel_slowTimeScale, 0.14 by default). That stays
// as it is unless the player turns the slowdown off for the thumb-rest wheel: the layer then holds the cvar
// at 1 from the frame that wheel presses the game's wheel (the game opens it 0.18 s later, so the value is in
// place by then) until kSlowdownRestoreSeconds after it lets go (the wheel's closing still runs on the slow
// clock), and writes the game's own value back. The stick's own wheel and a button's always slow time: one
// taking the wheel over gets the game's value back at once. With the slowdown on (the default) nothing is
// ever written.
//
// Pure: the layer writes the cvar when told (vkcore/rest_wheel.cpp), and writes it back on every other way
// out too: the controllers going stale, a multiplayer guard trip, the layer shutting down.

#include <cstdint>

namespace evr::input {

inline constexpr float kSlowdownRestoreSeconds = 0.3f;

enum class SlowdownAction : std::uint8_t {
    None,
    Hold,    // write 1, keeping the game's value
    Restore, // write the game's value back
};

class WheelSlowdown {
public:
    // `slowdown`: the setting; true (the game's own slowdown) never holds anything.
    explicit WheelSlowdown(bool slowdown = true) : holds_(!slowdown) {}

    // Once per mapper run. `restWheel`: the thumb-rest wheel holds the game's wheel; `otherWheel`: something
    // else does (the stick's down hold, a button). `dtSeconds`: non-finite or negative counts as zero.
    SlowdownAction update(bool restWheel, bool otherWheel, float dtSeconds);

    // Every other way out: Restore when the value is held, and nothing is held afterwards.
    SlowdownAction reset();

    [[nodiscard]] bool held() const { return held_; }

private:
    bool holds_;
    bool held_ = false;
    float sinceRelease_ = 0.0f;
};

} // namespace evr::input
