#include "vkcore/debug_commands.hpp"

#include "vkcore/debug_script.hpp"
#include "vkcore/game_text.hpp"
#include "vkcore/log.hpp"
#include "vkcore/mid_hook.hpp"
#include "vkcore/mp_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "debug-commands";
constexpr const char* kCmdSystemClass = ".?AVidCmdSystemLocal@@";

// idCmdSystemLocal's vtable (docs/research/03-doom-eternal-internals.md section 3.3).
constexpr std::size_t kGetRestriction = 0x08;
constexpr std::size_t kSetRestriction = 0x10;
constexpr std::size_t kExecuteCommandText = 0x40;
constexpr std::size_t kExecuteCommandBuffer = 0x58;

// How far after the lea of "cmdSystem->ExecuteCommandText" the engine loads the global (RVA 0x431D68 ->
// 0x431D78: a call in between).
constexpr std::size_t kGlobalLoadWindow = 0x30;

using GetRestrictionFn = int (*)(void* self);
using SetRestrictionFn = void (*)(void* self, int level);
using ExecuteCommandTextFn = void (*)(void* self, const char* text);

std::once_flag g_once;
std::atomic<bool> g_installed{false};
DebugScript g_script;
std::mutex g_stepMutex;                 // one thread runs due steps at a time
std::size_t g_nextStep = 0;             // under g_stepMutex
std::atomic<double> g_playerInMap{0.0}; // nowSeconds() at markPlayerInMap(), 0 before
GetRestrictionFn g_getRestriction = nullptr;
SetRestrictionFn g_setRestriction = nullptr;
ExecuteCommandTextFn g_execute = nullptr;

// Monotonic: a clock change does not move the schedule.
double nowSeconds() {
    return static_cast<double>(GetTickCount64()) / 1000.0;
}

