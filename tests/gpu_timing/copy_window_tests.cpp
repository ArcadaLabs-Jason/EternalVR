#include "gpu_timing/copy_window.hpp"
#include "gpu_timing/timestamps.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

using evr::gpu_timing::copyParts;
using evr::gpu_timing::CopyParts;
using evr::gpu_timing::CopyWindow;
using evr::gpu_timing::formatCopyReport;
using evr::gpu_timing::validMask;

namespace {

// Period 1000 ns: one tick is 1 us, 1000 ticks are 1 ms.
constexpr float kPeriod = 1000.0f;

} // namespace

TEST_CASE("a copy's two parts come from its three timestamps") {
    const std::optional<CopyParts> p = copyParts({5000, 5250, 5300}, 64, kPeriod);
    REQUIRE(p);
    CHECK(p->first == doctest::Approx(0.25));
    CHECK(p->second == doctest::Approx(0.05));
    // An empty second part (no mirror) is a time of zero, not a missing sample.
    const std::optional<CopyParts> q = copyParts({100, 400, 400}, 64, kPeriod);
    REQUIRE(q);
    CHECK(q->second == doctest::Approx(0.0));
}

TEST_CASE("a copy's timestamps are read across the counter's wrap") {
    const std::uint64_t top = validMask(36);
    const std::optional<CopyParts> p = copyParts({top - 99, 100, 300}, 36, kPeriod);
    REQUIRE(p);
    CHECK(p->first == doctest::Approx(0.2));
    CHECK(p->second == doctest::Approx(0.2));
}

TEST_CASE("stamps out of order, no valid bits or a copy over a second give no sample") {
    CHECK_FALSE(copyParts({500, 400, 600}, 64, kPeriod));
    CHECK_FALSE(copyParts({400, 600, 500}, 64, kPeriod));
    CHECK_FALSE(copyParts({400, 500, 600}, 0, kPeriod));
    CHECK_FALSE(copyParts({0, 2'000'000, 2'000'001}, 64, kPeriod)); // 2 s
}

TEST_CASE("the window's per-image figure adds both directions and leaves the mirror apart") {
    CopyWindow w;
    // A stereo tick: two presents (eye L and eye R) into the ring, one copy into the headset's image.
    w.addVulkan({0.10, 0.04});
    w.addVulkan({0.12, 0.00});
    w.addD3d({0.30, 0.02});
    // A second tick.
    w.addVulkan({0.10, 0.04});
    w.addVulkan({0.08, 0.00});
    w.addD3d({0.26, 0.02});
    const CopyWindow::Report r = w.take();
    CHECK(r.ring.count == 4);
    CHECK(r.image.count == 2);
    CHECK(r.ring.max == doctest::Approx(0.12));
    // (0.40 into the ring + 0.56 into the image + 0.04 of layers) / 2 images.
    CHECK(r.perImageMs == doctest::Approx(0.50));
    CHECK(r.mirrorPerImageMs == doctest::Approx(0.04));
    const std::string line = formatCopyReport(r);
    CHECK(line.find("copy-gpu: last 10 s:") == 0);
    CHECK(line.find("0.500 ms GPU per new headset image (the mirror 0.040 ms more)") != std::string::npos);
    // The next window starts empty.
    const CopyWindow::Report next = w.take();
    CHECK(next.ring.count == 0);
    CHECK(next.perImageMs == doctest::Approx(0.0));
    CHECK(formatCopyReport(next).find("no samples") != std::string::npos);
}

TEST_CASE("the window keeps at most its cap of samples per kind") {
    CopyWindow w;
    for (std::size_t i = 0; i < CopyWindow::kMaxSamples + 10; ++i) {
        w.addVulkan({0.1, 0.0});
    }
    w.addD3d({0.2, 0.0});
    const CopyWindow::Report r = w.take();
    CHECK(r.ring.count == CopyWindow::kMaxSamples);
    CHECK(r.image.count == 1);
}
