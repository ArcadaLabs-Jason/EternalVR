#include "ui_layer/gui_target.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <string>

using evr::ui_layer::candidateUsage;
using evr::ui_layer::checkTarget;
using evr::ui_layer::eyeCopyCandidateUsage;
using evr::ui_layer::GuiImageFields;
using evr::ui_layer::ImageCreateDesc;
using evr::ui_layer::ImageRecord;
using evr::ui_layer::TargetCheck;
namespace vk = evr::ui_layer::vk;

namespace {

// The check's name, so that a failure prints which check it was.
std::string named(TargetCheck check) {
    return evr::ui_layer::toString(check);
}

} // namespace

namespace {

// `_gui` as the engine creates it (idImageOpts flags 0x207: render target, storage).
ImageCreateDesc guiDesc() {
    ImageCreateDesc d;
    d.imageType = vk::kImageType2D;
    d.format = vk::kFormatR8G8B8A8Unorm;
    d.width = 2064;
    d.height = 2100;
    d.depth = 1;
    d.mipLevels = 1;
    d.arrayLayers = 1;
    d.samples = 1;
    d.usage = vk::kUsageColorAttachment | vk::kUsageSampled | vk::kUsageStorage;
    return d;
}

GuiImageFields guiFields() {
    GuiImageFields f;
    f.format = 3;
    f.width = 2064;
    f.height = 2100;
    f.flags = 0x207;
    f.vkImage = 0xABCD;
    return f;
}

ImageRecord guiRecord() {
    ImageRecord r;
    r.width = 2064;
    r.height = 2100;
    r.usage = 0x1D;
    r.sharingMode = vk::kSharingConcurrent;
    r.queueFamilies = {0, 2};
    return r;
}

} // namespace

TEST_CASE("gui target: the engine's _gui create info is a candidate and gets TRANSFER_SRC") {
    const auto usage = candidateUsage(guiDesc());
    REQUIRE(usage.has_value());
    CHECK(*usage == 0x1Du);
}

TEST_CASE("gui target: other images are not candidates") {
    auto d = guiDesc();
    d.format = 44; // B8G8R8A8
    CHECK_FALSE(candidateUsage(d).has_value());
    d = guiDesc();
    d.usage |= vk::kUsageTransferSrc; // already copyable: not the engine's _gui profile
    CHECK_FALSE(candidateUsage(d).has_value());
    d = guiDesc();
    d.usage = vk::kUsageColorAttachment | vk::kUsageSampled; // no storage
    CHECK_FALSE(candidateUsage(d).has_value());
    d = guiDesc();
    d.mipLevels = 2;
    CHECK_FALSE(candidateUsage(d).has_value());
    d = guiDesc();
    d.arrayLayers = 6;
    CHECK_FALSE(candidateUsage(d).has_value());
    d = guiDesc();
    d.samples = 4;
    CHECK_FALSE(candidateUsage(d).has_value());
    d = guiDesc();
    d.imageType = 2; // 3D
    CHECK_FALSE(candidateUsage(d).has_value());
}

TEST_CASE("gui target: a prepared, concurrent image in a known layout passes") {
    const ImageRecord r = guiRecord();
    CHECK(named(checkTarget(guiFields(), &r, vk::kLayoutColorAttachment, std::nullopt, 2)) ==
          named(TargetCheck::Ok));
    CHECK(named(checkTarget(guiFields(), &r, vk::kLayoutShaderReadOnly, 0u, 0)) == named(TargetCheck::Ok));
    CHECK(named(checkTarget(guiFields(), &r, vk::kLayoutGeneral, 0u, 2)) == named(TargetCheck::Ok));
}

