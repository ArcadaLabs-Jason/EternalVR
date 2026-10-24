#include "features/foveation/foveation_preset.hpp"
#include "features/foveation/foveation_region.hpp"
#include "features/foveation/rate_pattern.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <ostream>
#include <tuple>
#include <vector>

// What the presets leave at quarter rate on a Quest 3 eye image: a player's headset captures showed 4x4
// blocks on shiny floors at the bottom and the outer edge with fixed foveation and DLSS, which were all
// quarter rate. The presets now shade the bottom and the temporal edge at half rate at the very least.

using evr::Quat;
using evr::foveation::foveatedPattern;
using evr::foveation::FoveationPreset;
using evr::foveation::foveationRegion;
using evr::foveation::FoveationRegion;
using evr::foveation::kRateFull;
using evr::foveation::kRateQuarter;
using evr::foveation::RatePatternSize;
using evr::foveation::rateShares;
using evr::foveation::touches;
using evr::xr_math::Fov;

namespace {

float radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

// Quest 3 through the OpenXR simulator: left -54, right 40, up 44, down -54.3 degrees for eye L, mirrored for
// eye R.
const Fov kQuest3Left{radians(-54.0f), radians(40.0f), radians(44.0f), radians(-54.3f)};
const Fov kQuest3Right{radians(-40.0f), radians(54.0f), radians(44.0f), radians(-54.3f)};

const std::vector<FoveationPreset>& presets() {
    static const std::vector<FoveationPreset> all{FoveationPreset::Subtle, FoveationPreset::Balanced,
                                                  FoveationPreset::Aggressive, FoveationPreset::Maximum};
    return all;
}

struct PresetRegions {
    FoveationRegion full;
    FoveationRegion half;
};

PresetRegions regionsOf(const Fov& fov, FoveationPreset preset) {
    const float full = *evr::foveation::fullRateHalfAngleDegrees(preset);
    const float half = full + *evr::foveation::halfRateBandDegrees(preset);
    const auto f = foveationRegion(fov, Quat::identity(), full);
    const auto h = foveationRegion(fov, Quat::identity(), half);
    REQUIRE(f.has_value());
    REQUIRE(h.has_value());
    return {*f, *h};
}

// The rate of the point of the eye's image that a head-space direction (`right` degrees to the right of
// head-forward, `up` degrees up) lands on, or -1 off the image or more than `maxAngle` degrees from
// head-forward.
int rateAt(const Fov& fov, const PresetRegions& r, float right, float up, float maxAngle = 90.0f) {
    const float a = radians(right);
    const float b = radians(up);
    if (std::acos(std::cos(a) * std::cos(b)) > radians(maxAngle)) {
        return -1;
    }
    // Head-forward is the eye's -z: the eyes look straight ahead here.
    const float x = std::sin(a) * std::cos(b);
    const float y = std::sin(b);
    const float z = -std::cos(a) * std::cos(b);
    const float tl = std::tan(fov.angleLeft);
    const float tr = std::tan(fov.angleRight);
    const float tu = std::tan(fov.angleUp);
    const float td = std::tan(fov.angleDown);
    const float tx = x / -z;
    const float ty = y / -z;
    if (!(tx >= tl && tx <= tr && ty >= td && ty <= tu)) {
        return -1;
    }
    const float ndcX = -1.0f + 2.0f * (tx - tl) / (tr - tl);
    const float ndcY = 1.0f - 2.0f * (ty - td) / (tu - td);
    if (touches(r.full, ndcX, ndcX, ndcY, ndcY)) {
        return kRateFull;
    }
    return touches(r.half, ndcX, ndcX, ndcY, ndcY) ? 1 : kRateQuarter;
}

} // namespace

TEST_CASE("Quest 3: no preset shades the bottom or the temporal edge at quarter rate") {
    for (const auto preset : presets()) {
        CAPTURE(static_cast<int>(preset));
        for (const auto& [name, fov, sign] :
             {std::tuple{"eye L", kQuest3Left, -1.0f}, std::tuple{"eye R", kQuest3Right, 1.0f}}) {
            CAPTURE(name);
            const PresetRegions r = regionsOf(fov, preset);
            int bottomSamples = 0;
            int edgeSamples = 0;
            // The bottom: 30 to 54 degrees down, within 25 degrees of straight ahead sideways.
            for (float up = -54.0f; up <= -30.0f; up += 1.0f) {
                for (float right = -25.0f; right <= 25.0f; right += 2.5f) {
                    const int rate = rateAt(fov, r, right, up, 55.0f);
                    if (rate >= 0) {
                        ++bottomSamples;
                        CHECK_MESSAGE(rate != kRateQuarter, "right " << right << " up " << up);
                    }
                }
            }
            // The temporal edge: 40 to 53 degrees to the outer side, within 30 degrees of the horizon.
            for (float out = 40.0f; out <= 53.0f; out += 1.0f) {
                for (float up = -30.0f; up <= 30.0f; up += 3.0f) {
                    const int rate = rateAt(fov, r, sign * out, up, 55.0f);
                    if (rate >= 0) {
                        ++edgeSamples;
                        CHECK_MESSAGE(rate != kRateQuarter, "out " << out << " up " << up);
                    }
                }
            }
            CHECK(bottomSamples > 100);
            CHECK(edgeSamples > 100);
        }
    }
}

TEST_CASE("Quest 3: quarter rate stays in the far corners, so a wide headset still saves there") {
    // The lower temporal corner of eye L's image is about 63 degrees from head-forward, past the 56 degrees
    // where half rate ends.
    const PresetRegions r = regionsOf(kQuest3Left, FoveationPreset::Balanced);
    CHECK_FALSE(touches(r.half, -1.0f, -0.98f, 0.98f, 1.0f));
}

TEST_CASE("Quest 3: the stronger presets still shade less of the image at full cost") {
    // Shading work as a share of full rate: full 1, half 1/2, quarter 1/4, over the eye image at the render
    // size the rig used with DLSS Balanced.
    const RatePatternSize size{1192, 1288, 16, 16};
    double previous = 2.0;
    for (const auto preset : presets()) {
        CAPTURE(static_cast<int>(preset));
        const PresetRegions r = regionsOf(kQuest3Left, preset);
        const auto shares = rateShares(size, foveatedPattern(size, r.full, r.half));
        const double cost = shares.full + 0.5 * shares.half + 0.25 * shares.quarter;
        CHECK(cost < previous);
        CHECK(cost < 1.0);
        previous = cost;
    }
}
