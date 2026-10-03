#include "platform/mp_policy/saved_cvars.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <string>
#include <vector>

using evr::mp_policy::cvarLeftOnTrip;
using evr::mp_policy::cvarValueText;
using evr::mp_policy::sameCvarName;
using evr::mp_policy::SavedCvars;

TEST_CASE("the first value before the layer's writes is kept, and handed back once") {
    SavedCvars<int> saved;
    CHECK(saved.beforeWrite("r_antialiasing", "1", 10));
    CHECK(saved.beforeWrite("pm_noBob", "0", 20));
    // A later write (after the game put its own value back) keeps the first value; the name's case does not
    // matter.
    CHECK_FALSE(saved.beforeWrite("R_ANTIALIASING", "0", 11));
    CHECK(saved.saved("r_antialiasing"));
    CHECK_FALSE(saved.saved("r_TAASafeMode"));
    CHECK(saved.size() == 2);

    const auto entries = saved.takeAll();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "r_antialiasing");
    CHECK(entries[0].value == "1");
    CHECK(entries[0].target == 10);
    CHECK(entries[1].name == "pm_noBob");
    CHECK(entries[1].value == "0");
    CHECK(entries[1].target == 20);

    // Once only, and nothing is kept after the values were taken.
    CHECK(saved.takeAll().empty());
    CHECK(saved.taken());
    CHECK_FALSE(saved.beforeWrite("r_dof", "1", 30));
    CHECK(saved.takeAll().empty());
}

TEST_CASE("a cvar's value is written back as the integer, or as the float that reads back the same") {
    CHECK(cvarValueText(0, 0.0f) == "0");
    CHECK(cvarValueText(1, 1.0f) == "1");
    CHECK(cvarValueText(-1, -1.0f) == "-1");
    CHECK(cvarValueText(2016, 2016.0f) == "2016");
    CHECK(cvarValueText(1, 1.99f) == "1.99");
    CHECK(cvarValueText(1, 1.15f) == "1.15");
    CHECK(cvarValueText(0, 0.005f) == "0.005");
    CHECK(cvarValueText(0, 0.02857f) == "0.02857");
    for (const float f : {0.1f, 1.0f / 3.0f, 123456.78f, -0.75f}) {
        CHECK(std::strtof(cvarValueText(static_cast<int>(f), f).c_str(), nullptr) == f);
    }
}

TEST_CASE("the window, present and HDR cvars are left as they are on a trip") {
    for (const char* name : {"r_windowWidth", "r_windowHeight", "r_fullscreen", "r_swapInterval",
                             "r_hdrDisplay", "R_WINDOWWIDTH"}) {
        CHECK(cvarLeftOnTrip(name) != nullptr);
    }
    for (const char* name : {"r_antialiasing", "r_TAASafeMode", "pm_noBob", "view_skipDamageEffect",
                             "swf_platformOverride", "r_sharpening", "r_window"}) {
        CHECK(cvarLeftOnTrip(name) == nullptr);
    }
}

TEST_CASE("cvar names compare without regard to ASCII case") {
    CHECK(sameCvarName("r_TAASafeMode", "r_taasafemode"));
    CHECK_FALSE(sameCvarName("r_dof", "r_dofTAA"));
    CHECK_FALSE(sameCvarName("r_dof", "r_doe"));
}
