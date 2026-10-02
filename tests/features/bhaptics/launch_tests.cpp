#include "features/bhaptics/body_haptics.hpp"
#include "features/bhaptics/launch.hpp"
#include "features/bhaptics/pickups.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

using evr::bhaptics::BodyHaptics;
using evr::bhaptics::BodySignals;
using evr::bhaptics::Device;
using evr::bhaptics::Effect;
using evr::bhaptics::Frame;
using evr::bhaptics::kLaunchMillis;
using evr::bhaptics::kLaunchRepeatSeconds;
using evr::bhaptics::kLaunchSeconds;
using evr::bhaptics::kLaunchSteps;
using evr::bhaptics::kLaunchStepSeconds;
using evr::bhaptics::kVestColumns;
using evr::bhaptics::kVestRows;
using evr::bhaptics::Landing;
using evr::bhaptics::LaunchFilter;
using evr::bhaptics::launchStepAt;
using evr::bhaptics::Pickup;
using evr::bhaptics::PickupKind;

namespace {

constexpr double kFrame = 1.0 / 60.0; // a game frame
constexpr double kTick = 0.02;        // the layer's link thread

BodySignals playing(double seconds) {
    BodySignals s;
    s.seconds = seconds;
    s.gameplay = true;
    s.health = 100.0f;
    s.armor = 50.0f;
    return s;
}

std::vector<const Frame*> launches(const std::vector<Frame>& frames) {
    std::vector<const Frame*> out;
    for (const Frame& f : frames) {
        if (f.effect == Effect::Launch) {
            out.push_back(&f);
        }
    }
    return out;
}

int row(int index) {
    return index / kVestColumns;
}

// The strongest dot of a frame in `r`, 0 when the row is not in it.
int rowLevel(const Frame& frame, int r) {
    int best = 0;
    for (const auto& dot : frame.dots) {
        if (row(dot.index) == r) {
            best = std::max<int>(best, dot.intensity);
        }
    }
    return best;
}

// A launch handed over at `start` (and `extra` more on the updates in `again`), then link-thread updates for
// `forSeconds`: the launch frames by update.
std::vector<std::vector<Frame>>
run(BodyHaptics& body, double start, double forSeconds, double againUntil = 0.0) {
    std::vector<std::vector<Frame>> out;
    for (double t = 0.0; t < forSeconds; t += kTick) {
        BodySignals s = playing(start + t);
        s.launches = (t == 0.0 || t < againUntil) ? 1u : 0u;
        const std::vector<Frame> frames = body.update(s);
        std::vector<Frame> step;
        for (const Frame* f : launches(frames)) {
            step.push_back(*f);
        }
        if (!step.empty()) {
            out.push_back(step);
        }
    }
    return out;
}

} // namespace

TEST_CASE("the first touch of a pad is a launch; its next frames in the pad's volume are the same launch") {
    LaunchFilter filter;
    CHECK(filter.note(10.0));
    int fresh = 0;
    for (double t = 10.0 + kFrame; t < 10.3; t += kFrame) {
        fresh += filter.note(t) ? 1 : 0;
    }
    CHECK(fresh == 0);
}

TEST_CASE("touches keep the window moving: one launch however long they go on") {
    LaunchFilter filter;
    int fresh = 0;
    for (double t = 1.0; t < 4.0; t += kLaunchRepeatSeconds * 0.6) {
        fresh += filter.note(t) ? 1 : 0;
    }
    CHECK(fresh == 1);
}

TEST_CASE("a chain of pads a flight apart, or the same pad again later, is a new launch each time") {
    LaunchFilter filter;
    CHECK(filter.note(1.0));
    CHECK_FALSE(filter.note(1.0 + kFrame));
    CHECK(filter.note(2.25)); // the next pad, about a flight later
    CHECK(filter.note(3.5));
    CHECK_FALSE(filter.note(3.5 + kLaunchRepeatSeconds * 0.9));
    CHECK(filter.note(3.5 + kLaunchRepeatSeconds * 0.9 + kLaunchRepeatSeconds * 1.2));
}

TEST_CASE("a time that is not finite is ignored; reset forgets the last touch") {
    LaunchFilter filter;
    CHECK_FALSE(filter.note(std::numeric_limits<double>::quiet_NaN()));
    CHECK(filter.note(1.0));
    CHECK_FALSE(filter.note(std::numeric_limits<double>::infinity()));
    CHECK_FALSE(filter.note(1.1));
    filter.reset();
    CHECK(filter.note(1.2));
}

