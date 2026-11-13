#include "features/depth/depth_layer.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace evr::depth;

namespace {

// The eye projection's depth row on the rig (docs/rig-findings/presenter.md): near 0.06 units, far about
// 8000.
constexpr float kRigM10 = -1.0000075f;
constexpr float kRigM11 = -0.06f;

DepthModel rigModel() {
    const auto m = modelFromEngineRow(kRigM10, kRigM11);
    REQUIRE(m.has_value());
    return *m;
}

SourceRecord depthRecord() {
    SourceRecord r;
    r.format = vk::kFormatD32S8;
    r.width = 2048;
    r.height = 2208;
    r.usage = vk::kUsageSampled | vk::kUsageDepthStencilAttachment;
    r.sharingMode = vk::kSharingConcurrent;
    r.queueFamilies = {0, 2};
    return r;
}

} // namespace

TEST_CASE("the switch is on only for 1, on, true or yes; empty and 0 leave it off") {
    CHECK(parseSwitch("1") == true);
    CHECK(parseSwitch("On") == true);
    CHECK(parseSwitch(" TRUE ") == true);
    CHECK(parseSwitch("yes") == true);
    CHECK(parseSwitch("") == false);
    CHECK(parseSwitch("0") == false);
    CHECK(parseSwitch("off") == false);
    CHECK(parseSwitch("False") == false);
    CHECK_FALSE(parseSwitch("2").has_value());
    CHECK_FALSE(parseSwitch("maybe").has_value());
}

TEST_CASE("the swapchain format is a 32-bit float depth: D32_FLOAT first, then D32_FLOAT_S8X24_UINT") {
    CHECK(chooseFormat({29, 91, kDxgiD24UnormS8, kDxgiD32FloatS8X24, kDxgiD32Float}) == kDxgiD32Float);
    CHECK(chooseFormat({kDxgiD32FloatS8X24, kDxgiD16Unorm}) == kDxgiD32FloatS8X24);
    CHECK_FALSE(chooseFormat({kDxgiD24UnormS8, kDxgiD16Unorm, 29}).has_value());
    CHECK_FALSE(chooseFormat({}).has_value());
    CHECK(std::string(formatName(kDxgiD32Float)) == "D32_FLOAT");
}

TEST_CASE("the rig's depth row: reversed depth, the far plane at depth 0 and the near plane at depth 1") {
    const DepthModel m = rigModel();
    CHECK(m.b == doctest::Approx(0.06).epsilon(1e-6));
    CHECK(m.a < 0.0);
    // 0.06 / D, nearly: 1 at the near plane, 0 at the far plane, 0.006 at 10 units.
    CHECK(depthAt(m, 0.06) == doctest::Approx(1.0).epsilon(1e-4));
    CHECK(depthAt(m, 10.0) == doctest::Approx(0.006).epsilon(0.01));
    CHECK(std::fabs(depthAt(m, -m.b / m.a)) < 1e-9);
    // Round trip.
    for (const double units : {0.1, 0.5, 2.0, 25.0, 300.0}) {
        CHECK(unitsAt(m, depthAt(m, units)) == doctest::Approx(units).epsilon(1e-9));
    }
    // Depth 0 is the far plane (a finite one here); at or below the value at infinity: infinitely far.
    CHECK(unitsAt(m, 0.0) == doctest::Approx(-m.b / m.a));
    CHECK(std::isinf(unitsAt(m, m.a)));
}

TEST_CASE("OpenXR's planes in metres: nearZ the far plane, farZ the near plane, divided by the world size") {
    const DepthModel m = rigModel();
    const auto p1 = planesOf(m, 1.0f);
    REQUIRE(p1.has_value());
    CHECK(p1->nearZ == doctest::Approx(8000.0f).epsilon(0.01)); // the float row rounds the far plane a little
    CHECK(p1->farZ == doctest::Approx(0.06f).epsilon(1e-4));
    CHECK(p1->nearZ > p1->farZ);
    // World size 1.2: 1.2 game units per tracked metre, so everything is 1/1.2 as far in metres.
    const auto p12 = planesOf(m, 1.2f);
    REQUIRE(p12.has_value());
    CHECK(p12->farZ == doctest::Approx(0.05f).epsilon(1e-4));
    CHECK(p12->nearZ == doctest::Approx(p1->nearZ / 1.2f).epsilon(1e-5));
    const auto p085 = planesOf(m, 0.85f);
    REQUIRE(p085.has_value());
    CHECK(p085->farZ == doctest::Approx(0.06f / 0.85f).epsilon(1e-4));
    // A scale that is not positive and finite gives no planes.
    CHECK_FALSE(planesOf(m, 0.0f).has_value());
    CHECK_FALSE(planesOf(m, -1.0f).has_value());
    CHECK_FALSE(planesOf(m, std::numeric_limits<float>::infinity()).has_value());
}

