#include "stereo_seq/desktop_window.hpp"

#include <doctest/doctest.h>

using evr::stereo_seq::Mirror;
using evr::stereo_seq::MirrorStep;
using evr::stereo_seq::mirrorStep;
using evr::stereo_seq::parseMirror;
using evr::stereo_seq::parseSwapImages;
using evr::stereo_seq::stereoSwapchainImages;

TEST_CASE("swapchain images: the game's two are raised to the wanted count") {
    CHECK(stereoSwapchainImages(2, 4, 2, 8) == 4);
    CHECK(stereoSwapchainImages(2, 4, 2, 0) == 4); // no upper limit
    CHECK(stereoSwapchainImages(3, 0, 2, 8) == 3); // 0 keeps the game's count
    CHECK(stereoSwapchainImages(5, 4, 2, 8) == 5); // never fewer than the game asked for
}

TEST_CASE("swapchain images: the surface's limits win") {
    CHECK(stereoSwapchainImages(2, 4, 2, 3) == 3);
    CHECK(stereoSwapchainImages(1, 0, 2, 3) == 2);
}

TEST_CASE("swapchain images setting") {
    CHECK(parseSwapImages(L"4") == 4u);
    CHECK(parseSwapImages(L" 0 ") == 0u);
    CHECK(parseSwapImages(L"8") == 8u);
    CHECK_FALSE(parseSwapImages(L"9").has_value());
    CHECK_FALSE(parseSwapImages(L"").has_value());
    CHECK_FALSE(parseSwapImages(L"four").has_value());
    CHECK_FALSE(parseSwapImages(L"-1").has_value());
}

TEST_CASE("mirror setting") {
    CHECK(parseMirror(L"left") == Mirror::Left);
    CHECK(parseMirror(L" Right ") == Mirror::Right);
    CHECK(parseMirror(L"OFF") == Mirror::Off);
    CHECK(parseMirror(L"0") == Mirror::Off);
    CHECK_FALSE(parseMirror(L"").has_value());
    CHECK_FALSE(parseMirror(L"both").has_value());
}

TEST_CASE("mirror steps: the shown eye is kept, the other present gets it") {
    CHECK(mirrorStep(Mirror::Left, 0) == MirrorStep::Store);
    CHECK(mirrorStep(Mirror::Left, 1) == MirrorStep::Load);
    CHECK(mirrorStep(Mirror::Right, 1) == MirrorStep::Store);
    CHECK(mirrorStep(Mirror::Right, 0) == MirrorStep::Load);
    CHECK(mirrorStep(Mirror::Off, 0) == MirrorStep::Clear);
    CHECK(mirrorStep(Mirror::Off, 1) == MirrorStep::Clear);
}

using evr::stereo_seq::PresentKind;
using evr::stereo_seq::WindowPresentGate;

TEST_CASE("window gate: only the mirrored eye of a pair reaches the window") {
    WindowPresentGate left;
    CHECK(left.present(0.0, 120.0, PresentKind::EyeL, Mirror::Left));
    CHECK_FALSE(left.present(0.001, 120.0, PresentKind::EyeR, Mirror::Left));
    WindowPresentGate right;
    CHECK_FALSE(right.present(0.0, 120.0, PresentKind::EyeL, Mirror::Right));
    CHECK(right.present(0.001, 120.0, PresentKind::EyeR, Mirror::Right));
    WindowPresentGate off; // black, from eye L's present
    CHECK(off.present(0.0, 120.0, PresentKind::EyeL, Mirror::Off));
    CHECK_FALSE(off.present(0.001, 120.0, PresentKind::EyeR, Mirror::Off));
    CHECK(left.counters().otherEye == 1);
}

TEST_CASE("window gate: at most one present per two refreshes") {
    WindowPresentGate gate;
    // 110 stereo ticks per second on a 120 Hz display: every other tick's eye L reaches the window.
    int presented = 0;
    for (int tick = 0; tick < 110; ++tick) {
        const double t = tick / 110.0;
        presented += gate.present(t, 120.0, PresentKind::EyeL, Mirror::Left) ? 1 : 0;
        CHECK_FALSE(gate.present(t + 0.004, 120.0, PresentKind::EyeR, Mirror::Left));
    }
    CHECK(presented == 55);
    CHECK(gate.counters().tooSoon == 55);
}

