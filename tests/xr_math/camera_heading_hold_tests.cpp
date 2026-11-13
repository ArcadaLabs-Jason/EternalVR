// Still view's hold of a pickup camera's heading (camera_turn.hpp CameraHeadingHold) at the frame rates a
// headset runs at, from the rig's traces (docs/rig-findings/camera-animations.md section 7: the Sentinel
// Crystal's sync, run cv1; gore nest 3, run cv2, sampled at about 60 frames a second).

#include "xr_math/camera_turn.hpp"

#include "xr_math/head_aim.hpp"

#include "support/approx.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>

using evr::test::approxEqual;
using evr::xr_math::CameraHeadingHold;
using evr::xr_math::normalize180;

namespace {

constexpr double kRates[] = {30.0, 60.0, 90.0, 144.0};

// Gore nest 3's camera yaw from the view after its cut to the nest's side on, one value a game view at about
// 60 a second: the side-to-side swing (2 to 3.5 degrees a frame), then from index 83 the heart pull (3.83
// and 4.69).
const std::vector<float> kNest = {
    1.56f,   3.42f,   6.64f,   9.27f,   11.40f,  12.40f,  12.75f,  12.55f,  12.21f,  11.86f,  11.00f,
    9.10f,   6.04f,   2.51f,   -0.95f,  -4.33f,  -6.24f,  -7.72f,  -7.75f,  -7.78f,  -7.81f,  -7.83f,
    -7.86f,  -7.89f,  -7.92f,  -8.16f,  -8.50f,  -9.34f,  -10.31f, -11.73f, -13.16f, -14.44f, -15.53f,
    -16.31f, -16.90f, -17.22f, -17.29f, -17.18f, -16.75f, -16.18f, -15.09f, -13.96f, -12.47f, -10.96f,
    -9.54f,  -8.53f,  -7.51f,  -6.76f,  -6.38f,  -6.26f,  -6.14f,  -6.04f,  -5.98f,  -6.08f,  -6.57f,
    -7.08f,  -7.65f,  -8.20f,  -8.77f,  -9.32f,  -9.92f,  -10.49f, -10.98f, -11.34f, -11.44f, -10.80f,
    -9.96f,  -9.09f,  -8.19f,  -7.47f,  -6.97f,  -6.78f,  -6.65f,  -6.40f,  -6.19f,  -5.99f,  -5.69f,
    -5.38f,  -5.07f,  -4.75f,  -4.39f,  -4.03f,  -3.67f,  0.16f,   4.85f,   7.01f,   9.10f,   11.07f,
    12.60f,  13.69f,  14.31f,  14.67f,  14.44f,  14.06f,  12.75f,  11.24f,  9.10f,   6.93f,   4.83f,
    3.25f,   1.89f,   0.99f,   0.32f,   0.07f,   -0.18f,  -0.43f,  -0.69f,  -0.94f};
constexpr std::size_t kPull = 83; // the index of 0.16 (the pull's first step)

// The 60-a-second series at `rate` views a second (linear between its samples), from sample `from` to `to`.
std::vector<float> resample(
    const std::vector<float>& series, std::size_t from, std::size_t to, double rate, double phase = 0.0) {
    std::vector<float> out;
    for (double t = from / 60.0 + phase / rate; t <= to / 60.0; t += 1.0 / rate) {
        const double x = t * 60.0;
        const auto i = static_cast<std::size_t>(x);
        const float w = static_cast<float>(x - static_cast<double>(i));
        out.push_back(i + 1 < series.size() ? series[i] * (1.0f - w) + series[i + 1] * w : series[i]);
    }
    return out;
}

struct Run {
    std::vector<float> out;
    int starts = 0;
};

// One view: the offset (camera minus view angles) the follow sees, the hold's turn on it.
float followed(CameraHeadingHold& hold, float camera, float angles, double seconds, bool pickup) {
    return normalize180(camera - angles + hold.update(camera, angles, seconds, pickup));
}

// Feeds offsets (the camera's yaw, the view angles at 0) at `rate` views a second, a pickup running
// throughout.
Run feed(CameraHeadingHold& hold, const std::vector<float>& offsets, double rate, double t0 = 1.0) {
    Run r;
    double t = t0;
    for (const float o : offsets) {
        r.out.push_back(followed(hold, o, 0.0f, t, true));
        r.starts += hold.started() ? 1 : 0;
        t += 1.0 / rate;
    }
    return r;
}

// The largest change of the followed offset between two views.
float largestStep(const std::vector<float>& out) {
    float most = 0.0f;
    for (std::size_t i = 1; i < out.size(); ++i) {
        most = std::fmax(most, std::fabs(normalize180(out[i] - out[i - 1])));
    }
    return most;
}

} // namespace

