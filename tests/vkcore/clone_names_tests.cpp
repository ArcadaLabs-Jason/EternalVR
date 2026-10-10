#include "vkcore/clone_names.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace cn = evr::vkcore::clone_names;

TEST_CASE("a clone is named after its engine image, lower-cased, the same in every build") {
    std::vector<std::string> first;
    CHECK(cn::stableName("_frontColor", "0x66E2F78", first) == "_evrview1__frontcolor");
    CHECK(cn::stableName("_dc0_dofAccBufferFar0", "dc1+0x568.c0", first) ==
          "_evrview1__dc0_dofaccbufferfar0");
    std::vector<std::string> second;
    CHECK(cn::stableName("_frontColor", "0x66E2F78", second) == first[0]);
    CHECK(first.size() == 2);
    CHECK(second.size() == 1);
}

TEST_CASE("characters an image name should not hold become _") {
    CHECK(cn::plain("Textures/A b.C-1") == "textures_a_b_c_1");
    CHECK(cn::plain("") == "");
    std::vector<std::string> used;
    CHECK(cn::stableName("_viewColorScaled%d", "0x66E3210.c0", used) == "_evrview1__viewcolorscaled_d");
}

TEST_CASE("a second image of the same name in one build gets a suffix, deterministically") {
    std::vector<std::string> used;
    CHECK(cn::stableName("_water", "0x66E30F0", used) == "_evrview1__water");
    CHECK(cn::stableName("_WATER", "0x66E30F8", used) == "_evrview1__water_2");
    CHECK(cn::stableName("_water", "0x66E3100", used) == "_evrview1__water_3");
    // An engine name that looks like a suffixed one still gets a name of its own.
    CHECK(cn::stableName("_water_2", "0x66E3108", used) == "_evrview1__water_2_2");
    CHECK(used.size() == 4);
}

TEST_CASE("no engine name, or one too long for the engine, takes the source") {
    std::vector<std::string> used;
    CHECK(cn::stableName("", "dc1+0x2F8", used) == "_evrview1_dc1_0x2f8");
    const std::string longName(300, 'a');
    const std::string name = cn::stableName(longName, "0x66E3400.c1", used);
    CHECK(name == "_evrview1_0x66e3400_c1");
    // A source too long as well is cut, with room left for a suffix.
    const std::string cut = cn::stableName("", longName, used);
    CHECK(cut.size() == cn::kMaxLength - cn::kSuffixRoom);
    CHECK(cn::stableName("", longName, used).size() == cn::kMaxLength - cn::kSuffixRoom + 2);
    for (const std::string& n : used) {
        CHECK(n.size() < 256);
    }
}

TEST_CASE("a clone beside the one of its name, and a spare, never take a stable name") {
    std::vector<std::string> used;
    const std::string stable = cn::stableName("_frontColor", "0x66E2F78", used);
    CHECK(cn::besideName(stable) == "_evrview1b__frontcolor");
    // An engine image named "b_frontcolor" gets "_evrview1_b_frontcolor", not the beside name.
    CHECK(cn::stableName("b_frontColor", "0x66E2F80", used) == "_evrview1_b_frontcolor");
    CHECK(cn::besideName("other") == "_evrview1b_other");
    CHECK(cn::spareName(3, 17) == "_evrview1x_3_17");
    CHECK(cn::ours(stable));
    CHECK(cn::ours(cn::besideName(stable)));
    CHECK(cn::ours(cn::spareName(0, 0)));
    CHECK(cn::ours("_evrview1_0_5")); // ETERNALVR_TEST_VIEW_CLONE_NAMES=build, as the engine keeps it
    CHECK_FALSE(cn::ours("_frontcolor"));
    CHECK_FALSE(cn::ours("_evrview1"));
    CHECK_FALSE(cn::ours("_evrview"));
    CHECK_FALSE(cn::ours(""));
}

TEST_CASE("the names a build no longer gives are its orphans") {
    const std::vector<std::string> before = {"a", "rt0", "b", "rt1"};
    const std::vector<std::string> now = {"b", "a", "c"};
    CHECK(cn::dropped(before, now) == std::vector<std::string>{"rt0", "rt1"});
    CHECK(cn::dropped(now, now).empty());
    CHECK(cn::dropped({}, now).empty());
}
