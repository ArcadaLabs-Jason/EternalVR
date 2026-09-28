#include "common/result.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <utility>

namespace {

enum class SampleError : std::uint8_t { Bad };

evr::Result<int, SampleError> parsePositive(int value) {
    if (value <= 0) {
        return evr::fail(SampleError::Bad, "not positive: " + std::to_string(value));
    }
    return value;
}

} // namespace

TEST_CASE("result holds a value") {
    const auto result = parsePositive(5);
    REQUIRE(result.ok());
    CHECK(static_cast<bool>(result));
    CHECK(*result == 5);
}

TEST_CASE("result holds an error with its message") {
    const auto result = parsePositive(-1);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == SampleError::Bad);
    CHECK(result.error().message == "not positive: -1");
}

TEST_CASE("value can be moved out") {
    evr::Result<std::string, SampleError> result{std::string("payload")};
    const std::string taken = std::move(result).value();
    CHECK(taken == "payload");
}
