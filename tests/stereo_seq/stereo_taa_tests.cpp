#include "stereo_seq/stereo_taa.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <set>
#include <string>
#include <string_view>

using evr::stereo_seq::AccumPick;
using evr::stereo_seq::AccumRole;
using evr::stereo_seq::Eye;
using evr::stereo_seq::EyeTagQueue;
using evr::stereo_seq::NgxTwins;
using evr::stereo_seq::pickAccumulation;
using evr::stereo_seq::RenderTag;
using evr::stereo_seq::TaaReadiness;
using evr::stereo_seq::TaaResetPlanner;
using evr::stereo_seq::taaSubSample;

namespace {

RenderTag tag(Eye eye, std::uint32_t eyeSeq) {
    RenderTag t;
    t.eye = eye;
    t.eyeSeq = eyeSeq;
    return t;
}

// The image a pick names, as "pair * 2 + index".
int image(const AccumPick& p) {
    return p.pair * 2 + p.index;
}

} // namespace

TEST_CASE("stereo TAA: untagged frames and frames without eye R's images keep the engine's choice") {
    const RenderTag left = tag(Eye::Left, 4);
    CHECK(pickAccumulation(AccumRole::Output, nullptr, true).engine);
    CHECK(pickAccumulation(AccumRole::History, &left, false).engine);
}

TEST_CASE("stereo TAA: each eye reads what it wrote on its previous frame, never the other eye's") {
    // Two renders per tick through one tag queue: L, R, L, R, ... with a mono frame in between.
    EyeTagQueue q;
    q.rebase(10);
    const Eye order[] = {Eye::Left, Eye::Right, Eye::Left, Eye::Right, Eye::Mono, Eye::Left, Eye::Right};
    int lastWritten[2] = {-1, -1};
    std::set<int> leftImages;
    std::set<int> rightImages;
    std::uint32_t backend = 11;
    for (const Eye e : order) {
        RenderTag t;
        t.eye = e;
        REQUIRE(q.push(t));
        const RenderTag* in = q.peek(backend);
        REQUIRE(in != nullptr);
        const AccumPick out = pickAccumulation(AccumRole::Output, in, true);
        const AccumPick hist = pickAccumulation(AccumRole::History, in, true);
        CHECK_FALSE(out.engine);
        CHECK(out.pair == hist.pair);
        CHECK(out.index != hist.index);
        const int i = evr::stereo_seq::eyeIndex(e);
        if (lastWritten[i] >= 0) {
            CHECK(image(hist) == lastWritten[i]);
        }
        lastWritten[i] = image(out);
        (i == 0 ? leftImages : rightImages).insert(image(out));
        (i == 0 ? leftImages : rightImages).insert(image(hist));
        REQUIRE(q.pop(backend).tagged);
        ++backend;
    }
    // Eye L (and mono) only ever touch the engine's pair, eye R only the second one.
    CHECK(leftImages == std::set<int>{0, 1});
    CHECK(rightImages == std::set<int>{2, 3});
}

TEST_CASE("stereo TAA: both eyes of a tick share one jitter phase, consecutive ticks consecutive phases") {
    CHECK(taaSubSample(0, 32) == 0);
    CHECK(taaSubSample(31, 32) == 31);
    CHECK(taaSubSample(32, 32) == 0);
    CHECK(taaSubSample(100, 32) == 100 % 32);
    // Every phase of the sequence is reached by a single eye over N ticks (the engine's counter, which
    // advances twice per tick, would give each eye every other phase only).
    std::set<int> phases;
    for (std::uint64_t f = 1000; f < 1032; ++f) {
        phases.insert(taaSubSample(f, 32));
    }
    CHECK(phases.size() == 32);
    CHECK(taaSubSample(7, 0) == 0);
    CHECK(taaSubSample(700, 1000) == 700 % 255);
}

TEST_CASE("stereo TAA: eye L's exposure alternates by its own frames, eye R reads eye L's of the tick") {
    evr::stereo_seq::ExposurePlanner p;
    EyeTagQueue q;
    q.rebase(0);
    int lastLeftWritten = -1;
    std::uint32_t backend = 1;
    const Eye order[] = {Eye::Left, Eye::Right, Eye::Left, Eye::Right, Eye::Mono, Eye::Left, Eye::Right};
    for (const Eye e : order) {
        RenderTag t;
        t.eye = e;
        REQUIRE(q.push(t));
        const int index = p.indexFor(*q.peek(backend));
        if (e == Eye::Right) {
            CHECK(index == lastLeftWritten); // the exposure eye L wrote this tick
        } else {
            if (lastLeftWritten >= 0) {
                CHECK(index != lastLeftWritten); // writes the other image, reads the one written before
            }
            lastLeftWritten = index;
        }
        REQUIRE(q.pop(backend).tagged);
        ++backend;
    }
}

