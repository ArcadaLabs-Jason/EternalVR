#include "features/foveation/attachment_images.hpp"

#include <doctest/doctest.h>

#include <vector>

using evr::foveation::AttachmentImages;

namespace {

constexpr AttachmentImages::Handle kGui = 0x1000;
constexpr AttachmentImages::Handle kEye = 0x2000;
constexpr AttachmentImages::Handle kDepth = 0x3000;
constexpr AttachmentImages::Handle kGuiView = 0x10;
constexpr AttachmentImages::Handle kEyeView = 0x20;
constexpr AttachmentImages::Handle kDepthView = 0x30;
constexpr AttachmentImages::Handle kGuiPass = 0x100;
constexpr AttachmentImages::Handle kEyePass = 0x200;

} // namespace

TEST_CASE("attachment images: views map to their image") {
    AttachmentImages a;
    CHECK(a.addView(kGuiView, kGui));
    CHECK(a.imageOf(kGuiView) == kGui);
    CHECK_FALSE(a.imageOf(kEyeView).has_value());
    a.removeView(kGuiView);
    CHECK_FALSE(a.imageOf(kGuiView).has_value());
    CHECK(a.viewCount() == 0);
}

TEST_CASE("attachment images: a framebuffer draws into the images of its attachments") {
    AttachmentImages a;
    a.addView(kGuiView, kGui);
    a.addView(kEyeView, kEye);
    a.addView(kDepthView, kDepth);
    CHECK(a.addFramebuffer(kGuiPass, {kGuiView, kDepthView}));
    CHECK(a.addFramebuffer(kEyePass, {kEyeView, kDepthView}));
    CHECK(a.draws(kGuiPass, kGui));
    CHECK(a.draws(kGuiPass, kDepth));
    CHECK_FALSE(a.draws(kGuiPass, kEye));
    CHECK(a.draws(kEyePass, kEye));
    CHECK_FALSE(a.draws(kEyePass, kGui));
}

TEST_CASE("attachment images: a framebuffer keeps its images after its views are destroyed") {
    AttachmentImages a;
    a.addView(kGuiView, kGui);
    a.addFramebuffer(kGuiPass, {kGuiView});
    a.removeView(kGuiView);
    CHECK(a.draws(kGuiPass, kGui));
}

TEST_CASE("attachment images: unknown framebuffers and views draw into no known image") {
    AttachmentImages a;
    CHECK_FALSE(a.draws(kGuiPass, kGui));
    CHECK(a.addFramebuffer(kGuiPass, {kGuiView})); // its view was not kept
    CHECK_FALSE(a.draws(kGuiPass, kGui));
    CHECK(a.addFramebuffer(kEyePass, {})); // imageless: its views come with each pass
    CHECK_FALSE(a.draws(kEyePass, kEye));
}

TEST_CASE("attachment images: destroyed framebuffers are forgotten") {
    AttachmentImages a;
    a.addView(kGuiView, kGui);
    a.addFramebuffer(kGuiPass, {kGuiView});
    a.removeFramebuffer(kGuiPass);
    CHECK_FALSE(a.draws(kGuiPass, kGui));
    CHECK(a.framebufferCount() == 0);
    a.removeFramebuffer(kGuiPass); // twice is harmless
}

TEST_CASE("attachment images: a reused handle replaces the earlier entry") {
    AttachmentImages a;
    a.addView(kGuiView, kGui);
    a.addView(kGuiView, kEye);
    CHECK(a.imageOf(kGuiView) == kEye);
    CHECK(a.viewCount() == 1);
    a.addView(kEyeView, kGui);
    a.addFramebuffer(kGuiPass, {kGuiView});
    a.addFramebuffer(kGuiPass, {kEyeView});
    CHECK(a.draws(kGuiPass, kGui));
    CHECK_FALSE(a.draws(kGuiPass, kEye));
    CHECK(a.framebufferCount() == 1);
}

TEST_CASE("attachment images: full tables keep no new entries until some are removed") {
    AttachmentImages a(2, 1);
    CHECK(a.addView(kGuiView, kGui));
    CHECK(a.addView(kEyeView, kEye));
    CHECK_FALSE(a.addView(kDepthView, kDepth));
    CHECK_FALSE(a.imageOf(kDepthView).has_value());
    CHECK(a.addView(kEyeView, kDepth)); // a handle already kept is replaced, not added
    CHECK(a.viewCount() == 2);
    a.removeView(kEyeView);
    CHECK(a.addView(kDepthView, kDepth));

    CHECK(a.addFramebuffer(kGuiPass, {kGuiView}));
    CHECK_FALSE(a.addFramebuffer(kEyePass, {kDepthView}));
    CHECK_FALSE(a.draws(kEyePass, kDepth));
    CHECK(a.addFramebuffer(kGuiPass, {kDepthView})); // replaced
    CHECK(a.draws(kGuiPass, kDepth));
    a.removeFramebuffer(kGuiPass);
    CHECK(a.addFramebuffer(kEyePass, {kDepthView}));
    CHECK(a.framebufferCount() == 1);
}
