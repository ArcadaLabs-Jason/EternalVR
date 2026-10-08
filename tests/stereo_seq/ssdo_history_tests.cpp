#include "stereo_seq/ssdo_history.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

using evr::stereo_seq::Eye;
using evr::stereo_seq::kSsdoArrays;
using evr::stereo_seq::SsdoHistory;
using evr::stereo_seq::SsdoPair;
using evr::stereo_seq::SsdoPlan;
using evr::stereo_seq::SsdoRestart;

namespace {

// Distinct fake target pointers: eye L's 1 and 2, eye R's 3 and 4, the unfiltered target 9.
void* target(std::uintptr_t i) {
    return reinterpret_cast<void*>(i * 0x100);
}

SsdoHistory ready() {
    SsdoHistory h;
    h.reset({target(1), target(2)}, {target(3), target(4)}, target(9));
    return h;
}

void* writtenOf(const SsdoPlan& p, std::uint32_t counter) {
    return p.targets[counter & 1u];
}

void* readOf(const SsdoPlan& p, std::uint32_t counter) {
    return p.targets[(counter & 1u) ^ 1u];
}

// Alternating eyes from counter `from` to `to`: eye L on odd counters, eye R on even ones.
void alternate(SsdoHistory& h, std::uint32_t from, std::uint32_t to) {
    for (std::uint32_t c = from; c <= to; ++c) {
        h.beforeRender(c % 2 ? Eye::Left : Eye::Right, c);
    }
}

} // namespace

TEST_CASE("ssdo history: each eye reads the target it wrote last, whatever the counter's parity") {
    SsdoHistory h = ready();
    REQUIRE(h.ready());
    // Eye L on odd counters, eye R on even ones (two renders per tick): the engine alone would have eye L
    // write [1] and read [0], which eye R wrote.
    const auto l1 = h.beforeRender(Eye::Left, 11);
    const auto r1 = h.beforeRender(Eye::Right, 12);
    const auto l2 = h.beforeRender(Eye::Left, 13);
    const auto r2 = h.beforeRender(Eye::Right, 14);
    const auto l3 = h.beforeRender(Eye::Left, 15);
    CHECK(readOf(l2, 13) == writtenOf(l1, 11));
    CHECK(readOf(l3, 15) == writtenOf(l2, 13));
    CHECK(readOf(r2, 14) == writtenOf(r1, 12));
    CHECK(writtenOf(l2, 13) != writtenOf(l1, 11)); // alternates within its pair
    CHECK(writtenOf(r2, 14) != writtenOf(r1, 12));
    for (const auto* p : {&l1, &l2, &l3}) {
        CHECK((p->targets[0] == target(1) || p->targets[0] == target(2)));
        CHECK((p->targets[1] == target(1) || p->targets[1] == target(2)));
        CHECK(p->targets[2] == target(9));
    }
    for (const auto* p : {&r1, &r2}) {
        CHECK((p->targets[0] == target(3) || p->targets[0] == target(4)));
        CHECK((p->targets[1] == target(3) || p->targets[1] == target(4)));
        CHECK(p->targets[2] == target(9));
    }
}

TEST_CASE("ssdo history: the first render of each eye resets, steady stereo never does") {
    SsdoHistory h = ready();
    CHECK(h.beforeRender(Eye::Left, 1).restart == SsdoRestart::First);
    CHECK(h.beforeRender(Eye::Right, 2).restart == SsdoRestart::First);
    for (std::uint32_t c = 3; c <= 40; ++c) {
        CHECK(h.beforeRender(c % 2 ? Eye::Left : Eye::Right, c).restart == SsdoRestart::None);
    }
}

TEST_CASE("ssdo history: mono renders are eye L's chain") {
    SsdoHistory h = ready();
    const auto m1 = h.beforeRender(Eye::Mono, 1);
    CHECK(m1.restart == SsdoRestart::First);
    const auto m2 = h.beforeRender(Eye::Mono, 2);
    CHECK(m2.restart == SsdoRestart::None); // the render just before, its own
    CHECK(readOf(m2, 2) == writtenOf(m1, 1));
    // Eye L goes on from the mono renders; eye R never rendered, so both start over at the first stereo tick.
    const auto l = h.beforeRender(Eye::Left, 3);
    CHECK(l.restart == SsdoRestart::EyeRMissed);
    CHECK(readOf(l, 3) == writtenOf(m2, 2));
    CHECK(h.beforeRender(Eye::Right, 4).restart == SsdoRestart::First);
    CHECK(h.beforeRender(Eye::Left, 5).restart == SsdoRestart::None);
}

