#pragma once

// Parallel Eye Rendering: whether the frames the dispatcher sent (view_slots.cpp) rendered view 1, for the
// eye copy (presenter_eyes.hpp). Eye 1 takes view 1's image only when the frame being presented rendered it:
// on a frame with view 0 alone (a loading screen, the async compute safety net, ETERNALVR_TEST_VIEW_ONLY=0,
// the clones off) view 1's image is the last world frame's or one never written. The present can come a
// frame or two after its frame was sent, so view 1 counts as rendered only after `run` frames in a row
// with it, and again only after `run` more once its clones are made again (a frame sent before wrote the
// clones of before). Header-only, without the game (tests/vkcore/view_frames_tests.cpp).

#include <atomic>

namespace evr::vkcore {

class View1Frames {
public:
    explicit View1Frames(int run) : run_(run) {}

    // The dispatcher sent a frame, with view 1 or without (render thread).
    void frame(bool view1) {
        if (!view1) {
            streak_.store(0, std::memory_order_relaxed);
        } else if (streak_.load(std::memory_order_relaxed) < run_) {
            streak_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // View 1's clones were made again (a resize without a loading screen, say): the frames sent before wrote
    // the old ones, so the run starts again with the frame about to be sent (render thread).
    void restart() { streak_.store(0, std::memory_order_relaxed); }

    // The last `run` frames sent all rendered view 1 (any thread).
    bool rendered() const { return streak_.load(std::memory_order_relaxed) >= run_; }

private:
    int run_;
    std::atomic<int> streak_{0};
};

} // namespace evr::vkcore
