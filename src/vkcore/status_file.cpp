// The layer's state for the launcher (status_file.hpp).

#include "vkcore/status_file.hpp"

#include "vkcore/log.hpp"
#include "vkcore/status_fields.hpp"

#include <windows.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#ifndef EVR_LAYER_VERSION
#define EVR_LAYER_VERSION "unknown"
#endif

namespace evr::vkcore::status {

namespace {

std::mutex g_mutex;
std::string g_state;
std::string g_reason;
std::string g_stereo; // "on", "off: <reason>", or empty until decided
Fields g_fields;      // the headset keys and the start's steps
SaveState g_save;     // whether the last save reached the file
// g_save.dirty() for flush(), read without the lock.
std::atomic<bool> g_behind{false};
FlushGate g_flushGate{1000}; // flush()'s saves while the file is behind: at most one a second

// The launcher reads the file a few times a second; a move while it has it open fails (access denied).
constexpr int kMoveTries = 10;

std::string oneLine(std::string text) {
    for (char& c : text) {
        if (c == '\r' || c == '\n') {
            c = ' ';
        }
    }
    return text;
}

// Rewrites the file, trying the move up to `tries` times (1 ms apart); the caller holds g_mutex.
void save(int tries = kMoveTries) {
    const std::wstring dir = logDirectory();
    if (dir.empty()) {
        return;
    }
    const std::wstring path = dir + L"\\eternalvr-status.txt";
    const std::wstring temp = path + L".tmp";
    bool moved = false;
    std::FILE* f = _wfsopen(temp.c_str(), L"w", _SH_DENYWR);
    const int openError = f ? 0 : errno;
    if (f) {
        std::fprintf(f, "state=%s\nreason=%s\nstereo=%s\nversion=%s\npid=%lu\n", g_state.c_str(),
                     g_reason.c_str(), g_stereo.c_str(), EVR_LAYER_VERSION, GetCurrentProcessId());
        for (const auto& [key, value] : g_fields.items()) {
            std::fprintf(f, "%s=%s\n", key.c_str(), value.c_str());
        }
        std::fclose(f);
        moved = retried(
            tries,
            [&] { return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE; },
            [] { Sleep(1); });
    }
    const DWORD error = GetLastError();
    switch (g_save.saved(moved)) {
    case SaveState::Outcome::Lost:
        g_flushGate.lost();
        if (openError != 0) {
            EVR_LOG("status: the file's temporary copy could not be made (errno %d); tried again each second",
                    openError);
        } else {
            EVR_LOG("status: the file could not be replaced (error %lu); tried again each second", error);
        }
        break;
    case SaveState::Outcome::Recovered:
        EVR_LOG("status: the file is up to date again after a lost write");
        break;
    default:
        break;
    }
    g_behind.store(g_save.dirty(), std::memory_order_relaxed);
}

// Saves when `changed`, or when the last save was lost. The caller holds g_mutex.
void saveIfDue(bool changed) {
    if (g_save.due(changed)) {
        save();
    }
}

void write(const char* state, const char* reason) {
    const std::string text = oneLine(reason ? reason : "");
    std::lock_guard lock(g_mutex);
    // Once flat, a later "waiting" or "starting" does not hide why VR went off.
    if ((g_state == state && g_reason == text) || (g_state == "flat" && std::string(state) != "vr")) {
        saveIfDue(false);
        return;
    }
    g_state = state;
    g_reason = text;
    save();
    EVR_LOG("status: %s%s%s", state, text.empty() ? "" : ": ", text.c_str());
}

} // namespace

void starting(const char* reason) {
    write("starting", reason ? reason : "the mod is loaded; waiting for the game's graphics device");
}

void waiting(const char* reason) {
    write("waiting", reason);
}

void vr(const char* what) {
    write("vr", what);
}

void flat(const char* reason) {
    write("flat", reason);
}

void stereo(bool on, const char* reason) {
    const std::string value = on ? std::string("on") : "off: " + oneLine(reason ? reason : "");
    std::lock_guard lock(g_mutex);
    if (g_stereo == value) {
        saveIfDue(false);
        return;
    }
    g_stereo = value;
    save();
    EVR_LOG("status: stereo %s", value.c_str());
}

void field(const char* key, const std::string& value) {
    const std::string text = oneLine(value);
    std::lock_guard lock(g_mutex);
    saveIfDue(g_fields.set(key, text, false));
}

void reached(const char* key) {
    char seconds[32];
    std::snprintf(seconds, sizeof(seconds), "%.1f", logSeconds());
    std::lock_guard lock(g_mutex);
    saveIfDue(g_fields.set(key, seconds, true));
}

void flush() {
    if (!g_behind.load(std::memory_order_relaxed)) {
        return;
    }
    // Never waits: a save under way is saving the file anyway.
    std::unique_lock lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock() || !g_save.dirty() || !g_flushGate.due(GetTickCount64())) {
        return;
    }
    // One move, no sleep: called from the game's presents and the XR worker's loops. While the writes keep
    // failing (an antivirus holding the file, a read-only folder) that is one temp file written and moved per
    // second on those threads for the rest of the run, only in that failure state.
    save(1);
}

} // namespace evr::vkcore::status
