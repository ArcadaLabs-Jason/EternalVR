#include "vkcore/swap_images.hpp"

#include <doctest/doctest.h>

using namespace evr::vkcore::swap_images;

namespace {

Swapchain twoImages() {
    Swapchain s;
    s.images[0] = 0x1000;
    s.images[1] = 0x2000;
    s.count = 2;
    s.fallback = 0x9000;
    return s;
}

} // namespace

TEST_CASE("the swapchain's own images and its fallback are held") {
    const Swapchain s = twoImages();
    CHECK(holds(s, 0x1000));
    CHECK(holds(s, 0x2000));
    CHECK(holds(s, 0x9000)); // no swapchain, or the acquire failed: the fallback image
}

TEST_CASE("an image of the destroyed swapchain is not held") {
    Swapchain s = twoImages();
    CHECK_FALSE(holds(s, 0x3000)); // recreated: the old wrapper is gone
    // The destroy clears the array but leaves the count until the new swapchain sets it.
    s.images = {};
    CHECK_FALSE(holds(s, 0x1000));
}

TEST_CASE("only the first `count` entries count") {
    Swapchain s = twoImages();
    s.images[2] = 0x3000; // left over past the count
    CHECK_FALSE(holds(s, 0x3000));
    s.count = 3;
    CHECK(holds(s, 0x3000));
}

TEST_CASE("a count outside 0..8 is clamped; a null colour is never held") {
    Swapchain s = twoImages();
    s.images[7] = 0x8000;
    s.count = 100;
    CHECK(holds(s, 0x8000));
    s.count = -1;
    CHECK_FALSE(holds(s, 0x1000));
    CHECK(holds(s, 0x9000)); // the fallback does not depend on the count
    s.images[0] = 0;
    s.count = 2;
    s.fallback = 0;
    CHECK_FALSE(holds(s, 0)); // a null entry or fallback never matches a null colour
}

TEST_CASE("only view 0's pass outside the finish job is left out, and only when its image is gone") {
    CHECK(leaveOut(false, 0, false));
    CHECK_FALSE(leaveOut(false, 0, true));  // its image is the swapchain's: drawn as always
    CHECK_FALSE(leaveOut(true, 0, false));  // the finish job runs after the acquire: never left out
    CHECK_FALSE(leaveOut(false, 1, false)); // view 1 draws into its own clone
    CHECK_FALSE(leaveOut(false, -1, false));
}