TEST_CASE("turnYaw: the yaw grows by the turn, pitch and roll stay") {
    using evr::xr_math::anglesFromAxis;
    using evr::xr_math::axisFromAngles;
    const auto a = anglesFromAxis(evr::xr_math::turnYaw(axisFromAngles({10.0f, 30.0f, 5.0f}), 20.0f));
    CHECK(approxEqual(a.pitch, 10.0f, 1e-3f));
    CHECK(approxEqual(a.yaw, 50.0f, 1e-3f));
    CHECK(approxEqual(a.roll, 5.0f, 1e-3f));
    const auto b = anglesFromAxis(evr::xr_math::turnYaw(axisFromAngles({0.0f, 170.0f, 0.0f}), 20.0f));
    CHECK(approxEqual(normalize180(b.yaw + 170.0f), 0.0f, 1e-3f)); // across 180
}

// The crystal's sync: the camera on the player's angles, then its first frame 9.6 degrees off, then back and
// a swing of up to 180 degrees a second (3 a frame at 60) for 1.5 s.
TEST_CASE(
    "camera heading hold: the crystal's flick is never followed, its swing always, at 30 to 144 a second") {
    for (const double rate : kRates) {
        CAPTURE(rate);
        std::vector<float> offsets(10, 0.0f);
        offsets.push_back(-9.6f);
        for (double t = 0.0; t < 1.5; t += 1.0 / rate) {
            offsets.push_back(static_cast<float>(20.0 * std::sin(2.0 * std::numbers::pi * t / 0.7)));
        }
        CameraHeadingHold hold;
        const Run r = feed(hold, offsets, rate);
        CHECK(r.starts == 1);
        CHECK(r.out[10] == doctest::Approx(0.0f)); // the flick
        for (std::size_t i = 11; i < offsets.size(); ++i) {
            CHECK(r.out[i] == doctest::Approx(offsets[i])); // the swing, as it is
        }
        CHECK(largestStep(r.out) <= 180.0f / rate * 1.01f + 1e-3f);
    }
}

TEST_CASE("camera heading hold: a flick that comes back over two views is never seen") {
    for (const double rate : kRates) {
        CAPTURE(rate);
        CameraHeadingHold hold;
        const Run r = feed(hold, {0.0f, 0.0f, 0.0f, -13.0f, -7.0f, 0.0f, 0.0f}, rate);
        CHECK(r.starts == 1);
        for (const float o : r.out) {
            CHECK(o == doctest::Approx(0.0f));
        }
    }
}

TEST_CASE("camera heading hold: the gore nest's swing is followed as it is at 30 to 144 a second") {
    for (const double rate : kRates) {
        for (const double phase : {0.0, 0.5}) {
            CAPTURE(rate);
            CAPTURE(phase);
            const std::vector<float> swing = resample(kNest, 0, kPull - 1, rate, phase);
            CameraHeadingHold hold;
            const Run r = feed(hold, swing, rate);
            CHECK(r.starts == 0);
            for (std::size_t i = 0; i < swing.size(); ++i) {
                CHECK(r.out[i] == doctest::Approx(swing[i]));
            }
        }
    }
}

// The heart pull's yaw (3.83 and 4.69 in a frame at 60 a second, out of a nearly still camera). At 30 a
// second the sampling that puts the pull's two steps into one view (8.52 out of 0.72) starts a hold: it goes
// on the way it jumped, so it is followed one view late, never held twice; the other sampling and every
// faster rate follow it as it is.
TEST_CASE("camera heading hold: the heart pull is followed, at 30 a second at most one view late") {
    int startsAt30 = 0;
    for (const double rate : kRates) {
        for (const double phase : {0.0, 0.5}) {
            CAPTURE(rate);
            CAPTURE(phase);
            const std::vector<float> pull = resample(kNest, kPull - 8, kNest.size() - 1, rate, phase);
            CameraHeadingHold hold;
            const Run r = feed(hold, pull, rate);
            if (rate >= 60.0) {
                CHECK(r.starts == 0);
            }
            CHECK(r.starts <= 1);
            startsAt30 += rate < 60.0 ? r.starts : 0;
            std::size_t late = 0;
            for (std::size_t i = 0; i < pull.size(); ++i) {
                late += std::fabs(r.out[i] - pull[i]) > 1e-3f ? 1 : 0;
            }
            CHECK(late <= 1);
            CHECK(largestStep(r.out) <= 13.0f);
        }
    }
    CHECK(startsAt30 == 1); // the one sampling at 30 a second that puts the pull's two steps into one view
}

