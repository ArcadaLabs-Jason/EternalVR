#include "stereo_seq/debug_cvars.hpp"
#include "stereo_seq/seq_settings.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using evr::stereo_seq::CvarHold;
using evr::stereo_seq::debugCvarNames;
using evr::stereo_seq::debugCvarsTakeSsr;
using evr::stereo_seq::findCvarName;
using evr::stereo_seq::replaceSameName;

TEST_CASE("debug cvars: the names the list holds, not a name=? item or a cap that does not parse") {
    const auto names = debugCvarNames(" r_SSR = 0 ;r_lodScale=?;r_bad=<=x;r_cap=<=0.5;r_TAASafeMode=1;=2");
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "r_SSR");
    CHECK(names[1] == "r_cap");
    CHECK(names[2] == "r_TAASafeMode");
    CHECK(debugCvarNames("").empty());
    CHECK(debugCvarNames("r_lodScale=?").empty());
    // A later item replaces an earlier one (parseCvarList): a query after a value takes nothing.
    CHECK(debugCvarNames("r_SSR=0;r_SSR=?").empty());
}

TEST_CASE("debug cvars: a name is found whatever its case, and only the whole name") {
    const auto names = debugCvarNames("r_SSR=0;r_TAAAntiGhosting=1");
    REQUIRE(findCvarName(names, "R_ssr").has_value());
    CHECK(*findCvarName(names, "R_ssr") == 0);
    CHECK(*findCvarName(names, "r_taaantighosting") == 1);
    CHECK_FALSE(findCvarName(names, "r_SSRQuality").has_value());
    CHECK_FALSE(findCvarName(names, "r_SS").has_value());
    CHECK_FALSE(findCvarName(names, "r_lodScale").has_value());
}

TEST_CASE("debug cvars: r_SSR or the reflections' upscale quality in the list stops the r_SSR hold") {
    CHECK(debugCvarsTakeSsr(debugCvarNames("r_SSR=0")));
    CHECK(debugCvarsTakeSsr(debugCvarNames("r_lodScale=1;R_SSR=1")));
    CHECK(debugCvarsTakeSsr(debugCvarNames("r_raytracedReflectionsTemporalUpscaleQuality=2")));
    CHECK_FALSE(debugCvarsTakeSsr(debugCvarNames("")));
    CHECK_FALSE(debugCvarsTakeSsr(debugCvarNames("r_SSR=?")));
    CHECK_FALSE(debugCvarsTakeSsr(debugCvarNames("r_SSDO=0;r_SSRQuality=1")));
}

TEST_CASE("debug cvars: an entry replaces every entry for the same cvar, in the first one's place") {
    std::vector<CvarHold> held = {{"r_SSDO", "1"}, {"r_x", "2"}, {"R_SSDO", "0"}, {"r_y", "3"}};
    replaceSameName(held, CvarHold{"r_ssdo", "0"});
    REQUIRE(held.size() == 3);
    CHECK(held[0].name == "r_ssdo");
    CHECK(held[0].value == "0");
    CHECK(held[1].name == "r_x");
    CHECK(held[2].name == "r_y");
    // A cvar no entry holds is added at the end.
    replaceSameName(held, CvarHold{"r_SSR", "0"});
    REQUIRE(held.size() == 4);
    CHECK(held[3].name == "r_SSR");
    CHECK(held[1].value == "2");
    std::vector<CvarHold> none;
    replaceSameName(none, CvarHold{"r_SSR", "1"});
    REQUIRE(none.size() == 1);
    CHECK(none[0].value == "1");
}
