#include "platform/mp_policy/trip_listeners.hpp"

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

using evr::mp_policy::TripListeners;

namespace {

constexpr std::size_t kCount = TripListeners::kCapacity + 1;
std::array<std::atomic<int>, kCount> g_calls{};
std::vector<int> g_order;

template <std::size_t N>
void listener() {
    g_calls[N].fetch_add(1);
}

template <std::size_t N>
void ordered() {
    g_order.push_back(static_cast<int>(N));
}

constexpr std::array<TripListeners::Listener, kCount> kListeners{
    &listener<0>, &listener<1>, &listener<2>, &listener<3>, &listener<4>,
    &listener<5>, &listener<6>, &listener<7>, &listener<8>,
};

void resetCalls() {
    for (std::atomic<int>& c : g_calls) {
        c.store(0);
    }
}

} // namespace

TEST_CASE("trip listeners are called once each, in the order added, when fired") {
    g_order.clear();
    TripListeners list;
    CHECK(list.add(&ordered<0>));
    CHECK(list.add(&ordered<1>));
    CHECK(list.add(&ordered<2>));
    CHECK(g_order.empty());
    CHECK_FALSE(list.fired());
    list.fire();
    CHECK(list.fired());
    CHECK(g_order == std::vector<int>{0, 1, 2});
    list.fire();
    CHECK(g_order == std::vector<int>{0, 1, 2});
}

TEST_CASE("a trip listener added after the trip is called at once, and only then") {
    g_order.clear();
    TripListeners list;
    list.fire();
    CHECK(list.add(&ordered<3>));
    CHECK(g_order == std::vector<int>{3});
    list.fire();
    CHECK(g_order == std::vector<int>{3});
}

TEST_CASE("the trip listener list has a fixed room and refuses a null listener") {
    resetCalls();
    TripListeners list;
    CHECK_FALSE(list.add(nullptr));
    for (std::size_t i = 0; i < TripListeners::kCapacity; ++i) {
        CHECK(list.add(kListeners[i]));
    }
    CHECK(list.size() == TripListeners::kCapacity);
    CHECK_FALSE(list.add(kListeners[TripListeners::kCapacity]));
    list.fire();
    for (std::size_t i = 0; i < TripListeners::kCapacity; ++i) {
        CHECK(g_calls[i].load() == 1);
    }
    CHECK(g_calls[TripListeners::kCapacity].load() == 0);
}

TEST_CASE("trip listeners added while the trip fires on another thread run exactly once") {
    for (int round = 0; round < 200; ++round) {
        resetCalls();
        TripListeners list;
        std::atomic<bool> go{false};
        std::atomic<int> refused{0};
        std::vector<std::thread> threads;
        for (std::size_t t = 0; t < 4; ++t) {
            threads.emplace_back([&list, &go, &refused, t] {
                while (!go.load()) {
                }
                for (std::size_t i = t * 2; i < t * 2 + 2; ++i) {
                    if (!list.add(kListeners[i])) {
                        refused.fetch_add(1);
                    }
                }
            });
        }
        threads.emplace_back([&list, &go] {
            while (!go.load()) {
            }
            list.fire();
        });
        go.store(true);
        for (std::thread& t : threads) {
            t.join();
        }
        REQUIRE(refused.load() == 0);
        for (std::size_t i = 0; i < TripListeners::kCapacity; ++i) {
            REQUIRE(g_calls[i].load() == 1);
        }
    }
}