TEST_CASE("stereo TAA: the exposure index is held under Route S whatever the TAA mode") {
    using evr::stereo_seq::ExposureGate;
    using evr::stereo_seq::exposureIndexHeld;
    ExposureGate g;
    g.hooked = true;
    g.routeS = true;
    g.gameTouch = true;
    // Per-eye TAA off (ETERNALVR_STEREO_TAA=0, the launcher's anti-aliasing Off): held on its own.
    CHECK(exposureIndexHeld(g));
    // Per-eye TAA requested: from its first stereo tick, as before, and on its own once it failed closed.
    g.taaRequested = true;
    CHECK_FALSE(exposureIndexHeld(g));
    g.taaPerEye = true;
    CHECK(exposureIndexHeld(g));
    g.taaPerEye = false;
    g.taaFailedClosed = true;
    CHECK(exposureIndexHeld(g));
    // With eye R's skip off the engine's own chain stays, unless per-eye TAA is on (as before).
    g.exposureOnce = false;
    CHECK_FALSE(exposureIndexHeld(g)); // failed closed
    g.taaRequested = false;
    CHECK_FALSE(exposureIndexHeld(g)); // per-eye TAA off
    g.taaRequested = true;
    g.taaFailedClosed = false;
    g.taaPerEye = true;
    CHECK(exposureIndexHeld(g));
    g.taaPerEye = false;
    g.taaFailedClosed = true;
    g.exposureOnce = true;
    // Never without the hook, the eye tags or the multiplayer guard.
    for (bool ExposureGate::* piece :
         {&ExposureGate::hooked, &ExposureGate::routeS, &ExposureGate::gameTouch}) {
        ExposureGate missing = g;
        missing.*piece = false;
        CHECK_FALSE(exposureIndexHeld(missing));
        missing.taaRequested = false;
        CHECK_FALSE(exposureIndexHeld(missing));
    }
}

TEST_CASE("stereo TAA: history resets on the first stereo tick and after eye R missed a tick") {
    TaaResetPlanner p;
    CHECK(p.onLeft(100)); // nothing rendered yet
    CHECK(p.onRight(100));
    CHECK_FALSE(p.onLeft(101));
    CHECK_FALSE(p.onRight(101));
    CHECK_FALSE(p.onLeft(102));
    CHECK_FALSE(p.onRight(102));
    // Tick 103 was mono (eye R skipped): eye R's history is two ticks old.
    CHECK(p.onLeft(104));
    CHECK(p.onRight(104));
    CHECK_FALSE(p.onLeft(105));
    // Eye L decided 105 but eye R never came; eye R renders 106 after its eye L.
    CHECK(p.onLeft(106));
    CHECK(p.onRight(106));
}

TEST_CASE("stereo TAA: eye R without its own eye L decision resets") {
    TaaResetPlanner p;
    CHECK(p.onLeft(10));
    CHECK(p.onRight(10));
    CHECK_FALSE(p.onLeft(11));
    CHECK(p.onRight(12)); // not the frame eye L decided
}

TEST_CASE("stereo TAA: NGX twins follow the game's feature and reset on first use and after a gap") {
    NgxTwins t;
    CHECK(t.twinOf(0x10) == 0);
    CHECK_FALSE(t.known(0x10));
    t.created(0x10, 0x20);
    CHECK(t.known(0x10));
    CHECK(t.twinOf(0x10) == 0x20);
    CHECK(t.resetTwin(0x10, 50));
    CHECK_FALSE(t.resetTwin(0x10, 51));
    CHECK_FALSE(t.resetTwin(0x10, 52));
    CHECK(t.resetTwin(0x10, 54));
    // The game recreates its feature (a new size): a new twin, reset again.
    CHECK(t.released(0x10) == 0x20);
    CHECK(t.twinOf(0x10) == 0);
    CHECK(t.size() == 0);
    t.created(0x30, 0x40);
    CHECK(t.resetTwin(0x30, 55));
    // A twin that could not be created is remembered as none.
    t.created(0x50, 0);
    CHECK(t.known(0x50)); // tried once: not tried again
    CHECK(t.twinOf(0x50) == 0);
    CHECK(t.released(0x50) == 0);
    CHECK(t.released(0x99) == 0);
    CHECK(t.resetTwin(0x99, 1));
    CHECK(t.size() == 1);
}

TEST_CASE("stereo TAA: NGX twins that failed are forgotten for a new try, the others kept") {
    NgxTwins t;
    t.created(0x10, 0x20);
    CHECK_FALSE(t.anyFailed());
    t.created(0x30, 0);
    t.created(0x50, 0);
    CHECK(t.anyFailed());
    CHECK(t.forgetFailed() == 2);
    CHECK_FALSE(t.anyFailed());
    CHECK(t.size() == 1);
    CHECK(t.twinOf(0x10) == 0x20);
    CHECK_FALSE(t.known(0x30)); // eye R's next evaluation of it tries again
    t.created(0x30, 0x40);
    CHECK(t.twinOf(0x30) == 0x40);
    CHECK(t.forgetFailed() == 0);
    CHECK(t.size() == 2);
}