TEST_CASE("the launch's curve: the bottom row strongest first and fading, the row above joining in") {
    CHECK(launchStepAt(-0.01) == nullptr);
    CHECK(launchStepAt(std::numeric_limits<double>::quiet_NaN()) == nullptr);
    CHECK(launchStepAt(kLaunchSeconds) == nullptr);
    REQUIRE(launchStepAt(0.0) == &kLaunchSteps[0]);
    CHECK(launchStepAt(kLaunchStepSeconds * 0.99) == &kLaunchSteps[0]);
    CHECK(launchStepAt(kLaunchStepSeconds * 1.01) == &kLaunchSteps[1]);
    CHECK(kLaunchSteps[0].above == 0.0f);
    for (std::size_t i = 1; i < kLaunchSteps.size(); ++i) {
        CHECK(kLaunchSteps[i].bottom < kLaunchSteps[i - 1].bottom);
        CHECK(kLaunchSteps[i].above > 0.0f);
        CHECK(kLaunchSteps[i].above < kLaunchSteps[i].bottom);
    }
    CHECK(kLaunchMillis > kLaunchStepSeconds * 1000.0);
}

TEST_CASE("a launch plays its curve once on the bottom of the vest, front and back") {
    BodyHaptics body;
    const auto steps = run(body, 5.0, 1.0);
    REQUIRE(steps.size() == kLaunchSteps.size());
    for (std::size_t i = 0; i < steps.size(); ++i) {
        REQUIRE(steps[i].size() == 2);
        CHECK(steps[i][0].device == Device::VestFront);
        CHECK(steps[i][1].device == Device::VestBack);
        for (const Frame& f : steps[i]) {
            CHECK(f.durationMillis == kLaunchMillis);
            CHECK(rowLevel(f, kVestRows - 1) == static_cast<int>(kLaunchSteps[i].bottom));
            CHECK(rowLevel(f, kVestRows - 2) == static_cast<int>(kLaunchSteps[i].above));
            for (const auto& dot : f.dots) {
                CHECK(row(dot.index) >= kVestRows - 2);
            }
            // Every column of the row.
            CHECK(f.dots.size() == static_cast<std::size_t>(kVestColumns * (i == 0 ? 1 : 2)));
        }
    }
}

TEST_CASE("another launch during the curve does not restart it; one after it plays again") {
    BodyHaptics body;
    CHECK(run(body, 5.0, 1.0, kLaunchSeconds * 0.8).size() == kLaunchSteps.size());
    CHECK(run(body, 7.0, 1.0).size() == kLaunchSteps.size());
}

TEST_CASE("jumps, dashes and landings play no launch; the landing still plays its own") {
    BodyHaptics body;
    BodySignals s = playing(5.0);
    body.update(s);
    s.seconds = 5.02;
    s.landing = Landing{5.0f, 12.0f};
    const auto frames = body.update(s);
    CHECK(launches(frames).empty());
    CHECK(std::any_of(frames.begin(), frames.end(),
                      [](const Frame& f) { return f.effect == Effect::Landing; }));
    for (double t = 5.04; t < 6.0; t += kTick) {
        BodySignals quiet = playing(t);
        quiet.shots = 1;
        quiet.equipment = 1;
        CHECK(launches(body.update(quiet)).empty());
    }
}

TEST_CASE("no launch while dead; leaving play cancels one under way") {
    BodyHaptics dead;
    BodySignals d = playing(5.0);
    d.dead = true;
    d.launches = 1;
    CHECK(launches(dead.update(d)).empty());

    BodyHaptics body;
    BodySignals s = playing(5.0);
    s.launches = 1;
    CHECK(!launches(body.update(s)).empty());
    BodySignals menu = playing(5.02);
    menu.gameplay = false;
    body.update(menu);
    for (double t = 5.06; t < 5.5; t += kTick) {
        CHECK(launches(body.update(playing(t))).empty());
    }
}

TEST_CASE("the strength scales the launch") {
    BodyHaptics body(0.5f);
    BodySignals s = playing(5.0);
    s.launches = 1;
    const std::vector<Frame> all = body.update(s);
    const auto frames = launches(all);
    REQUIRE(frames.size() == 2);
    CHECK(rowLevel(*frames[0], kVestRows - 1) == 43); // 85 * 0.5, rounded
}

TEST_CASE("a pickup's wave gives way to a launch on the vest") {
    BodyHaptics body;
    BodySignals s = playing(5.0);
    body.update(s);
    s.seconds = 5.02;
    s.launches = 1;
    Pickup gain;
    gain.kind = PickupKind::Health;
    gain.amount = 25.0f;
    s.healthGain = gain;
    const auto frames = body.update(s);
    CHECK(!launches(frames).empty());
    CHECK(std::none_of(frames.begin(), frames.end(),
                       [](const Frame& f) { return f.effect == Effect::Health; }));
}
