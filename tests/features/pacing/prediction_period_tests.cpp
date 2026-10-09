#include "features/pacing/prediction_period.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <ostream>

using evr::pacing::predictionBaseMs;
using evr::pacing::predictionPeriodNs;

TEST_CASE("the runtime's own refresh period is the base when it gives one, else the measured base") {
    CHECK(predictionBaseMs(11.1, 0.0) == doctest::Approx(11.1));
    CHECK(predictionBaseMs(11.1, 13.9) == doctest::Approx(13.9)); // a refresh change the runtime reported
    CHECK(predictionBaseMs(22.2, 11.1) == doctest::Approx(11.1)); // throttled from the start
    CHECK(predictionBaseMs(0.0, 0.0) == 0.0);
    CHECK(predictionBaseMs(std::numeric_limits<double>::quiet_NaN(), -1.0) == 0.0);
}

TEST_CASE("a throttled period is held to 1.5 times the base") {
    // 90 Hz: 11.1 ms. Reported 22.2 ms (2x) and 56 ms (as in the player logs): 16.7 ms.
    CHECK(predictionPeriodNs(22'222'222, 11.111, false) == 16'666'500);
    CHECK(predictionPeriodNs(56'000'000, 11.111, false) == 16'666'500);
}

TEST_CASE("a period within the limit, or without a base, is used as it is") {
    CHECK(predictionPeriodNs(11'111'111, 11.111, false) == 11'111'111);
    CHECK(predictionPeriodNs(13'888'889, 11.111, false) == 13'888'889); // a refresh change to 72 Hz
    CHECK(predictionPeriodNs(8'333'333, 11.111, false) == 8'333'333);   // a faster refresh
    CHECK(predictionPeriodNs(22'222'222, 0.0, false) == 22'222'222);    // no base yet
    CHECK(predictionPeriodNs(0, 11.111, false) == 0);
    CHECK(predictionPeriodNs(-5, 11.111, false) == -5);
    CHECK(predictionPeriodNs(22'222'222, std::numeric_limits<double>::infinity(), false) == 22'222'222);
}

TEST_CASE("with the pose lead on the reported period is used as it is, throttled or not") {
    // The lead measures how late frames are shown against the reported period; a held period would be added
    // back by it and lag behind the next change.
    CHECK(predictionPeriodNs(22'222'222, 11.111, true) == 22'222'222);
    CHECK(predictionPeriodNs(56'000'000, 11.111, true) == 56'000'000);
    CHECK(predictionPeriodNs(11'111'111, 11.111, true) == 11'111'111);
}
