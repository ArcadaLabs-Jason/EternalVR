#include "vkcore/light_cull.hpp"

#include <doctest/doctest.h>

using evr::vkcore::lightFrustumWanted;

TEST_CASE("the light frustum fix is on unless ETERNALVR_LIGHT_FRUSTUM is 0") {
    CHECK(lightFrustumWanted(false, L"")); // unset
    CHECK_FALSE(lightFrustumWanted(true, L"0"));
    CHECK(lightFrustumWanted(true, L"1"));
    CHECK(lightFrustumWanted(true, L""));    // set but empty
    CHECK(lightFrustumWanted(true, L"off")); // only "0" turns it off
    CHECK(lightFrustumWanted(true, L"00"));
    CHECK(lightFrustumWanted(true, L" 0"));
}
