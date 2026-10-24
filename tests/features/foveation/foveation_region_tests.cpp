#include "features/foveation/foveation_region.hpp"

#include "features/foveation/foveation_preset.hpp"
#include "features/foveation/rate_pattern.hpp"
#include "xr_math/projection.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

using evr::Quat;
using evr::Vec3;
using evr::foveation::angleFromForward;
using evr::foveation::FoveationPreset;
using evr::foveation::foveationRegion;
using evr::foveation::FoveationRegion;
using evr::foveation::kRateFull;
using evr::foveation::kRateHalf;
using evr::foveation::kRateQuarter;
using evr::foveation::RatePatternSize;
using evr::foveation::touches;
using evr::xr_math::Fov;

namespace {

constexpr float kQuarterPi = std::numbers::pi_v<float> / 4.0f;
constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};

float radians(float degrees) {
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

double degrees(double radians) {
    return radians * 180.0 / std::numbers::pi;
}

// Quest 3 through the OpenXR simulator, as the Earth A/B rig runs logged it (the owner's headset gives the
// same angles): left -54, right 40, up 44, down -54.3 degrees for eye L, mirrored for eye R. Head-forward
// sits near the nasal edge and the top.
const Fov kQuest3Left{radians(-54.0f), radians(40.0f), radians(44.0f), radians(-54.3f)};
const Fov kQuest3Right{radians(-40.0f), radians(54.0f), radians(44.0f), radians(-54.3f)};
const Fov kSquare{-kQuarterPi, kQuarterPi, kQuarterPi, -kQuarterPi};

// A head-space direction `right` degrees to the right of head-forward and `up` degrees up.
Vec3 direction(float right, float up) {
    const float a = radians(right);
    const float b = radians(up);
    return {std::sin(a) * std::cos(b), std::sin(b), -std::cos(a) * std::cos(b)};
}

struct Ndc {
    float x;
    float y;
};

// Where a head-space direction (a point at infinity) lands in an eye's image, through the eye's own
// projection; nullopt off the image.
std::optional<Ndc> ndcOf(const Fov& fov, const Quat& eyeInHead, Vec3 headDirection) {
    const Vec3 eye = evr::rotate(evr::conjugate(eyeInHead), headDirection);
    const auto projection = evr::xr_math::makeProjectionStandard(fov, 0.1f, 100.0f);
    if (!projection || !(eye.z < 0.0f)) {
        return std::nullopt;
    }
    const Vec3 p = evr::perspectiveDivide(*projection * evr::toPoint(eye));
    if (!(p.x >= -1.0f && p.x <= 1.0f && p.y >= -1.0f && p.y <= 1.0f)) {
        return std::nullopt;
    }
    return Ndc{p.x, p.y};
}

struct Regions {
    FoveationRegion full;
    FoveationRegion half;
};

Regions regions(const Fov& fov, const Quat& eyeInHead, float fullDegrees, float halfDegrees) {
    const auto full = foveationRegion(fov, eyeInHead, fullDegrees);
    const auto half = foveationRegion(fov, eyeInHead, halfDegrees);
    REQUIRE(full.has_value());
    REQUIRE(half.has_value());
    return {*full, *half};
}

std::uint8_t pointRate(const Regions& r, Ndc p) {
    if (touches(r.full, p.x, p.x, p.y, p.y)) {
        return kRateFull;
    }
    return touches(r.half, p.x, p.x, p.y, p.y) ? kRateHalf : kRateQuarter;
}

// The rule up to 0.1.47, for comparison: four quarter ellipses around head-forward's projection, each side's
// radius the same fraction of that side's NDC extent, the fraction giving the cone's tangent-plane area.
struct OldRegion {
    float cx;
    float cy;
    float left;
    float right;
    float top;
    float bottom;
};

OldRegion oldRegion(const Fov& fov, float halfDegrees) {
    const FoveationRegion r = *foveationRegion(fov, Quat::identity(), halfDegrees);
    const double w = static_cast<double>(r.tangents.right) - r.tangents.left;
    const double h = static_cast<double>(r.tangents.up) - r.tangents.down;
    const auto f =
        static_cast<float>(2.0 * std::tan(static_cast<double>(radians(halfDegrees))) / std::sqrt(w * h));
    return {r.centerX,
            r.centerY,
            f * (1.0f + r.centerX),
            f * (1.0f - r.centerX),
            f * (1.0f + r.centerY),
            f * (1.0f - r.centerY)};
}

bool oldTouches(const OldRegion& e, float x0, float x1, float y0, float y1) {
    const float nx = std::clamp(e.cx, x0, x1) - e.cx;
    const float ny = std::clamp(e.cy, y0, y1) - e.cy;
    const float dx = nx / (nx < 0.0f ? e.left : e.right);
    const float dy = ny / (ny < 0.0f ? e.top : e.bottom);
    return dx * dx + dy * dy <= 1.0f;
}

std::uint8_t oldPointRate(const Fov& fov, float fullDegrees, float halfDegrees, Ndc p) {
    if (oldTouches(oldRegion(fov, fullDegrees), p.x, p.x, p.y, p.y)) {
        return kRateFull;
    }
    return oldTouches(oldRegion(fov, halfDegrees), p.x, p.x, p.y, p.y) ? kRateHalf : kRateQuarter;
}

std::vector<std::uint8_t>
oldPattern(const RatePatternSize& size, const Fov& fov, float fullDeg, float halfDeg) {
    const OldRegion full = oldRegion(fov, fullDeg);
    const OldRegion half = oldRegion(fov, halfDeg);
    const std::uint32_t w = (size.targetWidth + size.texelWidth - 1) / size.texelWidth;
    const std::uint32_t h = (size.targetHeight + size.texelHeight - 1) / size.texelHeight;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h, kRateQuarter);
    const float pw = 2.0f / static_cast<float>(size.targetWidth);
    const float ph = 2.0f / static_cast<float>(size.targetHeight);
    for (std::uint32_t ty = 0; ty < h; ++ty) {
        const float y0 = -1.0f + static_cast<float>(ty * size.texelHeight) * ph;
        const float y1 = std::min(1.0f, y0 + static_cast<float>(size.texelHeight) * ph);
        for (std::uint32_t tx = 0; tx < w; ++tx) {
            const float x0 = -1.0f + static_cast<float>(tx * size.texelWidth) * pw;
            const float x1 = std::min(1.0f, x0 + static_cast<float>(size.texelWidth) * pw);
            std::uint8_t& rate = out[static_cast<std::size_t>(ty) * w + tx];
            if (oldTouches(full, x0, x1, y0, y1)) {
                rate = kRateFull;
            } else if (oldTouches(half, x0, x1, y0, y1)) {
                rate = kRateHalf;
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> pattern(const RatePatternSize& size, const Regions& r) {
    return evr::foveation::foveatedPattern(size, r.full, r.half);
}

// The texel of `pattern` (for `size`) that holds NDC point p.
std::uint8_t texelAt(const RatePatternSize& size, const std::vector<std::uint8_t>& pattern, Ndc p) {
    const std::uint32_t w = (size.targetWidth + size.texelWidth - 1) / size.texelWidth;
    const std::uint32_t h = (size.targetHeight + size.texelHeight - 1) / size.texelHeight;
    const auto px = static_cast<std::uint32_t>((p.x + 1.0f) * 0.5f * static_cast<float>(size.targetWidth));
    const auto py = static_cast<std::uint32_t>((p.y + 1.0f) * 0.5f * static_cast<float>(size.targetHeight));
    const std::uint32_t tx = std::min(w - 1, px / size.texelWidth);
    const std::uint32_t ty = std::min(h - 1, py / size.texelHeight);
    return pattern[static_cast<std::size_t>(ty) * w + tx];
}

struct Preset {
    float full;
    float half;
};

std::vector<Preset> presets() {
    std::vector<Preset> out;
    for (const auto p : {FoveationPreset::Subtle, FoveationPreset::Balanced, FoveationPreset::Aggressive,
                         FoveationPreset::Maximum}) {
        const float full = *evr::foveation::fullRateHalfAngleDegrees(p);
        out.push_back({full, full + *evr::foveation::halfRateBandDegrees(p)});
    }
    return out;
}

} // namespace

TEST_CASE("the presets' angles are the ones these tests use") {
    const std::vector<Preset> p = presets();
    REQUIRE(p.size() == 4u);
    CHECK(p[1].full == 24.0f); // Balanced
    CHECK(p[1].half == 56.0f);
    CHECK(p[0].half == 56.0f); // Subtle
    CHECK(p[3].half == 56.0f); // Maximum
}

TEST_CASE("the view ray's angle matches the eye's projection, canted or not") {
    for (const Quat q : {Quat::identity(), Quat::fromAxisAngle(kUp, radians(10.0f))}) {
        const FoveationRegion r = *foveationRegion(kQuest3Left, q, 24.0f);
        for (const float right : {-50.0f, -20.0f, 0.0f, 15.0f, 30.0f}) {
            for (const float up : {-45.0f, -10.0f, 0.0f, 20.0f, 38.0f}) {
                CAPTURE(right);
                CAPTURE(up);
                const Vec3 d = direction(right, up);
                const auto p = ndcOf(kQuest3Left, q, d);
                if (!p) {
                    continue;
                }
                const double expected = std::acos(std::clamp(static_cast<double>(-d.z), -1.0, 1.0));
                CHECK(angleFromForward(r, p->x, p->y) == doctest::Approx(expected).epsilon(1e-4));
            }
        }
    }
}

TEST_CASE("Quest 3: the region is centred on head-forward's projection") {
    const FoveationRegion left = *foveationRegion(kQuest3Left, Quat::identity(), 24.0f);
    // The nose is to the right of the left eye; a narrower top puts head-forward above the image centre
    // (Vulkan NDC y is down).
    CHECK(left.centerX > 0.1f);
    CHECK(left.centerY < -0.05f);
    const float tl = std::tan(kQuest3Left.angleLeft);
    const float tr = std::tan(kQuest3Left.angleRight);
    const float tu = std::tan(kQuest3Left.angleUp);
    const float td = std::tan(kQuest3Left.angleDown);
    CHECK(left.centerX == doctest::Approx(-(tr + tl) / (tr - tl)).epsilon(1e-4));
    CHECK(left.centerY == doctest::Approx((tu + td) / (tu - td)).epsilon(1e-4));
    CHECK(angleFromForward(left, left.centerX, left.centerY) == doctest::Approx(0.0).epsilon(1e-4));
    // Canted outward by 0.1 rad: head-forward is at tangent tan 0.1 to the right of the eye's axis.
    const FoveationRegion canted = *foveationRegion(kQuest3Left, Quat::fromAxisAngle(kUp, 0.1f), 24.0f);
    CHECK(canted.centerX == doctest::Approx((2.0f * std::tan(0.1f) - (tr + tl)) / (tr - tl)).epsilon(1e-4));
    CHECK(canted.centerY == doctest::Approx(left.centerY).epsilon(1e-4));
}

TEST_CASE("a symmetric square FOV: the angle rule gives the old rule's pattern") {
    // The old rule's region there is the cone's circle too, so both rules agree texel for texel.
    const RatePatternSize size{1024, 1024, 16, 16};
    for (const Preset p : presets()) {
        CAPTURE(p.full);
        const auto now = pattern(size, regions(kSquare, Quat::identity(), p.full, p.half));
        const auto before = oldPattern(size, kSquare, p.full, p.half);
        CHECK(now == before);
    }
}

TEST_CASE("Quest 3 eyes: the same direction gets the same rate in both eyes") {
    // The half-rate ends the presets had when the earlier rule was compared (16 degrees past the full-rate
    // angle, 12 for Maximum); the presets' half-rate region has since grown to 56 degrees.
    const std::vector<float> historicHalf{46.0f, 40.0f, 34.0f, 24.0f};
    std::size_t index = 0;
    for (const Preset p : presets()) {
        const float oldHalf = historicHalf[index++];
        CAPTURE(p.full);
        const Regions left = regions(kQuest3Left, Quat::identity(), p.full, p.half);
        const Regions right = regions(kQuest3Right, Quat::identity(), p.full, p.half);
        int compared = 0;
        int oldDiffer = 0;
        for (float x = -56.0f; x <= 56.0f; x += 0.7f) {
            for (float y = -56.0f; y <= 46.0f; y += 0.7f) {
                const Vec3 d = direction(x, y);
                const auto l = ndcOf(kQuest3Left, Quat::identity(), d);
                const auto r = ndcOf(kQuest3Right, Quat::identity(), d);
                if (!l || !r) {
                    continue;
                }
                CAPTURE(x);
                CAPTURE(y);
                ++compared;
                REQUIRE(pointRate(left, *l) == pointRate(right, *r));
                oldDiffer += oldPointRate(kQuest3Left, p.full, oldHalf, *l) !=
                                     oldPointRate(kQuest3Right, p.full, oldHalf, *r)
                                 ? 1
                                 : 0;
            }
        }
        CHECK(compared > 5000);
        // The old rule gave many directions different rates in the two eyes (13% to 30% of these, by preset).
        CHECK(oldDiffer > compared / 20);
    }
    // The Earth A/B case: 26 degrees to the left was full rate in eye L and half rate in eye R; now half
    // rate in both.
    const Vec3 d = direction(-26.0f, 0.0f);
    const auto l = *ndcOf(kQuest3Left, Quat::identity(), d);
    const auto r = *ndcOf(kQuest3Right, Quat::identity(), d);
    CHECK(oldPointRate(kQuest3Left, 24.0f, 40.0f, l) == kRateFull);
    CHECK(oldPointRate(kQuest3Right, 24.0f, 40.0f, r) == kRateHalf);
    CHECK(pointRate(regions(kQuest3Left, Quat::identity(), 24.0f, 40.0f), l) == kRateHalf);
    CHECK(pointRate(regions(kQuest3Right, Quat::identity(), 24.0f, 40.0f), r) == kRateHalf);
}

TEST_CASE("Quest 3 eyes: the rate images agree away from the thresholds") {
    // The rig's eye image and rate texel. A texel takes the finest rate any of its pixels needs, so the
    // two eyes' different texel grids can disagree within a texel of a threshold; a 16 pixel texel spans
    // at most about a degree across here (1.4 along its diagonal).
    const RatePatternSize size{2048, 2208, 16, 16};
    const Regions left = regions(kQuest3Left, Quat::identity(), 24.0f, 40.0f);
    const Regions right = regions(kQuest3Right, Quat::identity(), 24.0f, 40.0f);
    const auto lp = pattern(size, left);
    const auto rp = pattern(size, right);
    int compared = 0;
    for (float x = -56.0f; x <= 56.0f; x += 1.0f) {
        for (float y = -56.0f; y <= 46.0f; y += 1.0f) {
            const Vec3 d = direction(x, y);
            const double angle = degrees(std::acos(static_cast<double>(-d.z)));
            if (std::fabs(angle - 24.0) < 1.5 || std::fabs(angle - 40.0) < 1.5) {
                continue;
            }
            const auto l = ndcOf(kQuest3Left, Quat::identity(), d);
            const auto r = ndcOf(kQuest3Right, Quat::identity(), d);
            if (!l || !r) {
                continue;
            }
            CAPTURE(x);
            CAPTURE(y);
            ++compared;
            const std::uint8_t expected = angle < 24.0 ? kRateFull : angle < 40.0 ? kRateHalf : kRateQuarter;
            REQUIRE(texelAt(size, lp, *l) == expected);
            REQUIRE(texelAt(size, rp, *r) == expected);
        }
    }
    CHECK(compared > 3000);
}

TEST_CASE("each preset's thresholds hold in every direction of both eyes") {
    for (const Preset p : presets()) {
        for (const Fov& fov : {kQuest3Left, kQuest3Right}) {
            const Regions r = regions(fov, Quat::identity(), p.full, p.half);
            for (float heading = 0.0f; heading < 360.0f; heading += 15.0f) {
                CAPTURE(p.full);
                CAPTURE(heading);
                const float c = std::cos(radians(heading));
                const float s = std::sin(radians(heading));
                const auto at = [&](float angle) {
                    // `angle` from head-forward, toward `heading` (0 right, 90 up).
                    const float t = std::tan(radians(angle));
                    return ndcOf(fov, Quat::identity(), evr::normalize(Vec3{t * c, t * s, -1.0f}));
                };
                for (const auto& [angle, rate] :
                     {std::pair{p.full - 0.1f, kRateFull}, std::pair{p.full + 0.1f, kRateHalf},
                      std::pair{p.half - 0.1f, kRateHalf}, std::pair{p.half + 0.1f, kRateQuarter}}) {
                    if (const auto n = at(angle)) {
                        CHECK(pointRate(r, *n) == rate);
                    }
                }
            }
        }
    }
}

TEST_CASE("edge texels: full rate exactly where a pixel is within the angle, clipped texels included") {
    // A size that is not a multiple of the texel: the last column and row are clipped at the image edge.
    // Without canting the cone meets the image plane in a circle, canted in an ellipse.
    const RatePatternSize size{300, 260, 16, 16};
    for (const Quat q : {Quat::identity(), Quat::fromAxisAngle(kUp, radians(8.0f))}) {
        CAPTURE(q.y);
        const Regions r = regions(kQuest3Left, q, 24.0f, 40.0f);
        const auto p = pattern(size, r);
        const std::uint32_t w = 19;
        const std::uint32_t h = 17;
        REQUIRE(p.size() == static_cast<std::size_t>(w) * h);
        constexpr int kSamples = 33;
        for (std::uint32_t ty = 0; ty < h; ++ty) {
            for (std::uint32_t tx = 0; tx < w; ++tx) {
                const float x0 = -1.0f + 2.0f * static_cast<float>(tx * 16) / 300.0f;
                const float x1 = std::min(1.0f, -1.0f + 2.0f * static_cast<float>(tx * 16 + 16) / 300.0f);
                const float y0 = -1.0f + 2.0f * static_cast<float>(ty * 16) / 260.0f;
                const float y1 = std::min(1.0f, -1.0f + 2.0f * static_cast<float>(ty * 16 + 16) / 260.0f);
                double nearest = 180.0;
                for (int i = 0; i < kSamples; ++i) {
                    for (int j = 0; j < kSamples; ++j) {
                        const float x = x0 + (x1 - x0) * static_cast<float>(i) / (kSamples - 1);
                        const float y = y0 + (y1 - y0) * static_cast<float>(j) / (kSamples - 1);
                        nearest = std::min(nearest, degrees(angleFromForward(r.full, x, y)));
                    }
                }
                CAPTURE(tx);
                CAPTURE(ty);
                CAPTURE(nearest);
                const std::uint8_t rate = p[static_cast<std::size_t>(ty) * w + tx];
                // Sampled every 1/32 of the texel: the true nearest angle is at most a fraction of a degree
                // below the sampled one.
                if (nearest <= 24.0) {
                    CHECK(rate == kRateFull);
                } else if (nearest > 24.5 && nearest <= 40.0) {
                    CHECK(rate == kRateHalf);
                } else if (nearest > 40.5) {
                    CHECK(rate == kRateQuarter);
                }
            }
        }
        // The image's corners are over 55 degrees out.
        CHECK(p[0] == kRateQuarter);
        CHECK(p[p.size() - 1] == kRateQuarter);
    }
}

TEST_CASE("canted eyes: the angle is to head-forward, not to each eye's axis") {
    // Each eye turned 10 degrees outward: eye L to the left (a positive yaw), eye R to the right.
    const Quat cantLeft = Quat::fromAxisAngle(kUp, radians(10.0f));
    const Quat cantRight = Quat::fromAxisAngle(kUp, radians(-10.0f));
    const Fov symmetricLeft{radians(-50.0f), radians(50.0f), radians(44.0f), radians(-54.3f)};
    const Fov symmetricRight = symmetricLeft;
    const Regions left = regions(symmetricLeft, cantLeft, 12.0f, 24.0f);
    const Regions right = regions(symmetricRight, cantRight, 12.0f, 24.0f);
    int compared = 0;
    for (float x = -50.0f; x <= 50.0f; x += 1.3f) {
        for (float y = -50.0f; y <= 40.0f; y += 1.3f) {
            const Vec3 d = direction(x, y);
            const auto l = ndcOf(symmetricLeft, cantLeft, d);
            const auto r = ndcOf(symmetricRight, cantRight, d);
            if (!l || !r) {
                continue;
            }
            CAPTURE(x);
            CAPTURE(y);
            ++compared;
            REQUIRE(pointRate(left, *l) == pointRate(right, *r));
        }
    }
    CHECK(compared > 1000);
    // Eye L's own axis is 10 degrees left of head-forward: half rate with a full-rate angle of 8 degrees,
    // although it is the middle of eye L's image.
    const Regions narrow = regions(symmetricLeft, cantLeft, 8.0f, 24.0f);
    CHECK(pointRate(narrow, *ndcOf(symmetricLeft, cantLeft, direction(-10.0f, 0.0f))) == kRateHalf);
    CHECK(pointRate(narrow, *ndcOf(symmetricLeft, cantLeft, direction(0.0f, 0.0f))) == kRateFull);
    CHECK(pointRate(narrow, *ndcOf(symmetricLeft, cantLeft, direction(7.5f, 0.0f))) == kRateFull);
    CHECK(pointRate(narrow, *ndcOf(symmetricLeft, cantLeft, direction(-7.5f, 0.0f))) == kRateFull);
}

TEST_CASE("cost: no preset shades more of a Quest 3 image at full or half rate than the old rule") {
    const RatePatternSize size{1024, 1104, 8, 8};
    for (const Preset p : presets()) {
        for (const Fov& fov : {kQuest3Left, kQuest3Right}) {
            CAPTURE(p.full);
            const auto now = evr::foveation::rateShares(
                size, pattern(size, regions(fov, Quat::identity(), p.full, p.half)));
            const auto before = evr::foveation::rateShares(size, oldPattern(size, fov, p.full, p.half));
            CHECK(now.full + now.half + now.quarter == doctest::Approx(1.0));
            // Each cone fits in the image, or is clipped by it where the old region was not: the area at full
            // rate is the same, and full plus half is the same or less.
            CHECK(now.full == doctest::Approx(before.full).epsilon(0.03));
            CHECK(now.full + now.half <= before.full + before.half + 0.005);
        }
    }
}

TEST_CASE("degenerate inputs yield no region") {
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::identity(), 0.0f).has_value());
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::identity(), 90.0f).has_value());
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::identity(), std::nanf("")).has_value());
    // Eye turned so far that head-forward is behind it.
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::fromAxisAngle(kUp, 2.0f), 24.0f).has_value());
    // Eye turned so far that head-forward is beside its image (the left edge is 54 degrees out).
    CHECK_FALSE(foveationRegion(kQuest3Left, Quat::fromAxisAngle(kUp, radians(-60.0f)), 24.0f).has_value());
    // A region of no angle or no image holds nothing.
    CHECK_FALSE(touches(FoveationRegion{}, -1.0f, 1.0f, -1.0f, 1.0f));
}

TEST_CASE("an eye FOV with no projection yields no region") {
    CHECK_FALSE(foveationRegion({0.5f, 0.5f, 0.8f, -0.8f}, Quat::identity(), 24.0f).has_value());
    CHECK_FALSE(foveationRegion({-0.9f, 0.9f, 0.8f, 0.8f}, Quat::identity(), 24.0f).has_value());
    CHECK_FALSE(foveationRegion({-2.0f, 0.9f, 0.8f, -0.8f}, Quat::identity(), 24.0f).has_value());
}