TEST_CASE("stereo TAA: the forced and fail-closed cvar sets") {
    bool antiGhosting = false;
    for (const auto& c : evr::stereo_seq::stereoTaaForcedCvars()) {
        CHECK(std::string(c.value) == "0");
        antiGhosting = antiGhosting || c.name == "r_TAAAntiGhosting";
    }
    CHECK(antiGhosting);
    const auto& closed = evr::stereo_seq::stereoTaaFailClosedCvars();
    REQUIRE(closed.size() == 2);
    CHECK(std::string(closed[0].name) == "r_antialiasing");
    CHECK(std::string(closed[0].value) == "0");
    CHECK(std::string(closed[1].name) == "r_TAASafeMode");
    CHECK(std::string(closed[1].value) == "1");
    CHECK(std::string(evr::stereo_seq::stereoTaaCommandLineCvars().front().name) == "r_TAASafeMode");
}

TEST_CASE("stereo TAA: the first missing piece is named") {
    TaaReadiness r;
    CHECK(std::string_view(evr::stereo_seq::taaMissingPiece(r)).find("selector") != std::string_view::npos);
    r.selectors = true;
    CHECK(std::string_view(evr::stereo_seq::taaMissingPiece(r)).find("eye R") != std::string_view::npos);
    r.secondPair = true;
    r.subSamples = true;
    CHECK(std::string_view(evr::stereo_seq::taaMissingPiece(r)).find("exposure") != std::string_view::npos);
    r.exposure = true;
    CHECK(std::string_view(evr::stereo_seq::taaMissingPiece(r)).find("cvar") != std::string_view::npos);
    r.cvarSetter = true;
    CHECK(evr::stereo_seq::taaMissingPiece(r) == nullptr);
}

TEST_CASE("stereo TAA: switch values") {
    using evr::stereo_seq::switchValue;
    CHECK(switchValue("", true));
    CHECK_FALSE(switchValue("", false));
    CHECK_FALSE(switchValue("0", true));
    CHECK_FALSE(switchValue("Off", true));
    CHECK_FALSE(switchValue("FALSE", true));
    CHECK(switchValue("1", false));
    CHECK(switchValue("on", false));
    CHECK(switchValue("maybe", true));
}

TEST_CASE("stereo TAA: the held anti-aliasing mode") {
    using evr::stereo_seq::heldAntialiasing;
    CHECK(heldAntialiasing(0, false, true) == 1); // AA off: TAA
    CHECK(heldAntialiasing(1, false, true) == 1);
    CHECK(heldAntialiasing(2, false, true) == 2);  // the player's DLSS, per eye
    CHECK(heldAntialiasing(1, true, true) == 2);   // the DLSS option
    CHECK(heldAntialiasing(2, false, false) == 1); // no DLSS feature for eye R: TAA
    CHECK(heldAntialiasing(0, true, false) == 1);
    CHECK(heldAntialiasing(7, false, true) == 1);
}

TEST_CASE("stereo TAA: the DLSS quality names and numbers map to r_dlssQuality") {
    using evr::stereo_seq::dlssQualityValue;
    CHECK(dlssQualityValue("ultra_performance") == 0);
    CHECK(dlssQualityValue("Performance") == 1);
    CHECK(dlssQualityValue("BALANCED") == 2);
    CHECK(dlssQualityValue("quality") == 3);
    CHECK(dlssQualityValue("0") == 0);
    CHECK(dlssQualityValue("3") == 3);
    // Unset, empty or unknown: the game's own setting stays.
    CHECK(dlssQualityValue("") == -1);
    CHECK(dlssQualityValue("4") == -1);
    CHECK(dlssQualityValue("5") == -1);
    CHECK(dlssQualityValue("ultra performance") == -1);
}

TEST_CASE("stereo TAA: DLAA holds r_dlssQuality at Quality and is named only as dlaa") {
    using evr::stereo_seq::dlssQualityIsDlaa;
    using evr::stereo_seq::dlssQualityValue;
    // The game maps r_dlssQuality 0 to 3 only; DLAA goes on NGX's parameter, and runs as Quality where it
    // cannot.
    CHECK(dlssQualityValue("dlaa") == 3);
    CHECK(dlssQualityValue("DLAA") == 3);
    CHECK(dlssQualityIsDlaa("dlaa"));
    CHECK(dlssQualityIsDlaa("Dlaa"));
    CHECK_FALSE(dlssQualityIsDlaa("quality"));
    CHECK_FALSE(dlssQualityIsDlaa("3"));
    CHECK_FALSE(dlssQualityIsDlaa("5"));
    CHECK_FALSE(dlssQualityIsDlaa(""));
}
