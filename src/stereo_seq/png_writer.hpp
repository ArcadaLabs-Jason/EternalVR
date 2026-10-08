#pragma once

// Minimal image output for the per-eye capture (ETERNALVR_CAPTURE_EYES, docs/VR_STEREO.md): swapchain
// pixels to 8-bit RGB, and RGB to a PNG file image. Each row takes the PNG filter that suits it best and
// the rows are compressed by our own deflate (deflate.hpp), so no compression library is needed. Any PNG
// reader opens them.

#include "stereo_seq/deflate.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace evr::stereo_seq {

// Layouts of the 4-byte swapchain formats the game presents in.
enum class PixelLayout : std::uint8_t {
    Bgra8,       // VK_FORMAT_B8G8R8A8_UNORM / _SRGB
    Rgba8,       // VK_FORMAT_R8G8B8A8_UNORM / _SRGB
    A2B10G10R10, // VK_FORMAT_A2B10G10R10_UNORM_PACK32 (R in the low bits)
    A2R10G10B10, // VK_FORMAT_A2R10G10B10_UNORM_PACK32 (B in the low bits)
};

// The layout of a VkFormat value, or nullopt for a format the capture does not handle.
std::optional<PixelLayout> pixelLayoutOfVkFormat(std::int32_t vkFormat);

// Tightly packed RGB8 from rows of 4-byte pixels (`rowPitch` bytes apart). 10-bit channels keep their top
// 8 bits.
std::vector<std::uint8_t> toRgb8(const std::uint8_t* pixels,
                                 std::uint32_t width,
                                 std::uint32_t height,
                                 std::size_t rowPitch,
                                 PixelLayout layout);

// How opaque an image is: the lowest and the mean alpha (0..255; the 2-bit alpha of the 10-bit layouts is
// scaled to 0..255) and the share of pixels below fully opaque. Some runtimes blend layer 0 by its alpha, so
// a projection layer is opaque only when every pixel's alpha is 255.
struct AlphaStats {
    std::uint32_t min = 255;
    double mean = 255.0;
    double belowOpaque = 0.0; // 0..1
};
AlphaStats alphaStats(const std::uint8_t* pixels,
                      std::uint32_t width,
                      std::uint32_t height,
                      std::size_t rowPitch,
                      PixelLayout layout);

// A complete PNG file (8-bit RGB, not interlaced) of tightly packed RGB8 rows. Compression::Stored skips the
// row filters and the compression (about 10x faster, about 2x larger) for captures taken while the game runs.
std::vector<std::uint8_t> encodePngRgb8(const std::uint8_t* rgb,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        Compression compression = Compression::Best);

// A complete PNG file (8-bit RGBA, not interlaced) of tightly packed RGBA8 rows (the UI capture keeps the
// alpha channel of the game's GUI target).
std::vector<std::uint8_t> encodePngRgba8(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc = 0);

} // namespace evr::stereo_seq
