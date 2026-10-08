#include "stereo_seq/png_writer.hpp"

#include "stereo_seq/deflate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace evr::stereo_seq {

namespace {

// VkFormat values (vulkan_core.h), kept here so this module needs no Vulkan headers.
constexpr std::int32_t kR8G8B8A8Unorm = 37;
constexpr std::int32_t kR8G8B8A8Srgb = 43;
constexpr std::int32_t kB8G8R8A8Unorm = 44;
constexpr std::int32_t kB8G8R8A8Srgb = 50;
constexpr std::int32_t kA2R10G10B10Unorm = 58;
constexpr std::int32_t kA2B10G10R10Unorm = 64;

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            t[n] = c;
        }
        return t;
    }();
    return table;
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void putChunk(std::vector<std::uint8_t>& out, const char type[4], const std::vector<std::uint8_t>& data) {
    putU32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t typeAt = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    putU32(out, crc32(out.data() + typeAt, 4 + data.size()));
}

} // namespace

std::optional<PixelLayout> pixelLayoutOfVkFormat(std::int32_t vkFormat) {
    switch (vkFormat) {
    case kB8G8R8A8Unorm:
    case kB8G8R8A8Srgb:
        return PixelLayout::Bgra8;
    case kR8G8B8A8Unorm:
    case kR8G8B8A8Srgb:
        return PixelLayout::Rgba8;
    case kA2B10G10R10Unorm:
        return PixelLayout::A2B10G10R10;
    case kA2R10G10B10Unorm:
        return PixelLayout::A2R10G10B10;
    default:
        return std::nullopt;
    }
}

std::vector<std::uint8_t> toRgb8(const std::uint8_t* pixels,
                                 std::uint32_t width,
                                 std::uint32_t height,
                                 std::size_t rowPitch,
                                 PixelLayout layout) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
    std::uint8_t* out = rgb.data();
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * rowPitch;
        for (std::uint32_t x = 0; x < width; ++x, out += 3) {
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
            switch (layout) {
            case PixelLayout::Bgra8:
                out[0] = p[2];
                out[1] = p[1];
                out[2] = p[0];
                break;
            case PixelLayout::Rgba8:
                out[0] = p[0];
                out[1] = p[1];
                out[2] = p[2];
                break;
            case PixelLayout::A2B10G10R10:
            case PixelLayout::A2R10G10B10: {
                std::uint32_t v = 0;
                std::memcpy(&v, p, sizeof(v));
                const std::uint32_t low = (v & 0x3FFu) >> 2;
                const std::uint32_t mid = ((v >> 10) & 0x3FFu) >> 2;
                const std::uint32_t high = ((v >> 20) & 0x3FFu) >> 2;
                const bool redLow = layout == PixelLayout::A2B10G10R10;
                out[0] = static_cast<std::uint8_t>(redLow ? low : high);
                out[1] = static_cast<std::uint8_t>(mid);
                out[2] = static_cast<std::uint8_t>(redLow ? high : low);
                break;
            }
            }
        }
    }
    return rgb;
}

