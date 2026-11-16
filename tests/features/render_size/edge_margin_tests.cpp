#include "features/render_size/edge_margin.hpp"

#include <doctest/doctest.h>

#include <string>

using evr::render_size::describeEdgeMargin;
using evr::render_size::kEdgeMarginLarge;
using evr::render_size::kEdgeMarginMedium;
using evr::render_size::kEdgeMarginSmall;
using evr::render_size::parseEdgeMargin;

TEST_CASE("edge margin setting: off, the launcher's sizes and a rig's percentage") {
    CHECK(*parseEdgeMargin(L"") == 1.0f);
    CHECK(*parseEdgeMargin(L" Off ") == 1.0f);
    CHECK(*parseEdgeMargin(L"None") == 1.0f);
    CHECK(*parseEdgeMargin(L"0") == 1.0f);
    CHECK(*parseEdgeMargin(L"small") == kEdgeMarginSmall);
    CHECK(*parseEdgeMargin(L"Medium") == kEdgeMarginMedium);
    CHECK(*parseEdgeMargin(L" LARGE") == kEdgeMarginLarge);
    CHECK(*parseEdgeMargin(L"15") == doctest::Approx(1.15f));
    CHECK(*parseEdgeMargin(L"1") == doctest::Approx(1.01f));
    CHECK(*parseEdgeMargin(L"50") == doctest::Approx(1.5f));
}

TEST_CASE("edge margin setting: anything else is refused") {
    CHECK_FALSE(parseEdgeMargin(L"51"));
    CHECK_FALSE(parseEdgeMargin(L"100"));
    CHECK_FALSE(parseEdgeMargin(L"-5"));
    CHECK_FALSE(parseEdgeMargin(L"1.2"));
    CHECK_FALSE(parseEdgeMargin(L"huge"));
    CHECK_FALSE(parseEdgeMargin(L"10%"));
}

TEST_CASE("edge margin setting: the launcher's sizes grow as the setting's text says") {
    // About 2.5, 5 and 7 degrees on a Quest 3 (xr_math edge_margin_tests), for 21, 44 and 69 % more pixels.
    CHECK(kEdgeMarginSmall * kEdgeMarginSmall == doctest::Approx(1.21f));
    CHECK(kEdgeMarginMedium * kEdgeMarginMedium == doctest::Approx(1.44f));
    CHECK(kEdgeMarginLarge * kEdgeMarginLarge == doctest::Approx(1.69f));
}

TEST_CASE("edge margin setting: described for the log") {
    CHECK(describeEdgeMargin(1.0f) == "off");
    CHECK(describeEdgeMargin(kEdgeMarginMedium) == "x1.20 (medium)");
    CHECK(describeEdgeMargin(*parseEdgeMargin(L"10")) == "x1.10 (small)");
    CHECK(describeEdgeMargin(*parseEdgeMargin(L"15")) == "x1.15");
}
