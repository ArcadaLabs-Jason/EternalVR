#include "common/finishes.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <new>
#include <stdexcept>
#include <system_error>
#include <thread>

TEST_CASE("finishes runs the body once and is true when it returns") {
    int runs = 0;
    CHECK(evr::finishes([&] { ++runs; }));
    CHECK(runs == 1);
}

TEST_CASE("finishes is false when the body runs out of memory or gets no thread") {
    CHECK_FALSE(evr::finishes([] { throw std::bad_alloc(); }));
    // What std::thread throws when it cannot start one.
    CHECK_FALSE(evr::finishes([] { throw std::system_error(std::error_code(), "no thread"); }));
    CHECK_FALSE(evr::finishes([] { throw std::runtime_error("any std::exception"); }));
}

TEST_CASE("finishes keeps the work the body did before it threw") {
    int done = 0;
    CHECK_FALSE(evr::finishes([&] {
        done = 1;
        throw std::bad_alloc();
    }));
    CHECK(done == 1);
}

TEST_CASE("finishes starts a detached thread") {
    std::atomic<bool> ran{false};
    REQUIRE(evr::finishes([&] { std::thread([&] { ran.store(true); }).detach(); }));
    while (!ran.load()) {
        std::this_thread::yield();
    }
    CHECK(ran.load());
}
