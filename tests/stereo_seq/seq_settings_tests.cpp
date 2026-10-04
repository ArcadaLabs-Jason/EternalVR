#include "stereo_seq/seq_settings.hpp"
#include "stereo_seq/stereo_taa.hpp"

#include <doctest/doctest.h>

#include <cstddef>
#include <string>

using evr::stereo_seq::cvarsOnCommandLine;
using evr::stereo_seq::parseCaptureSetting;
using evr::stereo_seq::sequentialCvars;

TEST_CASE("capture setting: folder and interval") {
    const auto s = parseCaptureSetting(L"D:\\evr\\tmp-vr\\eyes,30");
    REQUIRE(s.has_value());
    CHECK(s->directory == L"D:\\evr\\tmp-vr\\eyes");
    CHECK(s->everyPairs == 30);
}

TEST_CASE("capture setting: the interval defaults to 60 and spaces are trimmed") {
    const auto s = parseCaptureSetting(L"  E:\\eyes  ");
    REQUIRE(s.has_value());
    CHECK(s->directory == L"E:\\eyes");
    CHECK(s->everyPairs == 60);
    const auto t = parseCaptureSetting(L"E:\\eyes , 5 ");
    REQUIRE(t.has_value());
    CHECK(t->directory == L"E:\\eyes");
    CHECK(t->everyPairs == 5);
}

TEST_CASE("capture setting: rejected values") {
    CHECK_FALSE(parseCaptureSetting(L"").has_value());
    CHECK_FALSE(parseCaptureSetting(L",10").has_value());
    CHECK_FALSE(parseCaptureSetting(L"E:\\eyes,").has_value());
    CHECK_FALSE(parseCaptureSetting(L"E:\\eyes,0").has_value());
    CHECK_FALSE(parseCaptureSetting(L"E:\\eyes,-3").has_value());
    CHECK_FALSE(parseCaptureSetting(L"E:\\eyes,ten").has_value());
    CHECK_FALSE(parseCaptureSetting(L"E:\\eyes,99999999").has_value());
}

TEST_CASE("cvar check: the v1 set on a launch-helper command line") {
    const std::string line =
        "\"C:\\Games\\DOOMEternalx64vk.exe\" +logFile 2 +r_hdrDisplay 0 +r_TAASafeMode 1 "
        "+r_antialiasing 0 +r_jitter 0 +rs_enable 0 +r_swapInterval 0 +map game/sp/e1m1";
    const auto cvars = cvarsOnCommandLine(line, sequentialCvars());
    REQUIRE(cvars.size() == 5);
    for (const auto& c : cvars) {
        REQUIRE(c.actual.has_value());
        CHECK(*c.actual == c.expected);
    }
}

TEST_CASE("cvar check: missing, different, overridden and set forms") {
    const std::string line = "game.exe +r_TAASafeMode 1 +set r_antialiasing 2 +seta RS_ENABLE 0 +r_jitter "
                             "+r_swapInterval 1 +r_TAASafeMode 0";
    const auto cvars = cvarsOnCommandLine(line, sequentialCvars());
    REQUIRE(cvars.size() == 5);
    CHECK(cvars[0].name == "r_TAASafeMode");
    CHECK(cvars[0].actual == std::string("0")); // the later setting wins (S3 toggles this way)
    CHECK(cvars[1].actual == std::string("2"));
    CHECK_FALSE(cvars[2].actual.has_value()); // "+r_jitter" has no value before the next "+"
    CHECK(cvars[3].actual == std::string("0"));
    CHECK(cvars[4].actual == std::string("1"));
}

TEST_CASE("runtime cvars: per-eye temporal effects own the TAA cvars") {
    CHECK(evr::stereo_seq::stereoRuntimeCvars(evr::stereo_seq::StereoTemporal::PerEye).empty());
    CHECK(evr::stereo_seq::stereoRuntimeCvars(evr::stereo_seq::StereoTemporal::Off).size() == 2);
}

