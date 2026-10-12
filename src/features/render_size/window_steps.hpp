#pragma once

// When the render size turns on or changes, how the game's window is placed
// (docs/rig-findings/render-size.md, section 3, step 4).
//
// The mirror placement moves and resizes the window. A swapchain with present scaling only reports
// VK_SUBOPTIMAL_KHR for that (taken as success), but a swapchain made without it, at the window's own size
// (the game's first one when `auto` learns its size only after it), goes out of date at its next present,
// and the game recreates it from that error. So while the game has such a swapchain and none with present
// scaling, the window is first only told to read its client area again (the answered size): the game then
// recreates its swapchain through its own size check while the old one stays valid, and the window moves
// once a swapchain with present scaling exists. Before any swapchain the window moves at once, as before. A
// create without present scaling that is still running when the size arrives (the game's first create can
// take half a second) counts as such a swapchain: the window moving during it would make the new swapchain
// out of date as well.
//
// Plain logic: no Windows, Vulkan or OpenXR here.

#include <cstdint>

namespace evr::render_size {

enum class WindowStep : std::uint8_t {
    Move,           // the mirror placement now
    ReadClientArea, // only tell the window to read its client area; the move waits (WindowSteps)
};

// The game's swapchains on its window's surface when an answer switches on or changes.
struct GameSwapchains {
    bool unscaled = false;         // one made without present scaling exists
    bool unscaledCreating = false; // a create without present scaling has started and not returned yet
    bool scaled = false;           // one made with present scaling exists
};

// The step for an answer that switched on or changed.
WindowStep stepForAnswer(GameSwapchains swapchains);

// The move that waits for a swapchain with present scaling.
class WindowSteps {
public:
    // How long the move waits at most: a game that never recreates its swapchain still gets its window
    // placed (as before this wait existed).
    static constexpr double kMoveWaitSeconds = 10.0;

    // An answer switched on or changed at `seconds` (a monotonic clock): the step to take now. A move that
    // waited is replaced by this answer's step.
    WindowStep onAnswer(GameSwapchains swapchains, double seconds);
    // A swapchain with present scaling was made: true once when the waiting move is due now.
    bool onScaledSwapchain();
    // Any later moment: true once when the waiting move has waited kMoveWaitSeconds.
    bool overdue(double seconds);
    // The render size went off: no move waits any more.
    void cancel() { waiting_ = false; }
    [[nodiscard]] bool waiting() const { return waiting_; }

private:
    bool waiting_ = false;
    double since_ = 0.0;
};

} // namespace evr::render_size
