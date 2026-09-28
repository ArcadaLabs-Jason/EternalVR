#pragma once

// The motion-vector capture (ETERNALVR_CAPTURE_MOTION, docs/rig-findings/stereo-temporal.md section 2.2): the
// game's velocity targets `_motionVector0/1`, which TAA and DLSS read, copied per eye so eye R's motion
// vectors can be compared with eye L's. This module holds the parts that need no engine or Vulkan: which
// images the layer prepares for copying, the format table and the statistics written next to each capture.
//
// Plain integers only (Vulkan enum values as numbers), so this module needs no Vulkan headers.

#include "ui_layer/gui_target.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace evr::ui_layer {

namespace vk {
inline constexpr std::int32_t kFormatR16G16Unorm = 77;
inline constexpr std::int32_t kFormatR16G16Snorm = 78;
inline constexpr std::int32_t kFormatR16G16Sfloat = 83;
inline constexpr std::int32_t kFormatR32G32Sfloat = 103;
} // namespace vk

// Bytes per pixel of a two-channel format the capture takes, or 0 for any other format.
std::uint32_t motionBytesPerPixel(std::int32_t format);

// A single-sample 2D image with one mip and layer, a two-channel format above, sampled, and drawn to (colour
// attachment or storage): TRANSFER_SRC is added (the usage this returns) and its layout followed. The format
// the engine uses for its motion vectors is not known for certain, so every such image is prepared; the
// capture copies only the ones the engine binds as velocity.
std::optional<std::uint32_t> motionCandidateUsage(const ImageCreateDesc& desc);

// What one eye's velocity image holds, from its raw pixels (row after row, no padding).
struct MotionStats {
    std::uint64_t pixels = 0;
    std::uint64_t nonZero = 0;   // pixels with either channel non-zero
    std::uint64_t nonFinite = 0; // NaN or infinity in either channel (float formats)
    double meanAbs[2] = {};      // over all pixels, per channel
    double maxAbs[2] = {};
    double meanAbsNonZero[2] = {}; // over the non-zero pixels
};
// Nullopt for a format motionBytesPerPixel does not know, or too few bytes.
std::optional<MotionStats> motionStats(const std::uint8_t* data,
                                       std::size_t size,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       std::int32_t format);

// One line for the text file and the log.
std::string describe(const MotionStats& s);

// IEEE half to float (the R16G16_SFLOAT channels).
float halfToFloat(std::uint16_t h);

} // namespace evr::ui_layer
