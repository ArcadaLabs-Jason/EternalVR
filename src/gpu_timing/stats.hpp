#pragma once

// Summaries of timing samples for the 10 s log lines (docs/VR_STEREO.md, "GPU timing").

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace evr::gpu_timing {

struct Summary {
    std::size_t count = 0;
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

// Mean, nearest-rank percentiles and maximum; all zero for no samples.
Summary summarize(std::vector<double> values);

// "mean/p50/p95/p99/max 1.23/1.10/2.00/2.50/3.00 ms (n 600)", or "no samples".
std::string formatSummary(const Summary& s);

// "1", "on" or "true" (ETERNALVR_TEST_CB_CHECK and the like); anything else is no.
bool parseEnabled(std::wstring_view value);

// GPU timing's mode: off (the default), a few frames in every second or every frame.
enum class TimingMode : std::uint8_t { Off, Sampled, EveryFrame };

// ETERNALVR_GPU_TIMING: "1", "on" or "true" times every frame; "sample" or "sampled" times a few frames in
// every second; unset (or anything else, "0" included) leaves GPU timing off.
TimingMode parseMode(bool set, std::wstring_view value);

} // namespace evr::gpu_timing
