#include "vkcore/swapchain_book.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace evr::vkcore::swapchain_book;

namespace {

std::vector<Image> images(std::uintptr_t first, std::uint64_t width, std::uint32_t height) {
    return {{first, width, height}, {first + 0x100, width, height}, {first + 0x200, width, height}};
}

std::vector<std::uintptr_t> textures(std::uintptr_t first) {
    return {first, first + 0x100, first + 0x200};
}

// The OpenXR Simulator's numbering: a new swapchain's handle is the number it holds plus 2, and a handle it
// already holds is handed back with that swapchain's images (its xrCreateSwapchain).
struct Simulator {
    std::vector<std::uintptr_t> held;
    std::uintptr_t create() {
        const std::uintptr_t handle = held.size() + 2;
        for (const std::uintptr_t h : held) {
            if (h == handle) {
                return handle;
            }
        }
        held.push_back(handle);
        return handle;
    }
    void destroy(std::uintptr_t handle) { std::erase(held, handle); }
};

} // namespace

TEST_CASE("a new swapchain with images of its own of the size asked for is used") {
    Book book;
    book.add(2, textures(0x10000), "game image");
    const Check c = book.check(3, images(0x20000, 992, 488), 992, 488);
    CHECK(c.verdict == Verdict::Use);
}

TEST_CASE("a handle already held is that swapchain: neither used nor destroyed, whatever its images") {
    Book book;
    book.add(4, textures(0x10000), "game image");
    // ol-zb2: the UI swapchain asked 992x488 and got the game image swapchain (1984x488) back.
    Check c = book.check(4, images(0x10000, 1984, 488), 992, 488);
    CHECK(c.verdict == Verdict::HeldHandle);
    REQUIRE(c.owner != nullptr);
    CHECK(std::string(c.owner) == "game image");
    // Of the size asked for (mono: the UI and the ring have the same size): still not a new swapchain.
    c = book.check(4, images(0x10000, 992, 488), 992, 488);
    CHECK(c.verdict == Verdict::HeldHandle);
}

TEST_CASE("an image of a held swapchain under a new handle is shared: kept, not used") {
    Book book;
    book.add(2, textures(0x10000), "game image");
    book.add(3, {0x30000}, "reticle");
    std::vector<Image> made = images(0x50000, 64, 64);
    made[1].texture = 0x30000;
    Check c = book.check(7, made, 64, 64);
    CHECK(c.verdict == Verdict::SharedImage);
    CHECK(c.image == 1);
    REQUIRE(c.owner != nullptr);
    CHECK(std::string(c.owner) == "reticle");
    // Shared before the size: destroying it could free the held swapchain's image.
    made = images(0x10000, 1984, 488);
    c = book.check(7, made, 992, 488);
    CHECK(c.verdict == Verdict::SharedImage);
    CHECK(c.image == 0);
}

TEST_CASE("images of another size under a new handle are refused (destroyed)") {
    Book book;
    book.add(2, textures(0x10000), "game image");
    std::vector<Image> made = images(0x20000, 992, 488);
    made[2].height = 487;
    const Check c = book.check(5, made, 992, 488);
    CHECK(c.verdict == Verdict::WrongSize);
    CHECK(c.image == 2);
    CHECK(book.check(5, {}, 992, 488).verdict == Verdict::NoImages);
    CHECK(book.check(5, {{0, 0, 0}}, 992, 488).verdict == Verdict::WrongSize); // no texture: no size
}

TEST_CASE(
    "a swapchain that is or shares a held one is never destroyed now; only a refused one of its own is") {
    // ol-zb2 / pr-m1: destroying the UI swapchain the simulator handed back destroyed the game image
    // swapchain.
    CHECK(actionFor(Verdict::HeldHandle) == Action::Leave);
    CHECK(actionFor(Verdict::SharedImage) == Action::Keep);
    CHECK(actionFor(Verdict::Use) == Action::Use);
    CHECK(actionFor(Verdict::WrongSize) == Action::Destroy);
    CHECK(actionFor(Verdict::NoImages) == Action::Destroy);
    CHECK(actionFor(static_cast<Verdict>(99)) == Action::Leave);
    // The decision as the presenter takes it: the held handle of another size is left, not destroyed.
    Book book;
    book.add(4, textures(0x10000), "game image");
    CHECK(actionFor(book.check(4, images(0x10000, 1984, 488), 992, 488).verdict) == Action::Leave);
}

