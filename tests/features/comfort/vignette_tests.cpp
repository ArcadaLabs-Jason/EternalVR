#include "features/comfort/vignette.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace evr::comfort;

namespace {

constexpr double kFrame = 1.0 / 90.0;

VignetteMotion turning(float degreesPerSecond) {
    VignetteMotion m;
    m.turnDegreesPerSecond = degreesPerSecond;
    return m;
}

VignetteMotion moving(float magnitude) {
    VignetteMotion m;
    m.moveMagnitude = magnitude;
    return m;
}

// Seconds of `motion` until the amount first reaches `level`; -1 if it never does within `limit`.
double secondsTo(
    VignettePolicy& policy, const VignetteMotion& motion, float level, bool rising, double limit = 3.0) {
    for (int frame = 1; frame * kFrame <= limit; ++frame) {
        const float v = policy.update(motion, kFrame);
        if (rising ? v >= level : v <= level) {
            return frame * kFrame;
        }
    }
    return -1.0;
}

std::uint8_t
alphaAt(const std::vector<std::uint8_t>& px, std::uint32_t size, std::uint32_t x, std::uint32_t y) {
    return px[(static_cast<std::size_t>(y) * size + x) * 4 + 3];
}

} // namespace

TEST_CASE("the target follows the turn rate and the move stick") {
    const VignetteTiming timing;
    CHECK(vignetteTarget({}, timing) == 0.0f);
    CHECK(vignetteTarget(turning(5.0f), timing) == 0.0f);
    CHECK(vignetteTarget(turning(65.0f), timing) == doctest::Approx(0.5f));
    CHECK(vignetteTarget(turning(-230.0f), timing) == 1.0f); // either direction
    CHECK(vignetteTarget(moving(0.05f), timing) == 0.0f);
    CHECK(vignetteTarget(moving(0.4f), timing) == doctest::Approx(0.5f));
    CHECK(vignetteTarget(moving(1.0f), timing) == 1.0f);
    // The largest of the two.
    VignetteMotion both = turning(65.0f);
    both.moveMagnitude = 1.0f;
    CHECK(vignetteTarget(both, timing) == 1.0f);
    // The game moving the camera is full.
    VignetteMotion game;
    game.gameMotion = true;
    CHECK(vignetteTarget(game, timing) == 1.0f);
    CHECK(vignetteTarget(turning(std::nanf("")), timing) == 0.0f);
    CHECK(vignetteTarget(moving(INFINITY), timing) == 0.0f);
}

TEST_CASE("a smooth turn brings the vignette in within the rise time and it clears within the fall time") {
    VignettePolicy policy;
    const double in = secondsTo(policy, turning(230.0f), 1.0f, true);
    CHECK(in > 0.15);
    CHECK(in <= 0.22);
    const double out = secondsTo(policy, {}, 0.0f, false);
    CHECK(out > 0.4);
    CHECK(out <= 0.52);
}

TEST_CASE("the amount rests at a partial target") {
    VignettePolicy policy;
    for (int i = 0; i < 90; ++i) {
        policy.update(moving(0.4f), kFrame);
    }
    CHECK(policy.value() == doctest::Approx(0.5f));
}

TEST_CASE("a snap turn's single frame shows only a little") {
    VignettePolicy policy;
    // 45 degrees in one 11 ms frame is a very fast turn, but only for that frame.
    policy.update(turning(45.0f / static_cast<float>(kFrame)), kFrame);
    CHECK(policy.value() < 0.1f);
    CHECK(vignetteLevel(policy.value(), 8) == 0);
}

TEST_CASE("bad steps change nothing and a long one counts as one second") {
    VignettePolicy policy;
    policy.update(moving(1.0f), 0.1);
    const float before = policy.value();
    CHECK(policy.update(moving(1.0f), -1.0) == before);
    CHECK(policy.update(moving(1.0f), std::nan("")) == before);
    CHECK(policy.update({}, 60.0) == 0.0f);
    policy.update(moving(1.0f), 60.0);
    CHECK(policy.value() == 1.0f);
    policy.reset();
    CHECK(policy.value() == 0.0f);
}

TEST_CASE("unusable timing falls back to the defaults") {
    VignetteTiming bad;
    bad.turnStartDegreesPerSecond = 200.0f; // above the full rate
    bad.moveFull = std::nanf("");
    bad.riseSeconds = -1.0f;
    const VignettePolicy policy(bad);
    const VignetteTiming defaults;
    CHECK(policy.timing().turnStartDegreesPerSecond == defaults.turnStartDegreesPerSecond);
    CHECK(policy.timing().turnFullDegreesPerSecond == defaults.turnFullDegreesPerSecond);
    CHECK(policy.timing().moveFull == defaults.moveFull);
    CHECK(policy.timing().riseSeconds == defaults.riseSeconds);
}

