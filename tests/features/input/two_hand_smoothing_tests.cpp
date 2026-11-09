// Two-handed aim through the player's aim smoothing (features/input/two_hand.hpp, aim_smoothing.hpp).

#include "features/input/aim_smoothing.hpp"
#include "features/input/two_hand.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

using evr::Quat;
using evr::Vec3;
using evr::input::aimSmoothingParams;
using evr::input::kDefaultAimSmoothing;
using evr::input::OneEuroRotation;
using evr::input::SupportRow;
using evr::input::TwoHandBlock;
using evr::input::TwoHandFrame;
using evr::input::TwoHandHold;
using evr::input::TwoHandResult;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDt = 1.0f / 90.0f;

// The Rocket Launcher's row (data/weapons/viewmodel_offsets.toml): the shortest line, the most jitter.
constexpr SupportRow kRocket{0.163f, 0.003f, -0.004f};
constexpr Vec3 kGrip{0.2f, -0.3f, -0.4f};

float degrees(float radians) {
    return radians * 180.0f / kPi;
}

Vec3 forward(Quat q) {
    return evr::rotate(q, {0.0f, 0.0f, -1.0f});
}

float rayDegrees(Quat a, Quat b) {
    const Vec3 u = forward(a);
    const Vec3 v = forward(b);
    return degrees(std::atan2(evr::length(evr::cross(u, v)), evr::dot(u, v)));
}

TwoHandFrame frame(Vec3 grip, Vec3 offGrip, float squeeze) {
    TwoHandFrame f;
    f.block = TwoHandBlock::None;
    f.offGrip = squeeze;
    f.weaponTracked = true;
    f.offTracked = true;
    f.weaponGrip = grip;
    f.offGripPoint = offGrip;
    f.support = kRocket;
    f.dt = kDt;
    return f;
}

TwoHandHold smoothed() {
    return TwoHandHold(evr::input::kGripThresholds, aimSmoothingParams(kDefaultAimSmoothing));
}

double rms(const std::vector<float>& v) {
    double sum = 0.0;
    for (const float x : v) {
        sum += static_cast<double>(x) * x;
    }
    return std::sqrt(sum / static_cast<double>(v.size()));
}

} // namespace

TEST_CASE(
    "two-handed aim with aim smoothing: a still hold's grip noise is smoothed as the one-handed aim's") {
    // Both grips with 0.3 mm of noise on each axis, the off hand on the Rocket Launcher's support point: the
    // held aim's ray against the noiseless one, held without and with the default smoothing (Medium).
    const Vec3 support = evr::input::supportPoint(kGrip, Quat{}, kRocket);
    const auto run = [&](TwoHandHold hold) {
        std::mt19937 rng(7);
        std::normal_distribution<float> noise(0.0f, 0.0003f);
        const auto jitter = [&] {
            return Vec3{noise(rng), noise(rng), noise(rng)};
        };
        hold.update(frame(kGrip, support, 0.0f));
        REQUIRE(hold.update(frame(kGrip, support, 1.0f)).engaged);
        std::vector<float> off;
        std::vector<Quat> held;
        for (int i = 0; i < 900; ++i) {
            const TwoHandResult r = hold.update(frame(kGrip + jitter(), support + jitter(), 1.0f));
            REQUIRE(r.engaged);
            if (i >= 90) { // past the grab's ease and the filter's start
                off.push_back(rayDegrees(r.aim, Quat{}));
                held.push_back(r.aim);
            }
        }
        return std::pair{rms(off), held};
    };
    const auto [raw, rawHeld] = run(TwoHandHold());
    const auto [smooth, smoothHeld] = run(smoothed());
    MESSAGE("held aim jitter RMS: unsmoothed " << raw << " deg, smoothed " << smooth << " deg");
    CHECK(raw > 0.05);
    CHECK(smooth < 0.6 * raw);
    // The same filter as the one-handed aim's: the unsmoothed held aims through a OneEuroRotation with the
    // same setting give the smoothed ones (after its start).
    OneEuroRotation filter(*aimSmoothingParams(kDefaultAimSmoothing));
    double t = 0.0;
    float worst = 0.0f;
    for (std::size_t i = 0; i < rawHeld.size(); ++i) {
        t += kDt;
        const Quat f = filter.update(rawHeld[i], t);
        if (i >= 90) {
            worst = std::fmax(worst, rayDegrees(f, smoothHeld[i]));
        }
    }
    CHECK(worst < 0.01f);
}

