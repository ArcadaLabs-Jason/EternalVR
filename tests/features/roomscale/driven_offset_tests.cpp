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

TEST_CASE("the first anchor during a cutscene re-bases the episode: the camera stays where it was") {
    DrivenViewOffset d;
    double t = 0.0;
    // Before the first anchor the head is 0.15 m right, 0.12 m low and 0.46 m back in LOCAL as the runtime
    // set it, and a cutscene starts (the owner's e1m1 intro, 2026-10-07).
    const Vec3 beforeAnchor{0.153f, -0.122f, 0.456f};
    run(d, beforeAnchor, true, 60, t);
    CHECK(approxEqual(d.held(), beforeAnchor, 1e-3f));
    // The head leans 0.04 m right: the camera moves that much from the game's eye.
    const Vec3 lean{0.04f, 0.0f, 0.0f};
    Vec3 out = run(d, beforeAnchor + lean, true, 5, t);
    CHECK(approxEqual(out, lean, 1e-3f));
    // Anchored: the head's offset jumps to the lean and a little lift. The camera stays where it was;
    // without the re-base it would sit the old offset off the eye the other way (-0.11 0.14 -0.46 m).
    const Vec3 anchored{0.04f, 0.02f, 0.0f};
    CHECK(d.rebase(anchored));
    out = d.update(anchored, true, t);
    t += kFrame;
    CHECK(approxEqual(out, lean, 1e-3f));
    // The head moves from there and the camera with it.
    out = run(d, anchored + Vec3{0.02f, 0.0f, 0.0f}, true, 5, t);
    CHECK(approxEqual(out, lean + Vec3{0.02f, 0.0f, 0.0f}, 1e-3f));
    // The cutscene ends: the whole anchored offset comes back.
    out = run(d, anchored + Vec3{0.02f, 0.0f, 0.0f}, false, 60, t);
    CHECK(approxEqual(out, anchored + Vec3{0.02f, 0.0f, 0.0f}, 1e-3f));
}

TEST_CASE("a re-base outside a driven view, or with bad input, does nothing") {
    DrivenViewOffset d;
    double t = 0.0;
    run(d, {0.0f, 0.0f, -0.3f}, false, 10, t);
    CHECK_FALSE(d.rebase({0.0f, 0.0f, 0.0f}));
    CHECK(approxEqual(run(d, {0.0f, 0.0f, -0.3f}, false, 1, t), Vec3{0.0f, 0.0f, -0.3f}));
    run(d, {0.0f, 0.0f, -0.3f}, true, 60, t);
    CHECK_FALSE(d.rebase({std::nanf(""), 0.0f, 0.0f}));
    CHECK(approxEqual(d.held(), Vec3{0.0f, 0.0f, -0.3f}, 1e-3f));
}
