#include "features/flares/flare_book.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

using namespace evr::flares;

namespace {

FlareRecord flare(std::uintptr_t model, std::uintptr_t vertices = 0x10000, std::int32_t quads = 1) {
    FlareRecord r;
    r.model = model;
    r.vertices = vertices;
    r.quads = quads;
    r.intensity = 0.5f;
    r.context = 0x7000;
    return r;
}

constexpr RenderKey kRender{0x5000, 10};
constexpr RenderKey kNext{0x5000, 11};

} // namespace

TEST_CASE("a record is plausible with a model, an aligned block and a quad count the ring can hold") {
    CHECK(plausible(flare(0x100)));
    CHECK(plausible(flare(0x100, 0x10000, kMaxQuads)));
    CHECK_FALSE(plausible(flare(0, 0x10000)));
    CHECK_FALSE(plausible(flare(0x100, 0)));
    CHECK_FALSE(plausible(flare(0x100, 0x10008)));
    CHECK_FALSE(plausible(flare(0x100, 0x10000, 0)));
    CHECK_FALSE(plausible(flare(0x100, 0x10000, -1)));
    CHECK_FALSE(plausible(flare(0x100, 0x10000, kMaxQuads + 1)));
}

TEST_CASE("the update leaves the write pointer 0xC0 bytes per quad further on") {
    CHECK(vertexEnd(flare(0x100, 0x10000, 1)) == 0x100C0);
    CHECK(vertexEnd(flare(0x100, 0x10000, 3)) == 0x10240);
}

TEST_CASE("a render's records are taken once, in the order they came") {
    FlareBook book;
    CHECK(book.add(kRender, flare(0x100, 0x10000)) == AddResult::Added);
    CHECK(book.add(kRender, flare(0x200, 0x20000)) == AddResult::Added);
    std::array<FlareRecord, FlareBook::kCapacity> out{};
    const TakeResult taken = book.take(kRender, out);
    REQUIRE(taken.count == 2);
    CHECK_FALSE(taken.otherRender);
    CHECK(out[0].model == 0x100);
    CHECK(out[0].vertices == 0x10000);
    CHECK(out[0].intensity == 0.5f);
    CHECK(out[0].context == 0x7000);
    CHECK(out[1].model == 0x200);
    CHECK(book.take(kRender, out).count == 0);
}

TEST_CASE("a record after its render was taken is late and not kept") {
    FlareBook book;
    book.add(kRender, flare(0x100));
    std::array<FlareRecord, 4> out{};
    CHECK(book.take(kRender, out).count == 1);
    CHECK(book.add(kRender, flare(0x200)) == AddResult::Late);
    CHECK(book.take(kRender, out).count == 0);
}

TEST_CASE("a new render drops the last one's records; one never taken is counted") {
    FlareBook book;
    book.add(kRender, flare(0x100));
    CHECK(book.add(kNext, flare(0x200)) == AddResult::Added);
    CHECK(book.rendersNotTaken() == 1);
    std::array<FlareRecord, 4> out{};
    CHECK(book.take(kRender, out).count == 0);
    const TakeResult taken = book.take(kNext, out);
    REQUIRE(taken.count == 1);
    CHECK(out[0].model == 0x200);
    // Taken, then a new render: nothing more counted.
    book.add(RenderKey{0x5000, 12}, flare(0x300));
    CHECK(book.rendersNotTaken() == 1);
}

TEST_CASE("an older render of the same view after a newer one started is late") {
    FlareBook book;
    book.add(kNext, flare(0x100));
    CHECK(book.add(kRender, flare(0x200)) == AddResult::Late);
    std::array<FlareRecord, 4> out{};
    CHECK(book.take(kNext, out).count == 1);
}

TEST_CASE("the frame number wraps: the render after 0xFFFFFFFF is newer") {
    FlareBook book;
    book.add(RenderKey{0x5000, 0xFFFFFFFFu}, flare(0x100));
    CHECK(book.add(RenderKey{0x5000, 0}, flare(0x200)) == AddResult::Added);
    CHECK(book.rendersNotTaken() == 1);
}

TEST_CASE("another view starts a new render whatever its frame") {
    FlareBook book;
    book.add(kNext, flare(0x100));
    CHECK(book.add(RenderKey{0x6000, 3}, flare(0x200)) == AddResult::Added);
    std::array<FlareRecord, 4> out{};
    CHECK(book.take(RenderKey{0x6000, 3}, out).count == 1);
}

TEST_CASE("a model twice in one render keeps its first record") {
    FlareBook book;
    CHECK(book.add(kRender, flare(0x100, 0x10000)) == AddResult::Added);
    CHECK(book.add(kRender, flare(0x100, 0x100C0)) == AddResult::Duplicate);
    std::array<FlareRecord, 4> out{};
    REQUIRE(book.take(kRender, out).count == 1);
    CHECK(out[0].vertices == 0x10000);
}

TEST_CASE("a full render keeps the first kCapacity records") {
    FlareBook book;
    for (std::size_t i = 0; i < FlareBook::kCapacity; ++i) {
        CHECK(book.add(kRender, flare(0x100 + i * 0x10)) == AddResult::Added);
    }
    CHECK(book.add(kRender, flare(0x1)) == AddResult::Full);
    std::array<FlareRecord, FlareBook::kCapacity> out{};
    CHECK(book.take(kRender, out).count == FlareBook::kCapacity);
}

TEST_CASE("take copies no more than the buffer holds") {
    FlareBook book;
    book.add(kRender, flare(0x100));
    book.add(kRender, flare(0x200));
    book.add(kRender, flare(0x300));
    std::array<FlareRecord, 2> out{};
    CHECK(book.take(kRender, out).count == 2);
}

TEST_CASE("taking another render's key says the book holds records nobody took") {
    FlareBook book;
    std::array<FlareRecord, 4> out{};
    CHECK_FALSE(book.take(kRender, out).otherRender); // empty book
    book.add(kRender, flare(0x100));
    const TakeResult other = book.take(RenderKey{0x6000, 10}, out);
    CHECK(other.count == 0);
    CHECK(other.otherRender);
    CHECK(book.take(kRender, out).count == 1);
    CHECK_FALSE(book.take(RenderKey{0x6000, 10}, out).otherRender); // taken: nothing waiting
}