TEST_CASE("two-handed aim with aim smoothing: a moving line is followed with the filter's small lag") {
    // The off hand swung round the weapon hand's grip at 57 degrees per second: the held aim trails the line
    // by a fraction of a degree (the one-handed filter's 8 ms at that speed), never by much.
    const auto offAt = [&](float yaw) {
        const Quat turn = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, yaw);
        return kGrip + evr::rotate(turn, evr::input::supportPoint(kGrip, Quat{}, kRocket) - kGrip);
    };
    TwoHandHold hold = smoothed();
    TwoHandHold raw;
    hold.update(frame(kGrip, offAt(0.0f), 0.0f));
    raw.update(frame(kGrip, offAt(0.0f), 0.0f));
    REQUIRE(hold.update(frame(kGrip, offAt(0.0f), 1.0f)).engaged);
    REQUIRE(raw.update(frame(kGrip, offAt(0.0f), 1.0f)).engaged);
    const float rate = 1.0f; // radians per second
    float lag = 0.0f;
    for (int i = 1; i <= 45; ++i) {
        const float yaw = rate * kDt * static_cast<float>(i);
        const TwoHandResult a = hold.update(frame(kGrip, offAt(yaw), 1.0f));
        const TwoHandResult b = raw.update(frame(kGrip, offAt(yaw), 1.0f));
        REQUIRE(a.engaged);
        if (i > 20) {
            lag = std::fmax(lag, rayDegrees(a.target, b.target));
        }
    }
    MESSAGE("lag behind the line at 57 deg/s: " << lag << " deg (" << lag / 57.3f * 1000.0f << " ms)");
    CHECK(lag > 0.05f);
    CHECK(lag < 1.0f);
}

TEST_CASE("two-handed aim with aim smoothing: each grab starts on the line itself") {
    // A hold on one side of the gun, let go, and a grab 0.1 s later on the other side: the grab's first
    // frame is the new line's own aim, not drawn toward the last hold's.
    const Vec3 support = evr::input::supportPoint(kGrip, Quat{}, kRocket);
    const Vec3 left = support + Vec3{-0.035f, 0.0f, 0.0f};
    const Vec3 right = support + Vec3{0.035f, 0.0f, 0.0f};
    TwoHandHold hold = smoothed();
    TwoHandHold raw;
    for (TwoHandHold* h : {&hold, &raw}) {
        h->update(frame(kGrip, left, 0.0f));
        REQUIRE(h->update(frame(kGrip, left, 1.0f)).engaged);
        for (int i = 0; i < 30; ++i) {
            h->update(frame(kGrip, left, 1.0f));
        }
        for (int i = 0; i < 9; ++i) {
            CHECK_FALSE(h->update(frame(kGrip, right, 0.0f)).engaged);
        }
    }
    const TwoHandResult a = hold.update(frame(kGrip, right, 1.0f));
    const TwoHandResult b = raw.update(frame(kGrip, right, 1.0f));
    REQUIRE(a.started);
    REQUIRE(b.started);
    CHECK(rayDegrees(a.target, b.target) < 0.001f);
}

TEST_CASE("two-handed aim: built from the weapon hand's aim as tracked, not from its smoothed aim") {
    // The one-handed aim (smoothed) trails the tracked aim by 3 degrees of roll and 2 of yaw: the held aim's
    // roll is the tracked aim's (smoothed once by the hold), and its ray the line's either way.
    const Vec3 support = evr::input::supportPoint(kGrip, Quat{}, kRocket);
    const Quat tracked = Quat{};
    const Quat trailing = evr::normalize(Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 2.0f * kPi / 180.0f) *
                                         Quat::fromAxisAngle({0.0f, 0.0f, -1.0f}, 3.0f * kPi / 180.0f));
    TwoHandHold hold;
    TwoHandFrame f = frame(kGrip, support, 0.0f);
    f.aim = trailing;
    f.trackedAim = tracked;
    hold.update(f);
    f.offGrip = 1.0f;
    TwoHandResult r = hold.update(f);
    REQUIRE(r.engaged);
    for (int i = 0; i < 20; ++i) {
        r = hold.update(f);
    }
    // On the support point with the tracked aim: no turn at all, the held aim is the tracked aim.
    CHECK(rayDegrees(r.aim, tracked) < 0.01f);
    const Vec3 up = evr::rotate(r.aim, {0.0f, 1.0f, 0.0f});
    CHECK(degrees(std::atan2(evr::length(evr::cross(up, Vec3{0.0f, 1.0f, 0.0f})), up.y)) < 0.01f);
}