TEST_CASE("an infinite far plane gives nearZ = kMaxPlaneMetres (finite) and farZ = the near plane") {
    const auto m = modelFromEngineRow(-1.0f, -0.06f);
    REQUIRE(m.has_value());
    CHECK(m->a == 0.0);
    const auto p = planesOf(*m, 1.0f);
    REQUIRE(p.has_value());
    CHECK(p->nearZ == kMaxPlaneMetres);
    CHECK(std::isfinite(p->nearZ));
    CHECK(p->farZ == doctest::Approx(0.06f).epsilon(1e-6));
    // A row a float's rounding off -1 is the same infinite plane.
    const auto rounded = modelFromEngineRow(-0.99999994f, -0.06f);
    REQUIRE(rounded.has_value());
    CHECK(rounded->a == 0.0);
    // A finite far plane beyond the cap is handed over at the cap.
    const auto huge = modelFromEngineRow(-1.0000002f, -0.06f);
    REQUIRE(huge.has_value());
    CHECK(planesOf(*huge, 0.001f)->nearZ == kMaxPlaneMetres);
}

TEST_CASE("the a band: up to a float's rounding above 0 is an infinite far plane, past 1e-6 no model") {
    // m10 = -1 + a; a float near -1 has steps of 6e-8 and 1.2e-7.
    CHECK(modelFromEngineRow(-0.9999999f, -0.06f)->a == 0.0);        // a about 1.2e-7
    CHECK(modelFromEngineRow(-0.9999992f, -0.06f)->a == 0.0);        // a about 8e-7
    CHECK_FALSE(modelFromEngineRow(-0.999998f, -0.06f).has_value()); // a about 2e-6
    // Just below 0: a finite far plane, kept.
    CHECK(modelFromEngineRow(-1.0000006f, -0.06f)->a < 0.0);
}

TEST_CASE("unitsAt at its edges: the near plane, the value at infinity, outside 0..1") {
    const DepthModel m = rigModel();
    CHECK(unitsAt(m, 1.0) == doctest::Approx(m.b / (1.0 - m.a)));
    CHECK(std::isinf(unitsAt(m, m.a)));
    CHECK(std::isinf(unitsAt(m, m.a - 0.5)));
    CHECK(unitsAt(m, 2.0) < unitsAt(m, 1.0)); // nearer than the near plane: still a distance
    CHECK(std::isinf(depthAt(m, 0.0)));
    CHECK(std::isinf(depthAt(m, -1.0)));
}

TEST_CASE("depth rows that are not a reversed perspective give no model") {
    CHECK_FALSE(modelFromEngineRow(kRigM10, 0.06f).has_value()); // depth would grow away from the eye
    CHECK_FALSE(modelFromEngineRow(kRigM10, 0.0f).has_value());
    CHECK_FALSE(modelFromEngineRow(-0.99f, kRigM11).has_value()); // the far plane past infinity
    CHECK_FALSE(modelFromEngineRow(0.0f, kRigM11).has_value());   // depth 1 everywhere
    CHECK_FALSE(modelFromEngineRow(std::nanf(""), kRigM11).has_value());
    CHECK_FALSE(modelFromEngineRow(kRigM10, std::numeric_limits<float>::infinity()).has_value());
}

TEST_CASE("the nearest value written is the depth of kNearestMetres at the world size") {
    const DepthModel m = rigModel();
    CHECK(nearestDepth(m, 1.0f) == doctest::Approx(0.06 / 0.25).epsilon(1e-3));
    CHECK(nearestDepth(m, 1.2f) == doctest::Approx(0.06 / 0.30).epsilon(1e-3));
    // A near plane beyond that distance: nothing is held back.
    const auto distant = modelFromEngineRow(-1.0f, -1.0f);
    REQUIRE(distant.has_value());
    CHECK(nearestDepth(*distant, 1.0f) == 1.0f);
}

