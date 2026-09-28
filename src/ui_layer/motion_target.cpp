#include "ui_layer/motion_target.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace evr::ui_layer {

std::uint32_t motionBytesPerPixel(std::int32_t format) {
    switch (format) {
    case vk::kFormatR16G16Unorm:
    case vk::kFormatR16G16Snorm:
    case vk::kFormatR16G16Sfloat:
        return 4;
    case vk::kFormatR32G32Sfloat:
        return 8;
    default:
        return 0;
    }
}

std::optional<std::uint32_t> motionCandidateUsage(const ImageCreateDesc& desc) {
    const bool drawn = (desc.usage & (vk::kUsageColorAttachment | vk::kUsageStorage)) != 0;
    if (desc.imageType != vk::kImageType2D || motionBytesPerPixel(desc.format) == 0 || desc.depth != 1 ||
        desc.mipLevels != 1 || desc.arrayLayers != 1 || desc.samples != 1 || !drawn ||
        (desc.usage & vk::kUsageSampled) == 0 || desc.width == 0 || desc.height == 0) {
        return std::nullopt;
    }
    return desc.usage | vk::kUsageTransferSrc;
}

float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = (h >> 15) & 1u;
    const std::uint32_t exponent = (h >> 10) & 0x1Fu;
    const std::uint32_t mantissa = h & 0x3FFu;
    float value = 0.0f;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24); // subnormal
    } else if (exponent == 31) {
        value = mantissa ? std::nanf("") : INFINITY;
    } else {
        value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
    }
    return sign ? -value : value;
}

namespace {

float channel(const std::uint8_t* pixel, int c, std::int32_t format) {
    switch (format) {
    case vk::kFormatR16G16Sfloat: {
        std::uint16_t h = 0;
        std::memcpy(&h, pixel + c * 2, sizeof(h));
        return halfToFloat(h);
    }
    case vk::kFormatR16G16Unorm: {
        std::uint16_t u = 0;
        std::memcpy(&u, pixel + c * 2, sizeof(u));
        return static_cast<float>(u) / 65535.0f;
    }
    case vk::kFormatR16G16Snorm: {
        std::int16_t s = 0;
        std::memcpy(&s, pixel + c * 2, sizeof(s));
        return s < -32767 ? -1.0f : static_cast<float>(s) / 32767.0f;
    }
    case vk::kFormatR32G32Sfloat: {
        float f = 0.0f;
        std::memcpy(&f, pixel + c * 4, sizeof(f));
        return f;
    }
    default:
        return 0.0f;
    }
}

} // namespace

std::optional<MotionStats> motionStats(const std::uint8_t* data,
                                       std::size_t size,
                                       std::uint32_t width,
                                       std::uint32_t height,
                                       std::int32_t format) {
    const std::uint32_t bpp = motionBytesPerPixel(format);
    const std::size_t pixels = std::size_t{width} * height;
    if (bpp == 0 || !data || size < pixels * bpp) {
        return std::nullopt;
    }
    MotionStats s;
    s.pixels = pixels;
    double sum[2] = {};
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t* p = data + i * bpp;
        const float v[2] = {channel(p, 0, format), channel(p, 1, format)};
        if (!std::isfinite(v[0]) || !std::isfinite(v[1])) {
            ++s.nonFinite;
            continue;
        }
        if (v[0] == 0.0f && v[1] == 0.0f) {
            continue;
        }
        ++s.nonZero;
        for (int c = 0; c < 2; ++c) {
            const double a = std::fabs(static_cast<double>(v[c]));
            sum[c] += a;
            if (a > s.maxAbs[c]) {
                s.maxAbs[c] = a;
            }
        }
    }
    for (int c = 0; c < 2; ++c) {
        s.meanAbs[c] = pixels ? sum[c] / static_cast<double>(pixels) : 0.0;
        s.meanAbsNonZero[c] = s.nonZero ? sum[c] / static_cast<double>(s.nonZero) : 0.0;
    }
    return s;
}

std::string describe(const MotionStats& s) {
    char line[320];
    std::snprintf(
        line, sizeof(line),
        "%llu pixel(s), %llu non-zero (%.2f%%), %llu not finite; mean |x| %.6g |y| %.6g (non-zero: %.6g "
        "%.6g); max |x| %.6g |y| %.6g",
        static_cast<unsigned long long>(s.pixels), static_cast<unsigned long long>(s.nonZero),
        s.pixels ? 100.0 * static_cast<double>(s.nonZero) / static_cast<double>(s.pixels) : 0.0,
        static_cast<unsigned long long>(s.nonFinite), s.meanAbs[0], s.meanAbs[1], s.meanAbsNonZero[0],
        s.meanAbsNonZero[1], s.maxAbs[0], s.maxAbs[1]);
    return line;
}

} // namespace evr::ui_layer
