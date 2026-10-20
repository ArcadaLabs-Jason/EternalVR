#pragma once

// When the hand poses the layer wrote for the game's frames are still current (docs/VR_HANDS_HUD.md, "When
// the arms go back to the game").
//
// The camera hook writes the hands for each view of the game (the runtime's pose predicted for when that
// frame is shown), and the viewmodel hook places the arms model at each tick of the game's hands; the hooks
// of the ticks that follow (the arms, the gun, the shots) read them. Their age at a read is the gap since
// the last view, so a single long frame (a hitch) made them look old although they were the newest the
// runtime had given, and the arms went back to the game's animation. Here a pose is current while the
// views keep coming: written after the tick before the last one (whatever the frames' length), or no older
// than the stale limit (the views stopped: the camera hook no longer runs, the session lost focus). Never
// older than the hitch cap: when the views and the ticks both stop (a load, a freeze), a reader that runs
// before the first tick after it (a shot, the off hand) does not take the pose from before it. A lost
// controller does not depend on this: the runtime's location flags make its pose invalid at once.
//
// Pure, on any monotonic clock (the layer's is QueryPerformanceCounter's counts), so it is tested without
// the game.

#include <cstdint>

namespace evr::input {

class ViewPoseClock {
public:
    // At each tick of the game's hands (the viewmodel hook), on the clock the poses are stamped with.
    void tick(std::int64_t now);

    // Whether a pose stamped `written` is current at `now` (`stale`: the views' stale limit; `cap`: the
    // hitch cap, the oldest a pose may ever be; same units). Two ticks back, so the rule holds whichever way
    // the readers and the stamp fall within a tick.
    [[nodiscard]] bool
    current(std::int64_t written, std::int64_t now, std::int64_t stale, std::int64_t cap) const;

private:
    std::int64_t last_ = 0;     // the latest tick
    std::int64_t previous_ = 0; // the tick before it
    int ticks_ = 0;             // up to 2
};

// The clock as a reader sees it at `now`, with the limits `current` takes.
struct ViewClockRead {
    ViewPoseClock clock;
    std::int64_t now = 0;
    std::int64_t stale = 0;
    std::int64_t cap = 0;

    [[nodiscard]] bool current(std::int64_t written) const { return clock.current(written, now, stale, cap); }
};

} // namespace evr::input
