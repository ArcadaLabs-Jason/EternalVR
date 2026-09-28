#include "features/roomscale/driven_offset.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>

using evr::Vec3;
using evr::roomscale::DrivenViewOffset;
using evr::test::approxEqual;

namespace {

constexpr double kFrame = 1.0 / 90.0;

// Runs `frames` frames from `t` with the same input; returns the last output and advances `t`.
Vec3 run(DrivenViewOffset& d, Vec3 offset, bool driven, int frames, double& t) {
    Vec3 out;
    for (int i = 0; i < frames; ++i) {
        out = d.update(offset, driven, t);
        t += kFrame;
    }
    return out;
}

} // namespace

TEST_CASE("without a driven view the head's offset passes through") {
    DrivenViewOffset d;
    double t = 0.0;
    CHECK(approxEqual(run(d, {0.2f, -0.1f, -0.3f}, false, 30, t), Vec3{0.2f, -0.1f, -0.3f}));
    CHECK_FALSE(d.began());
}

TEST_CASE("a glory kill while standing puts the camera on the game's eye, and the head moves it from there") {
    DrivenViewOffset d;
    double t = 0.0;
    // Standing, the head 0.30 m forward and 0.10 m low of the body when the kill starts.
    const Vec3 atStart{0.0f, -0.10f, -0.30f};
    run(d, atStart, false, 10, t);
    Vec3 out = d.update(atStart, true, t);
    t += kFrame;
    CHECK(d.began());
    CHECK(std::fabs(out.z) <= 0.30f);   // eases, no jump
    out = run(d, atStart, true, 40, t); // under half a second in
    CHECK(approxEqual(out, Vec3{}, 1e-3f));
    CHECK(approxEqual(d.held(), atStart, 1e-3f));
    // The head leans 0.05 m right during the kill: the camera moves by that from the game's eye.
    out = run(d, atStart + Vec3{0.05f, 0.0f, 0.0f}, true, 5, t);
    CHECK(approxEqual(out, Vec3{0.05f, 0.0f, 0.0f}, 1e-3f));
    // The kill ends: the whole offset comes back smoothly.
    const Vec3 after = atStart + Vec3{0.05f, 0.0f, 0.0f};
    out = d.update(after, false, t);
    t += kFrame;
    CHECK(out.z > -0.30f);
    out = run(d, after, false, 40, t);
    CHECK(approxEqual(out, after, 1e-3f));
}

TEST_CASE("a new driven view during the ease back takes the offset it has then") {
    DrivenViewOffset d;
    double t = 0.0;
    run(d, {0.0f, 0.0f, -0.4f}, true, 30, t);
    run(d, {0.0f, 0.0f, -0.4f}, false, 2, t);
    run(d, {0.1f, 0.0f, -0.2f}, true, 60, t);
    CHECK(approxEqual(d.held(), Vec3{0.1f, 0.0f, -0.2f}, 1e-3f));
}

TEST_CASE("a long stall eases at most a tenth of a second, and bad input is passed through untouched") {
    DrivenViewOffset d;
    d.update({0.0f, 0.0f, -0.3f}, false, 0.0);
    const Vec3 out = d.update({0.0f, 0.0f, -0.3f}, true, 100.0);
    CHECK(out.z < -0.01f); // 0.1 s of a 0.06 s ease: most, not all, of the way
    CHECK(out.z > -0.3f);
    const Vec3 bad = d.update({std::nanf(""), 0.0f, 0.0f}, true, 100.1);
    CHECK(std::isnan(bad.x));
}
