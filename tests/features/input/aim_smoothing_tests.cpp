#include "features/input/aim_smoothing.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

using evr::Quat;
using evr::Vec3;
using evr::input::aimSmoothingParams;
using evr::input::kDefaultAimSmoothing;
using evr::input::OneEuroParams;
using evr::input::OneEuroRotation;

namespace {

constexpr double kDt = 1.0 / 90.0;
constexpr float kDegrees = std::numbers::pi_v<float> / 180.0f;

// Yaw about +Y, then pitch about +X (radians): the aim direction a small angle away from -Z.
Quat yawPitch(float yaw, float pitch) {
    return Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw) * Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, pitch);
}

// The angle between two orientations' aim rays (-Z), degrees.
float rayDegrees(Quat a, Quat b) {
    const Vec3 u = rotate(a, {0.0f, 0.0f, -1.0f});
    const Vec3 v = rotate(b, {0.0f, 0.0f, -1.0f});
    return std::atan2(length(cross(u, v)), dot(u, v)) / kDegrees;
}

OneEuroRotation defaultFilter() {
    return OneEuroRotation(*aimSmoothingParams(kDefaultAimSmoothing));
}

double rms(const std::vector<float>& values) {
    double sum = 0.0;
    for (const float v : values) {
        sum += static_cast<double>(v) * v;
    }
    return std::sqrt(sum / static_cast<double>(values.size()));
}

} // namespace

TEST_CASE("smoothing 0 is off, and the strength maps to lighter or heavier filters") {
    CHECK_FALSE(aimSmoothingParams(0.0f));
    CHECK_FALSE(aimSmoothingParams(-0.5f));
    CHECK_FALSE(aimSmoothingParams(std::numeric_limits<float>::quiet_NaN()));
    const OneEuroParams light = *aimSmoothingParams(0.1f);
    const OneEuroParams strong = *aimSmoothingParams(1.0f);
    CHECK(light.minCutoffHz > strong.minCutoffHz);
    CHECK(light.beta > strong.beta);
    CHECK(strong.minCutoffHz == doctest::Approx(0.5f));
    CHECK(aimSmoothingParams(3.0f)->minCutoffHz == doctest::Approx(strong.minCutoffHz));
}

TEST_CASE("the first sample and samples after a gap pass through unfiltered") {
    OneEuroRotation filter = defaultFilter();
    const Quat a = yawPitch(0.3f, 0.1f);
    CHECK(rayDegrees(filter.update(a, 1.0), a) < 1e-3f);
    const Quat b = yawPitch(-0.5f, 0.2f);
    CHECK(rayDegrees(filter.update(b, 1.0 + 2.0 * OneEuroRotation::kResetSeconds), b) < 1e-3f);
    filter.reset();
    CHECK(rayDegrees(filter.update(a, 5.0), a) < 1e-3f);
}

TEST_CASE("a sample at the same time as the last returns the last output") {
    OneEuroRotation filter = defaultFilter();
    filter.update(yawPitch(0.0f, 0.0f), 1.0);
    const Quat first = filter.update(yawPitch(5.0f * kDegrees, 0.0f), 1.0 + kDt);
    const Quat again = filter.update(yawPitch(9.0f * kDegrees, 0.0f), 1.0 + kDt);
    CHECK(rayDegrees(first, again) < 1e-4f);
}

TEST_CASE("a sudden 20 degree turn is followed almost at once") {
    OneEuroRotation filter = defaultFilter();
    double t = 0.0;
    for (int i = 0; i < 10; ++i, t += kDt) {
        filter.update(yawPitch(0.0f, 0.0f), t);
    }
    const Quat target = yawPitch(20.0f * kDegrees, 0.0f);
    const Quat one = filter.update(target, t);
    CHECK(rayDegrees(one, target) < 7.0f); // most of the way on the first frame
    Quat out = one;
    for (int i = 0; i < 2; ++i) {
        t += kDt;
        out = filter.update(target, t);
    }
    CHECK(rayDegrees(out, target) < 1.0f); // within a degree after three frames (33 ms)
}

TEST_CASE("a still hand's tremor is cut to under half") {
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.15f * kDegrees);
    OneEuroRotation filter = defaultFilter();
    const Quat rest = yawPitch(0.2f, -0.1f);
    std::vector<float> raw;
    std::vector<float> filtered;
    double t = 0.0;
    for (int i = 0; i < 2000; ++i, t += kDt) {
        // White tracking noise plus a 9 Hz physiological tremor of 0.2 degrees.
        const float tremor =
            0.2f * kDegrees * std::sin(2.0f * std::numbers::pi_v<float> * 9.0f * static_cast<float>(t));
        const Quat sample = yawPitch(0.2f + noise(rng) + tremor, -0.1f + noise(rng));
        const Quat out = filter.update(sample, t);
        if (i >= 200) {
            raw.push_back(rayDegrees(sample, rest));
            filtered.push_back(rayDegrees(out, rest));
        }
    }
    CHECK(rms(filtered) < 0.5 * rms(raw));
}

TEST_CASE("stronger smoothing cuts more tremor") {
    std::mt19937 rng(3);
    std::normal_distribution<float> noise(0.0f, 0.15f * kDegrees);
    OneEuroRotation light(*aimSmoothingParams(0.1f));
    OneEuroRotation strong(*aimSmoothingParams(1.0f));
    std::vector<float> lightError;
    std::vector<float> strongError;
    double t = 0.0;
    for (int i = 0; i < 2000; ++i, t += kDt) {
        const Quat sample = yawPitch(noise(rng), noise(rng));
        const Quat a = light.update(sample, t);
        const Quat b = strong.update(sample, t);
        if (i >= 200) {
            lightError.push_back(rayDegrees(a, Quat{}));
            strongError.push_back(rayDegrees(b, Quat{}));
        }
    }
    CHECK(rms(strongError) < 0.6 * rms(lightError));
}

TEST_CASE("the lag behind a steadily turning hand is bounded") {
    // Angular speed (rad/s) and the most lag allowed at the default strength (ms).
    struct Case {
        float speed;
        double maxLagMs;
    };
    for (const Case c : {Case{0.2f, 30.0}, Case{1.0f, 10.0}, Case{3.0f, 5.0}}) {
        CAPTURE(c.speed);
        OneEuroRotation filter = defaultFilter();
        Quat out;
        Quat raw;
        double t = 0.0;
        for (int i = 0; i < 180; ++i, t += kDt) {
            raw = yawPitch(c.speed * static_cast<float>(t), 0.0f);
            out = filter.update(raw, t);
        }
        const double lagMs = rayDegrees(out, raw) * kDegrees / c.speed * 1000.0;
        CHECK(lagMs < c.maxLagMs);
        CHECK(lagMs > 0.0);
    }
}

TEST_CASE("game frames faster or slower than the display are filtered alike") {
    for (const double dt : {1.0 / 50.0, 1.0 / 144.0}) {
        CAPTURE(dt);
        OneEuroRotation filter = defaultFilter();
        const Quat target = yawPitch(20.0f * kDegrees, 0.0f);
        double t = 0.0;
        filter.update(Quat{}, t);
        Quat out;
        for (std::size_t i = 0; i < 6; ++i) {
            t += dt;
            out = filter.update(target, t);
        }
        CHECK(rayDegrees(out, target) < 1.0f);
    }
}
