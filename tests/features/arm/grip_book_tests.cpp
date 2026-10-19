#include "features/arm/grip_book.hpp"

#include <doctest/doctest.h>

#include <array>
#include <string>

using evr::arm::GripBook;

namespace {

// A grip stands in for the game's (vkcore's Capture); the book only stores and returns it.
struct Grip {
    int id = 0;
};

// Decl pointers: a new level may hand one item's pointer to another.
std::array<char, 4> g_decls{};
const void* decl(int i) {
    return &g_decls.at(static_cast<std::size_t>(i));
}

} // namespace

TEST_CASE("grip book: new hands forget what each pointer named; a reused pointer is learned again") {
    GripBook<Grip> book;
    REQUIRE(book.learn(decl(0), "weapon/player/chaingun"));
    REQUIRE(book.keep(decl(0), Grip{1}));
    book.newHands();
    // The pointer is unknown on the new hands, so its item is read again from the game.
    CHECK(book.nameOf(decl(0)) == nullptr);
    CHECK(book.gripOf(decl(0)) == nullptr);
    REQUIRE(book.learn(decl(0), "weapon/player/shotgun"));
    REQUIRE(book.nameOf(decl(0)) != nullptr);
    CHECK(*book.nameOf(decl(0)) == "weapon/player/shotgun");
    // The shotgun has no grip of its own: it does not take the chaingun's.
    CHECK(book.gripOf(decl(0)) == nullptr);
}

TEST_CASE("grip book: a grip kept by name serves the item on new hands until one is read there") {
    GripBook<Grip> book;
    REQUIRE(book.learn(decl(0), "weapon/player/crucible"));
    CHECK(book.gripOf(decl(0)) == nullptr); // a first raise ever: no grip
    REQUIRE(book.keep(decl(0), Grip{1}));
    CHECK(book.readHere(decl(0)));
    book.newHands();
    REQUIRE(book.learn(decl(1), "weapon/player/crucible"));
    REQUIRE(book.gripOf(decl(1)) != nullptr);
    CHECK(book.gripOf(decl(1))->id == 1);
    CHECK_FALSE(book.readHere(decl(1)));
    // The first steady read on the new hands replaces it, here and by name.
    REQUIRE(book.keep(decl(1), Grip{2}));
    CHECK(book.gripOf(decl(1))->id == 2);
    CHECK(book.readHere(decl(1)));
    book.newHands();
    REQUIRE(book.learn(decl(2), "weapon/player/crucible"));
    CHECK(book.gripOf(decl(2))->id == 2);
}

TEST_CASE("grip book: an item not learned is not kept; the book holds a fixed number of items") {
    GripBook<Grip> book;
    CHECK_FALSE(book.keep(decl(0), Grip{1}));
    std::array<char, GripBook<Grip>::kMaxItems + 1> many{};
    for (std::size_t i = 0; i < GripBook<Grip>::kMaxItems; ++i) {
        REQUIRE(book.learn(&many.at(i), "item " + std::to_string(i)));
    }
    CHECK_FALSE(book.learn(&many.back(), "one more"));
    CHECK(book.learn(&many.front(), "item 0")); // known already
}

TEST_CASE("grip book: an empty name is not learned, so unread names do not share one grip") {
    GripBook<Grip> book;
    REQUIRE(book.learn(decl(0), "weapon/player/shotgun"));
    REQUIRE(book.keep(decl(0), Grip{1}));
    // A decl whose name the game's memory did not give reads as "": not learned, read again next time.
    CHECK_FALSE(book.learn(decl(1), ""));
    CHECK(book.nameOf(decl(1)) == nullptr);
    CHECK_FALSE(book.keep(decl(1), Grip{2}));
    CHECK(book.gripOf(decl(1)) == nullptr);
    CHECK_FALSE(book.learn(decl(2), ""));
    CHECK(book.gripOf(decl(2)) == nullptr);
    // Once its name reads, the item is learned.
    CHECK(book.learn(decl(1), "weapon/player/bfg"));
}

TEST_CASE("grip book: a kept grip serves only while it still holds; a grip read on these hands always does") {
    GripBook<Grip> book;
    REQUIRE(book.learn(decl(0), "weapon/player/chaingun"));
    REQUIRE(book.keep(decl(0), Grip{1}));
    const auto never = [](const Grip&) {
        return false;
    };
    CHECK(book.gripOf(decl(0), never) != nullptr); // read here
    book.newHands();
    REQUIRE(book.learn(decl(1), "weapon/player/chaingun"));
    // Kept from the hands before: refused while the gun is held elsewhere (a weapon mod's root), served when
    // not.
    CHECK(book.gripOf(decl(1), never) == nullptr);
    CHECK(book.gripOf(decl(1), [](const Grip& g) { return g.id == 1; }) != nullptr);
}
