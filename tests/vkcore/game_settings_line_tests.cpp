#include "vkcore/game_settings_line.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace gs = evr::vkcore::game_settings;
using gs::CvarKind;
using gs::LineSchedule;
using gs::SettingValue;

namespace {

SettingValue value(std::string_view name, CvarKind kind, int integer, float number = 0.0f) {
    SettingValue v;
    v.name = name;
    v.kind = kind;
    v.integer = integer;
    v.number = number;
    return v;
}

SettingValue missing(std::string_view name) {
    SettingValue v = value(name, CvarKind::Int, 0);
    v.state = SettingValue::State::NotFound;
    return v;
}

} // namespace

TEST_CASE("the game settings line names ray tracing, DLSS and the field of view, each once") {
    std::set<std::string> names;
    for (const gs::SettingCvar& c : gs::settingCvars()) {
        CHECK(names.insert(c.name).second);
    }
    const std::vector<std::string> wanted = {"r_enableRayTracing",
                                             "r_raytracedReflections",
                                             "r_antialiasing",
                                             "r_dlssQuality",
                                             "is_poolSize",
                                             "r_lightScatteringQuality",
                                             "r_motionBlurQuality",
                                             "r_dof",
                                             "r_chromaticAberration",
                                             "r_filmGrainRatio",
                                             "r_sharpening",
                                             "rs_enable",
                                             "r_hdrDisplay",
                                             "r_swapInterval",
                                             "g_fov"};
    for (const std::string& name : wanted) {
        CHECK(names.count(name) == 1);
    }
    CHECK(std::string(gs::settingCvars().front().name) == "r_enableRayTracing");
}

TEST_CASE("values are written as their cvar's type") {
    CHECK(gs::formatValue(CvarKind::Bool, 1, 0.0f) == "1");
    CHECK(gs::formatValue(CvarKind::Bool, 7, 0.0f) == "1");
    CHECK(gs::formatValue(CvarKind::Bool, 0, 1.0f) == "0");
    CHECK(gs::formatValue(CvarKind::Int, 4608, 0.0f) == "4608");
    CHECK(gs::formatValue(CvarKind::Int, -1, 0.0f) == "-1");
    CHECK(gs::formatValue(CvarKind::Float, 0, 2.5f) == "2.5");
    CHECK(gs::formatValue(CvarKind::Float, 0, 0.25f) == "0.25");
    CHECK(gs::formatValue(CvarKind::Float, 1, 16.0f) == "16");
    CHECK(gs::formatValue(CvarKind::Float, 1, 1.99f) == "1.99");
    CHECK(gs::formatValue(CvarKind::Float, 0, 15.833332f) == "15.8333");
}

TEST_CASE("the line lists the found cvars in order and marks an unreadable one") {
    SettingValue unreadable = value("r_lodScale", CvarKind::Float, 0);
    unreadable.state = SettingValue::State::Unreadable;
    const std::vector<SettingValue> values = {
        value("r_enableRayTracing", CvarKind::Bool, 1),   missing("r_dlssQuality"),
        value("is_poolSize", CvarKind::Int, 4608),        unreadable,
        value("r_sharpening", CvarKind::Float, 1, 1.99f), value("g_fov", CvarKind::Int, 110),
    };
    CHECK(gs::formatLine(values) ==
          "r_enableRayTracing 1, is_poolSize 4608, r_lodScale ?, r_sharpening 1.99, g_fov 110");
    CHECK(gs::notFoundList(values) == "r_dlssQuality");
    CHECK(gs::formatLine({missing("a"), missing("b")}).empty());
    CHECK(gs::notFoundList({missing("a"), missing("b")}) == "a, b");
    CHECK(gs::notFoundList({value("a", CvarKind::Int, 1)}).empty());
}

TEST_CASE("the line is read a few seconds after each map load and logged even when unchanged") {
    LineSchedule s;
    CHECK_FALSE(s.due(0, 1000));
    CHECK_FALSE(s.due(1, 2000)); // the main menu loads
    CHECK_FALSE(s.due(1, 2000 + LineSchedule::kSettleMs - 1));
    REQUIRE(s.due(1, 2000 + LineSchedule::kSettleMs));
    CHECK(s.shouldLog("r_enableRayTracing 1"));
    CHECK_FALSE(s.due(1, 2000 + LineSchedule::kSettleMs + 1));

    const std::uint64_t load = 100000;
    CHECK_FALSE(s.due(2, load));
    REQUIRE(s.due(2, load + LineSchedule::kSettleMs));
    CHECK(s.shouldLog("r_enableRayTracing 1")); // a map load logs the line again
}

TEST_CASE("a map load during the wait restarts it") {
    LineSchedule s;
    CHECK_FALSE(s.due(1, 0));
    CHECK_FALSE(s.due(2, LineSchedule::kSettleMs - 100));
    CHECK_FALSE(s.due(2, LineSchedule::kSettleMs));
    CHECK(s.due(2, 2 * LineSchedule::kSettleMs - 100));
}

TEST_CASE("in a map the line is checked every few seconds and logged only when a value changed") {
    LineSchedule s;
    CHECK_FALSE(s.due(1, 0));
    std::uint64_t now = LineSchedule::kSettleMs;
    REQUIRE(s.due(1, now));
    CHECK(s.shouldLog("r_enableRayTracing 1"));
    CHECK_FALSE(s.due(1, now + LineSchedule::kCheckMs - 1));
    now += LineSchedule::kCheckMs;
    REQUIRE(s.due(1, now));
    CHECK_FALSE(s.shouldLog("r_enableRayTracing 1"));
    now += LineSchedule::kCheckMs;
    REQUIRE(s.due(1, now));
    CHECK(s.shouldLog("r_enableRayTracing 0"));
    now += LineSchedule::kCheckMs;
    REQUIRE(s.due(1, now));
    CHECK_FALSE(s.shouldLog("r_enableRayTracing 0"));
}

TEST_CASE("without a map load the line is read once after the fallback wait") {
    LineSchedule s;
    CHECK_FALSE(s.due(0, 500));
    CHECK_FALSE(s.due(0, 500 + LineSchedule::kFallbackMs - 1));
    REQUIRE(s.due(0, 500 + LineSchedule::kFallbackMs));
    CHECK(s.shouldLog("g_fov 90"));
    CHECK_FALSE(s.due(0, 500 + LineSchedule::kFallbackMs + 1));
    CHECK(s.due(0, 500 + LineSchedule::kFallbackMs + LineSchedule::kCheckMs));
}
