#pragma once

// Glory kills in the headset (ETERNALVR_GLORY_KILLS; the options are in features/comfort/glory_kill.hpp and
// docs/VR_HEAD_TRACKED.md). The camera hook calls frame() once per game view, before it builds the view,
// and asks what the current option wants for this frame: the flat screen, a black view, or a steady
// heading. Camera hook thread only, apart from flat(), which the XR worker reads.

#include "features/comfort/glory_kill.hpp"

#include <atomic>
#include <cstddef>
#include <optional>

namespace evr::vkcore {

class GloryKills {
public:
    // `testStart`, `testDuration`: rig tests (ETERNALVR_TEST_GLORY=start,duration): a glory kill is taken to
    // run from `testStart` seconds on the debug-command schedule's clock (debug_commands.hpp) for
    // `testDuration` seconds, whatever the game does, so each option can be checked without a staggered
    // demon. A negative start is none.
    explicit GloryKills(comfort::GloryView view = comfort::GloryView::Follow,
                        double testStart = -1.0,
                        double testDuration = 0.0);

    [[nodiscard]] comfort::GloryView view() const { return view_; }

    // Camera hook, each game view: whether a sync kill runs for `player` (the view's object), the forced-view
    // gate, and a monotonic clock (qpcSeconds).
    comfort::GloryEpisode::Step frame(const std::byte* player, bool forcedView, double seconds);

    // Screen: this game view goes to the flat screen, as a cutscene does.
    [[nodiscard]] bool onScreen() const { return view_ == comfort::GloryView::Screen && episode_.active(); }
    // Fade: the view is held black.
    [[nodiscard]] bool black() const { return view_ == comfort::GloryView::Fade && episode_.active(); }
    // Steady: the body yaw the view keeps while the kill runs (the one before it started); nullopt otherwise.
    [[nodiscard]] std::optional<float> steadyYaw() const;
    // Steady, just after a kill: the body yaw head aim turns the game's aim back to (the view never turned
    // away from it), for a short window in case the game rewrites the aim as the kill ends; nullopt
    // otherwise.
    [[nodiscard]] std::optional<float> restoreYaw(double seconds) const;
    // Head aim turned the game's aim back by `degrees` to restoreYaw (logged for the first episodes).
    void noteRestored(float degrees);

    // Camera hook, after the view is built: the body yaw of this frame, latched when a kill starts; how far
    // it turned during a kill is logged when the kill ends.
    void noteBody(float yaw);

    // XR worker: the game's frames go to the flat screen now (Screen during a kill), so no head-tracked view
    // is shown from the frames drawn just before.
    [[nodiscard]] bool flat() const { return flat_.load(std::memory_order_acquire); }

private:
    comfort::GloryView view_;
    double testStart_ = -1.0;
    double testEnd_ = -1.0;
    comfort::GloryEpisode episode_;
    std::optional<float> lastBody_;
    std::optional<float> latched_;
    std::optional<float> killStartBody_; // the first body yaw of the running kill
    float killTurn_ = 0.0f;              // the largest turn of the body from it during the kill (degrees)
    double restoreUntil_ = -1.0;
    bool loggedRestore_ = false;
    std::atomic<bool> flat_{false};
};

} // namespace evr::vkcore
