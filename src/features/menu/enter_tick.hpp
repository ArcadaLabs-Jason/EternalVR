#pragma once

// When the menu pointer's vibration tick plays (docs/VR_MENUS.md): once as a hand's ray comes onto the menu
// panel, not again until the ray has been off the panel for `offSeconds`. A frame without controller data (a
// stale snapshot, an aim pose the runtime could not locate) says nothing about the ray, so it neither ends
// nor starts a stay on the panel. The state must outlive the frame: kept in the per-frame pointer state,
// which is rebuilt every frame, it ticked about 15 times a second on the main and pause menus (the owner's
// Quest 3, 2026-09-28). Pure: the time comes from the caller.

#include <optional>

namespace evr::menu {

class EnterTick {
public:
    EnterTick() = default;
    explicit EnterTick(double offSeconds) : offSeconds_(offSeconds) {}

    // `hit`: the ray meets the panel, misses it, or nullopt when this frame has no data. True on the one
    // update the tick plays.
    bool update(std::optional<bool> hit, double seconds);
    // The menu closed: the next stay on a panel ticks at once.
    void reset();

private:
    double offSeconds_ = 0.25;
    bool onPanel_ = false;
    std::optional<double> lastOn_; // when the ray was last seen on the panel
};

} // namespace evr::menu