TEST_CASE("runtime cvars: the scattering filter follows its per-eye history, not the TAA mode") {
    const auto on = evr::stereo_seq::stereoScatterFilterCvar(true);
    const auto off = evr::stereo_seq::stereoScatterFilterCvar(false);
    CHECK(std::string(on.name) == "r_lightScatteringTAA");
    CHECK(std::string(on.value) == "1");
    CHECK(std::string(off.name) == "r_lightScatteringTAA");
    CHECK(std::string(off.value) == "0");
    // Per-eye TAA's own set holds the same cvar (the filter off there unless the history is per eye).
    bool inPerEyeSet = false;
    for (const auto& c : evr::stereo_seq::stereoTaaForcedCvars()) {
        inPerEyeSet = inPerEyeSet || (c.name == off.name && c.value == off.value);
    }
    CHECK(inPerEyeSet);
    // Not in the Off set, which would hold it at one value.
    for (const auto& c : evr::stereo_seq::stereoRuntimeCvars()) {
        CHECK((c.name != on.name));
    }
}

TEST_CASE("runtime cvars: TAA off, a subset of the v1 set") {
    const auto& runtime = evr::stereo_seq::stereoRuntimeCvars();
    REQUIRE(runtime.size() == 2);
    CHECK(std::string(runtime[0].name) == "r_TAASafeMode");
    CHECK(std::string(runtime[0].value) == "1");
    CHECK(std::string(runtime[1].name) == "r_antialiasing");
    CHECK(std::string(runtime[1].value) == "0");
    for (const auto& r : runtime) {
        bool inV1 = false;
        for (const auto& v : sequentialCvars()) {
            inV1 = inV1 || (v.name == r.name && v.value == r.value);
        }
        CHECK(inV1);
    }
}

TEST_CASE("window cvars: fullscreen off and no vsync, the window size from the command line") {
    const auto held = evr::stereo_seq::stereoWindowCvars(
        "\"C:\\Games\\DOOMEternalx64vk.exe\" +r_fullscreen 0 +r_windowWidth 1415 +set r_windowHeight 1415 "
        "+map game/sp/e1m1_intro/e1m1_intro");
    REQUIRE(held.size() == 4);
    CHECK(held[0].name == "r_fullscreen");
    CHECK(held[0].value == "0");
    CHECK(held[1].name == "r_swapInterval");
    CHECK(held[1].value == "0");
    CHECK(held[2].name == "r_windowWidth");
    CHECK(held[2].value == "1415");
    CHECK(held[3].name == "r_windowHeight");
    CHECK(held[3].value == "1415");
}

TEST_CASE("window cvars: no size is held without a usable one on the command line") {
    CHECK(evr::stereo_seq::stereoWindowCvars("game.exe +map e1m1").size() == 2);
    CHECK(evr::stereo_seq::stereoWindowCvars("game.exe +r_windowWidth 0 +r_windowHeight -5").size() == 2);
    CHECK(evr::stereo_seq::stereoWindowCvars("game.exe +r_windowWidth abc +r_windowHeight 123456").size() ==
          2);
    // Only a width: no size is held.
    CHECK(evr::stereo_seq::stereoWindowCvars("game.exe +r_windowWidth 1280 +r_windowWidth 2064").size() == 2);
    const auto later = evr::stereo_seq::stereoWindowCvars(
        "game.exe +r_windowWidth 1280 +r_windowHeight 720 +r_windowWidth 2064");
    REQUIRE(later.size() == 4);
    CHECK(later[2].value == "2064"); // the later setting wins
}

TEST_CASE("window cvars: the window's size is told apart from the rest of the set") {
    const auto held = evr::stereo_seq::stereoWindowCvars("game.exe +r_windowWidth 672 +r_windowHeight 720");
    REQUIRE(held.size() == 4);
    CHECK_FALSE(evr::stereo_seq::isWindowSizeCvar(held[0].name)); // r_fullscreen
    CHECK_FALSE(evr::stereo_seq::isWindowSizeCvar(held[1].name)); // r_swapInterval
    CHECK(evr::stereo_seq::isWindowSizeCvar(held[2].name));
    CHECK(evr::stereo_seq::isWindowSizeCvar(held[3].name));
    CHECK(evr::stereo_seq::isWindowSizeCvar("R_WINDOWWIDTH"));
    CHECK_FALSE(evr::stereo_seq::isWindowSizeCvar("r_windowWidthX"));
    CHECK_FALSE(evr::stereo_seq::isWindowSizeCvar(""));
}

