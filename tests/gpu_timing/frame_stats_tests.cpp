#include "gpu_timing/frame_gpu.hpp"
#include "gpu_timing/stats.hpp"
#include "gpu_timing/timestamps.hpp"
#include "gpu_timing/timing_window.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

using evr::gpu_timing::BatchTimes;
using evr::gpu_timing::FrameEye;
using evr::gpu_timing::FrameGpu;
using evr::gpu_timing::FrameSample;
using evr::gpu_timing::kSampleEvery;
using evr::gpu_timing::kSampleRun;
using evr::gpu_timing::measureFrame;
using evr::gpu_timing::parseEnabled;
using evr::gpu_timing::parseMode;
using evr::gpu_timing::sampledFrame;
using evr::gpu_timing::sampledSummary;
using evr::gpu_timing::summarize;
using evr::gpu_timing::TimingMode;
using evr::gpu_timing::TimingWindow;
using evr::gpu_timing::validMask;

namespace {

// Period 1000 ns: one tick is 1 us, 1000 ticks are 1 ms.
constexpr float kPeriod = 1000.0f;

BatchTimes batch(std::uint64_t begin, std::uint64_t end, std::uint32_t family = 0, std::uint32_t bits = 64) {
    return BatchTimes{begin, end, family, bits};
}

FrameSample sample(std::uint64_t frame, FrameEye eye, double cpuMs, double busyMs) {
    FrameSample s;
    s.frame = frame;
    s.eye = eye;
    s.cpuMs = cpuMs;
    s.gpu.batches = busyMs > 0.0 ? 1 : 0;
    s.gpu.busyMs = busyMs;
    s.gpu.spanMs = busyMs;
    s.gpu.familyBusyMs[0] = busyMs;
    return s;
}

} // namespace

TEST_CASE("a frame's span runs from the first start to the last end, its busy time skips the gaps") {
    const FrameGpu f =
        measureFrame({batch(10'000, 12'000), batch(15'000, 16'000), batch(16'000, 20'000)}, kPeriod);
    CHECK(f.batches == 3);
    CHECK(f.spanMs == doctest::Approx(10.0));
    CHECK(f.busyMs == doctest::Approx(7.0));
    CHECK(f.familyBusyMs[0] == doctest::Approx(7.0));
}

