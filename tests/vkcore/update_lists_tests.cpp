#include "vkcore/update_lists.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

namespace ul = evr::vkcore::update_lists;

TEST_CASE("view 1's entries not on view 0's lists are added, the others counted") {
    ul::ListUnion u;
    u.start(100);
    const std::vector<std::int32_t> particles0 = {3, 7, 9};
    const std::vector<std::int32_t> flares0 = {20};
    u.mark(particles0);
    u.mark(flares0);
    std::vector<std::int32_t> out;
    const ul::ListCounts c = u.add(std::vector<std::int32_t>{7, 11, 3, 12}, 3, out);
    CHECK(out == std::vector<std::int32_t>{11, 12});
    CHECK(c.added == 2);
    CHECK(c.already == 2);
    CHECK(c.dropped == 0);
    CHECK(c.invalid == 0);
}

TEST_CASE("a model on another of view 0's lists, or twice on view 1's, is added once") {
    ul::ListUnion u;
    u.start(64);
    u.mark(std::vector<std::int32_t>{5});
    std::vector<std::int32_t> beams;
    const ul::ListCounts c = u.add(std::vector<std::int32_t>{5, 6, 6}, 0, beams);
    CHECK(beams == std::vector<std::int32_t>{6});
    CHECK(c.already == 2);
    std::vector<std::int32_t> ribbons;
    CHECK(u.add(std::vector<std::int32_t>{6}, 0, ribbons).already == 1);
    CHECK(ribbons.empty());
}

TEST_CASE("an entry outside the world's models is not added") {
    ul::ListUnion u;
    u.start(10);
    u.mark(std::vector<std::int32_t>{-1, 10, 2}); // view 0's own odd entries are ignored
    std::vector<std::int32_t> out;
    const ul::ListCounts c = u.add(std::vector<std::int32_t>{-5, 10, 0x7FFFFFFF, 9, 2}, 1, out);
    CHECK(out == std::vector<std::int32_t>{9});
    CHECK(c.invalid == 3);
    CHECK(c.already == 1);
    u.start(0);
    CHECK(u.add(std::vector<std::int32_t>{0}, 0, out).invalid == 1);
}

TEST_CASE("each list stays within its 0x2000 entries") {
    ul::ListUnion u;
    u.start(0x10000);
    std::vector<std::int32_t> view1;
    for (std::int32_t m = 0; m < 10; ++m) {
        view1.push_back(m);
    }
    std::vector<std::int32_t> out;
    ul::ListCounts c = u.add(view1, ul::kCapacity - 4, out);
    CHECK(out.size() == 4);
    CHECK(c.added == 4);
    CHECK(c.dropped == 6);
    // A dropped entry is not marked: another list with room takes it.
    std::vector<std::int32_t> other;
    c = u.add(view1, 0, other);
    CHECK(other == std::vector<std::int32_t>{4, 5, 6, 7, 8, 9});
    CHECK(c.already == 4);
    // A full list (or one past its room) takes nothing.
    std::vector<std::int32_t> none;
    CHECK(u.add(std::vector<std::int32_t>{100}, ul::kCapacity, none).dropped == 1);
    CHECK(u.add(std::vector<std::int32_t>{101}, ul::kCapacity + 5, none).dropped == 1);
    CHECK(none.empty());
}

TEST_CASE("a new frame starts with nothing marked") {
    ul::ListUnion u;
    u.start(200);
    u.mark(std::vector<std::int32_t>{150});
    u.start(200);
    std::vector<std::int32_t> out;
    CHECK(u.add(std::vector<std::int32_t>{150}, 0, out).added == 1);
    u.start(100); // a smaller world: 150 is not one of its models
    out.clear();
    CHECK(u.add(std::vector<std::int32_t>{150, 63, 64}, 0, out).invalid == 1);
    CHECK(out == std::vector<std::int32_t>{63, 64});
}
