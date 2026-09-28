#include "ui_layer/motion_target.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using evr::ui_layer::halfToFloat;
using evr::ui_layer::ImageCreateDesc;
using evr::ui_layer::motionBytesPerPixel;
using evr::ui_layer::motionCandidateUsage;
using evr::ui_layer::motionStats;
namespace vk = evr::ui_layer::vk;

namespace {

ImageCreateDesc velocityDesc() {
    ImageCreateDesc d;
    d.imageType = vk::kImageType2D;
    d.format = vk::kFormatR16G16Sfloat;
    d.width = 1280;
    d.height = 1400;
    d.depth = 1;
    d.mipLevels = 1;
    d.arrayLayers = 1;
    d.samples = 1;
    d.usage = vk::kUsageColorAttachment | vk::kUsageSampled;
    return d;
}

void putHalf(std::vector<std::uint8_t>& data, std::size_t pixel, int channel, std::uint16_t h) {
    std::memcpy(data.data() + pixel * 4 + static_cast<std::size_t>(channel) * 2, &h, sizeof(h));
}

} // namespace

TEST_CASE("a drawn, sampled two-channel image gets TRANSFER_SRC") {
    const auto usage = motionCandidateUsage(velocityDesc());
    REQUIRE(usage.has_value());
    CHECK(*usage == (vk::kUsageColorAttachment | vk::kUsageSampled | vk::kUsageTransferSrc));
    ImageCreateDesc storage = velocityDesc();
    storage.usage = vk::kUsageStorage | vk::kUsageSampled;
    CHECK(motionCandidateUsage(storage).has_value());
    ImageCreateDesc wide = velocityDesc();
    wide.format = vk::kFormatR32G32Sfloat;
    CHECK(motionCandidateUsage(wide).has_value());
}

TEST_CASE("other images are left as they are") {
    ImageCreateDesc rgba = velocityDesc();
    rgba.format = vk::kFormatR8G8B8A8Unorm;
    CHECK_FALSE(motionCandidateUsage(rgba).has_value());
    ImageCreateDesc mips = velocityDesc();
    mips.mipLevels = 4;
    CHECK_FALSE(motionCandidateUsage(mips).has_value());
    ImageCreateDesc layers = velocityDesc();
    layers.arrayLayers = 2;
    CHECK_FALSE(motionCandidateUsage(layers).has_value());
    ImageCreateDesc msaa = velocityDesc();
    msaa.samples = 4;
    CHECK_FALSE(motionCandidateUsage(msaa).has_value());
    ImageCreateDesc textureOnly = velocityDesc();
    textureOnly.usage = vk::kUsageSampled;
    CHECK_FALSE(motionCandidateUsage(textureOnly).has_value());
    ImageCreateDesc notSampled = velocityDesc();
    notSampled.usage = vk::kUsageColorAttachment;
    CHECK_FALSE(motionCandidateUsage(notSampled).has_value());
    ImageCreateDesc empty = velocityDesc();
    empty.width = 0;
    CHECK_FALSE(motionCandidateUsage(empty).has_value());
}

TEST_CASE("bytes per pixel") {
    CHECK(motionBytesPerPixel(vk::kFormatR16G16Sfloat) == 4);
    CHECK(motionBytesPerPixel(vk::kFormatR16G16Unorm) == 4);
    CHECK(motionBytesPerPixel(vk::kFormatR16G16Snorm) == 4);
    CHECK(motionBytesPerPixel(vk::kFormatR32G32Sfloat) == 8);
    CHECK(motionBytesPerPixel(vk::kFormatR8G8B8A8Unorm) == 0);
}

TEST_CASE("half floats") {
    CHECK(halfToFloat(0x0000) == 0.0f);
    CHECK(halfToFloat(0x3C00) == 1.0f);
    CHECK(halfToFloat(0xC000) == -2.0f);
    CHECK(halfToFloat(0x3800) == 0.5f);
    CHECK(halfToFloat(0x0001) == doctest::Approx(5.9604645e-8f));
    CHECK(std::isinf(halfToFloat(0x7C00)));
    CHECK(std::isnan(halfToFloat(0x7E00)));
}

TEST_CASE("statistics of an RG16F velocity image") {
    // 4 x 2 pixels: two moving pixels, one NaN, the rest still.
    std::vector<std::uint8_t> data(4 * 2 * 4, 0);
    putHalf(data, 1, 0, 0x3800); // x 0.5
    putHalf(data, 1, 1, 0xC000); // y -2
    putHalf(data, 5, 0, 0xBC00); // x -1
    putHalf(data, 6, 1, 0x7E00); // NaN
    const auto s = motionStats(data.data(), data.size(), 4, 2, vk::kFormatR16G16Sfloat);
    REQUIRE(s.has_value());
    CHECK(s->pixels == 8);
    CHECK(s->nonZero == 2);
    CHECK(s->nonFinite == 1);
    CHECK(s->maxAbs[0] == doctest::Approx(1.0));
    CHECK(s->maxAbs[1] == doctest::Approx(2.0));
    CHECK(s->meanAbs[0] == doctest::Approx(1.5 / 8.0));
    CHECK(s->meanAbsNonZero[0] == doctest::Approx(0.75));
    CHECK(s->meanAbsNonZero[1] == doctest::Approx(1.0));
}

TEST_CASE("statistics refuse unknown formats and short data") {
    std::vector<std::uint8_t> data(16, 0);
    CHECK_FALSE(motionStats(data.data(), data.size(), 2, 2, vk::kFormatR8G8B8A8Unorm).has_value());
    CHECK_FALSE(motionStats(data.data(), data.size(), 4, 4, vk::kFormatR16G16Sfloat).has_value());
    CHECK(motionStats(data.data(), data.size(), 2, 2, vk::kFormatR16G16Sfloat).has_value());
}