TEST_CASE("launch size watch: the first swapchain is the launch size, a change reports the pixel ratio") {
    evr::stereo_seq::LaunchSizeWatch watch;
    CHECK_FALSE(watch.onSwapchain({0, 0}).has_value());
    CHECK_FALSE(watch.onSwapchain({1415, 1415}).has_value());
    CHECK(watch.reference().width == 1415);
    CHECK_FALSE(watch.onSwapchain({1415, 1415}).has_value()); // the game's startup re-create
    const auto grown = watch.onSwapchain({3840, 2160});       // session 2's new campaign
    REQUIRE(grown.has_value());
    CHECK(*grown == doctest::Approx(3840.0 * 2160.0 / (1415.0 * 1415.0)));
    const auto shrunk = watch.onSwapchain({1280, 800});
    REQUIRE(shrunk.has_value());
    CHECK(*shrunk < 1.0);
    CHECK(watch.reference().height == 1415); // the launch size stays the reference
}

TEST_CASE("launch size watch: an expected size set by the layer replaces the launch size") {
    evr::stereo_seq::LaunchSizeWatch watch;
    CHECK_FALSE(watch.onSwapchain({1280, 720}).has_value()); // the mirror window at launch
    // The render size answers the client area: the game re-creates at it, which is expected.
    CHECK_FALSE(watch.onSwapchain({1280, 1400}, evr::stereo_seq::SwapchainSize{1280, 1400}).has_value());
    const auto big = watch.onSwapchain({3840, 2160}, evr::stereo_seq::SwapchainSize{1280, 1400});
    REQUIRE(big.has_value());
    CHECK(*big == doctest::Approx(3840.0 * 2160.0 / (1280.0 * 1400.0)));
    // An empty expected size falls back to the launch size.
    CHECK_FALSE(watch.onSwapchain({1280, 720}, evr::stereo_seq::SwapchainSize{0, 0}).has_value());
}

TEST_CASE("window cvars: the command line's size wins over ETERNALVR_WINDOW's") {
    // The launcher with the render size: the game starts at the render size, the window is the mirror.
    const auto held = evr::stereo_seq::stereoWindowCvars(
        "game.exe +r_fullscreen 0 +r_windowWidth 1280 +r_windowHeight 1400", "0,0,1280,720");
    REQUIRE(held.size() == 4);
    CHECK(held[2].name == "r_windowWidth");
    CHECK(held[2].value == "1280");
    CHECK(held[3].name == "r_windowHeight");
    CHECK(held[3].value == "1400");
}

TEST_CASE("window cvars: without a size on the command line, ETERNALVR_WINDOW's") {
    const auto held = evr::stereo_seq::stereoWindowCvars("game.exe +map e1m1", "3840,0,1415,1415");
    REQUIRE(held.size() == 4);
    CHECK(held[2].value == "1415");
    CHECK(held[3].value == "1415");
    const auto negative = evr::stereo_seq::stereoWindowCvars("game.exe", "-1280, 0, 1280, 720");
    REQUIRE(negative.size() == 4);
    CHECK(negative[2].value == "1280");
    CHECK(negative[3].value == "720");
    // An unusable ETERNALVR_WINDOW holds no size.
    for (const char* bad :
         {"", "1,2,3", "0,0,0,720", "0,0,1280,x", "0,0,1280,720,5", "a,b,c,d", "0,0,1280,720,"}) {
        CHECK(evr::stereo_seq::stereoWindowCvars("game.exe", bad).size() == 2);
    }
}