TEST_CASE("the shape closes in and darkens with the amount") {
    const VignetteShape none = vignetteShape(0.0f, kStrongVignette);
    CHECK(none.opacity == 0.0f);
    CHECK(none.clearDegrees == doctest::Approx(kStrongVignette.startClearDegrees));
    const VignetteShape full = vignetteShape(1.0f, kStrongVignette);
    CHECK(full.opacity == doctest::Approx(1.0f));
    CHECK(full.clearDegrees == doctest::Approx(kStrongVignette.fullClearDegrees));
    CHECK(full.darkDegrees ==
          doctest::Approx(kStrongVignette.fullClearDegrees + kStrongVignette.featherDegrees));
    const VignetteShape half = vignetteShape(0.5f, kLightVignette);
    CHECK(half.opacity == doctest::Approx(0.4f));
    CHECK(half.clearDegrees > kLightVignette.fullClearDegrees);
    CHECK(half.clearDegrees < kLightVignette.startClearDegrees);
    CHECK(vignetteShape(7.0f, kLightVignette).opacity == doctest::Approx(kLightVignette.opacity));
    CHECK(vignetteShape(std::nanf(""), kLightVignette).opacity == 0.0f);
    // Strong closes in further and darker than light.
    CHECK(vignetteShape(1.0f, kStrongVignette).clearDegrees <
          vignetteShape(1.0f, kLightVignette).clearDegrees);
    CHECK(vignetteShape(1.0f, kStrongVignette).opacity > vignetteShape(1.0f, kLightVignette).opacity);
}

TEST_CASE("the level is the nearest image, none below half of the first") {
    CHECK(vignetteLevel(0.0f, 8) == 0);
    CHECK(vignetteLevel(0.05f, 8) == 0);
    CHECK(vignetteLevel(0.07f, 8) == 1);
    CHECK(vignetteLevel(0.5f, 8) == 4);
    CHECK(vignetteLevel(0.99f, 8) == 8);
    CHECK(vignetteLevel(3.0f, 8) == 8);
    CHECK(vignetteLevel(-1.0f, 8) == 0);
    CHECK(vignetteLevel(std::nanf(""), 8) == 0);
    CHECK(vignetteLevel(1.0f, 0) == 0);
}

TEST_CASE("the image is clear in the centre and dark at the edges, with a smooth falloff") {
    constexpr std::uint32_t kSize = 256;
    const VignetteShape shape = vignetteShape(1.0f, kStrongVignette);
    const std::vector<std::uint8_t> px = vignetteImage(kSize, shape, 75.0f);
    REQUIRE(px.size() == kSize * kSize * 4);
    CHECK(alphaAt(px, kSize, kSize / 2, kSize / 2) == 0);
    CHECK(alphaAt(px, kSize, 0, kSize / 2) == 255);
    CHECK(alphaAt(px, kSize, 0, 0) == 255);
    // Premultiplied black: every colour byte is 0.
    bool black = true;
    for (std::size_t i = 0; i < px.size(); i += 4) {
        black = black && px[i] == 0 && px[i + 1] == 0 && px[i + 2] == 0;
    }
    CHECK(black);
    // From the centre out along a row the alpha never falls, and no step between neighbours is large.
    int previous = 0;
    int largestStep = 0;
    for (std::uint32_t x = kSize / 2; x < kSize; ++x) {
        const int a = alphaAt(px, kSize, x, kSize / 2);
        CHECK(a >= previous);
        largestStep = std::max(largestStep, a - previous);
        previous = a;
    }
    CHECK(largestStep < 40);
    // The same distance from the centre in any direction gives the same alpha.
    CHECK(alphaAt(px, kSize, kSize / 2, 20) == alphaAt(px, kSize, 20, kSize / 2 - 1));
}

TEST_CASE("the clear area follows the angle, whatever the quad's size") {
    constexpr std::uint32_t kSize = 256;
    VignetteShape shape;
    shape.clearDegrees = 30.0f;
    shape.darkDegrees = 30.5f;
    shape.opacity = 1.0f;
    for (const float half : {60.0f, 75.0f}) {
        const std::vector<std::uint8_t> px = vignetteImage(kSize, shape, half);
        // The pixel column at 30 degrees from the axis: tan(30) / tan(half) of the way to the edge.
        const float fraction = std::tan(30.0f / 57.2957795f) / std::tan(half / 57.2957795f);
        const auto edge = static_cast<std::uint32_t>(kSize / 2 + fraction * (kSize / 2));
        CHECK(alphaAt(px, kSize, edge - 3, kSize / 2) == 0);
        CHECK(alphaAt(px, kSize, edge + 3, kSize / 2) == 255);
    }
    CHECK(vignetteImage(0, shape, 75.0f).empty());
    // No opacity: fully transparent.
    const std::vector<std::uint8_t> clear = vignetteImage(16, vignetteShape(0.0f, kLightVignette), 75.0f);
    CHECK(alphaAt(clear, 16, 0, 0) == 0);
}
