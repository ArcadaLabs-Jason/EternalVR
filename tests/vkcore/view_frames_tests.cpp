#include "vkcore/view_frames.hpp"

#include <doctest/doctest.h>

using evr::vkcore::View1Frames;

TEST_CASE("view 1 counts as rendered only after a run of frames with it") {
    View1Frames frames(3);
    CHECK_FALSE(frames.rendered()); // nothing sent yet: the clone was never written
    frames.frame(true);
    frames.frame(true);
    CHECK_FALSE(frames.rendered()); // the present may still be of a frame before these
    frames.frame(true);
    CHECK(frames.rendered());
    for (int i = 0; i < 100; ++i) {
        frames.frame(true);
    }
    CHECK(frames.rendered());
}

TEST_CASE("one frame without view 1 (a loading screen) drops it until the run is back") {
    View1Frames frames(3);
    for (int i = 0; i < 10; ++i) {
        frames.frame(true);
    }
    frames.frame(false);
    CHECK_FALSE(frames.rendered()); // the clone holds the last world frame from before
    frames.frame(true);
    frames.frame(true);
    CHECK_FALSE(frames.rendered());
    frames.frame(true);
    CHECK(frames.rendered());
}

TEST_CASE("frames that never render view 1 (the async compute safety net) never show its image") {
    View1Frames frames(3);
    for (int i = 0; i < 1000; ++i) {
        frames.frame(false);
        CHECK_FALSE(frames.rendered());
    }
}

TEST_CASE("new clones (a resize without a loading screen) start the run again") {
    View1Frames frames(3);
    for (int i = 0; i < 10; ++i) {
        frames.frame(true);
    }
    CHECK(frames.rendered());
    frames.restart();
    CHECK_FALSE(frames.rendered()); // the frames sent before wrote the old clones
    frames.frame(true);             // the frame that made the new ones renders into them
    frames.frame(true);
    CHECK_FALSE(frames.rendered()); // the present may still be of a frame before the new clones
    frames.frame(true);
    CHECK(frames.rendered());
}
