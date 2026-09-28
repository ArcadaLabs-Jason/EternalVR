// GPU timing (gpu_timing.hpp): reading a frame's timestamps without waiting, the 10 s log summary and the
// per-frame CSV.

#include "gpu_timing/frame_gpu.hpp"
#include "gpu_timing/stats.hpp"
#include "vkcore/gpu_timing_impl.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <share.h>

#include <array>
#include <string>

namespace evr::vkcore::gpu_timing {

namespace {

enum class Read { Ready, NotReady, Failed };

// The pair's two timestamps if both are available (no wait).
Read readPair(TimingDevice& d, const Batch& b, gt::BatchTimes& out) {
    // Per query: the value, then its availability.
    std::array<std::uint64_t, 4> values{};
    const VkResult r = d.fn.getQueryPoolResults(
        d.device, d.pool, gt::QueryRing::beginQuery(b.pair), 2, sizeof(values), values.data(),
        2 * sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if (r != VK_SUCCESS && r != VK_NOT_READY) {
        return Read::Failed;
    }
    const std::uint32_t first = gt::QueryRing::beginQuery(b.pair);
    // A value equal to the one read at the query's last use is that use's: the pair's new command buffers
    // have not run yet (0 is the start value: never written).
    if (values[1] == 0 || values[3] == 0 || values[0] == d.lastValues[first] ||
        values[2] == d.lastValues[first + 1]) {
        return Read::NotReady;
    }
    out.begin = values[0];
    out.end = values[2];
    out.family = b.family;
    out.validBits = b.family < d.validBits.size() ? d.validBits[b.family] : 0;
    return Read::Ready;
}

// A frame longer than this is a misread (a pair given up on and written late), not a measurement.
constexpr double kImplausibleMs = 1000.0;

void releasePairs(TimingDevice& d, const Frame& f) {
    for (const Batch& b : f.batches) {
        d.ring.release(b.pair);
    }
}

void writeRow(TimingDevice& d, const gt::FrameSample& s, std::uint32_t untimed) {
    if (!d.csv) {
        return;
    }
    const gt::FrameGpu& g = s.gpu;
    std::fprintf(d.csv, "%llu,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u\n",
                 static_cast<unsigned long long>(s.frame), gt::frameEyeName(s.eye), s.cpuMs, g.spanMs,
                 g.busyMs, g.familyBusyMs[0], g.familyBusyMs[1], g.familyBusyMs[2], g.familyBusyMs[3],
                 g.batches, untimed);
}

unsigned long long delta(std::uint64_t now, std::uint64_t before) {
    return static_cast<unsigned long long>(now - before);
}

} // namespace

void resolveFrames(TimingDevice& d) {
    // The newest frame stays: the presenter tags its eye after this present's hook has run.
    while (d.closed.size() > 1) {
        Frame& f = d.closed.front();
        std::vector<gt::BatchTimes> times(f.batches.size());
        Read state = Read::Ready;
        for (std::size_t i = 0; i < f.batches.size() && state == Read::Ready; ++i) {
            state = readPair(d, f.batches[i], times[i]);
        }
        if (state != Read::Ready) {
            if (state == Read::NotReady && d.presents - f.id < kGiveUpPresents) {
                return; // later frames finish later: try again on the next present
            }
            ++d.counters.lostFrames;
            releasePairs(d, f);
            d.closed.pop_front();
            continue;
        }
        gt::FrameSample sample;
        sample.frame = f.id;
        sample.eye = f.eye;
        sample.cpuMs = f.cpuMs;
        sample.gpu = gt::measureFrame(times, d.periodNs);
        for (std::size_t i = 0; i < f.batches.size(); ++i) {
            d.lastValues[gt::QueryRing::beginQuery(f.batches[i].pair)] = times[i].begin;
            d.lastValues[gt::QueryRing::endQuery(f.batches[i].pair)] = times[i].end;
        }
        releasePairs(d, f);
        if (sample.gpu.spanMs > kImplausibleMs) {
            ++d.counters.lostFrames;
            d.closed.pop_front();
            continue;
        }
        d.window.add(sample);
        writeRow(d, sample, f.untimed);
        d.counters.timedBatches += f.batches.size();
        d.closed.pop_front();
    }
}

void reportIfDue(TimingDevice& d, Clock::time_point now) {
    for (const double age : d.poseAges) {
        d.window.addPoseAge(age);
    }
    d.poseAges.clear();
    const double seconds = std::chrono::duration<double>(now - d.lastReport).count();
    if (seconds < 10.0) {
        return;
    }
    const gt::TimingWindow::Report r = d.window.report();
    const Counters& c = d.counters;
    const Counters& l = d.reported;
    EVR_LOG("gpu: last %.1f s: %llu frame(s) measured, %llu stereo tick(s); %llu batch(es) timed, untimed "
            "%llu (ring full) / %llu (device group or protected) / %llu (no timestamps) / %llu (frame "
            "cap); %llu frame(s) lost; %u of %u query pairs in use",
            seconds, static_cast<unsigned long long>(r.frames), static_cast<unsigned long long>(r.ticks),
            delta(c.timedBatches, l.timedBatches), delta(c.ringFull, l.ringFull),
            delta(c.notBracketable, l.notBracketable), delta(c.noTimestamps, l.noTimestamps),
            delta(c.frameCap, l.frameCap), delta(c.lostFrames, l.lostFrames), d.ring.inUse(), kPairs);
    for (const gt::FrameEye eye : {gt::FrameEye::Mono, gt::FrameEye::Left, gt::FrameEye::Right}) {
        const auto e = static_cast<std::size_t>(eye);
        if (r.busy[e].count == 0 && r.cpu[e].count == 0) {
            continue;
        }
        EVR_LOG("gpu: %s%s frames: GPU busy %s; GPU span %s; CPU present interval %s",
                eye == gt::FrameEye::Mono ? "" : "eye ", gt::frameEyeName(eye),
                gt::formatSummary(r.busy[e]).c_str(), gt::formatSummary(r.span[e]).c_str(),
                gt::formatSummary(r.cpu[e]).c_str());
    }
    if (r.ticks > 0 || r.tickCpu.count > 0) {
        EVR_LOG("gpu: stereo tick (eye L + eye R): GPU busy %s; CPU %s",
                gt::formatSummary(r.tickBusy).c_str(), gt::formatSummary(r.tickCpu).c_str());
    }
    EVR_LOG(
        "gpu: pose age %s; GPU busy per frame by queue family: 0 %.2f ms, 1 %.2f ms, 2 %.2f ms, 3 %.2f ms",
        gt::formatSummary(r.poseAge).c_str(), r.familyBusyMean[0], r.familyBusyMean[1], r.familyBusyMean[2],
        r.familyBusyMean[3]);
    d.window.clear();
    d.reported = d.counters;
    d.lastReport = now;
    if (d.csv) {
        std::fflush(d.csv);
    }
}

void openCsv(TimingDevice& d) {
    const std::wstring dir = logDirectory();
    if (dir.empty() || d.csv) {
        return;
    }
    wchar_t name[96];
    swprintf_s(name, L"\\eternalvr-gpu-%lu.csv", GetCurrentProcessId());
    d.csv = _wfsopen((dir + name).c_str(), L"w", _SH_DENYWR);
    if (d.csv) {
        std::fputs("frame,eye,cpu_ms,gpu_span_ms,gpu_busy_ms,busy_f0_ms,busy_f1_ms,busy_f2_ms,busy_f3_ms,"
                   "batches,untimed_batches\n",
                   d.csv);
    }
}

void closeCsv(TimingDevice& d) {
    if (d.csv) {
        std::fclose(d.csv);
        d.csv = nullptr;
    }
}

} // namespace evr::vkcore::gpu_timing
