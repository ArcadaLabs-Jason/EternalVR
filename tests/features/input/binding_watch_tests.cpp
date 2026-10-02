#include "features/input/binding_watch.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

using evr::input::actionNames;
using evr::input::BindingWatch;
using evr::input::BindingWatchActions;
using evr::input::BindingWatchSample;
using evr::input::BoundAction;
using evr::input::Hand;
using evr::input::judgeBoundSources;
using evr::input::kBindingSettleSeconds;
using evr::input::kPosesNeverValidSeconds;
using evr::input::sourceHand;
using evr::input::unboundAdvice;
using evr::input::XrActionId;

namespace {

constexpr double kFrame = 1.0 / 90.0;

// A focused sync with the head tracked, both hands' profiles reported and no pose.
BindingWatchSample worn(double seconds) {
    BindingWatchSample s;
    s.seconds = seconds;
    s.synced = true;
    s.headTracked = true;
    s.profile = {true, true};
    return s;
}

struct Run {
    int listed = 0;
    int warned = 0;
    double firstListed = -1.0;
    double firstWarned = -1.0;
};

// Feeds frames from `from` to `to` (seconds), each one made by `make`.
template <typename Make>
Run feed(BindingWatch& watch, double from, double to, Make make) {
    Run run;
    for (double t = from; t < to; t += kFrame) {
        const BindingWatchActions out = watch.update(make(t));
        if (out.listSources) {
            ++run.listed;
            run.firstListed = run.firstListed < 0.0 ? t : run.firstListed;
        }
        if (out.posesNeverValid) {
            ++run.warned;
            run.firstWarned = run.firstWarned < 0.0 ? t : run.firstWarned;
        }
    }
    return run;
}

} // namespace

TEST_CASE("binding watch: the sources are listed once, after the profile has settled") {
    BindingWatch watch;
    // No profile yet: nothing to list.
    auto run = feed(watch, 0.0, 3.0, [](double t) {
        auto s = worn(t);
        s.profile = {false, false};
        return s;
    });
    CHECK(run.listed == 0);
    run = feed(watch, 3.0, 20.0, [](double t) { return worn(t); });
    CHECK(run.listed == 1);
    CHECK(run.firstListed >= 3.0 + kBindingSettleSeconds - kFrame);
    CHECK(run.firstListed < 3.0 + kBindingSettleSeconds + 2 * kFrame);
}

TEST_CASE("binding watch: a profile that comes and goes restarts the settle time") {
    BindingWatch watch;
    auto run = feed(watch, 0.0, 1.5, [](double t) { return worn(t); });
    run = feed(watch, 1.5, 2.0, [](double t) {
        auto s = worn(t);
        s.profile = {false, false};
        return s;
    });
    CHECK(run.listed == 0);
    run = feed(watch, 2.0, 5.0, [](double t) { return worn(t); });
    CHECK(run.listed == 1);
    CHECK(run.firstListed >= 4.0 - kFrame);
}

TEST_CASE("binding watch: no pose for ten seconds while worn warns once") {
    BindingWatch watch;
    const auto run = feed(watch, 0.0, 60.0, [](double t) { return worn(t); });
    CHECK(run.warned == 1);
    CHECK(run.firstWarned >= kPosesNeverValidSeconds - kFrame);
    CHECK(run.firstWarned < kPosesNeverValidSeconds + 2 * kFrame);
    CHECK_FALSE(watch.anyPoseSeen());
}

TEST_CASE("binding watch: one valid pose and it never warns") {
    BindingWatch watch;
    feed(watch, 0.0, 5.0, [](double t) { return worn(t); });
    auto seen = worn(5.0);
    seen.poseValid = {false, true};
    CHECK_FALSE(watch.update(seen).posesNeverValid);
    CHECK(watch.anyPoseSeen());
    // The poses may be lost afterwards (controllers off, out of view); that is not a binding problem.
    const auto run = feed(watch, 5.0 + kFrame, 60.0, [](double t) { return worn(t); });
    CHECK(run.warned == 0);
}

