#include "vkcore/job_nodes.hpp"
#include "vkcore/view_async.hpp"

#include <doctest/doctest.h>

#include <cstdint>

namespace nodes = evr::vkcore::job_nodes;
using Result = nodes::BoundedWait::Result;

TEST_CASE("a node's state sits at index * 0x80 - 0x80 in the node array") {
    CHECK(nodes::stateOffset(1) == 0);
    CHECK(nodes::stateOffset(0x89BC3) == 0x09BC3 * 0x80 - 0x80); // the index is bits 0-17
    CHECK(nodes::stateOffset(0xFFFFFFFFFC0897E5ull) == static_cast<std::int64_t>(0x097E5) * 0x80 - 0x80);
    CHECK(nodes::stateOffset(0) == -0x80);
}

TEST_CASE("a node has finished once its generation moved on") {
    // A sink handle from the headset log: generation 0xFFFFFFFFFC000000, index 0x097E5.
    const std::uint64_t handle = 0xFFFFFFFFFC0897E5ull;
    // Waiting: same generation, any counts (dependencies in bits 2-13, references in bits 14-25).
    CHECK_FALSE(nodes::finished(handle, 0xFFFFFFFFFC000000ull));
    CHECK_FALSE(nodes::finished(handle, 0xFFFFFFFFFC000000ull | 0x4004 | 0x8));
    // Finished: the generation counts down.
    CHECK(nodes::finished(handle, 0xFFFFFFFFF8000000ull));
    CHECK(nodes::finished(0x89BC3, 0xFFFFFFFFFC000000ull));
    CHECK_FALSE(nodes::finished(0x89BC3, 0x4000));
}

namespace {

// A clock that moves one tick on each read, and a mark that appears after a number of pauses.
struct Fake {
    std::int64_t clock = 0;
    int pauses = 0;
    int readyAfter = -1; // never
    bool ready() const { return readyAfter >= 0 && pauses >= readyAfter; }
};

Result run(nodes::BoundedWait& wait, Fake& f) {
    return wait.wait([&] { return f.ready(); }, [&] { return f.clock++; }, [&] { ++f.pauses; });
}

} // namespace

TEST_CASE("the bounded wait returns at once when the mark is there") {
    nodes::BoundedWait wait(100, 4);
    Fake f;
    f.readyAfter = 0;
    CHECK(run(wait, f) == Result::Ready);
    CHECK(f.pauses == 0);
}

TEST_CASE("the bounded wait sees a mark that comes in time") {
    nodes::BoundedWait wait(100, 4);
    Fake f;
    f.readyAfter = 10;
    CHECK(run(wait, f) == Result::Waited);
    CHECK(f.pauses == 10);
}

TEST_CASE("the bounded wait runs out at its limit") {
    nodes::BoundedWait wait(50, 4);
    Fake f;
    CHECK(run(wait, f) == Result::TimedOut);
    CHECK(f.pauses <= 50);
    CHECK(f.clock <= 53);
}

TEST_CASE("the bounded wait stops after the streak of waits that ran out, and a success resets it") {
    nodes::BoundedWait wait(20, 3);
    Fake f;
    CHECK(run(wait, f) == Result::TimedOut);
    CHECK(run(wait, f) == Result::TimedOut);
    Fake soon;
    soon.readyAfter = 2;
    CHECK(run(wait, soon) == Result::Waited); // resets the streak
    CHECK_FALSE(wait.off());
    CHECK(run(wait, f) == Result::TimedOut);
    CHECK(run(wait, f) == Result::TimedOut);
    CHECK(run(wait, f) == Result::TimedOut);
    CHECK(wait.off());
    const int before = f.pauses;
    CHECK(run(wait, f) == Result::Off); // no more spinning
    CHECK(f.pauses == before);
    Fake there;
    there.readyAfter = 0;
    CHECK(run(wait, there) == Result::Ready); // a mark already there is still seen
}

TEST_CASE("a compute-only queue family is the async compute one") {
    CHECK(evr::vkcore::computeOnlyFamily(VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT));
    CHECK_FALSE(
        evr::vkcore::computeOnlyFamily(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT));
    CHECK_FALSE(evr::vkcore::computeOnlyFamily(VK_QUEUE_TRANSFER_BIT));
    CHECK_FALSE(evr::vkcore::computeOnlyFamily(VK_QUEUE_GRAPHICS_BIT));
}