TEST_CASE("the source candidates: 2D single-sample sampled depth attachments, never changed") {
    ImageDesc d;
    d.imageType = vk::kImageType2D;
    d.format = vk::kFormatD32S8;
    d.width = 2048;
    d.height = 2208;
    d.depth = 1;
    d.mipLevels = 1;
    d.arrayLayers = 1;
    d.samples = 1;
    d.usage = vk::kUsageSampled | vk::kUsageDepthStencilAttachment;
    CHECK(isCandidate(d));
    for (const int f : {vk::kFormatD16, vk::kFormatX8D24, vk::kFormatD32, vk::kFormatD24S8}) {
        d.format = f;
        CHECK(isCandidate(d));
    }
    d.format = vk::kFormatD32S8;
    d.usage = vk::kUsageDepthStencilAttachment; // not sampled: not one the copy could read
    CHECK_FALSE(isCandidate(d));
    d.usage = vk::kUsageSampled | vk::kUsageDepthStencilAttachment | vk::kUsageTransientAttachment;
    CHECK_FALSE(isCandidate(d));
    d.usage = vk::kUsageSampled; // not drawn as depth
    CHECK_FALSE(isCandidate(d));
    d.usage = vk::kUsageSampled | vk::kUsageDepthStencilAttachment;
    d.format = 37; // RGBA8
    CHECK_FALSE(isCandidate(d));
    d.format = vk::kFormatD32S8;
    d.width = 128; // a small shadow or probe target
    CHECK_FALSE(isCandidate(d));
    d.width = 2048;
    d.mipLevels = 2;
    CHECK_FALSE(isCandidate(d));
    d.mipLevels = 1;
    d.arrayLayers = 6;
    CHECK_FALSE(isCandidate(d));
    d.arrayLayers = 1;
    d.samples = 4;
    CHECK_FALSE(isCandidate(d));
}

TEST_CASE("a depth-stencil barrier names both aspects; a depth-only one the depth") {
    CHECK(barrierAspects(vk::kFormatD32S8) == (vk::kAspectDepth | vk::kAspectStencil));
    CHECK(barrierAspects(vk::kFormatD24S8) == (vk::kAspectDepth | vk::kAspectStencil));
    CHECK(barrierAspects(vk::kFormatD32) == vk::kAspectDepth);
    CHECK(barrierAspects(vk::kFormatD16) == vk::kAspectDepth);
}

TEST_CASE("the source check: a sampled depth image of the eye's size in an expected layout, shared") {
    const SourceRecord r = depthRecord();
    CHECK((checkSource(&r, vk::kLayoutShaderReadOnly, 0u, 2, false, 2048, 2208) == SourceCheck::Ok));
    CHECK((checkSource(&r, vk::kLayoutDepthStencilReadOnly, 0u, 0, true, 2048, 2208) == SourceCheck::Ok));
    CHECK((checkSource(&r, vk::kLayoutDepthStencilAttachment, 0u, 0, true, 2048, 2208) == SourceCheck::Ok));
    // The attachment layout needs a change, refused on a queue without graphics.
    CHECK((checkSource(&r, vk::kLayoutDepthStencilAttachment, 0u, 2, false, 2048, 2208) ==
           SourceCheck::BadLayout));
    CHECK((checkSource(nullptr, vk::kLayoutShaderReadOnly, 0u, 0, true, 2048, 2208) ==
           SourceCheck::UnknownImage));
    CHECK((checkSource(&r, std::nullopt, 0u, 0, true, 2048, 2208) == SourceCheck::NoLayout));
    CHECK((checkSource(&r, 1 /* GENERAL */, 0u, 0, true, 2048, 2208) == SourceCheck::BadLayout));
    CHECK((checkSource(&r, 1000001002 /* PRESENT_SRC */, 0u, 0, true, 2048, 2208) == SourceCheck::BadLayout));
    CHECK((checkSource(&r, vk::kLayoutShaderReadOnly, 0u, 1, true, 2048, 2208) ==
           SourceCheck::OtherQueueFamily));
    CHECK((checkSource(&r, vk::kLayoutShaderReadOnly, 0u, 0, true, 1365, 1472) == SourceCheck::SizeMismatch));
    SourceRecord colour = r;
    colour.format = 37;
    CHECK(
        (checkSource(&colour, vk::kLayoutShaderReadOnly, 0u, 0, true, 2048, 2208) == SourceCheck::NotDepth));
    SourceRecord unsampled = r;
    unsampled.usage = vk::kUsageDepthStencilAttachment;
    CHECK((checkSource(&unsampled, vk::kLayoutShaderReadOnly, 0u, 0, true, 2048, 2208) ==
           SourceCheck::NotSampled));
    SourceRecord exclusive = r;
    exclusive.sharingMode = 0;
    exclusive.queueFamilies.clear();
    CHECK((checkSource(&exclusive, vk::kLayoutShaderReadOnly, 0u, 0, true, 2048, 2208) == SourceCheck::Ok));
    CHECK((checkSource(&exclusive, vk::kLayoutShaderReadOnly, 0u, 2, false, 2048, 2208) ==
           SourceCheck::OtherQueueFamily));
    CHECK((checkSource(&exclusive, vk::kLayoutShaderReadOnly, std::nullopt, 0, true, 2048, 2208) ==
           SourceCheck::OtherQueueFamily));
}

