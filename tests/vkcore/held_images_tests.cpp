#include "vkcore/held_images.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using evr::vkcore::HeldImage;
namespace held_images = evr::vkcore::held_images;

namespace {

// A swapchain handle for the tests: compared as a value only.
VkSwapchainKHR swapchain(std::uintptr_t n) {
    return reinterpret_cast<VkSwapchainKHR>(n);
}

struct Released {
    std::vector<std::uint32_t> images;
    void operator()(const HeldImage& h) { images.push_back(h.image); }
};

} // namespace

TEST_CASE("held images go back once their copies are done, in the order they were held") {
    std::vector<HeldImage> held;
    held.reserve(2); // as decideWindow reserves them
    held.push_back({swapchain(0x10), 1, 7});
    held.push_back({swapchain(0x10), 2, 9});
    Released released;
    held_images::handBack(held, 6, released); // neither copy has finished
    CHECK(released.images.empty());
    CHECK(held.size() == 2);
    held_images::handBack(held, 8, released);
    CHECK(released.images == std::vector<std::uint32_t>{1});
    REQUIRE(held.size() == 1);
    CHECK(held[0].image == 2);
    held_images::handBack(held, 9, released);
    CHECK(released.images == std::vector<std::uint32_t>{1, 2});
    CHECK(held.empty());
    CHECK(held.capacity() >= 2); // a later hold does not allocate
    held_images::handBack(held, 100, released);
    CHECK(released.images.size() == 2); // each image is handed back once
}

TEST_CASE("held images whose copies are done go back in one call, the others stay in their order") {
    std::vector<HeldImage> held{{swapchain(0x10), 4, 12}, {swapchain(0x20), 0, 3}, {swapchain(0x10), 5, 5}};
    Released released;
    held_images::handBack(held, 5, released);
    CHECK(released.images == std::vector<std::uint32_t>{0, 5});
    REQUIRE(held.size() == 1);
    CHECK(held[0].image == 4);
    CHECK(held[0].swapchain == swapchain(0x10));
}

TEST_CASE("a held image is taken off the list even when its release fails") {
    std::vector<HeldImage> held{{swapchain(0x10), 3, 1}};
    int calls = 0;
    held_images::handBack(held, 1, [&](const HeldImage&) { ++calls; }); // the driver refused it
    CHECK(calls == 1);
    CHECK(held.empty()); // counted as failed by the presenter, never tried again
}

TEST_CASE("images held from the window still go back once copies are off, while the ring is there") {
    // The headset is lost (or a copy failed) with two images held: the presents after it are not copied, and
    // each one hands back what its copies finished, as the copied presents do.
    std::vector<HeldImage> held{{swapchain(0x10), 0, 20}, {swapchain(0x10), 1, 21}};
    Released released;
    const bool ringReady = true;
    REQUIRE(held_images::handsBackUncopied(!held.empty(), ringReady));
    held_images::handBack(held, 20, released);
    REQUIRE(held_images::handsBackUncopied(!held.empty(), ringReady));
    held_images::handBack(held, 21, released);
    CHECK(released.images == std::vector<std::uint32_t>{0, 1});
    CHECK_FALSE(held_images::handsBackUncopied(!held.empty(), ringReady)); // nothing left to hand back
    // Without the ring its timeline is gone: what is held goes with its swapchain (dropHeldImages).
    CHECK_FALSE(held_images::handsBackUncopied(true, false));
}
