#pragma once

// One game frame's GPU time from the timestamps around its submit batches (docs/VR_STEREO.md, "GPU
// timing"). A frame is every batch submitted on the device since the previous present, on any queue.

#include <array>
#include <cstdint>
#include <vector>

namespace evr::gpu_timing {

// Queue families 0 to kFamilies - 1 get their own busy time; later ones count in the totals only.
inline constexpr std::uint32_t kFamilies = 4;

struct BatchTimes {
    std::uint64_t begin = 0; // timestamp before the batch's work
    std::uint64_t end = 0;   // timestamp after it
    std::uint32_t family = 0;
    std::uint32_t validBits = 64; // the family's timestampValidBits
};

struct FrameGpu {
    std::uint32_t batches = 0;
    // From the earliest batch start to the latest batch end, idle gaps included.
    double spanMs = 0.0;
    // Time some batch was running (the union of the batches' intervals): the frame's GPU cost.
    double busyMs = 0.0;
    std::array<double, kFamilies> familyBusyMs{}; // the same per queue family
};

// Batches in submission order; timestamps compared at the narrowest valid bits among them, relative to
// the first batch's start (so a counter wrap within the frame is harmless). A batch whose end reads
// before its start counts as empty.
FrameGpu measureFrame(const std::vector<BatchTimes>& batches, float periodNs);

} // namespace evr::gpu_timing
