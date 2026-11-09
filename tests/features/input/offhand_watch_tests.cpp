#include "features/input/offhand_watch.hpp"

#include <doctest/doctest.h>

#include <string>

using evr::Vec3;
using evr::input::ArmReason;
using evr::input::OffhandWatch;
using evr::input::OffhandWatchFrame;
using evr::input::OffhandWatchTick;

namespace {

bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("the off hand's line counts the decisions, their reasons, the weights and the tracking") {
    OffhandWatch w;
    CHECK(w.empty());
    w.noteRefused(7);
    for (int i = 0; i < 8; ++i) {
        w.noteTick({ArmReason::Controller, 1.0f, true});
    }
    w.noteTick({ArmReason::Untracked, 0.4f, false});
    w.noteTick({ArmReason::Untracked, 0.0f, false});
    w.noteTick({ArmReason::Controller, 0.6f, true});
    w.noteTick({ArmReason::Action, 0.0f, false});
    w.noteRefused(9);
    CHECK_FALSE(w.empty());
    const std::string line = w.line();
    CHECK(has(line, "12 tick(s), the controller's 9, the game's 3 (untracked 2, left-arm action 1)"));
    CHECK(has(line, "weight 0.00 to 1.00"));
    CHECK(has(line, "2 refused"));
    CHECK(has(line, "the off hand not tracked in 3 tick(s) (lost 2 time(s))"));
    CHECK(has(line, "the glove not measured (no posed frame)"));
}

TEST_CASE("the off hand's line gives how far the glove was drawn from where it was meant") {
    OffhandWatch w;
    w.noteTick({ArmReason::Controller, 1.0f, true});
    const Vec3 meant{1.0f, 2.0f, 0.5f};
    w.noteFrame({meant + Vec3{0.001f, 0.0f, 0.0f}, meant, 0.0});
    w.noteFrame({meant + Vec3{0.0f, 0.002f, 0.0f}, meant, 0.0});
    w.noteFrame({meant + Vec3{0.0f, 0.0f, 0.120f}, meant, 11.2}); // the weapon hand's move in between
    const std::string line = w.line();
    CHECK(has(line, "the glove drawn from where it was meant: median 2.0 mm, at most 120.0 mm (3 frame(s))"));
    CHECK(has(line, "posed from hands at most 11.20 ms apart from its placement's"));
    // The steps with the controller still: 2.2 mm, then 120 mm.
    CHECK(has(line, "largest step in a frame with the controller still 120.0 mm (2 step(s))"));
}

TEST_CASE("the glove's steps count only while its controller holds still, and restart after a break") {
    OffhandWatch w;
    w.noteTick({ArmReason::Controller, 1.0f, true});
    // The controller moves 5 cm a frame and the glove with it: no step counted.
    w.noteFrame({{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0});
    w.noteFrame({{0.05f, 0.0f, 0.0f}, {0.05f, 0.0f, 0.0f}, 0.0});
    CHECK(has(w.line(), "still 0.0 mm (0 step(s))"));
    // A frame without a measure (the arm the game's): the next frame starts a new chain.
    w.noteBreak();
    w.noteFrame({{0.5f, 0.0f, 0.0f}, {0.05f, 0.0f, 0.0f}, 0.0});
    CHECK(has(w.line(), "still 0.0 mm (0 step(s))"));
    // Across the 10 s the chain goes on: the controller still, the glove 1 cm away from the last frame's.
    w.reset();
    w.noteTick({ArmReason::Controller, 1.0f, true});
    w.noteFrame({{0.51f, 0.0f, 0.0f}, {0.05f, 0.0f, 0.0f}, 0.0});
    CHECK(has(w.line(), "still 10.0 mm (1 step(s))"));
}

TEST_CASE("the refused count and the off hand's tracking carry over a reset") {
    OffhandWatch w;
    w.noteRefused(3);
    w.noteTick({ArmReason::Untracked, 0.0f, false});
    w.reset();
    CHECK(w.empty());
    // Still untracked from the last window: not a new loss. Nothing refused since.
    w.noteTick({ArmReason::Untracked, 0.0f, false});
    w.noteRefused(3);
    CHECK(has(w.line(), "0 refused"));
    CHECK(has(w.line(), "not tracked in 1 tick(s) (lost 0 time(s))"));
}
