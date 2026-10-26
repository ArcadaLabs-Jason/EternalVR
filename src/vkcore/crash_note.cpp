#include "vkcore/crash_note.hpp"

#include "vkcore/crash_note_seen.hpp"
#include "vkcore/log.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

namespace evr::vkcore {

namespace {

constexpr std::size_t kStackWords = 512; // 4 KiB of the faulting thread's stack
constexpr int kStackPlaces = 6;

std::atomic_flag g_busy = ATOMIC_FLAG_INIT; // one note at a time; a fault inside a note is not noted
std::atomic<bool> g_installed{false};
CrashSeen g_seen;
LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;

HMODULE moduleOf(std::uint64_t address) {
    HMODULE module = nullptr;
    if (address == 0 || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                            reinterpret_cast<LPCWSTR>(address), &module)) {
        return nullptr;
    }
    return module;
}

// "name.dll+0x1234" for an address in a module, "0x..." otherwise.
void describe(std::uint64_t address, char* out, std::size_t size) {
    if (const HMODULE module = moduleOf(address)) {
        wchar_t path[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameW(module, path, MAX_PATH);
        const wchar_t* name = path;
        for (DWORD i = 0; i < n; ++i) {
            if (path[i] == L'\\' || path[i] == L'/') {
                name = path + i + 1;
            }
        }
        std::snprintf(out, size, "%ls+0x%llX", name,
                      static_cast<unsigned long long>(address - reinterpret_cast<std::uint64_t>(module)));
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

const char* codeName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "privileged instruction";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer divide by zero";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "array bounds exceeded";
    case EXCEPTION_IN_PAGE_ERROR:
        return "in-page error";
    case 0xC0000374u:
        return "heap corruption";
    case 0xC0000409u:
        return "stack buffer overrun";
    default:
        return "exception";
    }
}

// One line for the exception: what, where, the access violation's address, and the first return addresses
// on the stack that lie in the layer or the game's exe (a rough call chain: words on the stack that point
// into code, not an unwound one).
void note(const EXCEPTION_POINTERS* info, const char* kind, std::uint64_t repeats) {
    const EXCEPTION_RECORD* r = info->ExceptionRecord;
    const std::uint64_t at = reinterpret_cast<std::uint64_t>(r->ExceptionAddress);
    char where[160];
    describe(at, where, sizeof(where));
    char access[96] = "";
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        const ULONG_PTR op = r->ExceptionInformation[0];
        std::snprintf(access, sizeof(access), ", %s 0x%llX",
                      op == 0   ? "read of"
                      : op == 1 ? "write to"
                                : "execute at",
                      static_cast<unsigned long long>(r->ExceptionInformation[1]));
    }
    const HMODULE self = moduleOf(reinterpret_cast<std::uint64_t>(&installCrashNote));
    const HMODULE game = GetModuleHandleW(nullptr);
    char stack[700] = "";
    std::size_t used = 0;
    int found = 0;
    const auto* sp = reinterpret_cast<const std::uint64_t*>(info->ContextRecord->Rsp);
    for (std::size_t i = 0; i < kStackWords && found < kStackPlaces; ++i) {
        std::uint64_t word = 0;
        if (!readWord(sp + i, word)) {
            break;
        }
        const HMODULE module = moduleOf(word);
        if (module == nullptr || (module != self && module != game)) {
            continue;
        }
        char one[160];
        describe(word, one, sizeof(one));
        const int n = std::snprintf(stack + used, sizeof(stack) - used, "%s%s", found ? ", " : "", one);
        if (n < 0 || used + static_cast<std::size_t>(n) >= sizeof(stack)) {
            break;
        }
        used += static_cast<std::size_t>(n);
        ++found;
    }
    char line[1200];
    std::snprintf(
        line, sizeof(line),
        "crash note: %s 0x%08lX (%s%s) at %s; stack: %s; %llu not logged since the last line, %llu in "
        "total",
        kind, static_cast<unsigned long>(r->ExceptionCode), codeName(r->ExceptionCode), access, where,
        found ? stack : "nothing in the layer or the game", static_cast<unsigned long long>(repeats),
        static_cast<unsigned long long>(g_seen.total()));
    logCrashLine(line);
}

LONG CALLBACK firstChance(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord || !crashClassCode(info->ExceptionRecord->ExceptionCode)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (g_busy.test_and_set(std::memory_order_acquire)) {
        return EXCEPTION_CONTINUE_SEARCH; // a fault while a note is written (its own guarded stack reads)
    }
    std::uint64_t repeats = 0;
    if (g_seen.note(reinterpret_cast<std::uint64_t>(info->ExceptionRecord->ExceptionAddress), repeats)) {
        note(info, "first-chance exception (it may still be handled)", repeats);
    }
    g_busy.clear(std::memory_order_release);
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI testCrash(LPVOID seconds) {
    Sleep(static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(seconds)) * 1000);
    EVR_LOG("crash note: ETERNALVR_TEST_CRASH: raising an access violation now");
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    return 0;
}

LONG WINAPI unhandled(EXCEPTION_POINTERS* info) {
    if (info && info->ExceptionRecord && !g_busy.test_and_set(std::memory_order_acquire)) {
        note(info, "UNHANDLED exception (the game ends here unless its own handler carries on)", 0);
        g_busy.clear(std::memory_order_release);
    }
    return g_previous ? g_previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void installCrashNote() {
    if (g_installed.exchange(true)) {
        return;
    }
    const bool vectored = AddVectoredExceptionHandler(1, &firstChance) != nullptr;
    g_previous = SetUnhandledExceptionFilter(&unhandled);
    // Worded without 'crash' or 'exception': the rig QA reads those words in a log as a run that went wrong.
    EVR_LOG("fault note: %s; an unhandled one is noted while this filter is the process's%s",
            vectored ? "faults a process dies of are noted when they happen (first-chance)"
                     : "the first-chance handler could not be added",
            g_previous ? " (the one before it chained)" : "");
    std::wstring value;
    if (readEnv(L"ETERNALVR_TEST_CRASH", value)) {
        const unsigned long seconds = std::wcstoul(value.c_str(), nullptr, 10);
        EVR_LOG("crash note: ETERNALVR_TEST_CRASH: a deliberate crash in %lu s", seconds);
        if (const HANDLE thread =
                CreateThread(nullptr, 0, &testCrash,
                             reinterpret_cast<LPVOID>(static_cast<std::uintptr_t>(seconds)), 0, nullptr)) {
            CloseHandle(thread);
        }
    }
}

} // namespace evr::vkcore
