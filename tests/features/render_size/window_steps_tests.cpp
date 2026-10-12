#include "features/render_size/window_steps.hpp"

#include <doctest/doctest.h>

using evr::render_size::GameSwapchains;
using evr::render_size::stepForAnswer;
using evr::render_size::WindowStep;
using evr::render_size::WindowSteps;

TEST_CASE("window steps: an answer before the game's first swapchain moves the window at once") {
    CHECK(stepForAnswer(GameSwapchains{}) == WindowStep::Move);
    WindowSteps steps;
    CHECK(steps.onAnswer(GameSwapchains{}, 1.0) == WindowStep::Move);
    CHECK_FALSE(steps.waiting());
    CHECK_FALSE(steps.onScaledSwapchain()); // the scaled create that follows moves nothing more
    CHECK_FALSE(steps.overdue(100.0));
}

TEST_CASE("window steps: a late answer while only an unscaled swapchain exists waits for a scaled one") {
    CHECK(stepForAnswer(GameSwapchains{.unscaled = true}) == WindowStep::ReadClientArea);
    WindowSteps steps;
    CHECK(steps.onAnswer(GameSwapchains{.unscaled = true}, 14.0) == WindowStep::ReadClientArea);
    CHECK(steps.waiting());
    CHECK_FALSE(steps.overdue(15.0));
    CHECK(steps.onScaledSwapchain()); // the move is due once
    CHECK_FALSE(steps.waiting());
    CHECK_FALSE(steps.onScaledSwapchain());
    CHECK_FALSE(steps.overdue(100.0));
}

TEST_CASE("window steps: a change while a scaled swapchain exists moves at once (only suboptimal follows)") {
    CHECK(stepForAnswer(GameSwapchains{.scaled = true}) == WindowStep::Move);
    CHECK(stepForAnswer(GameSwapchains{.unscaled = true, .scaled = true}) ==
          WindowStep::Move); // an old unscaled one still alive beside it
    WindowSteps steps;
    CHECK(steps.onAnswer(GameSwapchains{.scaled = true}, 30.0) == WindowStep::Move);
    CHECK_FALSE(steps.waiting());
}

TEST_CASE("window steps: a move that never sees a scaled swapchain is placed after the wait") {
    WindowSteps steps;
    REQUIRE(steps.onAnswer(GameSwapchains{.unscaled = true}, 20.0) == WindowStep::ReadClientArea);
    CHECK_FALSE(steps.overdue(20.0 + WindowSteps::kMoveWaitSeconds - 0.1));
    CHECK(steps.overdue(20.0 + WindowSteps::kMoveWaitSeconds));
    CHECK_FALSE(steps.waiting());
    CHECK_FALSE(steps.overdue(1000.0)); // once
    CHECK_FALSE(steps.onScaledSwapchain());
}

TEST_CASE("window steps: a newer answer replaces the waiting move, and off drops it") {
    WindowSteps steps;
    REQUIRE(steps.onAnswer(GameSwapchains{.unscaled = true}, 10.0) == WindowStep::ReadClientArea);
    // The size changed again before the recreate; the wait starts over.
    REQUIRE(steps.onAnswer(GameSwapchains{.unscaled = true}, 18.0) == WindowStep::ReadClientArea);
    CHECK_FALSE(steps.overdue(10.0 + WindowSteps::kMoveWaitSeconds));
    CHECK(steps.waiting());
    // A change once the scaled swapchain exists: the move goes now and nothing waits.
    CHECK(steps.onAnswer(GameSwapchains{.unscaled = true, .scaled = true}, 19.0) == WindowStep::Move);
    CHECK_FALSE(steps.waiting());
    REQUIRE(steps.onAnswer(GameSwapchains{.unscaled = true}, 30.0) == WindowStep::ReadClientArea);
    steps.cancel();
    CHECK_FALSE(steps.onScaledSwapchain());
    CHECK_FALSE(steps.overdue(1000.0));
}

TEST_CASE(
    "window steps: an answer while the game's first create without present scaling runs waits as well") {
    // The answer came after the create began (it was not scaled) and before it returned (rig run pr-d1: the
    // create took 578 ms); moving the window then would make the new swapchain out of date.
    CHECK(stepForAnswer(GameSwapchains{.unscaledCreating = true}) == WindowStep::ReadClientArea);
    CHECK(stepForAnswer(GameSwapchains{.unscaled = true, .unscaledCreating = true}) ==
          WindowStep::ReadClientArea);
    CHECK(stepForAnswer(GameSwapchains{.unscaledCreating = true, .scaled = true}) == WindowStep::Move);
    WindowSteps steps;
    CHECK(steps.onAnswer(GameSwapchains{.unscaledCreating = true}, 14.0) == WindowStep::ReadClientArea);
    CHECK(steps.waiting());
    CHECK(steps.onScaledSwapchain());
}
