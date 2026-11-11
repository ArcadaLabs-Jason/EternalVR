#include "vkcore/hook_guard.hpp"

#include <doctest/doctest.h>

#include <cstdarg>
#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// The layer's log, for these tests: the lines are kept to be checked (the layer's own log.cpp is not linked).
namespace {
std::vector<std::string> g_lines;
} // namespace

namespace evr::vkcore {
void logf(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_lines.emplace_back(line);
}
} // namespace evr::vkcore

using evr::vkcore::HookGuard;

TEST_CASE("a hook guard runs the body and is true when it returns, false when it throws") {
    g_lines.clear();
    HookGuard guard("a test boundary");
    int runs = 0;
    CHECK(guard.run([&] { ++runs; }));
    CHECK(runs == 1);
    CHECK(g_lines.empty());
    CHECK_FALSE(guard.run([] { throw std::bad_alloc(); }));
    CHECK_FALSE(guard.run([] { throw std::runtime_error("no slot"); }));
}

TEST_CASE("a hook guard logs a site's first failure with its message, then every 1000th") {
    g_lines.clear();
    HookGuard guard("a test boundary");
    const auto fail = [&guard] {
        return guard.run([] { throw std::runtime_error("no memory here"); });
    };
    CHECK_FALSE(fail());
    REQUIRE(g_lines.size() == 1);
    CHECK(g_lines[0].find("a test boundary") != std::string::npos);
    CHECK(g_lines[0].find("no memory here") != std::string::npos);
    CHECK(g_lines[0].find("1 time(s)") != std::string::npos);
    for (int i = 2; i <= 2000; ++i) {
        fail();
    }
    REQUIRE(g_lines.size() == 3);
    CHECK(g_lines[1].find("1000 time(s)") != std::string::npos);
    CHECK(g_lines[2].find("2000 time(s)") != std::string::npos);
}

TEST_CASE("each site of a hook guard counts its own failures") {
    g_lines.clear();
    HookGuard guard("a shared boundary");
    CHECK_FALSE(guard.run([] { throw std::bad_alloc(); }));
    CHECK_FALSE(guard.run([] { throw std::bad_alloc(); })); // another site: its first failure is logged too
    CHECK(g_lines.size() == 2);
}

TEST_CASE("a hook guard logs failure 1 and every 1000th") {
    CHECK(HookGuard::logged(1));
    CHECK_FALSE(HookGuard::logged(2));
    CHECK_FALSE(HookGuard::logged(999));
    CHECK(HookGuard::logged(1000));
    CHECK_FALSE(HookGuard::logged(1001));
    CHECK(HookGuard::logged(3000));
}
