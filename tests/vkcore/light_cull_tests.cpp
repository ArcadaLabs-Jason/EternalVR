#include "vkcore/light_cull.hpp"
#include "vkcore/light_cull_builds.hpp"

#include <doctest/doctest.h>

#include <ostream>
#include <string_view>
#include <vector>

using evr::vkcore::lightFrustumWanted;
namespace lc = evr::vkcore::light_cull;

TEST_CASE("the light frustum fix is on unless ETERNALVR_LIGHT_FRUSTUM is 0") {
    CHECK(lightFrustumWanted(false, L"")); // unset
    CHECK_FALSE(lightFrustumWanted(true, L"0"));
    CHECK(lightFrustumWanted(true, L"1"));
    CHECK(lightFrustumWanted(true, L""));    // set but empty
    CHECK(lightFrustumWanted(true, L"off")); // only "0" turns it off
    CHECK(lightFrustumWanted(true, L"00"));
    CHECK(lightFrustumWanted(true, L" 0"));
}

TEST_CASE("the light gather's build is picked by the game's PE timestamp") {
    const lc::Build* steam = lc::findBuild(0x6A7B9B8C);
    REQUIRE(steam != nullptr);
    CHECK(std::string_view(steam->name) == "Steam build 25216728");
    CHECK(steam->anchorRva == 0x1C7851E);

    const lc::Build* gamePass = lc::findBuild(0x69BC663D);
    REQUIRE(gamePass != nullptr);
    CHECK(std::string_view(gamePass->name) == "Game Pass 1.0.56.0");
    CHECK(gamePass->anchorRva == 0x1D04AAE);
    // The Game Pass gather is the Steam one 0x8C590 further on.
    CHECK(gamePass->anchorRva - steam->anchorRva == 0x8C590);

    // Any other build: none, so the game keeps its own culling.
    CHECK(lc::findBuild(0) == nullptr);
    CHECK(lc::findBuild(0x6A7B9B8D) == nullptr);
    CHECK(lc::findBuild(0x69BC663C) == nullptr);
    CHECK(lc::findBuild(0xFFFFFFFF) == nullptr);
}

TEST_CASE("the Steam row keeps the values it was read with") {
    const lc::Build* steam = lc::findBuild(0x6A7B9B8C);
    REQUIRE(steam != nullptr);
    CHECK(std::string_view(steam->signature) ==
          "48 8B 47 18 80 78 24 00 0F 84 3F 04 00 00 8B 53 54 85 D2 0F 84");
    const lc::GatherOffsets& o = steam->at;
    CHECK(o.gateA == 0x23);
    CHECK(o.gateB == 0x2C9);
    CHECK(o.sphere == 0x432);
    CHECK(o.gateBDrop == 0x43C);
    CHECK(o.skipCheck == 0x44D);
    CHECK(o.frustum == 0x453);
    CHECK(o.verdict == 0x4A5);
    CHECK(o.keep == 0x4AD);
    CHECK(o.frustumDrop == 0x2B);
    REQUIRE(steam->checks.size() == 9);
    CHECK(steam->checks[0].offset == 0x23);
    CHECK(steam->checks[0].size == 8);
    CHECK(steam->checks[0].bytes[3] == 0x85);
    CHECK(steam->checks[6].bytes[4] == 0x7E); // the frustum verdict's jne rel32
}

TEST_CASE("every offset each build hooks or resumes at has a byte check") {
    for (const lc::Build& build : lc::kBuilds) {
        INFO(build.name);
        CHECK_FALSE(lc::uncheckedOffset(build).has_value());
        CHECK(lc::findBuild(build.timestamp) == &build); // no earlier row with the same timestamp
        CHECK(std::string_view(build.signature).size() > 0);
        for (const lc::ByteCheck& check : build.checks) {
            CHECK(check.size > 0);
            CHECK(check.size <= sizeof(check.bytes));
        }
    }
}

TEST_CASE("a build row with an offset left unchecked is caught") {
    const lc::Build* gamePass = lc::findBuild(0x69BC663D);
    REQUIRE(gamePass != nullptr);

    // The keep check taken out.
    std::vector<lc::ByteCheck> partial;
    for (const lc::ByteCheck& check : gamePass->checks) {
        if (check.offset != gamePass->at.keep) {
            partial.push_back(check);
        }
    }
    lc::Build missing = *gamePass;
    missing.checks = partial;
    CHECK(lc::uncheckedOffset(missing) == gamePass->at.keep);

    // The verdict hooked one byte off from where its bytes are checked.
    lc::Build moved = *gamePass;
    moved.at.verdict += 1;
    CHECK(lc::uncheckedOffset(moved) == gamePass->at.verdict + 1);

    // A hook site checked over fewer bytes than a hook writes.
    std::vector<lc::ByteCheck> shortHook(gamePass->checks.begin(), gamePass->checks.end());
    for (lc::ByteCheck& check : shortHook) {
        if (check.offset == gamePass->at.sphere) {
            check.size = 1;
        }
    }
    lc::Build shortSphere = *gamePass;
    shortSphere.checks = shortHook;
    CHECK(lc::uncheckedOffset(shortSphere) == gamePass->at.sphere);

    // No checks at all: the first offset is reported.
    lc::Build none = *gamePass;
    none.checks = {};
    CHECK(lc::uncheckedOffset(none) == gamePass->at.gateA);
}