TEST_CASE("gui target: each failed check is named") {
    const ImageRecord r = guiRecord();
    auto f = guiFields();
    f.format = 2; // FMT_RGBA16F
    CHECK(named(checkTarget(f, &r, vk::kLayoutColorAttachment, 0u, 0)) == named(TargetCheck::NotRgba8));
    f = guiFields();
    f.flags |= 0x100;
    CHECK(named(checkTarget(f, &r, vk::kLayoutColorAttachment, 0u, 0)) == named(TargetCheck::ImageSet));
    f = guiFields();
    f.width = 0;
    CHECK(named(checkTarget(f, &r, vk::kLayoutColorAttachment, 0u, 0)) == named(TargetCheck::BadSize));
    f = guiFields();
    f.height = 20000;
    CHECK(named(checkTarget(f, &r, vk::kLayoutColorAttachment, 0u, 0)) == named(TargetCheck::BadSize));
    f = guiFields();
    f.vkImage = 0;
    CHECK(named(checkTarget(f, &r, vk::kLayoutColorAttachment, 0u, 0)) == named(TargetCheck::NoVkImage));
    CHECK(named(checkTarget(guiFields(), nullptr, vk::kLayoutColorAttachment, 0u, 0)) ==
          named(TargetCheck::UnknownImage));
    ImageRecord small = guiRecord();
    small.width = 1280;
    CHECK(named(checkTarget(guiFields(), &small, vk::kLayoutColorAttachment, 0u, 0)) ==
          named(TargetCheck::SizeMismatch));
    ImageRecord noSrc = guiRecord();
    noSrc.usage = 0x1C;
    CHECK(named(checkTarget(guiFields(), &noSrc, vk::kLayoutColorAttachment, 0u, 0)) ==
          named(TargetCheck::NoTransferSrc));
    CHECK(named(checkTarget(guiFields(), &r, std::nullopt, 0u, 0)) == named(TargetCheck::NoLayout));
    CHECK(named(checkTarget(guiFields(), &r, 7 /* TRANSFER_DST */, 0u, 0)) == named(TargetCheck::BadLayout));
    CHECK(named(checkTarget(guiFields(), &r, 0 /* UNDEFINED */, 0u, 0)) == named(TargetCheck::BadLayout));
    CHECK(named(checkTarget(guiFields(), &r, vk::kLayoutColorAttachment, 0u, 1)) ==
          named(TargetCheck::OtherQueueFamily));
}

TEST_CASE("gui target: an exclusive image is copied only on the family that used it last") {
    ImageRecord r = guiRecord();
    r.sharingMode = 0;
    r.queueFamilies.clear();
    CHECK(named(checkTarget(guiFields(), &r, vk::kLayoutColorAttachment, 0u, 0)) == named(TargetCheck::Ok));
    CHECK(named(checkTarget(guiFields(), &r, vk::kLayoutColorAttachment, 0u, 2)) ==
          named(TargetCheck::OtherQueueFamily));
    CHECK(named(checkTarget(guiFields(), &r, vk::kLayoutColorAttachment, std::nullopt, 0)) ==
          named(TargetCheck::OtherQueueFamily));
}

TEST_CASE("gui target: every check has a name") {
    for (int i = 0; i <= static_cast<int>(TargetCheck::OtherQueueFamily); ++i) {
        CHECK(std::string(evr::ui_layer::toString(static_cast<TargetCheck>(i))) != "?");
    }
}

TEST_CASE("eye copy: screen-sized four-byte colour targets are candidates and get TRANSFER_SRC") {
    auto d = guiDesc();
    d.width = 1280;
    d.height = 720;
    d.usage = vk::kUsageColorAttachment | vk::kUsageSampled;
    auto usage = eyeCopyCandidateUsage(d);
    REQUIRE(usage.has_value());
    CHECK(*usage == (vk::kUsageColorAttachment | vk::kUsageSampled | vk::kUsageTransferSrc));
    for (const std::int32_t format : {37, 43, 44, 50, 58, 64, 122}) {
        d.format = format;
        CHECK(eyeCopyCandidateUsage(d).has_value());
    }
    d = guiDesc();
    d.usage = vk::kUsageStorage | vk::kUsageSampled;
    CHECK(eyeCopyCandidateUsage(d).has_value());
}

TEST_CASE("eye copy: other images are not candidates") {
    auto d = guiDesc();
    d.format = 97; // R16G16B16A16_SFLOAT
    CHECK_FALSE(eyeCopyCandidateUsage(d).has_value());
    d = guiDesc();
    d.width = 160; // a reduced copy
    CHECK_FALSE(eyeCopyCandidateUsage(d).has_value());
    d = guiDesc();
    d.usage = vk::kUsageSampled; // a texture
    CHECK_FALSE(eyeCopyCandidateUsage(d).has_value());
    d = guiDesc();
    d.usage = vk::kUsageColorAttachment; // never sampled
    CHECK_FALSE(eyeCopyCandidateUsage(d).has_value());
    d = guiDesc();
    d.mipLevels = 2;
    CHECK_FALSE(eyeCopyCandidateUsage(d).has_value());
    d = guiDesc();
    d.arrayLayers = 2;
    CHECK_FALSE(eyeCopyCandidateUsage(d).has_value());
    d = guiDesc();
    d.samples = 4;
    CHECK_FALSE(eyeCopyCandidateUsage(d).has_value());
}
