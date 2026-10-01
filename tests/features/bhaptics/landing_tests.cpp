#include "features/bhaptics/body_haptics.hpp"
#include "features/bhaptics/landing.hpp"

#include <doctest/doctest.h>

#include <optional>
#include <vector>

using evr::bhaptics::BodyHaptics;
using evr::bhaptics::BodySignals;
using evr::bhaptics::Device;
using evr::bhaptics::Effect;
using evr::bhaptics::Frame;
using evr::bhaptics::kLandingDrop;
using evr::bhaptics::kVestColumns;
using evr::bhaptics::kVestRows;
using evr::bhaptics::Landing;
using evr::bhaptics::LandingDetector;

namespace {

constexpr double kFrame = 1.0 / 120.0;
constexpr float kGravity = 20.0f;

// A body under gravity, fed to the detector once a frame; the landings it reported.
struct Fall {
    LandingDetector detector;
    double seconds = 1.0;
    float height = 0.0f;
    float speed = 0.0f;
    std::vector<Landing> landings;

    void frame(float floor) {
        seconds += kFrame;
        speed -= kGravity * static_cast<float>(kFrame);
        height += speed * static_cast<float>(kFrame);
        if (height <= floor) {
            height = floor;
            speed = 0.0f;
        }
        if (const auto l = detector.update(height, seconds)) {
            landings.push_back(*l);
        }
    }

    void run(double forSeconds, float floor) {
        for (double t = 0.0; t < forSeconds; t += kFrame) {
            frame(floor);
        }
    }
};

} // namespace

TEST_CASE("standing still is no landing") {
    Fall f;
    f.run(2.0, 0.0f);
    CHECK(f.landings.empty());
    CHECK_FALSE(f.detector.airborne());
}

TEST_CASE("a jump lands its own height below its peak") {
    Fall f;
    f.run(0.5, 0.0f);
    f.speed = 7.0f; // peak 7^2 / (2 * 20) = 1.225
    f.run(2.0, 0.0f);
    REQUIRE(f.landings.size() == 1);
    CHECK(f.landings[0].drop == doctest::Approx(1.225f).epsilon(0.05));
    CHECK(f.landings[0].fallSpeed == doctest::Approx(7.0f).epsilon(0.05));
    CHECK(f.landings[0].drop < kLandingDrop);
}

TEST_CASE("walking off a ledge is a fall from the ledge") {
    Fall f;
    f.height = 5.0f;
    f.run(0.5, 5.0f);
    f.run(2.0, 0.0f);
    REQUIRE(f.landings.size() == 1);
    CHECK(f.landings[0].drop == doctest::Approx(5.0f).epsilon(0.02));
}

TEST_CASE("a double jump is one landing, from the second peak") {
    Fall f;
    f.run(0.5, 0.0f);
    f.speed = 7.0f;
    // Up, over the top and falling again, then the second jump mid-air.
    while (!(f.speed < -3.0f)) {
        f.frame(0.0f);
    }
    const float turn = f.height;
    f.speed = 7.0f;
    f.run(3.0, 0.0f);
    REQUIRE(f.landings.size() == 1);
    CHECK(f.landings[0].drop == doctest::Approx(turn + 1.225f).epsilon(0.05));
}

TEST_CASE("a teleport, a lift and repeats are not landings") {
    LandingDetector d;
    double t = 1.0;
    CHECK_FALSE(d.update(0.0f, t));
    // Moved 40 units down in one frame, then still: a teleport.
    CHECK_FALSE(d.update(-40.0f, t += kFrame));
    for (int i = 0; i < 60; ++i) {
        CHECK_FALSE(d.update(-40.0f, t += kFrame));
    }
    // A lift going up 3 units a second, then stopping: never fell.
    float h = -40.0f;
    for (int i = 0; i < 120; ++i) {
        CHECK_FALSE(d.update(h += 3.0f * static_cast<float>(kFrame), t += kFrame));
    }
    for (int i = 0; i < 60; ++i) {
        CHECK_FALSE(d.update(h, t += kFrame));
    }
    CHECK_FALSE(d.airborne());
    // The same reading again changes nothing.
    CHECK_FALSE(d.update(h - 1.0f, t));
    CHECK_FALSE(d.airborne());
}

TEST_CASE("a gap in the readings starts over") {
    LandingDetector d;
    double t = 1.0;
    float h = 10.0f;
    CHECK_FALSE(d.update(h, t));
    for (int i = 0; i < 30; ++i) {
        CHECK_FALSE(d.update(h -= 0.1f, t += kFrame));
    }
    CHECK(d.airborne());
    // A load: the next reading is a second later, at rest.
    CHECK_FALSE(d.update(0.0f, t += 1.0));
    CHECK_FALSE(d.update(0.0f, t += kFrame));
    CHECK_FALSE(d.airborne());
}

TEST_CASE("the vest feels a fall but not a jump") {
    BodyHaptics body;
    BodySignals s;
    s.gameplay = true;
    s.health = 100.0f;
    s.armor = 50.0f;
    // As in the layer: the detector on every game frame, the vest updated with what it found.
    Fall f;
    std::size_t taken = 0;
    const auto feed = [&](double forSeconds, float floor) {
        std::vector<Frame> frames;
        for (double t = 0.0; t < forSeconds; t += kFrame) {
            f.frame(floor);
            s.seconds = f.seconds;
            s.landing.reset();
            if (f.landings.size() > taken) {
                s.landing = f.landings.back();
                taken = f.landings.size();
            }
            for (Frame& frame : body.update(s)) {
                frames.push_back(frame);
            }
        }
        return frames;
    };
    f.run(0.5, 0.0f);
    f.speed = 7.0f;
    std::vector<Frame> jump = feed(2.0, 0.0f);
    CHECK(body.landings() == 1);
    for (const Frame& frame : jump) {
        CHECK(frame.effect != Effect::Landing);
    }
    f.height = 6.0f;
    feed(0.5, 6.0f);
    const std::vector<Frame> fall = feed(2.0, 0.0f);
    CHECK(body.landings() == 2);
    REQUIRE(body.lastLanding());
    CHECK(body.lastLanding()->drop == doctest::Approx(6.0f).epsilon(0.02));
    int landingFrames = 0;
    for (const Frame& frame : fall) {
        if (frame.effect != Effect::Landing) {
            continue;
        }
        ++landingFrames;
        CHECK((frame.device == Device::VestFront || frame.device == Device::VestBack));
        REQUIRE(frame.dots.size() == static_cast<std::size_t>(kVestColumns));
        for (const auto& dot : frame.dots) {
            CHECK(dot.index / kVestColumns == kVestRows - 1); // the bottom row
            CHECK(dot.intensity > 0);
        }
    }
    CHECK(landingFrames == 2);
    // A landing while dead is counted but not felt.
    s.dead = true;
    s.landing = Landing{8.0f, 12.0f};
    for (const Frame& frame : body.update(s)) {
        CHECK(frame.effect != Effect::Landing);
    }
    CHECK(body.landings() == 3);
}
