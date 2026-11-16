#include "xr_math/edge_margin.hpp"

#include "xr_math/projection.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

using evr::test::approxEqual;
using evr::xr_math::Fov;
using evr::xr_math::FovTangents;
using evr::xr_math::toTangents;
using evr::xr_math::viewRectInImage;
using evr::xr_math::widenFov;

namespace {

constexpr float kRadians = 3.14159265358979f / 180.0f;
constexpr float kDegrees = 180.0f / 3.14159265358979f;

// Quest 3's eye L through Virtual Desktop's runtime, as a player's log has it.
Fov quest3EyeL() {
    return {-54.0f * kRadians, 40.0f * kRadians, 44.0f * kRadians, -55.0f * kRadians};
}

// The NDC (x right, y down) a view-space direction lands at through `fov`'s projection.
std::pair<float, float> ndcOf(const Fov& fov, float x, float y) {
    const auto m = evr::xr_math::makeProjectionStandard(fov, 0.1f, 100.0f);
    REQUIRE(m.has_value());
    const float cx = m->at(0, 0) * x + m->at(0, 2) * -1.0f;
    const float cy = m->at(1, 1) * y + m->at(1, 2) * -1.0f;
    const float w = m->at(3, 2) * -1.0f;
    return {cx / w, cy / w};
}

} // namespace

TEST_CASE("edge margin: a factor of 1 keeps the FOV") {
    const auto wide = widenFov(quest3EyeL(), 1.0f);
    REQUIRE(wide.has_value());
    CHECK(approxEqual(wide->angleLeft, quest3EyeL().angleLeft));
    CHECK(approxEqual(wide->angleRight, quest3EyeL().angleRight));
    CHECK(approxEqual(wide->angleUp, quest3EyeL().angleUp));
    CHECK(approxEqual(wide->angleDown, quest3EyeL().angleDown));
}

TEST_CASE("edge margin: every tangent grows by the factor") {
    const auto wide = widenFov(quest3EyeL(), 1.2f);
    REQUIRE(wide.has_value());
    const FovTangents a = toTangents(quest3EyeL());
    const FovTangents b = toTangents(*wide);
    CHECK(approxEqual(b.left, a.left * 1.2f));
    CHECK(approxEqual(b.right, a.right * 1.2f));
    CHECK(approxEqual(b.up, a.up * 1.2f));
    CHECK(approxEqual(b.down, a.down * 1.2f));
}

TEST_CASE("edge margin: the degrees each size adds on a Quest 3") {
    // Small, Medium and Large (render_size/edge_margin.hpp): about 2.5, 5 and 7 degrees past every edge.
    struct Row {
        float factor;
        float left;
        float right;
        float up;
        float down;
    };
    for (const Row& r : {Row{1.1f, 2.56f, 2.71f, 2.73f, 2.52f}, Row{1.2f, 4.81f, 5.20f, 5.21f, 4.74f},
                         Row{1.3f, 6.80f, 7.49f, 7.46f, 6.69f}}) {
        const auto wide = widenFov(quest3EyeL(), r.factor);
        REQUIRE(wide.has_value());
        CHECK(approxEqual((quest3EyeL().angleLeft - wide->angleLeft) * kDegrees, r.left, 0.01f));
        CHECK(approxEqual((wide->angleRight - quest3EyeL().angleRight) * kDegrees, r.right, 0.01f));
        CHECK(approxEqual((wide->angleUp - quest3EyeL().angleUp) * kDegrees, r.up, 0.01f));
        CHECK(approxEqual((quest3EyeL().angleDown - wide->angleDown) * kDegrees, r.down, 0.01f));
    }
}

TEST_CASE("edge margin: the centre's pixels keep their size when the image grows by the factor") {
    // A direction near the optical axis moves the same number of pixels per degree in an image of W pixels
    // with the view's FOV as in one of W * factor pixels with the widened FOV.
    const float factor = 1.2f;
    const auto wide = widenFov(quest3EyeL(), factor);
    REQUIRE(wide.has_value());
    const float w = 2000.0f;
    const auto [a0, ay0] = ndcOf(quest3EyeL(), 0.0f, 0.0f);
    const auto [a1, ay1] = ndcOf(quest3EyeL(), 0.01f, 0.01f);
    const auto [b0, by0] = ndcOf(*wide, 0.0f, 0.0f);
    const auto [b1, by1] = ndcOf(*wide, 0.01f, 0.01f);
    CHECK(approxEqual((a1 - a0) * 0.5f * w, (b1 - b0) * 0.5f * w * factor, 1e-3f));
    CHECK(approxEqual((ay1 - ay0) * 0.5f * w, (by1 - by0) * 0.5f * w * factor, 1e-3f));
}

