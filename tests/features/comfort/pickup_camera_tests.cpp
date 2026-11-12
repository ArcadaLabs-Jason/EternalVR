#include "features/comfort/pickup_camera.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace evr::comfort;

TEST_CASE("pickup camera: names parse back, any case and spacing") {
    for (const PickupCamera camera : {PickupCamera::Still, PickupCamera::Play}) {
        const auto parsed = parsePickupCamera(pickupCameraName(camera));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == camera);
    }
    CHECK(parsePickupCamera("  Play ") == PickupCamera::Play);
    CHECK(parsePickupCamera("STILL") == PickupCamera::Still);
    CHECK(std::string(pickupCameraName(PickupCamera::Still)) == "still");
    CHECK(std::string(pickupCameraName(PickupCamera::Play)) == "play");
}

TEST_CASE("pickup camera: anything else is not a value") {
    CHECK_FALSE(parsePickupCamera("").has_value());
    CHECK_FALSE(parsePickupCamera("   ").has_value());
    CHECK_FALSE(parsePickupCamera("1").has_value());
    CHECK_FALSE(parsePickupCamera("plays").has_value());
    CHECK_FALSE(parsePickupCamera("follow").has_value());
    CHECK_FALSE(parsePickupCamera("pl ay").has_value());
}
