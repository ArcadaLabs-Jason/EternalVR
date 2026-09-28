#include "gpu_timing/cpu_split.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using evr::gpu_timing::CpuSplitWindow;
using evr::gpu_timing::CpuStage;
using evr::gpu_timing::cpuStageName;
using evr::gpu_timing::CpuTick;
using evr::gpu_timing::formatShare;
using evr::gpu_timing::rankThreads;
using evr::gpu_timing::ThreadCycles;
using evr::gpu_timing::ThreadShare;

namespace {

std::size_t at(CpuStage stage) {
    return static_cast<std::size_t>(stage);
}

CpuTick tick(double period, double frontendCpu, double frameEnd, double right) {
    CpuTick t;
    t.periodMs = period;
    t.frontendCpuMs = frontendCpu;
    t.wallMs[at(CpuStage::FrameEndLeft)] = frameEnd;
    t.cpuMs[at(CpuStage::FrameEndLeft)] = frameEnd / 2.0;
    t.wallMs[at(CpuStage::RightEye)] = right;
    t.cpuMs[at(CpuStage::RightEye)] = right;
    return t;
}

} // namespace

TEST_CASE("the frontend's time outside the frame-end jobs is the tick minus the wrapped stages") {
    CpuSplitWindow w;
    w.add(tick(12.0, 10.0, 1.0, 4.0));
    w.add(tick(14.0, 11.0, 2.0, 5.0));
    const CpuSplitWindow::Report r = w.report();
    CHECK(r.ticks == 2);
    CHECK(r.period.mean == doctest::Approx(13.0));
    // Wall: 12 - 5 = 7 and 14 - 7 = 7.
    CHECK(r.outsideWall.mean == doctest::Approx(7.0));
    // CPU: 10 - (0.5 + 4) = 5.5 and 11 - (1 + 5) = 5.
    CHECK(r.outsideCpu.mean == doctest::Approx(5.25));
    CHECK(r.wall[at(CpuStage::RightEye)].mean == doctest::Approx(4.5));
    CHECK(r.cpu[at(CpuStage::FrameEndLeft)].max == doctest::Approx(1.0));
}

TEST_CASE("call stages count calls per tick and never make the outside time negative") {
    CpuSplitWindow w;
    CpuTick t = tick(5.0, 1.0, 3.0, 4.0); // wrapped stages longer than the tick (clock skew)
    t.wallMs[at(CpuStage::Submit)] = 2.0;
    t.calls[at(CpuStage::Submit)] = 6;
    w.add(t);
    t.calls[at(CpuStage::Submit)] = 3;
    w.add(t);
    const CpuSplitWindow::Report r = w.report();
    CHECK(r.outsideWall.max == 0.0);
    CHECK(r.outsideCpu.max == 0.0);
    CHECK(r.callsPerTick[at(CpuStage::Submit)] == doctest::Approx(4.5));
    CHECK(r.wall[at(CpuStage::Submit)].mean == doctest::Approx(2.0));
}

TEST_CASE("a tick with an unknown frontend CPU time still counts for the wall and process times") {
    CpuSplitWindow w;
    CpuTick t = tick(12.0, -1.0, 1.0, 4.0);
    t.processCpuMs = 30.0;
    w.add(t);
    w.add(tick(10.0, 8.0, 1.0, 4.0));
    const CpuSplitWindow::Report r = w.report();
    CHECK(r.ticks == 2);
    CHECK(r.period.mean == doctest::Approx(11.0));
    CHECK(r.outsideWall.count == 2);
    CHECK(r.frontendCpu.count == 1);
    CHECK(r.outsideCpu.count == 1);
    CHECK(r.outsideCpu.mean == doctest::Approx(3.5)); // 8 - (0.5 + 4)
    CHECK(r.processCpu.max == doctest::Approx(30.0));
}

TEST_CASE("clear starts a new window") {
    CpuSplitWindow w;
    w.add(tick(12.0, 10.0, 1.0, 4.0));
    w.clear();
    CHECK(w.report().ticks == 0);
    CHECK(w.report().period.count == 0);
}

TEST_CASE("threads are ranked by CPU time per tick, new threads counted from zero") {
    // 1000 cycles per ms, a 100 ms window of 10 ticks.
    const std::vector<ThreadCycles> before{{1, 1000, "main"}, {2, 5000, "render"}, {3, 7000, ""}};
    const std::vector<ThreadCycles> after{{1, 51000, "main"},   // 50 ms: 5 ms per tick, 50 %
                                          {2, 85000, "render"}, // 80 ms: 8 ms per tick, 80 %
                                          {3, 7000, ""},        // idle: left out
                                          {4, 10000, "job"}};   // new: 10 ms
    double total = 0.0;
    const std::vector<ThreadShare> shares = rankThreads(before, after, 1000.0, 100.0, 10, 2, total);
    REQUIRE(shares.size() == 2);
    CHECK(shares[0].name == "render");
    CHECK(shares[0].msPerTick == doctest::Approx(8.0));
    CHECK(shares[0].busyPercent == doctest::Approx(80.0));
    CHECK(shares[1].id == 1);
    CHECK(total == doctest::Approx(14.0)); // 8 + 5 + 1, the cut-off thread included
}

TEST_CASE("a thread id reused with a smaller counter counts from zero; no ticks, no ranking") {
    double total = 1.0;
    const std::vector<ThreadShare> reused =
        rankThreads({{7, 90000, ""}}, {{7, 3000, ""}}, 1000.0, 10.0, 1, 5, total);
    REQUIRE(reused.size() == 1);
    CHECK(reused[0].msPerTick == doctest::Approx(3.0));
    CHECK(rankThreads({}, {{1, 5, ""}}, 1000.0, 10.0, 0, 5, total).empty());
    CHECK(total == 0.0);
}

TEST_CASE("stage names and thread shares format for the log") {
    CHECK(std::string(cpuStageName(CpuStage::FenceWait)) == "vkWaitForFences");
    ThreadShare s;
    s.id = 42;
    s.msPerTick = 7.1;
    s.busyPercent = 58.0;
    CHECK(formatShare(s) == "thread 42 7.10 ms (58%)");
    s.name = "RenderThread";
    CHECK(formatShare(s) == "RenderThread [42] 7.10 ms (58%)");
}