TEST_CASE("binding watch: time unfocused, untracked or without a profile does not count") {
    for (int which = 0; which < 3; ++which) {
        CAPTURE(which);
        BindingWatch watch;
        const auto run = feed(watch, 0.0, 60.0, [which](double t) {
            auto s = worn(t);
            if (which == 0) {
                s.synced = false;
            } else if (which == 1) {
                s.headTracked = false;
            } else {
                s.profile = {false, false};
            }
            return s;
        });
        CHECK(run.warned == 0);
    }
}

TEST_CASE("binding watch: the waiting time adds up across interruptions but not across gaps") {
    BindingWatch watch;
    // 6 s worn, the headset off for 30 s, 6 s worn again: 12 s counted.
    auto run = feed(watch, 0.0, 6.0, [](double t) { return worn(t); });
    CHECK(run.warned == 0);
    run = feed(watch, 6.0, 36.0, [](double t) {
        auto s = worn(t);
        s.headTracked = false;
        return s;
    });
    CHECK(run.warned == 0);
    run = feed(watch, 36.0, 42.0, [](double t) { return worn(t); });
    CHECK(run.warned == 1);

    // A stall between two syncs (no samples for a minute) is not counted.
    BindingWatch stalled;
    run = feed(stalled, 0.0, 5.0, [](double t) { return worn(t); });
    run = feed(stalled, 65.0, 69.0, [](double t) { return worn(t); });
    CHECK(run.warned == 0);
}

TEST_CASE("binding watch: the hand of a source path") {
    CHECK(sourceHand("/user/hand/left/input/trigger/value") == Hand::Left);
    CHECK(sourceHand("/user/hand/right/input/aim/pose") == Hand::Right);
    CHECK_FALSE(sourceHand("/user/hand/leftish/input/x/click").has_value());
    CHECK_FALSE(sourceHand("/user/gamepad/input/a/click").has_value());
    CHECK_FALSE(sourceHand("").has_value());
}

TEST_CASE("binding watch: nothing bound is the verdict only when every action has no source") {
    const std::vector<BoundAction> none{{XrActionId::Trigger, {}}, {XrActionId::AimPose, {}}};
    auto verdict = judgeBoundSources(none);
    CHECK(verdict.noneBound());
    CHECK(verdict.actions == 2);
    CHECK(verdict.boundActions == 0);
    CHECK(actionNames(verdict.unbound[0]) == "trigger, aim_pose");
    CHECK(actionNames(verdict.unbound[1]) == "trigger, aim_pose");

    const std::vector<BoundAction> some{
        {XrActionId::Trigger,
         {"/user/hand/left/input/trigger/value", "/user/hand/right/input/trigger/value"}},
        {XrActionId::AimPose, {"/user/hand/right/input/aim/pose"}},
        {XrActionId::GripPose, {}},
    };
    verdict = judgeBoundSources(some);
    CHECK_FALSE(verdict.noneBound());
    CHECK(verdict.boundActions == 2);
    CHECK(actionNames(verdict.unbound[static_cast<std::size_t>(Hand::Left)]) == "aim_pose, grip_pose");
    CHECK(actionNames(verdict.unbound[static_cast<std::size_t>(Hand::Right)]) == "grip_pose");

    CHECK_FALSE(judgeBoundSources({}).noneBound());
    CHECK(actionNames({}) == "none");
}

TEST_CASE("binding watch: the advice names SteamVR's binding UI for SteamVR only") {
    const std::string steam(unboundAdvice("SteamVR/OpenXR"));
    CHECK(steam.find("SteamVR") != std::string::npos);
    CHECK(steam.find("default binding") != std::string::npos);
    const std::string other(unboundAdvice("Oculus"));
    CHECK(other.find("SteamVR") == std::string::npos);
    CHECK(other.find("default") != std::string::npos);
}