TEST_CASE("edge margin: no widened FOV for a bad factor or a FOV with no projection") {
    CHECK_FALSE(widenFov(quest3EyeL(), 0.9f));
    CHECK_FALSE(widenFov(quest3EyeL(), std::numeric_limits<float>::quiet_NaN()));
    CHECK_FALSE(widenFov(quest3EyeL(), std::numeric_limits<float>::infinity()));
    CHECK_FALSE(widenFov(Fov{-1.6f, 0.7f, 0.7f, -0.7f}, 1.1f));
    CHECK_FALSE(widenFov(Fov{0.5f, 0.5f, 0.7f, -0.7f}, 1.1f));
    CHECK_FALSE(widenFov(Fov{std::numeric_limits<float>::quiet_NaN(), 0.7f, 0.7f, -0.7f}, 1.1f));
}

TEST_CASE("edge margin: the view's rectangle in the widened image") {
    SUBCASE("no margin: the whole image") {
        const auto r = viewRectInImage(quest3EyeL(), quest3EyeL());
        REQUIRE(r.has_value());
        CHECK(approxEqual(static_cast<float>(r->x), 0.0f));
        CHECK(approxEqual(static_cast<float>(r->y), 0.0f));
        CHECK(approxEqual(static_cast<float>(r->width), 1.0f));
        CHECK(approxEqual(static_cast<float>(r->height), 1.0f));
    }
    SUBCASE("a factor of 1.2: 1/1.2 of each side, off-centre as the eye's FOV is") {
        const auto wide = widenFov(quest3EyeL(), 1.2f);
        REQUIRE(wide.has_value());
        const auto r = viewRectInImage(*wide, quest3EyeL());
        REQUIRE(r.has_value());
        CHECK(approxEqual(static_cast<float>(r->width), 1.0f / 1.2f));
        CHECK(approxEqual(static_cast<float>(r->height), 1.0f / 1.2f));
        // The margin is the same share of each side's tangent: the wider (temporal, left for eye L) side has
        // more of it, so the view sits right of centre; the same below, so it sits above centre.
        const float leftMargin = static_cast<float>(r->x);
        const float rightMargin = 1.0f - static_cast<float>(r->x + r->width);
        CHECK(leftMargin > rightMargin);
        const FovTangents t = toTangents(*wide);
        CHECK(approxEqual(leftMargin, (1.0f - 1.0f / 1.2f) * -t.left / (t.right - t.left)));
        const float topMargin = static_cast<float>(r->y);
        const float bottomMargin = 1.0f - static_cast<float>(r->y + r->height);
        CHECK(bottomMargin > topMargin);
    }
    SUBCASE("the rectangle's edges are where the view's edges project") {
        const auto wide = widenFov(quest3EyeL(), 1.3f);
        REQUIRE(wide.has_value());
        const auto r = viewRectInImage(*wide, quest3EyeL());
        REQUIRE(r.has_value());
        const FovTangents v = toTangents(quest3EyeL());
        const auto [left, top] = ndcOf(*wide, v.left, v.up);
        const auto [right, bottom] = ndcOf(*wide, v.right, v.down);
        CHECK(approxEqual(static_cast<float>(r->x), (left + 1.0f) * 0.5f));
        CHECK(approxEqual(static_cast<float>(r->x + r->width), (right + 1.0f) * 0.5f));
        CHECK(approxEqual(static_cast<float>(r->y), (top + 1.0f) * 0.5f));
        CHECK(approxEqual(static_cast<float>(r->y + r->height), (bottom + 1.0f) * 0.5f));
    }
    SUBCASE("a view the image does not hold") {
        const auto wide = widenFov(quest3EyeL(), 1.2f);
        REQUIRE(wide.has_value());
        CHECK_FALSE(viewRectInImage(quest3EyeL(), *wide));
        CHECK_FALSE(viewRectInImage(Fov{-1.6f, 0.7f, 0.7f, -0.7f}, quest3EyeL()));
    }
}

