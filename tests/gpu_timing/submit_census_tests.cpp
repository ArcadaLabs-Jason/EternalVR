#include "gpu_timing/submit_census.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>

using evr::gpu_timing::formatSubmitCensus;
using evr::gpu_timing::SubmitCensus;
using evr::gpu_timing::SubmitRecord;

namespace {

SubmitRecord submit(std::uint64_t queue, double atMs, std::uint32_t batches = 1, bool fence = false) {
    SubmitRecord r;
    r.queue = queue;
    r.thread = 7;
    r.atMs = atMs;
    r.batches = batches;
    r.commandBuffers = batches * 2;
    r.waits = batches ? 1 : 0;
    r.signals = batches ? 1 : 0;
    r.fence = fence;
    return r;
}

} // namespace

TEST_CASE("the census counts the shape of every submit") {
    SubmitCensus c;
    c.add(submit(1, 0.0, 2, true));
    c.add(submit(1, 5.0, 0, true));
    const SubmitCensus::Report r = c.report();
    CHECK(r.calls == 2);
    CHECK(r.batches == 2);
    CHECK(r.commandBuffers == 4);
    CHECK(r.waits == 1);
    CHECK(r.signals == 1);
    CHECK(r.fences == 2);
    CHECK(r.emptyCalls == 1);
    CHECK(r.queues == 1);
    CHECK(r.threads == 1);
}

TEST_CASE("gaps are measured per queue and bucketed, and the first submit of a queue has none") {
    SubmitCensus c;
    c.add(submit(1, 0.0));
    c.add(submit(2, 0.01)); // first on queue 2
    c.add(submit(1, 0.03)); // 0.03 ms after queue 1's last
    c.add(submit(1, 0.13)); // 0.1
    c.add(submit(1, 0.63)); // 0.5
    c.add(submit(1, 3.63)); // 3.0
    const SubmitCensus::Report r = c.report();
    CHECK(r.queues == 2);
    CHECK(r.busiestQueueCalls == 5);
    CHECK(r.gaps[0] == 1);
    CHECK(r.gaps[1] == 1);
    CHECK(r.gaps[2] == 1);
    CHECK(r.gaps[3] == 1);
    CHECK(r.mergeable == 2);
}

TEST_CASE("a new window keeps the last submit per queue, so its first gap is still known") {
    SubmitCensus c;
    c.add(submit(1, 0.0));
    c.clear();
    c.add(submit(1, 0.1));
    const SubmitCensus::Report r = c.report();
    CHECK(r.calls == 1);
    CHECK(r.gaps[1] == 1);
    CHECK(r.mergeable == 1);
}

TEST_CASE("the census line is per tick") {
    SubmitCensus c;
    for (int i = 0; i < 4; ++i) {
        c.add(submit(1, i * 0.1));
    }
    const std::string line = formatSubmitCensus(c.report(), 2);
    CHECK(line.find("2.0 call(s)") == 0);
    CHECK(line.find("per tick") != std::string::npos);
    CHECK(line.find("mergeable (gap < 0.2 ms) 1.5") != std::string::npos);
}
