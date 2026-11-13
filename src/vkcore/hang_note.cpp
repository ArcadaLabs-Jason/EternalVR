// The hang note (hang_note.hpp): the watch's notes, the module list and the threads' places.

#include "vkcore/hang_note.hpp"

#include "gpu_timing/present_hang.hpp"
#include "gpu_timing/present_stall.hpp"
#include "vkcore/debug_commands.hpp"
#include "vkcore/log.hpp"
#include "vkcore/stall_watch.hpp"
#include "vkcore/window_timing.hpp"

#include <windows.h>

#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace evr::vkcore::hang_note {

namespace {

namespace gt = ::evr::gpu_timing;

constexpr std::size_t kMaxModules = 512;
constexpr std::size_t kMaxThreads = 192;
constexpr std::size_t kStackWords = 2048; // 16 KiB of each thread's stack
constexpr std::size_t kStackPlaces = 8;
constexpr std::uint64_t kModuleRefreshMicros = 60ull * 1000 * 1000;
constexpr std::uint64_t kHealthyMicros = 1000ull * 1000; // a present this recent: the game is presenting
constexpr std::uint32_t kTestHangPresentsInPlay = 2000;

struct Module {
    std::uint64_t base = 0;
    std::uint64_t end = 0;
    bool system = false; // ntdll, kernelbase, kernel32: not counted as stack places
    char name[64] = {};
};

struct Place {
    DWORD id = 0;
    std::uint64_t start = 0;
    std::uint64_t at = 0;
    std::array<std::uint64_t, kStackPlaces> stack{};
    std::size_t found = 0;
};

using QueryThread = LONG(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
constexpr int kThreadQuerySetWin32StartAddress = 9;

// The XR worker's own state: only check() touches it.
struct State {
    bool installed = false;
    gt::HangWatch watch;
    std::vector<Module> modules;  // sorted by base
    std::uint64_t modulesAt = 0;  // nowMicros of the last refresh
    std::vector<DWORD> threadIds; // reserved once
    std::vector<Place> places;    // reserved once
    QueryThread queryThread = nullptr;
};
State& g_state = *new State;

std::atomic<std::uint32_t> g_testHangMs{0};
std::atomic<double> g_testHangAtSeconds{-1.0}; // in the map; negative: after kTestHangPresentsInPlay
std::atomic<std::uint32_t> g_testPresentsInPlay{0};
std::atomic<bool> g_testHangDone{false};

bool systemModule(const wchar_t* name) {
    return _wcsicmp(name, L"ntdll.dll") == 0 || _wcsicmp(name, L"kernelbase.dll") == 0 ||
           _wcsicmp(name, L"kernel32.dll") == 0;
}

// The loaded modules, while the game presents (the loader's lock is taken here, never during a note).
void refreshModules() {
    std::array<HMODULE, kMaxModules> handles{};
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), handles.data(), static_cast<DWORD>(sizeof(handles)),
                               &needed)) {
        return;
    }
    const std::size_t count = std::min<std::size_t>(needed / sizeof(HMODULE), kMaxModules);
    std::vector<Module> modules;
    modules.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        MODULEINFO info{};
        if (!K32GetModuleInformation(GetCurrentProcess(), handles[i], &info, sizeof(info))) {
            continue;
        }
        wchar_t path[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameW(handles[i], path, MAX_PATH);
        const wchar_t* name = path;
        for (DWORD c = 0; c < n; ++c) {
            if (path[c] == L'\\' || path[c] == L'/') {
                name = path + c + 1;
            }
        }
        Module m;
        m.base = reinterpret_cast<std::uint64_t>(info.lpBaseOfDll);
        m.end = m.base + info.SizeOfImage;
        m.system = systemModule(name);
        std::snprintf(m.name, sizeof(m.name), "%ls", name);
        modules.push_back(m);
    }
    std::sort(modules.begin(), modules.end(),
              [](const Module& a, const Module& b) { return a.base < b.base; });
    g_state.modules = std::move(modules);
}

