#include "vkcore/view_snapshot_impl.hpp"

#include "vkcore/log.hpp"
#include "vkcore/view_slots.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace evr::vkcore::view_snapshot {

namespace {

constexpr std::uint64_t kWindowMs = 10000;

// Under the snapshots' mutex.
Counters g_mark;             // the counts at the last 10 s line
std::uint64_t g_markMs = 0;  // when it was logged (0: no present yet)
std::uint64_t g_firstMs = 0; // the first present
std::array<TraceEntry, kTrace> g_trace{};
std::uint64_t g_traced = 0;  // presents in the trace so far
std::uint64_t g_dumpAt = 0;  // the present after which the trace is logged (0: none asked)
std::uint64_t g_askedAt = 0; // the present the capture fired at

unsigned long long n(const Counters& c, Count which) {
    return static_cast<unsigned long long>(c[which]);
}

double share(const Counters& c, Count which) {
    const std::uint64_t presents = c[Count::Presents];
    return presents == 0 ? 0.0 : 100.0 * static_cast<double>(c[which]) / static_cast<double>(presents);
}

// The test knobs' part of the pairs' first line ("" with none of them set).
std::string knobCounts(const Counters& c) {
    std::string out;
    char part[200];
    if (parallelEyesSettings().eye1Lag) {
        std::snprintf(
            part, sizeof(part),
            "; eye 1 a frame late (ETERNALVR_TEST_PE_EYE1_LAG=1) %llu, two or more frames late %llu, "
            "its own frame's (no pair shown before) %llu",
            n(c, Count::Eye1Late), n(c, Count::Eye1LateMore), n(c, Count::Eye1NotLate));
        out += part;
    }
    if (const std::uint32_t every = parallelEyesSettings().dropEvery) {
        std::snprintf(part, sizeof(part), "; new pairs left out (ETERNALVR_TEST_PE_DROP=%u) %llu (%.1f%%)",
                      every, n(c, Count::PairDropped), share(c, Count::PairDropped));
        out += part;
    }
    return out;
}

// The pairs' lines (the default).
void logPairs(const char* span, const Counters& c) {
    EVR_LOG(
        "%s: %s: %llu present(s): a new pair %llu (%.1f%%), the last pair kept %llu (%.1f%%), no pair (the "
        "presented image and view 1's) %llu (%.1f%%), a new swapchain image not drawn yet %llu; pairs gone "
        "past by a newer one %llu, written over with one eye only %llu%s",
        kTag, span, n(c, Count::Presents), n(c, Count::PairNew), share(c, Count::PairNew),
        n(c, Count::PairKept), share(c, Count::PairKept), n(c, Count::PairNone), share(c, Count::PairNone),
        n(c, Count::Undrawn), n(c, Count::PairSkipped), n(c, Count::PairNeverComplete),
        knobCounts(c).c_str());
    EVR_LOG(
        "%s: %s: eye 0 copies %llu (no slot free %llu, no swapchain image %llu, the image handed to another "
        "queue family %llu, another queue family %llu, the game's signals not movable %llu); view 1 screen "
        "passes %llu (not submitted before the "
        "next %llu, no command buffer %llu), eye 1 copies %llu (no slot free %llu, image unknown %llu, "
        "another queue family %llu); pairs whose eyes came with different view records %llu",
        kTag, span, n(c, Count::Copies0), n(c, Count::Busy0), n(c, Count::NotSwapchain0),
        n(c, Count::Released0), n(c, Count::OtherFamily0), n(c, Count::Unordered0), n(c, Count::Arms),
        n(c, Count::Dropped), n(c, Count::NoCb), n(c, Count::Copies), n(c, Count::Busy), n(c, Count::Unknown),
        n(c, Count::OtherFamily), n(c, Count::PairViewDiffers));
}

// The guess's lines (ETERNALVR_TEST_PE_PAIRING=guess).
void logGuess(const char* span, const Counters& c) {
    EVR_LOG("%s: %s: %llu present(s): eye 1 from its frame's copy %llu (%.1f%%), the same copy as the last "
            "present %llu (%.1f%%, %s), from view 1's image (no unread copy) %llu (%.1f%%); view 1 screen "
            "passes %llu (not submitted before the next %llu, no command buffer %llu, no semaphore signalled "
            "%llu, another queue family %llu), copies %llu (no slot free %llu, image unknown %llu)",
            kTag, span, n(c, Count::Presents), n(c, Count::Matched), share(c, Count::Matched),
            n(c, Count::Repeated), share(c, Count::Repeated),
            parallelEyesSettings().showRepeats ? "shown: ETERNALVR_TEST_PE_REPEATS=show"
                                               : "not shown: the headset keeps the last pair",
            n(c, Count::NoCopy), share(c, Count::NoCopy), n(c, Count::Arms), n(c, Count::Dropped),
            n(c, Count::NoCb), n(c, Count::NoTag), n(c, Count::OtherFamily), n(c, Count::Copies),
            n(c, Count::Busy), n(c, Count::Unknown));
    EVR_LOG(
        "%s: %s: the presented image was drawn by the matched frame %llu, the frame before %llu, both "
        "%llu, neither known %llu; no copy of its own, the last one again %llu; a new swapchain image not "
        "drawn yet, the last pair kept %llu%s",
        kTag, span, n(c, Count::DrewMatched), n(c, Count::DrewBefore), n(c, Count::DrewBoth),
        n(c, Count::DrewNotKnown), n(c, Count::LastAgain), n(c, Count::Undrawn), shownNote());
}

void logCounts(const char* span, const Counters& c) {
    if (pairsOn()) {
        logPairs(span, c);
    } else {
        logGuess(span, c);
    }
    EVR_LOG(
        "%s: %s: frames with view 0's command buffer submitted before view 1's %llu, in the same submit "
        "%llu, after it %llu, never seen %llu; presents with the newest frame's view 0 submitted %llu, not "
        "yet %llu; the frame shown 0 frames behind the newest %llu, 1 %llu, 2 or more %llu, changes %llu; "
        "copies never shown %llu",
        kTag, span, n(c, Count::V0Before), n(c, Count::V0With), n(c, Count::V0After), n(c, Count::V0Never),
        n(c, Count::AtPresentSubmitted), n(c, Count::AtPresentNotYet), n(c, Count::Lag0), n(c, Count::Lag1),
        n(c, Count::LagMore), n(c, Count::LagChanges), n(c, Count::NeverShown));
}

// Four presents a line, oldest first.
std::vector<std::string> traceLines() {
    std::vector<std::string> lines;
    const std::uint64_t count = std::min<std::uint64_t>(g_traced, kTrace);
    char line[768];
    std::snprintf(
        line, sizeof(line),
        "%s: trace of the last %llu presents, the capture fired at present %llu; each: present, "
        "seconds, newest frame with view 1 submitted / frame shown, outcome (pairs: N a new pair, K "
        "the last one kept, P no pair%s; guess: s its frame's copy, r the last copy again, n no copy; h "
        "held: a new swapchain image), that frame's view 0 (s submitted, w not yet), its "
        "submit against view 1's (b before, w same, a after), the presented image drawn by (m the "
        "matched frame, p the one before, b both), image index + 1, presenter copy, view record",
        kTag, static_cast<unsigned long long>(count), static_cast<unsigned long long>(g_askedAt),
        parallelEyesSettings().dropEvery ? ", D a new pair left out (ETERNALVR_TEST_PE_DROP)" : "");
    lines.emplace_back(line);
    std::string out;
    for (std::uint64_t i = 0; i < count; ++i) {
        const TraceEntry& t = g_trace[(g_traced - count + i) % kTrace];
        char entry[128];
        std::snprintf(entry, sizeof(entry), "%s%llu %.3f %llu/%llu %c%c%c%c i%u c%llu v%llu",
                      out.empty() ? "" : " | ", static_cast<unsigned long long>(t.present), t.seconds,
                      static_cast<unsigned long long>(t.own), static_cast<unsigned long long>(t.shown),
                      t.outcome, t.view0, t.order, t.drew, t.image, static_cast<unsigned long long>(t.value),
                      static_cast<unsigned long long>(t.view));
        out += entry;
        if (i % 4 == 3 || i + 1 == count) {
            lines.push_back(std::string(kTag) + ": trace " + out);
            out.clear();
        }
    }
    return lines;
}

// On a thread of its own: each line is a synchronous write, too many for the present hook.
void logTrace() {
    auto write = [lines = traceLines()] {
        for (const std::string& l : lines) {
            EVR_LOG("%s", l.c_str());
        }
    };
    try {
        // The DLL is pinned by the XR worker, so the thread's code outlives any unload of the layer.
        std::thread(std::move(write)).detach();
    } catch (const std::system_error&) {
        EVR_LOG("%s: no thread for the trace; not logged", kTag);
    }
}

} // namespace

