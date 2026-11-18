#include "gpu_timing/copy_window.hpp"

#include "gpu_timing/timestamps.hpp"

#include <cstdio>
#include <utility>

namespace evr::gpu_timing {

namespace {

constexpr double kLongestCopyMs = 1000.0;

void keep(std::vector<double>& samples, double ms) {
    if (samples.size() < CopyWindow::kMaxSamples) {
        samples.push_back(ms);
    }
}

double sumOf(const Summary& s) {
    return s.mean * static_cast<double>(s.count);
}

} // namespace

std::optional<CopyParts>
copyParts(const std::array<std::uint64_t, 3>& stamps, std::uint32_t validBits, float periodNs) {
    const std::int64_t first = signedTicks(stamps[0], stamps[1], validBits);
    const std::int64_t second = signedTicks(stamps[1], stamps[2], validBits);
    if (validBits == 0 || first < 0 || second < 0) {
        return std::nullopt;
    }
    CopyParts parts{ticksToMs(static_cast<double>(first), periodNs),
                    ticksToMs(static_cast<double>(second), periodNs)};
    if (parts.first + parts.second > kLongestCopyMs) {
        return std::nullopt;
    }
    return parts;
}

void CopyWindow::addVulkan(const CopyParts& parts) {
    keep(ring_, parts.first);
    keep(mirror_, parts.second);
}

void CopyWindow::addD3d(const CopyParts& parts) {
    keep(image_, parts.first);
    keep(layers_, parts.second);
}

CopyWindow::Report CopyWindow::take() {
    Report r;
    r.ring = summarize(std::exchange(ring_, {}));
    r.mirror = summarize(std::exchange(mirror_, {}));
    r.image = summarize(std::exchange(image_, {}));
    r.layers = summarize(std::exchange(layers_, {}));
    if (r.image.count != 0) {
        const auto images = static_cast<double>(r.image.count);
        r.perImageMs = (sumOf(r.ring) + sumOf(r.image) + sumOf(r.layers)) / images;
        r.mirrorPerImageMs = sumOf(r.mirror) / images;
    }
    return r;
}

std::string formatCopyReport(const CopyWindow::Report& r) {
    char text[160];
    std::snprintf(text, sizeof(text),
                  "; the interop copies %.3f ms GPU per new headset image (the mirror %.3f ms more)",
                  r.perImageMs, r.mirrorPerImageMs);
    return "copy-gpu: last 10 s: into the ring (eyes, GUI, depth) per present " + formatSummary(r.ring) +
           "; desktop mirror and captures per present " + formatSummary(r.mirror) +
           "; the ring into the headset's image per new image " + formatSummary(r.image) +
           "; GUI and depth images per new image " + formatSummary(r.layers) + text;
}

} // namespace evr::gpu_timing