TEST_CASE("ssdo history: a skipped eye R resets both eyes at the next tick") {
    SsdoHistory h = ready();
    alternate(h, 1, 6);
    CHECK(h.beforeRender(Eye::Left, 7).restart == SsdoRestart::None);
    // Eye R skipped this tick: eye L renders again at 8.
    CHECK(h.beforeRender(Eye::Left, 8).restart == SsdoRestart::EyeRMissed);
    CHECK(h.beforeRender(Eye::Right, 9).restart == SsdoRestart::Gap); // its own last render is 6
    CHECK(h.beforeRender(Eye::Left, 10).restart == SsdoRestart::None);
    CHECK(h.beforeRender(Eye::Right, 11).restart == SsdoRestart::None);
}

TEST_CASE("ssdo history: a mono stretch resets eye R, and eye L at the first stereo tick after it") {
    SsdoHistory h = ready();
    alternate(h, 1, 6);
    for (std::uint32_t c = 7; c <= 30; ++c) {
        CHECK(h.beforeRender(Eye::Mono, c).restart == SsdoRestart::None);
    }
    CHECK(h.beforeRender(Eye::Left, 31).restart == SsdoRestart::EyeRMissed);
    CHECK(h.beforeRender(Eye::Right, 32).restart == SsdoRestart::Gap);
    CHECK(h.beforeRender(Eye::Left, 33).restart == SsdoRestart::None);
}

TEST_CASE("ssdo history: eye R after a run of its own renders, and a counter that went back") {
    SsdoHistory h = ready();
    alternate(h, 1, 6);
    CHECK(h.beforeRender(Eye::Right, 7).restart == SsdoRestart::None); // its own render just before
    CHECK(h.beforeRender(Eye::Left, 8).restart == SsdoRestart::Gap);   // its own last render is 5
    // The same counter again (a render that never happened in between) is not continuous.
    CHECK(h.beforeRender(Eye::Left, 8).restart == SsdoRestart::Gap);
}

TEST_CASE("ssdo history: an untagged render right after eye L does not take eye L's history") {
    SsdoHistory h = ready();
    alternate(h, 1, 6);
    CHECK(h.beforeRender(Eye::Left, 7).restart == SsdoRestart::None);
    // Eye R's render of this tick without its tag: counted as mono, one render after eye L.
    const auto m = h.beforeRender(Eye::Mono, 8);
    CHECK(m.restart == SsdoRestart::Untagged);
    // Eye L's next render finds eye R missing and starts over; eye R starts over after its gap.
    CHECK(h.beforeRender(Eye::Left, 9).restart == SsdoRestart::EyeRMissed);
    CHECK(h.beforeRender(Eye::Right, 10).restart == SsdoRestart::Gap);
    CHECK(h.beforeRender(Eye::Left, 11).restart == SsdoRestart::None);
    // A mono stretch after mono renders is not affected: only a mono render right after eye L's.
    CHECK(h.beforeRender(Eye::Mono, 13).restart == SsdoRestart::None);
    CHECK(h.beforeRender(Eye::Mono, 14).restart == SsdoRestart::None);
}

TEST_CASE("ssdo history: dynamic resolution held off, then both eyes start over for that reason") {
    SsdoHistory h = ready();
    alternate(h, 1, 6);
    h.invalidate(SsdoRestart::Resumed);
    CHECK(h.beforeRender(Eye::Left, 41).restart == SsdoRestart::Resumed);
    CHECK(h.beforeRender(Eye::Right, 42).restart == SsdoRestart::Resumed);
    CHECK(h.beforeRender(Eye::Left, 43).restart == SsdoRestart::None);
}

TEST_CASE("ssdo history: after a resize both eyes start over and keep their targets") {
    SsdoHistory h = ready();
    alternate(h, 1, 6);
    h.invalidate();
    const auto l = h.beforeRender(Eye::Left, 7);
    const auto r = h.beforeRender(Eye::Right, 8);
    CHECK(l.restart == SsdoRestart::Resize);
    CHECK(r.restart == SsdoRestart::Resize);
    CHECK(h.beforeRender(Eye::Left, 9).restart == SsdoRestart::None);
    CHECK(h.beforeRender(Eye::Right, 10).restart == SsdoRestart::None);
    CHECK((writtenOf(l, 7) == target(1) || writtenOf(l, 7) == target(2)));
    CHECK((writtenOf(r, 8) == target(3) || writtenOf(r, 8) == target(4)));
}

