#include "features/posture/anchor_detector.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>

using evr::Pose;
using evr::Quat;
using evr::Vec3;
using evr::posture::AnchorDetector;
using evr::posture::AnchorThresholds;
using evr::posture::HeadSample;

namespace {

constexpr double kRate = 90.0; // samples per second, a typical display rate

// A small deterministic generator (the standard distributions differ between libraries).
class Noise {
public:
    explicit Noise(std::uint32_t seed) : state_(seed) {}
    // Uniform in [-1, 1].
    float next() {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / static_cast<float>(1u << 23) - 1.0f;
    }

private:
    std::uint32_t state_;
};

Pose headAt(Vec3 position, float yawRadians, float pitchRadians) {
    const Quat yaw = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yawRadians);
    const Quat pitch = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitchRadians);
    return {yaw * pitch, position};
}

using PoseAt = std::function<Pose(double seconds)>;

// Feeds `seconds` of samples from `start`; returns the time of the anchor, if one was taken.
std::optional<double> run(AnchorDetector& detector,
                          const PoseAt& poseAt,
                          double seconds,
                          double start = 0.0,
                          bool focused = true,
                          std::optional<bool> present = std::nullopt) {
    std::optional<double> anchoredAt;
    const int count = static_cast<int>(seconds * kRate);
    for (int i = 0; i < count; ++i) {
        const double t = start + i / kRate;
        HeadSample s;
        s.seconds = t;
        s.pose = poseAt(t);
        s.tracked = true;
        s.focused = focused;
        s.userPresent = present;
        if (detector.update(s) && !anchoredAt) {
            anchoredAt = t;
        }
    }
    return anchoredAt;
}

// A worn, seated head: slow sway of a few millimetres and a few tenths of a degree, plus jitter.
PoseAt wornHead(std::uint32_t seed) {
    auto noise = std::make_shared<Noise>(seed);
    return [noise](double t) {
        const float sway = static_cast<float>(std::sin(t * 1.7));
        const Vec3 p{0.003f * sway + 0.0008f * noise->next(), 1.20f + 0.002f * noise->next(),
                     0.002f * static_cast<float>(std::cos(t * 1.1)) + 0.0008f * noise->next()};
        return headAt(p, 0.006f * sway + 0.001f * noise->next(), 0.004f * noise->next());
    };
}

} // namespace

TEST_CASE("a headset lying still on a desk never anchors") {
    AnchorDetector detector;
    const Pose desk = headAt({0.1f, 0.75f, -0.3f}, 0.4f, -0.2f);
    CHECK_FALSE(run(detector, [&](double) { return desk; }, 60.0));
    CHECK_FALSE(detector.anchored());
}

TEST_CASE("a desk headset with tracking noise never anchors") {
    AnchorDetector detector;
    Noise noise(7);
    const PoseAt desk = [&](double) {
        // 0.1 mm and 0.01 degree of noise: far above what a still headset reports, below a worn head.
        return headAt({0.1f + 0.0001f * noise.next(), 0.75f + 0.0001f * noise.next(), -0.3f},
                      0.4f + 0.00017f * noise.next(), -0.2f);
    };
    CHECK_FALSE(run(detector, desk, 60.0));
}

TEST_CASE("worn-head jitter anchors within 2 seconds") {
    for (std::uint32_t seed : {1u, 2u, 3u, 42u, 1234u}) {
        AnchorDetector detector;
        const auto at = run(detector, wornHead(seed), 5.0);
        REQUIRE(at);
        CHECK(*at <= 2.0);
        CHECK(*at >= 1.0); // a full window first
        CHECK(detector.anchored());
    }
}

TEST_CASE("the anchor is the newest sample's pose, given once") {
    AnchorDetector detector;
    const PoseAt worn = wornHead(5);
    std::optional<Pose> anchor;
    int anchors = 0;
    double anchorTime = 0.0;
    for (int i = 0; i < 400; ++i) {
        HeadSample s{i / kRate, worn(i / kRate), true, true, std::nullopt};
        if (const auto a = detector.update(s)) {
            anchor = a;
            anchorTime = s.seconds;
            ++anchors;
        }
    }
    CHECK(anchors == 1);
    REQUIRE(anchor);
    CHECK(anchor->position.y == doctest::Approx(worn(anchorTime).position.y).epsilon(0.01));
}

TEST_CASE("a head still moving into place does not anchor until it settles") {
    AnchorDetector detector;
    const PoseAt worn = wornHead(9);
    // Putting the headset on: 3 s of 10 cm/s motion, then a worn, settled head.
    const PoseAt settling = [&](double t) {
        Pose p = worn(t);
        if (t < 3.0) {
            p.position.y += static_cast<float>(0.1 * (3.0 - t));
        }
        return p;
    };
    const auto at = run(detector, settling, 6.0);
    REQUIRE(at);
    CHECK(*at >= 3.5);
    CHECK(*at <= 5.0);
}

TEST_CASE("an unfocused session or an absent user never anchors") {
    AnchorDetector unfocused;
    CHECK_FALSE(run(unfocused, wornHead(3), 10.0, 0.0, false));
    AnchorDetector absent;
    CHECK_FALSE(run(absent, wornHead(3), 10.0, 0.0, true, false));
    AnchorDetector present;
    CHECK(run(present, wornHead(3), 10.0, 0.0, true, true));
}

TEST_CASE("an interruption restarts the window") {
    AnchorDetector detector;
    const PoseAt worn = wornHead(11);
    // 0.9 s worn, one untracked sample, then worn again: the anchor needs a full window after the gap.
    CHECK_FALSE(run(detector, worn, 0.9));
    HeadSample lost{0.9, worn(0.9), false, true, std::nullopt};
    CHECK_FALSE(detector.update(lost));
    const auto at = run(detector, worn, 3.0, 0.91);
    REQUIRE(at);
    CHECK(*at >= 1.9);
}

TEST_CASE("time going backwards restarts the window") {
    AnchorDetector detector;
    const PoseAt worn = wornHead(13);
    CHECK_FALSE(run(detector, worn, 0.8, 10.0));
    const auto at = run(detector, worn, 3.0, 5.0);
    REQUIRE(at);
    CHECK(*at >= 6.0);
}

TEST_CASE("reset lets the detector anchor again") {
    AnchorDetector detector;
    CHECK(run(detector, wornHead(17), 3.0));
    CHECK_FALSE(run(detector, wornHead(17), 3.0, 3.0));
    detector.reset();
    CHECK_FALSE(detector.anchored());
    CHECK(run(detector, wornHead(17), 3.0, 6.0));
}

TEST_CASE("unusable thresholds fall back to the defaults") {
    AnchorThresholds bad;
    bad.stableMetres = std::nanf("");
    CHECK(AnchorDetector(bad).thresholds().stableMetres == doctest::Approx(0.02f));
    AnchorThresholds inverted;
    inverted.stableMetres = 0.0005f; // not above the worn span
    CHECK(AnchorDetector(inverted).thresholds().stableMetres == doctest::Approx(0.02f));
}
