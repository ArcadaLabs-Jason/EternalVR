#include "common/retire_queue.hpp"

#include <doctest/doctest.h>

#include <vector>

using evr::RetireQueue;

TEST_CASE("retired items are destroyed only once the counter reaches their point") {
    RetireQueue<int> queue;
    queue.retire(1, 10);
    queue.retire(2, 12);
    queue.retire(3, 11);
    std::vector<int> destroyed;
    const auto destroy = [&destroyed](int item) {
        destroyed.push_back(item);
    };

    CHECK(queue.collect(9, destroy) == 0);
    CHECK(queue.size() == 3);
    CHECK(queue.collect(11, destroy) == 2);
    CHECK(destroyed == std::vector<int>{1, 3});
    CHECK(queue.size() == 1);
    CHECK(queue.collect(11, destroy) == 0);
    CHECK(queue.collect(40, destroy) == 1);
    CHECK(destroyed == std::vector<int>{1, 3, 2});
    CHECK(queue.empty());
}

TEST_CASE("draining destroys everything regardless of the counter") {
    RetireQueue<int> queue;
    queue.retire(7, 1000);
    queue.retire(8, 5);
    std::vector<int> destroyed;
    queue.drain([&destroyed](int item) { destroyed.push_back(item); });
    CHECK(destroyed == std::vector<int>{7, 8});
    CHECK(queue.empty());
}

TEST_CASE("the queue stays bounded when items keep being retired and collected") {
    RetireQueue<int> queue;
    int destroyedCount = 0;
    for (std::uint64_t counter = 0; counter < 1000; ++counter) {
        queue.retire(static_cast<int>(counter), counter + 8);
        queue.collect(counter, [&destroyedCount](int) { ++destroyedCount; });
        CHECK(queue.size() <= 9);
    }
    CHECK(destroyedCount == 1000 - 8);
}