TEST_CASE("camera heading hold: a cut that stays is followed two views late, once") {
    for (const double rate : kRates) {
        CAPTURE(rate);
        std::vector<float> offsets(5, -180.0f);
        for (std::size_t i = 0; i < 20; ++i) {
            offsets.push_back(kNest[i]);
        }
        CameraHeadingHold hold;
        const Run r = feed(hold, offsets, rate);
        CHECK(r.starts == 1);
        CHECK(r.out[5] == doctest::Approx(-180.0f));
        CHECK(r.out[6] == doctest::Approx(-180.0f));
        CHECK(r.out[7] == doctest::Approx(kNest[2]));
    }
}

// A snap turn or a fast hand during a pickup moves the view angles: no hold starts from such a view, also
// when the camera stays (the animation's), and one that runs ends.
TEST_CASE("camera heading hold: a snap turn or a fast hand during a pickup never starts a hold") {
    CameraHeadingHold hold;
    double t = 1.0;
    const auto next = [&](float camera, float angles) {
        t += 1.0 / 60.0;
        return followed(hold, camera, angles, t, true);
    };
    next(135.0f, 135.0f);
    next(135.0f, 135.0f);
    // A snap turn of 90 with the camera still the player's own: both move, the offset stays.
    CHECK(next(225.0f, 225.0f) == doctest::Approx(0.0f));
    CHECK_FALSE(hold.held());
    // A snap turn with the camera the animation's: the offset jumps 90, but the view angles moved.
    CHECK(next(225.0f, 135.0f) == doctest::Approx(90.0f));
    CHECK_FALSE(hold.held());
    // A fast hand (12 degrees in a view) with the camera still: no hold either.
    CHECK(next(225.0f, 147.0f) == doctest::Approx(78.0f));
    CHECK_FALSE(hold.held());
    // A flick that starts as the view angles stay is held; the angles moving next ends it.
    next(225.0f, 147.0f);
    CHECK(next(215.0f, 147.0f) == doctest::Approx(78.0f));
    CHECK(hold.held());
    CHECK(next(215.0f, 160.0f) == doctest::Approx(55.0f));
    CHECK_FALSE(hold.held());
}

// The crystal behind the player: the camera 180 from the view angles, so the offset is near +-180.
TEST_CASE("camera heading hold: a hold across +-180") {
    CameraHeadingHold hold;
    double t = 1.0;
    const auto next = [&](float camera) {
        t += 1.0 / 60.0;
        return followed(hold, camera, -45.0f, t, true);
    };
    next(135.0f);
    CHECK(std::fabs(normalize180(next(135.0f) - 180.0f)) < 1e-3f);
    CHECK(std::fabs(normalize180(next(125.4f) - 180.0f)) < 1e-3f); // the flick, across the wrap
    CHECK(hold.held());
    CHECK(std::fabs(normalize180(next(135.0f) - 180.0f)) < 1e-3f);
    CHECK_FALSE(hold.held());
    next(135.0f);
    CHECK(std::fabs(normalize180(next(144.6f) - 180.0f)) < 1e-3f); // to the other side of 180: held too
    CHECK(hold.held());
}

TEST_CASE("camera heading hold: the frame's time, a view with none, and stalls") {
    // Two views at the same time: the threshold is 5 degrees, the flick is held.
    CameraHeadingHold same;
    followed(same, 0.0f, 0.0f, 1.0, true);
    followed(same, 0.0f, 0.0f, 1.0, true);
    CHECK(followed(same, -9.6f, 0.0f, 1.0, true) == doctest::Approx(0.0f));
    // After a stall of 0.7 s, or of over a second, the threshold is 240 degrees a second over 0.25 s (60),
    // with no jump in it at 1 s: a flick of 9.6 after it is taken as the camera having moved meanwhile and
    // followed, a jump of 65 is still held.
    for (const double stall : {0.7, 1.5}) {
        CAPTURE(stall);
        CameraHeadingHold hold;
        followed(hold, 0.0f, 0.0f, 1.0, true);
        followed(hold, 0.0f, 0.0f, 1.0 + 1.0 / 60.0, true);
        CHECK(followed(hold, -9.6f, 0.0f, 1.0 + 1.0 / 60.0 + stall, true) == doctest::Approx(-9.6f));
        CHECK(followed(hold, -75.0f, 0.0f, 1.0 + 2.0 / 60.0 + 2.0 * stall, true) == doctest::Approx(-9.6f));
    }
}

TEST_CASE("camera heading hold: a clock that is not finite in a hold ends it") {
    CameraHeadingHold hold;
    followed(hold, 0.0f, 0.0f, 1.0, true);
    followed(hold, 0.0f, 0.0f, 1.0 + 1.0 / 60.0, true);
    CHECK(followed(hold, -9.6f, 0.0f, 1.0 + 2.0 / 60.0, true) == doctest::Approx(0.0f));
    CHECK(hold.held());
    CHECK(hold.update(-9.6f, 0.0f, std::numeric_limits<double>::quiet_NaN(), true) == doctest::Approx(0.0f));
    CHECK_FALSE(hold.held());
    // Nothing kept from before it: the next view cannot start one.
    CHECK(followed(hold, -20.0f, 0.0f, 1.1, true) == doctest::Approx(-20.0f));
    CHECK_FALSE(hold.held());
}