// The cached module an address lies in, or nullptr (no lock, no allocation: safe with a thread suspended).
const Module* moduleOf(std::uint64_t address) {
    const auto& modules = g_state.modules;
    auto it = std::upper_bound(modules.begin(), modules.end(), address,
                               [](std::uint64_t a, const Module& m) { return a < m.base; });
    if (it == modules.begin()) {
        return nullptr;
    }
    --it;
    return address < it->end ? &*it : nullptr;
}

void describe(std::uint64_t address, char* out, std::size_t size) {
    if (const Module* m = moduleOf(address)) {
        std::snprintf(out, size, "%s+0x%llX", m->name, static_cast<unsigned long long>(address - m->base));
        return;
    }
    std::snprintf(out, size, "0x%llX", static_cast<unsigned long long>(address));
}

bool readWord(const std::uint64_t* at, std::uint64_t& value) {
    __try {
        value = *at;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void install() {
    g_state.installed = true;
    g_state.threadIds.reserve(kMaxThreads);
    g_state.places.reserve(kMaxThreads);
    if (const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        g_state.queryThread =
            reinterpret_cast<QueryThread>(GetProcAddress(ntdll, "NtQueryInformationThread"));
    }
    std::wstring value;
    if (readEnv(L"ETERNALVR_TEST_HANG", value)) {
        if (const std::optional<gt::TestHang> hang = gt::parseTestHang(value)) {
            g_testHangAtSeconds.store(hang->atSecondsInMap, std::memory_order_relaxed);
            g_testHangMs.store(hang->ms, std::memory_order_relaxed);
            if (hang->atSecondsInMap >= 0.0) {
                EVR_LOG(
                    "hang note: ETERNALVR_TEST_HANG: one present in play is held %u ms, %.1f s into the map",
                    hang->ms, hang->atSecondsInMap);
            } else {
                EVR_LOG("hang note: ETERNALVR_TEST_HANG: one present in play is held %u ms", hang->ms);
            }
        } else {
            EVR_LOG(
                "hang note: ETERNALVR_TEST_HANG='%ls' is not <seconds> or <milliseconds>ms[@<seconds>]; no "
                "present is held",
                value.c_str());
        }
    }
    EVR_LOG("hang note: no game present for %.0f s in play gets the threads' places (again at %.0f s, for at "
            "most %u gaps)",
            gt::kHangFirstNoteMs / 1000.0, gt::kHangSecondNoteMs / 1000.0, gt::kHangsNoted);
}

// One thread's place: suspended only for the context and the stack copy.
bool capture(DWORD id, Place& place) {
    const HANDLE thread =
        OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, id);
    if (!thread) {
        return false;
    }
    place = Place{};
    place.id = id;
    if (g_state.queryThread) {
        PVOID start = nullptr;
        if (g_state.queryThread(thread, kThreadQuerySetWin32StartAddress, &start, sizeof(start), nullptr) >=
            0) {
            place.start = reinterpret_cast<std::uint64_t>(start);
        }
    }
    bool ok = false;
    if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
        alignas(16) CONTEXT context{};
        context.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(thread, &context)) {
            ok = true;
            place.at = context.Rip;
            const auto* sp = reinterpret_cast<const std::uint64_t*>(context.Rsp);
            for (std::size_t i = 0; i < kStackWords && place.found < kStackPlaces; ++i) {
                std::uint64_t word = 0;
                if (!readWord(sp + i, word)) {
                    break;
                }
                const Module* m = moduleOf(word);
                if (m && !m->system) {
                    place.stack[place.found++] = word;
                }
            }
        }
        ResumeThread(thread);
    }
    CloseHandle(thread);
    return ok;
}