TEST_CASE("the read layout: only expected layouts; a change only on a graphics queue") {
    for (const bool graphics : {true, false}) {
        CHECK(readLayoutFor(vk::kLayoutShaderReadOnly, graphics)->layout == vk::kLayoutShaderReadOnly);
        CHECK_FALSE(readLayoutFor(vk::kLayoutShaderReadOnly, graphics)->transition);
        CHECK_FALSE(readLayoutFor(vk::kLayoutDepthStencilReadOnly, graphics)->transition);
    }
    const auto change = readLayoutFor(vk::kLayoutDepthStencilAttachment, true);
    REQUIRE(change.has_value());
    CHECK(change->transition);
    CHECK(change->layout == vk::kLayoutDepthStencilReadOnly);
    CHECK_FALSE(readLayoutFor(vk::kLayoutDepthStencilAttachment, false).has_value());
    for (const int other :
         {0, 1, 6, 7, 1000001002}) { // UNDEFINED, GENERAL, the transfer layouts, PRESENT_SRC
        CHECK_FALSE(readLayoutFor(other, true).has_value());
    }
}

TEST_CASE("the depth image: a copy into it is shown with the next new image and kept while it repeats") {
    SwapImageState st;
    CHECK_FALSE(st.acquired());
    st.onAcquired(2);
    st.onWaited();
    st.onCopyRecorded(Planes{8000.0f, 0.06f});
    CHECK(st.pending());
    CHECK(st.onNewImage()); // released with the image
    CHECK(st.shown());
    CHECK(st.planes().nearZ == 8000.0f);
    CHECK_FALSE(st.acquired());
    // Repeats call nothing: the depth stays shown.
    CHECK(st.shown());
    // A new image without depth: not shown with the old one's.
    CHECK_FALSE(st.onNewImage());
    CHECK_FALSE(st.shown());
}

TEST_CASE("the depth image: an abandoned copy keeps the image acquired and shows nothing") {
    SwapImageState st;
    st.onAcquired(0);
    st.onWaited();
    st.onCopyRecorded(Planes{8000.0f, 0.06f});
    st.onCopyAbandoned();
    CHECK_FALSE(st.onNewImage()); // nothing to release: the image stays for the next copy
    CHECK(st.acquired());
    CHECK(st.waited());
    CHECK_FALSE(st.shown());
    // The next copy goes into the same image.
    st.onCopyRecorded(std::nullopt); // a frame without planes
    CHECK(st.onNewImage());
    CHECK_FALSE(st.shown());
}

TEST_CASE("the depth image: not waited yet, refused, detached") {
    SwapImageState st;
    st.onAcquired(1);
    st.onCopyRecorded(Planes{1.0f, 0.1f}); // not waited: no copy counts
    CHECK_FALSE(st.pending());
    st.onWaited();
    st.onCopyRecorded(Planes{1.0f, 0.1f});
    CHECK(st.onNewImage());
    CHECK(st.shown());
    st.onRefused();
    CHECK_FALSE(st.shown());
    st.onAcquired(2);
    st.onDetached();
    CHECK_FALSE(st.acquired());
    CHECK_FALSE(st.pending());
    CHECK_FALSE(st.shown());
}

TEST_CASE("only results that name the layers stop the depth; a lost session or runtime does not") {
    CHECK(refusesDepth(-1));        // XR_ERROR_VALIDATION_FAILURE
    CHECK(refusesDepth(-23));       // XR_ERROR_LAYER_INVALID
    CHECK(refusesDepth(-25));       // XR_ERROR_SWAPCHAIN_RECT_INVALID
    CHECK(refusesDepth(-26));       // XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED
    CHECK_FALSE(refusesDepth(0));   // XR_SUCCESS
    CHECK_FALSE(refusesDepth(3));   // XR_SESSION_LOSS_PENDING
    CHECK_FALSE(refusesDepth(-2));  // XR_ERROR_RUNTIME_FAILURE
    CHECK_FALSE(refusesDepth(-13)); // XR_ERROR_INSTANCE_LOST
    CHECK_FALSE(refusesDepth(-17)); // XR_ERROR_SESSION_LOST
    CHECK_FALSE(refusesDepth(-16)); // XR_ERROR_SESSION_NOT_RUNNING
    CHECK_FALSE(refusesDepth(-24)); // XR_ERROR_LAYER_LIMIT_EXCEEDED: too many layers, not the depth's
}