TEST_CASE("a static image may be larger than its pixels, never smaller") {
    Book book;
    CHECK(book.check(9, images(0x20000, 64, 64), 64, 64, Fit::AtLeast).verdict == Verdict::Use);
    CHECK(book.check(9, images(0x20000, 128, 64), 64, 64, Fit::AtLeast).verdict == Verdict::Use);
    CHECK(book.check(9, images(0x20000, 63, 64), 64, 64, Fit::AtLeast).verdict == Verdict::WrongSize);
    CHECK(book.check(9, images(0x20000, 64, 63), 64, 64, Fit::AtLeast).verdict == Verdict::WrongSize);
    // The game image and the UI are copied whole: exactly their size.
    CHECK(book.check(9, images(0x20000, 128, 64), 64, 64).verdict == Verdict::WrongSize);
}

TEST_CASE("a destroyed swapchain's handle and images are free again") {
    Book book;
    book.add(2, textures(0x10000), "game image");
    book.add(0, textures(0x90000), "none"); // a null handle is not held
    CHECK(book.size() == 1);
    CHECK(book.remove(2));
    CHECK_FALSE(book.remove(2));
    CHECK(book.check(2, images(0x10000, 992, 488), 992, 488).verdict == Verdict::Use);
    book.add(3, textures(0x10000), "UI");
    book.clear();
    CHECK(book.size() == 0);
    CHECK_FALSE(book.holds(3));
}

TEST_CASE("the window resize under the simulator: the rebuild's order keeps the swapchains in use") {
    // Session start: the game image (2), the fade (3), the UI (4) and the reticle (5).
    Simulator sim;
    Book book;
    const char* names[] = {"game image", "fade", "UI", "reticle"};
    for (const char* name : names) {
        const std::uintptr_t h = sim.create();
        REQUIRE(book.check(h, images(h << 16, 8, 8), 8, 8).verdict == Verdict::Use);
        book.add(h, textures(h << 16), name);
    }
    // The old order (ol-zb2): the UI and reticle destroyed, the new game image made, the old one destroyed,
    // then the UI made: the simulator hands back the new game image swapchain.
    {
        Simulator old = sim;
        Book oldBook = book;
        for (const std::uintptr_t h : {std::uintptr_t{4}, std::uintptr_t{5}}) {
            old.destroy(h);
            oldBook.remove(h);
        }
        const std::uintptr_t game = old.create();
        REQUIRE(oldBook.check(game, images(game << 16, 8, 8), 8, 8).verdict == Verdict::Use);
        oldBook.add(game, textures(game << 16), "game image");
        old.destroy(2);
        oldBook.remove(2);
        const std::uintptr_t ui = old.create();
        CHECK(ui == game);
        CHECK(oldBook.check(ui, images(game << 16, 8, 8), 4, 4).verdict == Verdict::HeldHandle);
    }
    // The rebuild's order: the new game image and UI swapchains first, then the old ones go: both new.
    const std::uintptr_t game = sim.create();
    CHECK(book.check(game, images(game << 16, 8, 8), 8, 8).verdict == Verdict::Use);
    book.add(game, textures(game << 16), "game image");
    const std::uintptr_t ui = sim.create();
    CHECK(book.check(ui, images(ui << 16, 4, 4), 4, 4).verdict == Verdict::Use);
    book.add(ui, textures(ui << 16), "UI");
    for (const std::uintptr_t h : {std::uintptr_t{2}, std::uintptr_t{4}}) {
        sim.destroy(h);
        CHECK(book.remove(h));
    }
    // The next rebuild: the simulator hands back the game image swapchain in use; it is caught, not
    // destroyed.
    const std::uintptr_t again = sim.create();
    CHECK(again == game);
    CHECK(book.check(again, images(game << 16, 8, 8), 8, 8).verdict == Verdict::HeldHandle);
}
