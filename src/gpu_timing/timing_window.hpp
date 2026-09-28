#pragma once

// The measured frames of one 10 s log period (docs/VR_STEREO.md, "GPU timing"): per eye (mono, eye L,
// eye R), per stereo tick (an eye L frame followed by the next frame as eye R), and the pose age the
// presenter reports.

#include "gpu_timing/frame_gpu.hpp"
#include "gpu_timing/stats.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace evr::gpu_timing {

// The values of stereo_seq::Eye.
enum class FrameEye : std::uint8_t { Mono = 0, Left = 1, Right = 2 };

const char* frameEyeName(FrameEye eye);

struct FrameSample {
    std::uint64_t frame = 0; // present count on the device (consecutive frames differ by one)
    FrameEye eye = FrameEye::Mono;
    double cpuMs = 0.0; // since the previous present (0: none before)
    FrameGpu gpu;       // no batches: nothing was timed (the frame counts for the CPU only)
};

class TimingWindow {
public:
    void add(const FrameSample& sample);
    void addPoseAge(double ms);

    struct Report {
        std::uint64_t frames = 0;
        std::uint64_t ticks = 0;
        std::array<Summary, 3> busy{}; // indexed by FrameEye
        std::array<Summary, 3> span{};
        std::array<Summary, 3> cpu{};
        Summary tickBusy; // eye L busy + eye R busy
        Summary tickCpu;  // eye L's CPU interval + eye R's (present to present of eye L frames)
        Summary poseAge;
        std::array<double, kFamilies> familyBusyMean{}; // per timed frame
    };
    [[nodiscard]] Report report() const;

    // Starts a new period (a pending eye L frame is forgotten).
    void clear();

private:
    std::uint64_t frames_ = 0;
    std::uint64_t timed_ = 0;
    std::array<std::vector<double>, 3> busy_;
    std::array<std::vector<double>, 3> span_;
    std::array<std::vector<double>, 3> cpu_;
    std::vector<double> tickBusy_;
    std::vector<double> tickCpu_;
    std::vector<double> poseAge_;
    std::array<double, kFamilies> familySum_{};
    std::optional<FrameSample> left_; // the latest eye L frame, waiting for its eye R
};

} // namespace evr::gpu_timing