TEST_CASE("depth image waits that time out: the first logged, five in a row stop the layer") {
    WaitTimeouts w;
    auto s1 = w.onTimeout();
    CHECK(s1.log);
    CHECK_FALSE(s1.stop);
    w.onWaited();
    for (std::uint32_t i = 1; i < kMaxWaitTimeouts; ++i) {
        const auto st = w.onTimeout();
        CHECK_FALSE(st.stop);
        CHECK_FALSE(st.log);
    }
    CHECK(w.onTimeout().stop);
    CHECK(w.total == kMaxWaitTimeouts + 1);
}

TEST_CASE("the layer is off by default, and says the first reason") {
    StartState s;
    CHECK((whyOff(s) == Off::Switch));
    s.switchOn = true;
    CHECK((whyOff(s) == Off::NoExtension));
    s.extensionEnabled = true;
    CHECK((whyOff(s) == Off::NotRouteS));
    s.routeS = true;
    CHECK((whyOff(s) == Off::NoTracking));
    s.tracking = true;
    CHECK((whyOff(s) == Off::None));
    s.formatOffered = false;
    CHECK((whyOff(s) == Off::NoFormat));
    s.formatOffered = true;
    s.alternateEyes = true;
    CHECK((whyOff(s) == Off::AlternateEyes)); // on or auto
    s.parallelEyes = true;
    CHECK((whyOff(s) == Off::ParallelEyes));
    s.switchOn = false;
    CHECK((whyOff(s) == Off::Switch));
    CHECK((std::string(toString(Off::None)) == "on"));
}

TEST_CASE("read-back statistics: the sky, the nearest band, invalid texels and the distances") {
    const DepthModel m = rigModel();
    const float nearest = nearestDepth(m, 1.0f);
    // Two eyes of 4 x 4 in rows of 10 (padding after them); eye L at x 0, eye R at x 4.
    std::vector<float> buffer(10 * 4, -5.0f);
    const auto at = [&](std::size_t x, std::size_t y) -> float& {
        return buffer[y * 10 + x];
    };
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 8; ++x) {
            at(x, y) = static_cast<float>(depthAt(m, 2.0 + static_cast<double>(x % 4)));
        }
    }
    for (std::size_t x = 0; x < 8; ++x) {
        at(x, 0) = 0.0f; // the sky in the top row
    }
    at(1, 1) = 0.9f;          // the gun, squeezed to the near plane
    at(3, 3) = std::nanf(""); // a broken texel
    const EyeStats s = statsOf(buffer.data(), 10, 0, 4, 4, 1, m, 1.0f, nearest);
    CHECK(s.samples == 16);
    CHECK(s.sky == 4);
    CHECK(s.clamped == 1);
    CHECK(s.invalid == 1);
    CHECK(s.minMetres == doctest::Approx(0.06 / 0.9).epsilon(1e-3));
    CHECK(s.maxMetres == doctest::Approx(5.0).epsilon(1e-6));
    CHECK(s.centreMetres == doctest::Approx(4.0).epsilon(1e-6)); // texel (2, 2)
    CHECK(s.medianMetres >= 2.0);
    CHECK(s.medianMetres <= 5.0);
    // World size 2: half the distance in metres.
    const EyeStats half = statsOf(buffer.data(), 10, 4, 4, 4, 1, m, 2.0f, nearest);
    CHECK(half.maxMetres == doctest::Approx(2.5).epsilon(1e-6));
    // Every other texel.
    CHECK(statsOf(buffer.data(), 10, 0, 4, 4, 2, m, 1.0f, nearest).samples == 4);
}

TEST_CASE("the eyes' same share: 1 for one eye's depth in both halves, the sky not counted") {
    std::vector<float> same(8 * 2, 0.0f);
    same[1] = same[5] = 0.25f;
    same[8 + 2] = same[8 + 6] = 0.5f;
    CHECK(sameShare(same.data(), 8, 4, 2, 1) == doctest::Approx(1.0));
    std::vector<float> differ = same;
    differ[5] = 0.26f;
    CHECK(sameShare(differ.data(), 8, 4, 2, 1) == doctest::Approx(0.5));
    std::vector<float> sky(8 * 2, 0.0f);
    CHECK(sameShare(sky.data(), 8, 4, 2, 1) == 0.0);
}