TEST_CASE("overlapping batches on two queue families count once in the total, fully per family") {
    // Family 0: 0..10 ms; family 2 (async compute): 4..14 ms.
    const FrameGpu f = measureFrame({batch(0, 10'000, 0), batch(4'000, 14'000, 2)}, kPeriod);
    CHECK(f.spanMs == doctest::Approx(14.0));
    CHECK(f.busyMs == doctest::Approx(14.0));
    CHECK(f.familyBusyMs[0] == doctest::Approx(10.0));
    CHECK(f.familyBusyMs[2] == doctest::Approx(10.0));
    CHECK(f.familyBusyMs[1] == 0.0);
}

TEST_CASE("a batch on another queue that started before the first submitted one extends the span") {
    const FrameGpu f = measureFrame({batch(5'000, 8'000, 0), batch(1'000, 3'000, 1)}, kPeriod);
    CHECK(f.spanMs == doctest::Approx(7.0));
    CHECK(f.busyMs == doctest::Approx(5.0));
}

TEST_CASE("a frame across the counter's wrap measures the same as one without") {
    const std::uint64_t top = validMask(40);
    // The first batch starts 2 ms before the 40-bit counter wraps and ends as it wraps; the second ends
    // 3 ms after the wrap.
    const FrameGpu wrapped = measureFrame({batch(top - 1'999, 0, 0, 40), batch(0, 3'000, 0, 40)}, kPeriod);
    CHECK(wrapped.spanMs == doctest::Approx(5.0));
    CHECK(wrapped.busyMs == doctest::Approx(5.0));
}

TEST_CASE("mixed valid bits compare at the narrowest") {
    const std::uint64_t top36 = validMask(36);
    // Family 0 has 64 bits and a value with bits above 36 set; family 1 wraps at 36 bits.
    const std::uint64_t high = std::uint64_t{7} << 36;
    const FrameGpu f =
        measureFrame({batch(high | (top36 - 999), high | top36, 0, 64), batch(0, 2'000, 1, 36)}, kPeriod);
    CHECK(f.spanMs == doctest::Approx(3.0));
}

TEST_CASE("an end before its start and an empty frame measure as nothing") {
    const FrameGpu f = measureFrame({batch(5'000, 4'000)}, kPeriod);
    CHECK(f.busyMs == 0.0);
    CHECK(f.spanMs == 0.0);
    const FrameGpu none = measureFrame({}, kPeriod);
    CHECK(none.batches == 0);
    CHECK(none.busyMs == 0.0);
    CHECK(measureFrame({batch(0, 5'000, 0, 0)}, kPeriod).busyMs == 0.0);
}

TEST_CASE("families past the tracked ones count in the totals only") {
    const FrameGpu f = measureFrame({batch(0, 1'000, 7)}, kPeriod);
    CHECK(f.busyMs == doctest::Approx(1.0));
    for (const double v : f.familyBusyMs) {
        CHECK(v == 0.0);
    }
}

TEST_CASE("summaries use nearest-rank percentiles") {
    std::vector<double> values;
    for (int i = 100; i >= 1; --i) {
        values.push_back(static_cast<double>(i));
    }
    const auto s = summarize(values);
    CHECK(s.count == 100);
    CHECK(s.mean == doctest::Approx(50.5));
    CHECK(s.p50 == 50.0);
    CHECK(s.p95 == 95.0);
    CHECK(s.p99 == 99.0);
    CHECK(s.max == 100.0);

    const auto one = summarize({4.0});
    CHECK(one.p50 == 4.0);
    CHECK(one.p99 == 4.0);
    const auto three = summarize({3.0, 1.0, 2.0});
    CHECK(three.p50 == 2.0);
    CHECK(three.p95 == 3.0);
    CHECK(summarize({}).count == 0);
}

TEST_CASE("summaries format for the log") {
    CHECK(evr::gpu_timing::formatSummary(summarize({})) == "no samples");
    CHECK(evr::gpu_timing::formatSummary(summarize({1.0, 2.0, 3.0, 4.0})) ==
          "mean/p50/p95/p99/max 2.50/2.00/4.00/4.00/4.00 ms (n 4)");
}

TEST_CASE("an explicit yes is 1, on or true") {
    CHECK(parseEnabled(L"1"));
    CHECK(parseEnabled(L"on"));
    CHECK(parseEnabled(L"true"));
    CHECK_FALSE(parseEnabled(L""));
    CHECK_FALSE(parseEnabled(L"0"));
    CHECK_FALSE(parseEnabled(L"yes please"));
}

TEST_CASE("the window keeps each eye apart and pairs eye L with the next frame's eye R into a tick") {
    TimingWindow w;
    w.add(sample(1, FrameEye::Left, 5.0, 4.0));
    w.add(sample(2, FrameEye::Right, 6.0, 3.0));
    w.add(sample(3, FrameEye::Left, 5.0, 4.5));
    w.add(sample(4, FrameEye::Right, 6.0, 3.5));
    w.addPoseAge(20.0);
    w.addPoseAge(30.0);
    const auto r = w.report();
    CHECK(r.frames == 4);
    CHECK(r.ticks == 2);
    CHECK(r.busy[1].count == 2);
    CHECK(r.busy[1].mean == doctest::Approx(4.25));
    CHECK(r.busy[2].mean == doctest::Approx(3.25));
    CHECK(r.busy[0].count == 0);
    CHECK(r.tickBusy.mean == doctest::Approx(7.5));
    CHECK(r.tickCpu.mean == doctest::Approx(11.0));
    CHECK(r.poseAge.mean == doctest::Approx(25.0));
    CHECK(r.familyBusyMean[0] == doctest::Approx(3.75));
}

TEST_CASE("an eye R without its eye L just before it makes no tick") {
    TimingWindow w;
    w.add(sample(1, FrameEye::Left, 5.0, 4.0));
    w.add(sample(2, FrameEye::Mono, 5.0, 4.0));
    w.add(sample(3, FrameEye::Right, 5.0, 4.0)); // its eye L was not the frame before
    w.add(sample(5, FrameEye::Left, 5.0, 4.0));
    w.add(sample(7, FrameEye::Right, 5.0, 4.0)); // a frame between them was lost
    const auto r = w.report();
    CHECK(r.ticks == 0);
    CHECK(r.tickCpu.count == 0);
    CHECK(r.busy[0].count == 1);
}

TEST_CASE("untimed frames count for the CPU only, and clearing starts a new period") {
    TimingWindow w;
    w.add(sample(1, FrameEye::Mono, 8.0, 0.0));
    w.add(sample(2, FrameEye::Mono, 0.0, 2.0)); // no present before it: no CPU interval
    auto r = w.report();
    CHECK(r.frames == 2);
    CHECK(r.cpu[0].count == 1);
    CHECK(r.busy[0].count == 1);
    CHECK(r.familyBusyMean[0] == doctest::Approx(2.0));
    w.add(sample(3, FrameEye::Left, 5.0, 4.0));
    w.clear();
    w.add(sample(4, FrameEye::Right, 5.0, 4.0));
    r = w.report();
    CHECK(r.frames == 1);
    CHECK(r.ticks == 0);
}

TEST_CASE("GPU timing is off unless ETERNALVR_GPU_TIMING asks for sampled or every frame") {
    CHECK(parseMode(false, L"") == TimingMode::Off);
    CHECK(parseMode(true, L"") == TimingMode::Off);
    CHECK(parseMode(true, L"yes please") == TimingMode::Off);
    CHECK(parseMode(true, L"0") == TimingMode::Off);
    CHECK(parseMode(true, L"off") == TimingMode::Off);
    CHECK(parseMode(true, L"false") == TimingMode::Off);
    CHECK(parseMode(true, L"sample") == TimingMode::Sampled);
    CHECK(parseMode(true, L"sampled") == TimingMode::Sampled);
    CHECK(parseMode(true, L"1") == TimingMode::EveryFrame);
    CHECK(parseMode(true, L"on") == TimingMode::EveryFrame);
    CHECK(parseMode(true, L"true") == TimingMode::EveryFrame);
}

TEST_CASE("sampling takes the first three frames of every 45, so a run holds eye L then eye R") {
    std::vector<std::uint64_t> sampled;
    for (std::uint64_t frame = 0; frame <= 2 * kSampleEvery + 1; ++frame) {
        if (sampledFrame(frame)) {
            sampled.push_back(frame);
        }
    }
    CHECK(kSampleRun == 3);
    CHECK(sampled == std::vector<std::uint64_t>{1, 2, 3, 46, 47, 48, 91});
    // Under Route S frames alternate eyes: three in a row always hold an eye L followed by its eye R.
    TimingWindow w;
    w.add(sample(46, FrameEye::Right, 5.0, 3.0));
    w.add(sample(47, FrameEye::Left, 5.0, 4.0));
    w.add(sample(48, FrameEye::Right, 5.0, 3.5));
    CHECK(w.report().ticks == 1);
}

TEST_CASE("the sampled line gives each eye's and the tick's GPU busy average and longest") {
    TimingWindow w;
    CHECK(sampledSummary(w.report()) == "no frame timed");
    w.add(sample(1, FrameEye::Left, 5.0, 4.0));
    w.add(sample(2, FrameEye::Right, 6.0, 3.0));
    w.add(sample(46, FrameEye::Left, 5.0, 6.0));
    w.add(sample(47, FrameEye::Right, 6.0, 5.0));
    CHECK(sampledSummary(w.report()) ==
          "eye L GPU busy 5.00 ms average, 6.00 ms longest (n 2); eye R GPU busy 4.00 ms average, 5.00 ms "
          "longest (n 2); stereo tick GPU busy 9.00 ms average, 11.00 ms longest (n 2)");
    TimingWindow mono;
    mono.add(sample(1, FrameEye::Mono, 8.0, 2.5));
    CHECK(sampledSummary(mono.report()) == "mono GPU busy 2.50 ms average, 2.50 ms longest (n 1)");
}