TEST_CASE("ssdo history: the counter wraps without a reset") {
    SsdoHistory h = ready();
    std::uint32_t c = 0xFFFFFFFCu;
    h.beforeRender(Eye::Left, c++);
    h.beforeRender(Eye::Right, c++);
    for (int i = 0; i < 8; ++i, ++c) {
        CHECK(h.beforeRender(i % 2 ? Eye::Right : Eye::Left, c).restart == SsdoRestart::None);
    }
}

TEST_CASE("ssdo history: a key always names the same three targets") {
    SsdoHistory h = ready();
    std::set<int> keys;
    std::array<std::array<void*, 3>, kSsdoArrays> byKey{};
    // Stereo with eye L on odd counters, a mono stretch, then stereo with eye L on even counters after a
    // skipped eye R: every parity of every eye.
    const auto eyeOf = [](std::uint32_t c) {
        if (c <= 20) {
            return c % 2 ? Eye::Left : Eye::Right;
        }
        if (c <= 30) {
            return Eye::Mono;
        }
        return c <= 32 || c % 2 == 0 ? Eye::Left : Eye::Right;
    };
    for (std::uint32_t c = 1; c <= 64; ++c) {
        const Eye eye = eyeOf(c);
        const auto p = h.beforeRender(eye, c);
        REQUIRE(p.key >= 0);
        REQUIRE(p.key < kSsdoArrays);
        if (keys.insert(p.key).second) {
            byKey[static_cast<std::size_t>(p.key)] = p.targets;
        } else {
            CHECK(byKey[static_cast<std::size_t>(p.key)] == p.targets);
        }
    }
    CHECK(keys.size() == static_cast<std::size_t>(kSsdoArrays));
}

TEST_CASE("ssdo history: not ready without four distinct targets and the unfiltered one") {
    SsdoHistory h;
    CHECK_FALSE(h.ready());
    h.reset({target(1), target(2)}, {target(3), nullptr}, target(9));
    CHECK_FALSE(h.ready());
    h.reset({target(1), target(2)}, {target(3), target(4)}, nullptr);
    CHECK_FALSE(h.ready());
    h.reset({target(1), target(2)}, {target(1), target(4)}, target(9)); // eye R given an engine target
    CHECK_FALSE(h.ready());
    h.reset({target(1), target(1)}, {target(3), target(4)}, target(9));
    CHECK_FALSE(h.ready());
    h.reset({target(1), target(2)}, {target(3), target(4)}, target(9));
    CHECK(h.ready());
    CHECK(h.beforeRender(Eye::Right, 5).restart == SsdoRestart::First); // a reset forgets the renders
}

TEST_CASE("ssdo readiness: the filter runs only with every piece in place, and says what is missing") {
    using evr::stereo_seq::ssdoNotReady;
    using evr::stereo_seq::SsdoReadiness;
    SsdoReadiness r;
    r.requested = true;
    r.installed = true;
    r.gameTouch = true;
    r.targetsMade = true;
    CHECK(ssdoNotReady(r) == nullptr);
    SsdoReadiness noTargets = r;
    noTargets.targetsMade = false; // the device context hook never ran
    REQUIRE(ssdoNotReady(noTargets) != nullptr);
    CHECK(std::string(ssdoNotReady(noTargets)).find("targets were not made") != std::string::npos);
    SsdoReadiness dynamic = r;
    dynamic.dynamicResolution = true;
    REQUIRE(ssdoNotReady(dynamic) != nullptr);
    CHECK(std::string(ssdoNotReady(dynamic)).find("rs_enable") != std::string::npos);
    SsdoReadiness closed = r;
    closed.failedClosed = true;
    CHECK(std::string(ssdoNotReady(closed)) == "it failed closed");
    SsdoReadiness off = r;
    off.requested = false;
    CHECK(std::string(ssdoNotReady(off)).find("ETERNALVR_STEREO_SSDO_TAA=0") != std::string::npos);
    SsdoReadiness notInstalled = r;
    notInstalled.installed = false;
    CHECK(ssdoNotReady(notInstalled) != nullptr);
    SsdoReadiness guard = r;
    guard.gameTouch = false;
    CHECK(ssdoNotReady(guard) != nullptr);
}

TEST_CASE("ssdo history: restart names") {
    CHECK(std::string(evr::stereo_seq::ssdoRestartName(SsdoRestart::EyeRMissed)) == "eye R missed a tick");
    CHECK(std::string(evr::stereo_seq::ssdoRestartName(SsdoRestart::None)) == "none");
    CHECK(std::string(evr::stereo_seq::ssdoRestartName(SsdoRestart::Untagged)) ==
          "an untagged render after eye L");
    CHECK(std::string(evr::stereo_seq::ssdoRestartName(SsdoRestart::Resumed)) == "after dynamic resolution");
}