TEST_CASE("window gate: mono frames and an unknown refresh") {
    WindowPresentGate gate;
    CHECK(gate.present(0.0, 0.0, PresentKind::Mono, Mirror::Left));
    CHECK_FALSE(gate.present(0.030, 0.0, PresentKind::Mono, Mirror::Left)); // 60 Hz assumed: 33.3 ms
    CHECK(gate.present(0.034, 0.0, PresentKind::Mono, Mirror::Left));
}

using evr::stereo_seq::panelMirror;

TEST_CASE("menu panel mirror: gated, left: only the present the window shows keeps and loads the panel") {
    // Eye L reaches the window: its GUI goes straight to the window.
    CHECK(panelMirror(Mirror::Left, true, PresentKind::EyeL, true).keep);
    CHECK(panelMirror(Mirror::Left, true, PresentKind::EyeL, true).step == MirrorStep::Load);
    // Eye L handed back: nothing is copied.
    CHECK_FALSE(panelMirror(Mirror::Left, true, PresentKind::EyeL, false).keep);
    CHECK(panelMirror(Mirror::Left, true, PresentKind::EyeL, false).step == MirrorStep::None);
    // Eye R handed back: nothing.
    CHECK_FALSE(panelMirror(Mirror::Left, true, PresentKind::EyeR, false).keep);
    CHECK(panelMirror(Mirror::Left, true, PresentKind::EyeR, false).step == MirrorStep::None);
}

TEST_CASE("menu panel mirror: gated, right: eye L keeps the GUI, eye R shows it") {
    const auto left = panelMirror(Mirror::Right, true, PresentKind::EyeL, false);
    CHECK(left.keep);
    CHECK(left.step == MirrorStep::None);
    const auto right = panelMirror(Mirror::Right, true, PresentKind::EyeR, true);
    CHECK_FALSE(right.keep); // eye R has no GUI of its own
    CHECK(right.step == MirrorStep::Load);
}

TEST_CASE("menu panel mirror: ungated, both presents show the panel eye L kept") {
    for (const Mirror m : {Mirror::Left, Mirror::Right}) {
        const auto left = panelMirror(m, false, PresentKind::EyeL, true);
        CHECK(left.keep);
        CHECK(left.step == MirrorStep::Load);
        const auto right = panelMirror(m, false, PresentKind::EyeR, true);
        CHECK_FALSE(right.keep);
        CHECK(right.step == MirrorStep::Load);
    }
}

TEST_CASE("menu panel mirror: mono frames carry the GUI; off stays black") {
    const auto shown = panelMirror(Mirror::Left, true, PresentKind::Mono, true);
    CHECK(shown.keep);
    CHECK(shown.step == MirrorStep::Load);
    const auto handedBack = panelMirror(Mirror::Right, true, PresentKind::Mono, false);
    CHECK_FALSE(handedBack.keep);
    CHECK(handedBack.step == MirrorStep::None);
    CHECK(panelMirror(Mirror::Off, false, PresentKind::EyeL, true).step == MirrorStep::Clear);
    CHECK_FALSE(panelMirror(Mirror::Off, false, PresentKind::EyeL, true).keep);
    CHECK(panelMirror(Mirror::Off, true, PresentKind::Mono, false).step == MirrorStep::None);
}

TEST_CASE("window presents setting") {
    using evr::stereo_seq::parseWindowPresents;
    using evr::stereo_seq::WindowPresents;
    CHECK(parseWindowPresents(L"all") == WindowPresents::All);
    CHECK(parseWindowPresents(L" ALL ") == WindowPresents::All);
    CHECK(parseWindowPresents(L"gated") == WindowPresents::Gated);
    CHECK(parseWindowPresents(L"") == WindowPresents::Default);
    CHECK(parseWindowPresents(L"some") == WindowPresents::Default);
}

TEST_CASE("window presents are gated by default on NVIDIA only") {
    using evr::stereo_seq::windowGateWanted;
    using evr::stereo_seq::WindowPresents;
    CHECK(windowGateWanted(WindowPresents::Default, 0x10DE));
    CHECK_FALSE(windowGateWanted(WindowPresents::Default, 0x1002)); // AMD
    CHECK_FALSE(windowGateWanted(WindowPresents::Default, 0x8086)); // Intel
    CHECK_FALSE(windowGateWanted(WindowPresents::All, 0x10DE));
    CHECK(windowGateWanted(WindowPresents::Gated, 0x1002));
}