void note(const gt::HangResult& r, bool second) {
    // The thread ids first (a snapshot of the system's threads; no thread of this process is held).
    g_state.threadIds.clear();
    const DWORD self = GetCurrentThreadId();
    const DWORD process = GetCurrentProcessId();
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Thread32First(snapshot, &entry); more && g_state.threadIds.size() < kMaxThreads;
             more = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID == process && entry.th32ThreadID != self) {
                g_state.threadIds.push_back(entry.th32ThreadID);
            }
        }
        CloseHandle(snapshot);
    }
    g_state.places.clear();
    std::size_t unreadable = 0;
    for (const DWORD id : g_state.threadIds) {
        Place place;
        if (capture(id, place)) {
            g_state.places.push_back(place);
        } else {
            ++unreadable;
        }
    }
    char tick[48] = "no game tick yet";
    if (r.tickAgeMs >= 0.0) {
        std::snprintf(tick, sizeof(tick), "the last game tick %.1f s ago", r.tickAgeMs / 1000.0);
    }
    EVR_LOG("hang: no game present for %.1f s in play (%s; hang %u, note %d of 2): %zu thread(s), where each "
            "stands and the first return addresses on its stack in the game, the layer or a driver",
            r.gapMs / 1000.0, tick, r.hang, second ? 2 : 1, g_state.places.size());
    for (const Place& p : g_state.places) {
        char start[96];
        char at[96];
        describe(p.start, start, sizeof(start));
        describe(p.at, at, sizeof(at));
        char stack[900] = "";
        std::size_t used = 0;
        for (std::size_t i = 0; i < p.found; ++i) {
            char one[96];
            describe(p.stack[i], one, sizeof(one));
            const int n = std::snprintf(stack + used, sizeof(stack) - used, "%s%s", i ? ", " : "", one);
            if (n < 0 || used + static_cast<std::size_t>(n) >= sizeof(stack)) {
                break;
            }
            used += static_cast<std::size_t>(n);
        }
        EVR_LOG("hang:   thread %lu (start %s): at %s; stack %s", static_cast<unsigned long>(p.id), start, at,
                p.found ? stack : "-");
    }
    if (unreadable) {
        EVR_LOG("hang: %zu thread(s) could not be read (exited, or not opened)", unreadable);
    }
}

} // namespace

void check() {
    if (!g_state.installed) {
        install();
    }
    const std::uint64_t now = window_timing::nowMicros();
    const std::uint64_t lastPresent = stall_watch::lastPresentMicros();
    if (lastPresent != 0 && now >= lastPresent && now - lastPresent < kHealthyMicros &&
        (g_state.modulesAt == 0 || now - g_state.modulesAt >= kModuleRefreshMicros)) {
        refreshModules();
        g_state.modulesAt = now;
    }
    gt::HangCheck c;
    c.nowMicros = now;
    c.lastPresentMicros = lastPresent;
    c.lastTickMicros = stall_watch::lastGameTickMicros();
    c.ticksKnown = stall_watch::ticksJudged();
    const gt::HangResult r = g_state.watch.onCheck(c);
    switch (r.action) {
    case gt::HangAction::FirstNote:
        note(r, false);
        break;
    case gt::HangAction::SecondNote:
        note(r, true);
        break;
    case gt::HangAction::Resumed:
        EVR_LOG("hang: the game presented again after %.1f s (hang %u)", r.gapMs / 1000.0, r.hang);
        break;
    case gt::HangAction::None:
        break;
    }
}

void testHangAtPresent() {
    const std::uint32_t holdMs = g_testHangMs.load(std::memory_order_relaxed);
    if (holdMs == 0 || g_testHangDone.load(std::memory_order_relaxed)) {
        return;
    }
    const std::uint64_t tick = stall_watch::lastGameTickMicros();
    const std::uint64_t now = window_timing::nowMicros();
    if (tick == 0 || now < tick || static_cast<double>(now - tick) / 1000.0 > gt::kInPlayTickMs) {
        return; // not in play
    }
    const double atSeconds = g_testHangAtSeconds.load(std::memory_order_relaxed);
    const bool due = atSeconds >= 0.0 ? secondsInMap() >= atSeconds
                                      : g_testPresentsInPlay.fetch_add(1, std::memory_order_relaxed) + 1 >=
                                            kTestHangPresentsInPlay;
    if (!due || g_testHangDone.exchange(true)) {
        return;
    }
    EVR_LOG("hang note: ETERNALVR_TEST_HANG: holding this present %u ms", holdMs);
    Sleep(holdMs);
}

} // namespace evr::vkcore::hang_note
