#include "stereo_seq/stack_budget.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

using evr::stereo_seq::kNestedStackFirstGuess;
using evr::stereo_seq::kNestedStackMargin;
using evr::stereo_seq::nestedDepth;
using evr::stereo_seq::nestedRenderFits;
using evr::stereo_seq::stackHeadroom;
using evr::stereo_seq::stackKnown;
using evr::stereo_seq::StackPosition;

namespace {

constexpr std::uintptr_t kLow = 0x10000000;
constexpr std::size_t kKiB = 1024;

StackPosition stack(std::size_t size, std::size_t used) {
    StackPosition s;
    s.low = kLow;
    s.high = kLow + size;
    s.sp = s.high - used;
    return s;
}

} // namespace

TEST_CASE("stack: headroom is what lies below the stack pointer") {
    const StackPosition s = stack(256 * kKiB, 100 * kKiB);
    CHECK(stackKnown(s));
    CHECK(stackHeadroom(s) == 156 * kKiB);
}

TEST_CASE("stack: a stack pointer outside the limits is an unknown stack and is not refused") {
    StackPosition s = stack(256 * kKiB, 10 * kKiB);
    s.sp = kLow - 64; // a stack of its own that the thread's limits do not describe
    CHECK_FALSE(stackKnown(s));
    CHECK(stackHeadroom(s) == 0);
    CHECK(nestedRenderFits(s, 1024 * kKiB));
    CHECK_FALSE(stackKnown(StackPosition{}));
}

TEST_CASE("stack: eye R is refused when the rest of the stack cannot hold its chain and the margin") {
    const std::size_t chain = 80 * kKiB;
    CHECK(nestedRenderFits(stack(8 * 1024 * kKiB, 200 * kKiB), chain)); // the process default
    CHECK(nestedRenderFits(stack(256 * kKiB, 256 * kKiB - chain - kNestedStackMargin), chain));
    CHECK_FALSE(nestedRenderFits(stack(256 * kKiB, 256 * kKiB - chain - kNestedStackMargin + 1), chain));
    CHECK_FALSE(nestedRenderFits(stack(256 * kKiB, 200 * kKiB), chain)); // a deep eye L frame end
}

TEST_CASE("stack: before a chain was measured the first guess stands in for it") {
    const std::size_t need = kNestedStackFirstGuess + kNestedStackMargin;
    CHECK(nestedRenderFits(stack(512 * kKiB, 512 * kKiB - need), 0));
    CHECK_FALSE(nestedRenderFits(stack(512 * kKiB, 512 * kKiB - need + 16), 0));
    // A measured chain shallower than the guess does not lower the bar.
    CHECK_FALSE(nestedRenderFits(stack(512 * kKiB, 512 * kKiB - need + 16), 8 * kKiB));
}

TEST_CASE("stack: nested depth counts only points below the outer call") {
    CHECK(nestedDepth(0x5000, 0x3000) == 0x2000);
    CHECK(nestedDepth(0x3000, 0x5000) == 0);
    CHECK(nestedDepth(0x3000, 0x3000) == 0);
}