// A cut, and at once another one: the same way (it goes on: followed a view late) or back past where it came
// from (not back within 5: held to the end of the two views).
TEST_CASE("camera heading hold: a cut followed at once by a second cut, either way") {
    for (const float second : {60.0f, -60.0f}) {
        CAPTURE(second);
        CameraHeadingHold hold;
        double t = 1.0;
        const auto next = [&](float offset) {
            t += 1.0 / 60.0;
            return followed(hold, offset, 0.0f, t, true);
        };
        next(0.0f);
        next(0.0f);
        CHECK(next(30.0f) == doctest::Approx(0.0f));
        if (second > 0.0f) {
            CHECK(next(second) == doctest::Approx(second)); // onward: followed
        } else {
            CHECK(next(second) == doctest::Approx(0.0f)); // the second view of the hold
            CHECK(next(second) == doctest::Approx(second));
        }
        CHECK_FALSE(hold.held());
    }
}

// A same-way drift under kOnwardDegrees does not end a hold: a flick whose camera creeps on returns unseen.
TEST_CASE("camera heading hold: a small drift the way of the jump is not a motion") {
    CameraHeadingHold hold;
    double t = 1.0;
    const auto next = [&](float offset) {
        t += 1.0 / 60.0;
        return followed(hold, offset, 0.0f, t, true);
    };
    next(0.0f);
    next(0.0f);
    CHECK(next(-9.6f) == doctest::Approx(0.0f));
    CHECK(next(-9.61f) == doctest::Approx(0.0f));
    CHECK(next(0.0f) == doctest::Approx(0.0f));
}

TEST_CASE("camera heading hold: nothing is held after 10 s of one pickup, again after the next starts") {
    CameraHeadingHold hold;
    double t = 0.0;
    int timedOut = 0;
    for (; t < 10.5; t += 1.0 / 60.0) {
        // A view without the flag, or without the read, now and then does not restart the pickup's clock.
        const bool drop = static_cast<int>(t * 60.0) % 97 == 0;
        hold.update(0.0f, drop ? std::nullopt : std::optional<float>(0.0f), t, !drop || t < 5.0);
        timedOut += hold.timedOut() ? 1 : 0;
    }
    CHECK(timedOut == 1);
    CHECK(followed(hold, 0.0f, 0.0f, t, true) == doctest::Approx(0.0f));
    t += 1.0 / 60.0;
    CHECK(followed(hold, -9.6f, 0.0f, t, true) ==
          doctest::Approx(-9.6f)); // the flag stuck: followed as it is
    CHECK_FALSE(hold.held());
    // The pickup ends (over 0.5 s without it), the next starts: held again, and the timeout logs again later.
    for (int i = 0; i < 40; ++i) {
        t += 1.0 / 60.0;
        followed(hold, 0.0f, 0.0f, t, false);
    }
    t += 1.0 / 60.0;
    CHECK(followed(hold, -9.6f, 0.0f, t, true) == doctest::Approx(0.0f)); // its first view starts one
    CHECK(hold.held());
    int again = 0;
    for (const double end = t + 10.5; t < end; t += 1.0 / 60.0) {
        hold.update(0.0f, 0.0f, t, true);
        again += hold.timedOut() ? 1 : 0;
    }
    CHECK(again == 1);
}

// The view angles and the camera's yaw come in apart: the offset is the hold's own subtraction, and a view
// whose angles were not read keeps nothing.
TEST_CASE("camera heading hold: no view angles, outside a pickup, values that are not finite") {
    CameraHeadingHold hold;
    CHECK(hold.update(135.0f, 135.0f, 0.0, false) == doctest::Approx(0.0f));
    CHECK(hold.update(125.4f, 135.0f, 1.0 / 60.0, false) == doctest::Approx(0.0f)); // outside a pickup
    CHECK_FALSE(hold.held());
    hold.update(135.0f, 135.0f, 2.0 / 60.0, true);
    CHECK(hold.update(125.4f, std::nullopt, 3.0 / 60.0, true) == doctest::Approx(0.0f)); // not read
    CHECK_FALSE(hold.held());
    CHECK(hold.update(125.4f, 135.0f, 4.0 / 60.0, true) == doctest::Approx(0.0f)); // nothing before it
    CHECK(hold.update(std::numeric_limits<float>::quiet_NaN(), 135.0f, 5.0 / 60.0, true) ==
          doctest::Approx(0.0f));
    CHECK(hold.update(135.0f, std::numeric_limits<float>::infinity(), 6.0 / 60.0, true) ==
          doctest::Approx(0.0f));
    CHECK_FALSE(hold.held());
}
