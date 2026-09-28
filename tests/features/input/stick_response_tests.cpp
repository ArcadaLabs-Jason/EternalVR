#include "features/input/stick_response.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <ostream>

using evr::input::applyAxisResponse;
using evr::input::applyStickResponse;
using evr::input::Axis2;
using evr::input::kMoveStickResponse;
using evr::input::magnitude;
using evr::input::StickResponse;

TEST_CASE("inside the radial deadzone gives zero") {
    CHECK(applyStickResponse({0.1f, 0.1f}, kMoveStickResponse) == Axis2{});
    CHECK(applyStickResponse({0.0f, -0.15f}, kMoveStickResponse) == Axis2{});
}

TEST_CASE("the deadzone is radial, so a diagonal escapes it like a cardinal") {
    // 0.12 on each axis is inside a 0.15 per-axis deadzone but has magnitude 0.17.
    const Axis2 out = applyStickResponse({0.12f, 0.12f}, kMoveStickResponse);
    CHECK(out.x > 0.0f);
    CHECK(out.x == doctest::Approx(out.y));
}

TEST_CASE("output ramps from zero at the deadzone to one at the outer edge") {
    CHECK(magnitude(applyStickResponse({0.0f, 0.16f}, kMoveStickResponse)) < 0.02f);
    CHECK(magnitude(applyStickResponse({0.0f, 0.95f}, kMoveStickResponse)) == doctest::Approx(1.0f));
    CHECK(magnitude(applyStickResponse({0.0f, 1.0f}, kMoveStickResponse)) == doctest::Approx(1.0f));
    // Halfway between deadzone and edge with a linear response.
    CHECK(magnitude(applyStickResponse({0.55f, 0.0f}, kMoveStickResponse)) == doctest::Approx(0.5f));
}

TEST_CASE("direction is preserved") {
    const Axis2 out = applyStickResponse({-0.3f, 0.4f}, kMoveStickResponse);
    CHECK(out.x / out.y == doctest::Approx(-0.75f));
}

TEST_CASE("an exponent above one softens small deflections") {
    const StickResponse curved{0.0f, 1.0f, 2.0f};
    CHECK(applyAxisResponse(0.5f, curved) == doctest::Approx(0.25f));
    CHECK(applyAxisResponse(-0.5f, curved) == doctest::Approx(-0.25f));
    CHECK(applyAxisResponse(1.0f, curved) == doctest::Approx(1.0f));
}

TEST_CASE("non-finite input gives zero") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(applyStickResponse({nan, 0.5f}, kMoveStickResponse) == Axis2{});
    CHECK(applyAxisResponse(nan, kMoveStickResponse) == 0.0f);
}

TEST_CASE("a centred stick gives zero whatever the response") {
    // A negative deadzone once made a centred stick divide zero by zero.
    const StickResponse negative{-0.1f, 0.95f, 1.0f};
    CHECK(applyStickResponse({0.0f, 0.0f}, negative) == Axis2{});
    CHECK(applyAxisResponse(0.0f, negative) == 0.0f);
}

TEST_CASE("unusable responses fall back") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const StickResponse fallback{0.15f, 0.95f, 1.0f};
    for (const StickResponse& bad :
         {StickResponse{-0.1f, 0.95f, 1.0f}, StickResponse{nan, 0.95f, 1.0f}, StickResponse{0.5f, 0.4f, 1.0f},
          StickResponse{0.1f, 1.5f, 1.0f}, StickResponse{0.1f, 0.9f, 0.0f}, StickResponse{0.1f, 0.9f, nan}}) {
        const StickResponse result = evr::input::sanitizedResponse(bad, fallback);
        CHECK(result.deadzone == fallback.deadzone);
        CHECK(result.outerEdge == fallback.outerEdge);
        CHECK(result.exponent == fallback.exponent);
    }
    const StickResponse good{0.1f, 0.9f, 2.0f};
    CHECK(evr::input::sanitizedResponse(good, fallback).exponent == 2.0f);
}

TEST_CASE("a response starting from a later deflection ramps from zero there") {
    const StickResponse later = evr::input::startingFrom(kMoveStickResponse, 0.35f);
    CHECK(applyAxisResponse(0.35f, later) == 0.0f);
    CHECK(applyAxisResponse(0.36f, later) < 0.02f);
    CHECK(applyAxisResponse(0.95f, later) == doctest::Approx(1.0f));
    // An earlier start changes nothing.
    CHECK(evr::input::startingFrom(kMoveStickResponse, 0.05f).deadzone == kMoveStickResponse.deadzone);
    // A start beyond the outer edge still leaves a ramp.
    const StickResponse late = evr::input::startingFrom({0.1f, 0.5f, 1.0f}, 0.8f);
    CHECK(late.outerEdge > late.deadzone);
}
