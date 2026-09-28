#include "gpu_timing/stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace evr::gpu_timing {

namespace {

// Nearest rank: the smallest sample with at least `percent` of the samples at or below it.
double percentile(const std::vector<double>& sorted, double percent) {
    const double rank = std::ceil(percent / 100.0 * static_cast<double>(sorted.size()));
    const std::size_t index = rank < 1.0 ? 0 : static_cast<std::size_t>(rank) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

} // namespace

Summary summarize(std::vector<double> values) {
    Summary s;
    if (values.empty()) {
        return s;
    }
    std::sort(values.begin(), values.end());
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    s.count = values.size();
    s.mean = sum / static_cast<double>(values.size());
    s.p50 = percentile(values, 50.0);
    s.p95 = percentile(values, 95.0);
    s.p99 = percentile(values, 99.0);
    s.max = values.back();
    return s;
}

std::string formatSummary(const Summary& s) {
    if (s.count == 0) {
        return "no samples";
    }
    char text[128];
    std::snprintf(text, sizeof(text), "mean/p50/p95/p99/max %.2f/%.2f/%.2f/%.2f/%.2f ms (n %zu)", s.mean,
                  s.p50, s.p95, s.p99, s.max, s.count);
    return text;
}

bool parseEnabled(std::wstring_view value) {
    return value == L"1" || value == L"on" || value == L"true";
}

} // namespace evr::gpu_timing
