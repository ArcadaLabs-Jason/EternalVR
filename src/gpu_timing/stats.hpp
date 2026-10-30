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

// "0", "off" or "false": a switch that is on by default turned off.
bool parseDisabled(std::wstring_view value);

// GPU timing's mode: off, a few frames in every second (the default) or every frame.
enum class TimingMode : std::uint8_t { Off, Sampled, EveryFrame };

// ETERNALVR_GPU_TIMING: "1", "on" or "true" times every frame; "0", "off" or "false" turns GPU timing off;
// unset or anything else ("sample" and "sampled" included) times a few frames in every second, so a
// player's log says how busy the GPU is.
TimingMode parseMode(bool set, std::wstring_view value);

// ETERNALVR_CPU_TIMING: on unless set to "0", "off" or "false", so a player's log says how busy the
// processor is.
bool parseCpuTiming(bool set, std::wstring_view value);

} // namespace evr::gpu_timing
