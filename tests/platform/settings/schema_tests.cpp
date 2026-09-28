#include "platform/settings/schema.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>

using evr::settings::Schema;
using evr::settings::SettingDef;
using evr::settings::SettingType;
using evr::settings::SettingValue;
using evr::settings::ValidationStatus;

namespace {

Schema sampleSchema() {
    Schema schema;
    schema.add({"a.float", SettingType::Float, 0.5, 0.0, 1.0, {}, true, "A float."});
    schema.add({"a.int", SettingType::Int, std::int64_t{10}, 5.0, 20.0, {}, false, "An int."});
    schema.add({"a.enum", SettingType::Enum, std::string("one"), {}, {}, {"one", "two"}, false, "An enum."});
    schema.add({"a.bool", SettingType::Bool, false, {}, {}, {}, false, "A bool."});
    schema.add({"a.string", SettingType::String, std::string(), {}, {}, {}, false, "A string."});
    return schema;
}

} // namespace

TEST_CASE("schema lists and finds its keys") {
    const Schema schema = sampleSchema();
    CHECK(schema.keys().size() == 5);
    REQUIRE(schema.find("a.float") != nullptr);
    CHECK(schema.find("a.float")->liveTunable);
    CHECK(schema.find("missing") == nullptr);
}

TEST_CASE("schema refuses duplicate keys and invalid defaults") {
    Schema schema = sampleSchema();
    CHECK_FALSE(schema.add({"a.bool", SettingType::Bool, true, {}, {}, {}, false, ""}));
    // Default outside its own range.
    CHECK_FALSE(schema.add({"b.int", SettingType::Int, std::int64_t{99}, 0.0, 10.0, {}, false, ""}));
    // Default of the wrong type.
    CHECK_FALSE(schema.add({"b.bool", SettingType::Bool, std::int64_t{1}, {}, {}, {}, false, ""}));
    // Enum default not among the choices.
    CHECK_FALSE(schema.add({"b.enum", SettingType::Enum, std::string("x"), {}, {}, {"y"}, false, ""}));
    CHECK(schema.find("b.int") == nullptr);
    CHECK(schema.keys().size() == 5);
}

TEST_CASE("in-range values validate unchanged") {
    const Schema schema = sampleSchema();
    const auto validation = schema.validate("a.float", 0.25);
    CHECK(validation.status == ValidationStatus::Ok);
    CHECK(validation.accepted());
    CHECK(validation.value == SettingValue{0.25});
    CHECK(validation.message.empty());
}

TEST_CASE("out-of-range numbers are clamped and reported") {
    const Schema schema = sampleSchema();

    const auto high = schema.validate("a.float", 1.5);
    CHECK(high.status == ValidationStatus::Clamped);
    CHECK(high.accepted());
    CHECK(high.value == SettingValue{1.0});
    CHECK(high.message.find("a.float") != std::string::npos);

    const auto low = schema.validate("a.int", std::int64_t{1});
    CHECK(low.status == ValidationStatus::Clamped);
    CHECK(low.value == SettingValue{std::int64_t{5}});

    const auto huge = schema.validate("a.int", std::numeric_limits<std::int64_t>::max());
    CHECK(huge.value == SettingValue{std::int64_t{20}});
}

TEST_CASE("integers are accepted for float settings") {
    const Schema schema = sampleSchema();
    const auto validation = schema.validate("a.float", std::int64_t{1});
    CHECK(validation.status == ValidationStatus::Ok);
    CHECK(validation.value == SettingValue{1.0});
}

TEST_CASE("invalid values are rejected with a reason") {
    const Schema schema = sampleSchema();
    CHECK(schema.validate("nope", true).status == ValidationStatus::UnknownKey);
    CHECK(schema.validate("a.bool", std::string("true")).status == ValidationStatus::TypeMismatch);
    CHECK(schema.validate("a.int", 12.5).status == ValidationStatus::TypeMismatch);
    CHECK(schema.validate("a.enum", std::string("three")).status == ValidationStatus::InvalidChoice);
    CHECK(schema.validate("a.float", std::nan("")).status == ValidationStatus::NotFinite);
    CHECK(schema.validate("a.float", std::numeric_limits<double>::infinity()).status ==
          ValidationStatus::NotFinite);

    const auto rejected = schema.validate("a.enum", std::string("three"));
    CHECK_FALSE(rejected.accepted());
    CHECK_FALSE(rejected.message.empty());
}

TEST_CASE("whole-number floats are accepted for int settings") {
    const Schema schema = sampleSchema();
    const auto whole = schema.validate("a.int", 12.0);
    CHECK(whole.status == ValidationStatus::Ok);
    CHECK(whole.value == SettingValue{std::int64_t{12}});

    const auto clamped = schema.validate("a.int", 45.0);
    CHECK(clamped.status == ValidationStatus::Clamped);
    CHECK(clamped.value == SettingValue{std::int64_t{20}});

    CHECK(schema.validate("a.int", -0.0).status == ValidationStatus::Clamped);
    CHECK(schema.validate("a.int", 45.5).status == ValidationStatus::TypeMismatch);
    CHECK(schema.validate("a.int", std::nan("")).status == ValidationStatus::TypeMismatch);
    CHECK(schema.validate("a.int", std::numeric_limits<double>::infinity()).status ==
          ValidationStatus::TypeMismatch);
    // Whole, but beyond any 64-bit integer.
    CHECK(schema.validate("a.int", 1e30).status == ValidationStatus::TypeMismatch);
}