AlphaStats alphaStats(const std::uint8_t* pixels,
                      std::uint32_t width,
                      std::uint32_t height,
                      std::size_t rowPitch,
                      PixelLayout layout) {
    AlphaStats stats;
    const std::uint64_t count = static_cast<std::uint64_t>(width) * height;
    if (count == 0) {
        return stats;
    }
    std::uint64_t sum = 0;
    std::uint64_t below = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * rowPitch;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
            std::uint32_t a = 0;
            if (layout == PixelLayout::Bgra8 || layout == PixelLayout::Rgba8) {
                a = p[3];
            } else {
                a = static_cast<std::uint32_t>(p[3] >> 6) * 85u; // 2-bit alpha: 0, 85, 170, 255
            }
            sum += a;
            below += a < 255 ? 1u : 0u;
            stats.min = a < stats.min ? a : stats.min;
        }
    }
    stats.mean = static_cast<double>(sum) / static_cast<double>(count);
    stats.belowOpaque = static_cast<double>(below) / static_cast<double>(count);
    return stats;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t crc) {
    const auto& table = crcTable();
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

namespace {

// Paeth's predictor (PNG filter type 4), written to compile to selects rather than branches.
inline int paeth(int a, int b, int c) {
    const int pa = std::abs(b - c); // |p - a| with p = a + b - c
    const int pb = std::abs(a - c);
    const int pc = std::abs(a + b - 2 * c);
    const int bc = pb <= pc ? b : c;
    return pa <= pb && pa <= pc ? a : bc;
}

// Row filters 1 to 4 (Sub, Up, Average, Paeth) of one scanline (`bytes` long, `bpp` bytes per pixel, the
// row above in `up`) into out[0..3]. The first pixel has no left neighbour (it counts as 0).
void filterRows(const std::uint8_t* row,
                const std::uint8_t* up,
                std::size_t bytes,
                std::size_t bpp,
                std::uint8_t* const out[4]) {
    for (std::size_t i = 0; i < bpp; ++i) {
        out[0][i] = row[i];
        out[1][i] = static_cast<std::uint8_t>(row[i] - up[i]);
        out[2][i] = static_cast<std::uint8_t>(row[i] - (up[i] >> 1));
        out[3][i] = static_cast<std::uint8_t>(row[i] - up[i]); // Paeth of (0, b, 0) is b
    }
    for (std::size_t i = bpp; i < bytes; ++i) {
        const int x = row[i];
        const int a = row[i - bpp];
        const int b = up[i];
        const int c = up[i - bpp];
        out[0][i] = static_cast<std::uint8_t>(x - a);
        out[1][i] = static_cast<std::uint8_t>(x - b);
        out[2][i] = static_cast<std::uint8_t>(x - ((a + b) >> 1));
        out[3][i] = static_cast<std::uint8_t>(x - paeth(a, b, c));
    }
}

// The sum of a filtered row's bytes read as signed magnitudes. The filter with the smallest sum is the one
// the PNG specification suggests per row; its output is the one deflate packs best, near enough.
std::uint32_t magnitudeSum(const std::uint8_t* v, std::size_t bytes) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < bytes; ++i) {
        const std::uint32_t x = v[i];
        sum += x < 128 ? x : 256u - x;
    }
    return sum;
}

// 8-bit PNG of `channels` (3: RGB, 4: RGBA) tightly packed bytes per pixel.
std::vector<std::uint8_t> encodePng(const std::uint8_t* pixels,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    std::uint32_t channels,
                                    Compression compression) {
    const std::size_t rowBytes = static_cast<std::size_t>(width) * channels;
    // Each scanline: its filter type byte, then the row filtered by whichever of None, Sub, Up, Average and
    // Paeth gives the smallest magnitude sum (stored: always None, as filters only help compression).
    std::vector<std::uint8_t> raw((rowBytes + 1) * height);
    const std::vector<std::uint8_t> zeros(rowBytes);
    std::vector<std::uint8_t> scratch(rowBytes * 4);
    std::uint8_t* const filtered[4] = {scratch.data(), scratch.data() + rowBytes,
                                       scratch.data() + 2 * rowBytes, scratch.data() + 3 * rowBytes};
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = pixels + y * rowBytes;
        if (compression == Compression::Stored) {
            std::uint8_t* out = raw.data() + y * (rowBytes + 1);
            out[0] = 0;
            std::memcpy(out + 1, row, rowBytes);
            continue;
        }
        filterRows(row, y == 0 ? zeros.data() : row - rowBytes, rowBytes, channels, filtered);
        const std::uint8_t* best = row;
        std::uint8_t type = 0;
        std::uint32_t bestSum = magnitudeSum(row, rowBytes);
        for (std::uint8_t f = 0; f < 4; ++f) {
            const std::uint32_t sum = magnitudeSum(filtered[f], rowBytes);
            if (sum < bestSum) {
                bestSum = sum;
                best = filtered[f];
                type = static_cast<std::uint8_t>(f + 1);
            }
        }
        std::uint8_t* out = raw.data() + y * (rowBytes + 1);
        out[0] = type;
        std::memcpy(out + 1, best, rowBytes);
    }
    const std::vector<std::uint8_t> z = zlibCompress(raw.data(), raw.size(), compression);

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<std::uint8_t> header;
    putU32(header, width);
    putU32(header, height);
    header.push_back(8);                     // bit depth
    header.push_back(channels == 4 ? 6 : 2); // colour type: RGBA or RGB
    header.push_back(0);                     // compression
    header.push_back(0);                     // filter method
    header.push_back(0);                     // no interlace
    putChunk(png, "IHDR", header);
    putChunk(png, "IDAT", z);
    putChunk(png, "IEND", {});
    return png;
}

} // namespace

std::vector<std::uint8_t>
encodePngRgb8(const std::uint8_t* rgb, std::uint32_t width, std::uint32_t height, Compression compression) {
    return encodePng(rgb, width, height, 3, compression);
}

std::vector<std::uint8_t>
encodePngRgba8(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height) {
    return encodePng(rgba, width, height, 4, Compression::Best);
}

} // namespace evr::stereo_seq
