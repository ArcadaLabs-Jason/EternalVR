#pragma once

// The presenter's interop copies' GPU time (ETERNALVR_TEST_COPY_GPU, a rig switch;
// docs/rig-findings/perf-targets.md): three timestamps around each copy, before it, between its two parts and
// after it, and the 10 s window of them. The present hook's copy into the ring has the eyes, the GUI image
// and the depth as its first part and the desktop mirror and the rig's captures as its second; the XR
// worker's D3D12 copy has the ring into the headset's image first and the GUI and depth images second.

#include "gpu_timing/stats.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace evr::gpu_timing {

// One copy's two parts, in milliseconds.
struct CopyParts {
    double first = 0.0;
    double second = 0.0;
};

// The two parts of a copy from its three timestamps on a counter of `validBits` bits ticking every `periodNs`
// nanoseconds; none when the stamps are out of order (a result never written, a counter reset) or the copy
// took more than a second (not a copy's time).
std::optional<CopyParts>
copyParts(const std::array<std::uint64_t, 3>& stamps, std::uint32_t validBits, float periodNs);

class CopyWindow {
public:
    // Samples kept per kind in one window; later ones in the same window are dropped (a window holds about
    // 2200 presents and 900 headset images at 90 Hz).
    static constexpr std::size_t kMaxSamples = 16384;

    void addVulkan(const CopyParts& parts); // a present's copy into the ring
    void addD3d(const CopyParts& parts);    // an XR frame's copy into the headset's images

    struct Report {
        Summary ring;   // the eyes, the GUI image and the depth into the ring, per present
        Summary mirror; // the desktop mirror and the rig's captures, per present
        Summary image;  // the ring into the headset's image, per new image
        Summary layers; // the GUI and depth images, per new image
        // Both directions of the interop copies (ring, image and layers, not the mirror) per new image the
        // headset got, and the mirror's share on top; 0 without a D3D12 copy in the window.
        double perImageMs = 0.0;
        double mirrorPerImageMs = 0.0;
    };
    // The window's report; the next window starts empty.
    Report take();

private:
    std::vector<double> ring_, mirror_, image_, layers_;
};

// "copy-gpu: ..." for the 10 s line.
std::string formatCopyReport(const CopyWindow::Report& r);

} // namespace evr::gpu_timing