TEST_CASE("edge margin: eye R and an asymmetric headset at every size") {
    // Eye R mirrors eye L: its wider side is the right one, so it takes eye L's left degrees on its right and
    // its view sits left of centre in its image.
    const Fov eyeR{-40.0f * kRadians, 54.0f * kRadians, 44.0f * kRadians, -55.0f * kRadians};
    // A headset whose sides all differ (an Index-like eye L).
    const Fov odd{-56.0f * kRadians, 47.0f * kRadians, 52.0f * kRadians, -53.0f * kRadians};
    for (const float factor : {1.1f, 1.2f, 1.3f}) {
        CAPTURE(factor);
        const auto l = widenFov(quest3EyeL(), factor);
        const auto r = widenFov(eyeR, factor);
        REQUIRE(l.has_value());
        REQUIRE(r.has_value());
        CHECK(approxEqual(r->angleRight, -l->angleLeft));
        CHECK(approxEqual(r->angleLeft, -l->angleRight));
        CHECK(approxEqual(r->angleUp, l->angleUp));
        CHECK(approxEqual(r->angleDown, l->angleDown));
        const auto rect = evr::xr_math::marginViewRect(*r, eyeR);
        REQUIRE(rect.has_value());
        CHECK(approxEqual(static_cast<float>(rect->width), 1.0f / factor));
        CHECK(approxEqual(static_cast<float>(rect->height), 1.0f / factor));
        const float leftMargin = static_cast<float>(rect->x);
        const float rightMargin = 1.0f - static_cast<float>(rect->x + rect->width);
        CHECK(rightMargin > leftMargin);

        const auto w = widenFov(odd, factor);
        REQUIRE(w.has_value());
        const FovTangents a = toTangents(odd);
        const FovTangents b = toTangents(*w);
        CHECK(approxEqual(b.left, a.left * factor));
        CHECK(approxEqual(b.right, a.right * factor));
        CHECK(approxEqual(b.up, a.up * factor));
        CHECK(approxEqual(b.down, a.down * factor));
        const auto oddRect = evr::xr_math::marginViewRect(*w, odd);
        REQUIRE(oddRect.has_value());
        CHECK(approxEqual(static_cast<float>(oddRect->width), 1.0f / factor));
        CHECK(approxEqual(static_cast<float>(oddRect->height), 1.0f / factor));
    }
}

TEST_CASE("edge margin: a half-angle near 90 degrees widens to just under it") {
    // Widening in tangent space never reaches 90 degrees: a side at 80, 89 or 89.9 degrees stays projectable
    // and below 90 after x1.3, so the eye renders it rather than falling back to its view.
    for (const float degrees : {80.0f, 89.0f, 89.9f}) {
        CAPTURE(degrees);
        const Fov near90{-degrees * kRadians, 40.0f * kRadians, 44.0f * kRadians, -55.0f * kRadians};
        const auto wide = widenFov(near90, 1.3f);
        REQUIRE(wide.has_value());
        CHECK(-wide->angleLeft * kDegrees < 90.0f);
        CHECK(-wide->angleLeft >= -near90.angleLeft);
        CHECK(evr::xr_math::marginViewRect(*wide, near90).has_value());
    }
    // A side already at 90 degrees has no projection: no widened FOV, no rectangle.
    const Fov at90{-std::numbers::pi_v<float> / 2.0f, 40.0f * kRadians, 44.0f * kRadians, -55.0f * kRadians};
    CHECK_FALSE(widenFov(at90, 1.1f));
    CHECK_FALSE(evr::xr_math::marginViewRect(at90, at90));
}

TEST_CASE("edge margin: one eye widened and the other not") {
    // prepareEyes widens each eye on its own: an eye whose FOV cannot be widened keeps its view, and its
    // record has no rectangle (the window shows its whole image) while the other eye's has one.
    const Fov bad{0.5f, 0.5f, 0.7f, -0.7f}; // no width
    const auto good = widenFov(quest3EyeL(), 1.2f);
    REQUIRE(good.has_value());
    CHECK_FALSE(widenFov(bad, 1.2f));
    CHECK(evr::xr_math::marginViewRect(*good, quest3EyeL()).has_value());
    CHECK_FALSE(evr::xr_math::marginViewRect(quest3EyeL(), quest3EyeL())); // rendered as the view: no margin
    CHECK_FALSE(evr::xr_math::marginViewRect(bad, bad));
    // A rendered FOV that does not hold the view (the wrong pair) gives no rectangle either.
    CHECK_FALSE(evr::xr_math::marginViewRect(quest3EyeL(), *good));
}

TEST_CASE("edge margin: the view's rectangle in whole pixels") {
    using evr::xr_math::ImageRect;
    using evr::xr_math::PixelBox;
    using evr::xr_math::toPixels;
    CHECK(toPixels(ImageRect{}, 1536, 1680) == PixelBox{0, 0, 1536, 1680});
    // A Medium margin on the simulator's eye: the view is 1/1.2 of each side.
    const auto wide = widenFov(quest3EyeL(), 1.2f);
    REQUIRE(wide.has_value());
    const auto r = viewRectInImage(*wide, quest3EyeL());
    REQUIRE(r.has_value());
    const PixelBox box = toPixels(*r, 1536, 1680);
    CHECK(box.width == 1280);
    CHECK(box.height == 1400);
    CHECK(box.x + box.width <= 1536);
    CHECK(box.y + box.height <= 1680);
    // Kept inside the image, at least one pixel.
    CHECK(toPixels(ImageRect{-0.5, -0.5, 3.0, 3.0}, 100, 50) == PixelBox{0, 0, 100, 50});
    CHECK(toPixels(ImageRect{1.0, 1.0, 0.0, 0.0}, 100, 50) == PixelBox{99, 49, 1, 1});
    CHECK(toPixels(ImageRect{0.2, 0.2, 0.0, 0.0}, 100, 50) == PixelBox{20, 10, 1, 1});
    CHECK(toPixels(ImageRect{}, 0, 50) == PixelBox{});
}
