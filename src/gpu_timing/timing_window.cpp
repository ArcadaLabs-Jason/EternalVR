#include "gpu_timing/timing_window.hpp"

#include <cstddef>
#include <cstdio>

namespace evr::gpu_timing {

const char* frameEyeName(FrameEye eye) {
    switch (eye) {
    case FrameEye::Left:
        return "L";
    case FrameEye::Right:
        return "R";
    case FrameEye::Mono:
        break;
    }
    return "mono";
}

void TimingWindow::add(const FrameSample& sample) {
    const auto e = static_cast<std::size_t>(sample.eye) < 3 ? static_cast<std::size_t>(sample.eye) : 0;
    ++frames_;
    const bool timed = sample.gpu.batches > 0;
    if (sample.cpuMs > 0.0) {
        cpu_[e].push_back(sample.cpuMs);
    }
    if (timed) {
        ++timed_;
        busy_[e].push_back(sample.gpu.busyMs);
        span_[e].push_back(sample.gpu.spanMs);
        for (std::uint32_t f = 0; f < kFamilies; ++f) {
            familySum_[f] += sample.gpu.familyBusyMs[f];
        }
    }
    if (sample.eye == FrameEye::Left) {
        left_ = sample;
        return;
    }
    if (sample.eye == FrameEye::Right && left_ && left_->frame + 1 == sample.frame) {
        if (timed && left_->gpu.batches > 0) {
            tickBusy_.push_back(left_->gpu.busyMs + sample.gpu.busyMs);
        }
        if (left_->cpuMs > 0.0 && sample.cpuMs > 0.0) {
            tickCpu_.push_back(left_->cpuMs + sample.cpuMs);
        }
    }
    left_.reset();
}

void TimingWindow::addPoseAge(double ms) {
    poseAge_.push_back(ms);
}

TimingWindow::Report TimingWindow::report() const {
    Report r;
    r.frames = frames_;
    r.ticks = tickBusy_.size();
    for (std::size_t e = 0; e < 3; ++e) {
        r.busy[e] = summarize(busy_[e]);
        r.span[e] = summarize(span_[e]);
        r.cpu[e] = summarize(cpu_[e]);
    }
    r.tickBusy = summarize(tickBusy_);
    r.tickCpu = summarize(tickCpu_);
    r.poseAge = summarize(poseAge_);
    for (std::uint32_t f = 0; f < kFamilies; ++f) {
        r.familyBusyMean[f] = timed_ ? familySum_[f] / static_cast<double>(timed_) : 0.0;
    }
    return r;
}

void TimingWindow::clear() {
    *this = TimingWindow{};
}

bool sampledFrame(std::uint64_t frame) {
    return frame > 0 && (frame - 1) % kSampleEvery < kSampleRun;
}

std::string sampledSummary(const TimingWindow::Report& r) {
    std::string text;
    const auto add = [&text](const char* label, const Summary& s) {
        if (s.count == 0) {
            return;
        }
        char part[128];
        std::snprintf(part, sizeof(part), "%s GPU busy %.2f ms average, %.2f ms longest (n %zu)", label,
                      s.mean, s.max, s.count);
        text += (text.empty() ? "" : "; ") + std::string(part);
    };
    add("mono", r.busy[static_cast<std::size_t>(FrameEye::Mono)]);
    add("eye L", r.busy[static_cast<std::size_t>(FrameEye::Left)]);
    add("eye R", r.busy[static_cast<std::size_t>(FrameEye::Right)]);
    add("stereo tick", r.tickBusy);
    return text.empty() ? "no frame timed" : text;
}

} // namespace evr::gpu_timing
