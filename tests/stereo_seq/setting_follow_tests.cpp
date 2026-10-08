#include "stereo_seq/setting_follow.hpp"

#include <doctest/doctest.h>

#include <string>

using evr::stereo_seq::ssdoHoldValue;
using evr::stereo_seq::SsrHold;

TEST_CASE("SSR hold: the launch value until the game's Reflections setting runs, then that setting's") {
    SsrHold h;
    REQUIRE(h.start("1"));
    CHECK(h.follows());
    CHECK_FALSE(h.followed());
    // The forced set's 0 (and an unknown cvar): no evidence, the launch value stays.
    for (int none : {0, -1}) {
        const auto t = h.tick(none);
        CHECK((t.value == "1"));
        CHECK_FALSE(t.changed);
    }
    CHECK_FALSE(h.followed());
    // A player's 0.1.33 export: the profile's load wrote Low (3) before the first stereo tick.
    auto t = h.tick(3);
    CHECK((t.value == "0"));
    CHECK(t.changed);
    CHECK(h.followed());
    // The quality is back at 0 after the tick: the value held stays.
    t = h.tick(0);
    CHECK((t.value == "0"));
    CHECK_FALSE(t.changed);
    // A menu apply at Medium, then High.
    CHECK((h.tick(2).value == "1"));
    t = h.tick(1);
    CHECK((t.value == "1"));
    CHECK_FALSE(t.changed);
    // Failed closed: nothing held, and the restore is not told it followed.
    h.release();
    CHECK(h.tick(3).value.empty());
    CHECK_FALSE(h.followed());
}

TEST_CASE("SSR hold: the player's 0 follows too; off holds 0 whatever the setting; others hold nothing") {
    SsrHold low;
    REQUIRE(low.start("0"));
    CHECK((low.tick(0).value == "0"));
    CHECK((low.tick(2).value == "1"));
    CHECK(low.followed());

    SsrHold off;
    REQUIRE(off.start("off"));
    CHECK_FALSE(off.follows());
    for (int quality : {1, 2, 3, 0}) {
        const auto t = off.tick(quality);
        CHECK((t.value == "0"));
        CHECK_FALSE(t.changed);
    }
    CHECK_FALSE(off.followed());

    SsrHold none;
    CHECK_FALSE(none.start("2"));
    CHECK(none.tick(3).value.empty());
    CHECK_FALSE(none.followed());
}

TEST_CASE("SSDO hold: the launch value until the game's Directional Occlusion setting runs") {
    CHECK((ssdoHoldValue("1", -1) == "1"));
    CHECK((ssdoHoldValue("0", -1) == "0"));
    CHECK((ssdoHoldValue("1", 0) == "0"));
    CHECK((ssdoHoldValue("0", 1) == "1"));
    CHECK((ssdoHoldValue("1", 7) == "1"));
}