const std::byte* readPointer(const std::byte* at) {
    const std::byte* value = nullptr;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// At the start of ExecuteCommandBuffer: rcx is the command system.
void onExecuteBuffer(const HookRegisters& r) {
    if (!mp_guard::allowsGameTouch()) {
        return;
    }
    const std::unique_lock lock(g_stepMutex, std::try_to_lock);
    if (!lock.owns_lock() || g_nextStep >= g_script.steps.size()) {
        return;
    }
    const double start = g_playerInMap.load(std::memory_order_acquire);
    if (start == 0.0) {
        return;
    }
    const double elapsed = nowSeconds() - start;
    auto* self = reinterpret_cast<void*>(r.rcx);
    while (g_nextStep < g_script.steps.size() && g_script.steps[g_nextStep].seconds <= elapsed) {
        const DebugStep& step = g_script.steps[g_nextStep++];
        const int restriction = g_getRestriction(self);
        g_setRestriction(self, 0);
        for (const std::string& command : step.commands) {
            // A command can trip the guard (the policy screens the script, the guard has the last word):
            // nothing more runs once it has.
            if (!mp_guard::allowsGameTouch()) {
                EVR_LOG("%s: the multiplayer guard tripped; the rest of the schedule is dropped", kTag);
                g_nextStep = g_script.steps.size();
                break;
            }
            EVR_LOG("%s: %.1f s: %s", kTag, elapsed, command.c_str());
            g_execute(self, command.c_str());
        }
        g_setRestriction(self, restriction);
    }
}

std::string narrow(const std::wstring& text) {
    std::string out;
    for (const wchar_t c : text) {
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

// The command system's object: the global the engine loads right after naming
// "cmdSystem->ExecuteCommandText".
const std::byte* findCommandSystem(const GameImage& image) {
    const std::byte* name = findUniqueString(image, "cmdSystem->ExecuteCommandText");
    if (!name) {
        EVR_LOG("%s: the \"cmdSystem->ExecuteCommandText\" string is missing", kTag);
        return nullptr;
    }
    for (const std::byte* lea : findLeaReferences(image, name)) {
        for (const std::byte* at = lea + 7; at + 7 <= lea + 7 + kGlobalLoadWindow; ++at) {
            // mov rcx, [rip + disp32]
            if (at[0] != std::byte{0x48} || at[1] != std::byte{0x8B} || at[2] != std::byte{0x0D}) {
                continue;
            }
            const std::byte* global = ripTarget(image, at + 3, at + 7);
            if (!global) {
                continue;
            }
            const std::byte* object = readPointer(global);
            if (object && image.contains(readPointer(object), kExecuteCommandBuffer + sizeof(void*)) &&
                rttiName(image, readPointer(object)) == kCmdSystemClass) {
                return object;
            }
        }
    }
    EVR_LOG("%s: no global near the \"cmdSystem->ExecuteCommandText\" reference holds an %s", kTag,
            kCmdSystemClass);
    return nullptr;
}

} // namespace

bool installDebugCommands() {
    std::call_once(g_once, [] {
        std::wstring value;
        if (!readEnv(L"ETERNALVR_DEBUG_COMMANDS", value) || value.empty()) {
            return;
        }
        g_script = parseDebugScript(narrow(value));
        if (!g_script.error.empty()) {
            EVR_LOG("%s: ETERNALVR_DEBUG_COMMANDS not used: %s", kTag, g_script.error.c_str());
            return;
        }
        for (const std::string& refused : g_script.refused) {
            EVR_LOG("%s: left out of the schedule, %s", kTag, refused.c_str());
        }
        if (g_script.steps.empty()) {
            return;
        }
        if (!mp_guard::allowsGameTouch()) {
            EVR_LOG("%s: the multiplayer guard is not armed; not installed", kTag);
            return;
        }
        GameImage image;
        if (!locateGameImage(image, kTag)) {
            return;
        }
        const std::byte* system = findCommandSystem(image);
        if (!system) {
            return;
        }
        const std::byte* vtable = readPointer(system);
        const std::byte* get = readPointer(vtable + kGetRestriction);
        const std::byte* set = readPointer(vtable + kSetRestriction);
        const std::byte* execute = readPointer(vtable + kExecuteCommandText);
        const std::byte* runBuffer = readPointer(vtable + kExecuteCommandBuffer);
        if (!image.inText(get) || !image.inText(set) || !image.inText(execute) || !image.inText(runBuffer) ||
            functionStart(image, runBuffer) != runBuffer) {
            EVR_LOG("%s: the command system's vtable does not check out; not installed", kTag);
            return;
        }
        g_getRestriction = reinterpret_cast<GetRestrictionFn>(const_cast<std::byte*>(get));
        g_setRestriction = reinterpret_cast<SetRestrictionFn>(const_cast<std::byte*>(set));
        g_execute = reinterpret_cast<ExecuteCommandTextFn>(const_cast<std::byte*>(execute));
        std::string error;
        if (!installMidHook(const_cast<std::byte*>(runBuffer), &onExecuteBuffer, error)) {
            EVR_LOG("%s: hook failed: %s", kTag, error.c_str());
            return;
        }
        g_installed.store(true, std::memory_order_release);
        EVR_LOG(
            "%s: %zu step(s) scheduled; ExecuteCommandBuffer hooked (RVA 0x%X), first at %.1f s after the "
            "player is in the map",
            kTag, g_script.steps.size(), image.rva(runBuffer), g_script.steps.front().seconds);
    });
    return g_installed.load(std::memory_order_acquire);
}

double secondsInMap() {
    const double since = g_playerInMap.load(std::memory_order_acquire);
    return since > 0.0 ? nowSeconds() - since : -1.0;
}

void markPlayerInMap() {
    double expected = 0.0;
    if (g_playerInMap.compare_exchange_strong(expected, nowSeconds(), std::memory_order_acq_rel) &&
        g_installed.load(std::memory_order_acquire)) {
        EVR_LOG("%s: the player is in the map; the schedule's clock starts", kTag);
    }
}

} // namespace evr::vkcore
