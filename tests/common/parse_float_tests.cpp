#include "common/parse_float.hpp"

#include <doctest/doctest.h>

#include <ostream>

using evr::parseFloat;

TEST_CASE("plain decimal numbers are read") {
    CHECK(parseFloat("0") == 0.0f);
    CHECK(parseFloat("-0.25") == -0.25f);
    CHECK(parseFloat("+45") == 45.0f);
    CHECK(parseFloat("230.5") == 230.5f);
    CHECK(parseFloat("1e2") == 100.0f);
    CHECK(parseFloat(".5") == 0.5f);
}

TEST_CASE("anything that is not the whole of a finite decimal number is refused") {
    CHECK_FALSE(parseFloat("").has_value());
    CHECK_FALSE(parseFloat(" 1").has_value());
    CHECK_FALSE(parseFloat("1 ").has_value());
    CHECK_FALSE(parseFloat("1.2.3").has_value());
    CHECK_FALSE(parseFloat("fast").has_value());
    CHECK_FALSE(parseFloat("nan").has_value());
    CHECK_FALSE(parseFloat("inf").has_value());
    CHECK_FALSE(parseFloat("0x10").has_value());
    CHECK_FALSE(parseFloat("1e99").has_value());
    CHECK_FALSE(parseFloat("-").has_value());
}

TEST_CASE("fractions and exponents are read exactly enough for settings") {
    CHECK(*parseFloat("0.1") == doctest::Approx(0.1f));
    CHECK(*parseFloat("-1.5e-3") == doctest::Approx(-0.0015f));
    CHECK(*parseFloat("2.5E+1") == 25.0f);
    CHECK_FALSE(parseFloat("1e").has_value());
    CHECK_FALSE(parseFloat("1,5").has_value());
    CHECK_FALSE(parseFloat("+").has_value());
    CHECK_FALSE(parseFloat(".").has_value());
}