TEST_CASE("comfort cvars: HDR and the camera effects are held off, each name once") {
    const auto& held = evr::stereo_seq::stereoComfortCvars();
    const auto value = [&](const char* name) -> std::string {
        for (const auto& c : held) {
            if (c.name == name) {
                return std::string(c.value);
            }
        }
        return "absent";
    };
    CHECK(value("r_hdrDisplay") == "0");
    CHECK(value("r_motionblur") == "0");
    CHECK(value("r_dof") == "0");
    CHECK(value("pm_noBob") == "1");
    CHECK(value("view_skipKicks") == "1");
    // The damage tint and blur go; the directional damage arcs stay.
    CHECK(value("view_skipDamageEffect") == "1");
    CHECK(value("view_showPlayerDamageViewEffect") == "0");
    CHECK(value("view_damageBlur") == "0");
    // The low health red vignette in the eyes goes with the view effects' screen overlays.
    CHECK(value("g_skipViewEffects") == "1");
    CHECK(value("view_enableHelmetFX") == "absent");
    CHECK(value("hud_showDamage") == "absent");
    for (std::size_t i = 0; i < held.size(); ++i) {
        for (std::size_t j = i + 1; j < held.size(); ++j) {
            CHECK((held[i].name != held[j].name));
        }
        // Not also in the window set or the temporal set, which hold their own values.
        for (const auto& w :
             evr::stereo_seq::stereoWindowCvars("game.exe +r_windowWidth 1 +r_windowHeight 1")) {
            CHECK((held[i].name != w.name));
        }
        for (const auto& t : evr::stereo_seq::stereoRuntimeCvars()) {
            CHECK((held[i].name != t.name));
        }
    }
}

TEST_CASE("SSDO cvar: held on unless the setting is 0, never part of the other sets") {
    for (const char* on : {"", "1"}) {
        const auto c = evr::stereo_seq::stereoSsdoCvar(on);
        REQUIRE(c.has_value());
        CHECK((c->name == "r_SSDO"));
        CHECK((c->value == "1"));
    }
    for (const char* none : {"0", "2", "on", " 1"}) {
        CHECK_FALSE(evr::stereo_seq::stereoSsdoCvar(none).has_value());
    }
    for (const auto& c : evr::stereo_seq::stereoComfortCvars()) {
        CHECK((c.name != "r_SSDO"));
    }
    for (const auto& t : evr::stereo_seq::stereoRuntimeCvars()) {
        CHECK((t.name != "r_SSDO"));
    }
}

TEST_CASE("cvar list: name=value items, trimmed, a later item replaces an earlier one") {
    const auto held = evr::stereo_seq::parseCvarList(
        " r_shadowMaxStaleFrames = 1,1,1,2,2 ;r_skipPlayerShadow=1;;=5;noequals;R_SKIPPLAYERSHADOW=0;x=");
    REQUIRE(held.size() == 3);
    CHECK(held[0].name == "r_shadowMaxStaleFrames");
    CHECK(held[0].value == "1,1,1,2,2");
    CHECK(held[1].name == "r_skipPlayerShadow");
    CHECK(held[1].value == "0");
    CHECK(held[2].name == "x");
    CHECK(held[2].value.empty());
    CHECK(evr::stereo_seq::parseCvarList("").empty());
    CHECK(evr::stereo_seq::parseCvarList("1").empty());
    // "name=?" (only log the value) passes through.
    const auto query = evr::stereo_seq::parseCvarList("r_lodScale=?");
    REQUIRE(query.size() == 1);
    CHECK(query[0].value == "?");
}

TEST_CASE("cvar cap: <=N is a cap, anything else is a plain value") {
    using evr::stereo_seq::parseCvarCap;
    REQUIRE(parseCvarCap("<=1").has_value());
    CHECK(*parseCvarCap("<=1") == doctest::Approx(1.0f));
    CHECK(*parseCvarCap("<=0.8") == doctest::Approx(0.8f));
    CHECK(*parseCvarCap("<=2.25") == doctest::Approx(2.25f));
    for (const char* plain : {"1", "", "<=", "<=x", "<=1,2", "<= 1", "=<1", "<=1.0f", "<=nan", "<=inf"}) {
        CHECK_FALSE(parseCvarCap(plain).has_value());
    }
    // The list keeps the "<=" for the layer to see.
    const auto held = evr::stereo_seq::parseCvarList("r_shadowsDistanceFadeMultiplier=<=1");
    REQUIRE(held.size() == 1);
    CHECK(held[0].value == "<=1");
}