void tracePresent(const TraceEntry& entry) {
    g_trace[g_traced++ % kTrace] = entry;
    if (g_dumpAt != 0 && entry.present >= g_dumpAt) {
        g_dumpAt = 0;
        logTrace();
    }
}

void traceCopied(std::uint64_t present, std::uint64_t value) {
    TraceEntry& t = g_trace[(g_traced + kTrace - 1) % kTrace];
    if (g_traced != 0 && t.present == present) {
        t.value = value;
    }
}

void traceCapture(std::uint32_t frames) {
    if (g_dumpAt != 0 || g_traced == 0) {
        return; // one at a time
    }
    // The burst's presents go in too (about two a frame at most), within the trace.
    const std::uint64_t after = std::min<std::uint64_t>(kTrace / 2, 16 + 2 * std::uint64_t{frames});
    g_askedAt = g_trace[(g_traced - 1) % kTrace].present;
    g_dumpAt = g_askedAt + after;
}

void logWindow(const Counters& now) {
    const std::uint64_t ms = GetTickCount64();
    if (g_markMs == 0) {
        g_markMs = ms;
        g_firstMs = ms;
        return;
    }
    if (ms - g_markMs < kWindowMs) {
        return;
    }
    Counters window;
    for (std::size_t i = 0; i < window.n.size(); ++i) {
        window.n[i] = now.n[i] - g_mark.n[i];
    }
    logCounts("last 10 s", window);
    g_mark = now;
    g_markMs = ms;
}

void logSession(const Counters& now) {
    if (now[Count::Presents] == 0 && now[Count::Arms] == 0) {
        return;
    }
    char span[48];
    std::snprintf(
        span, sizeof(span), "session (%llu s)",
        static_cast<unsigned long long>(g_firstMs == 0 ? 0 : (GetTickCount64() - g_firstMs) / 1000));
    logCounts(span, now);
}

} // namespace evr::vkcore::view_snapshot
