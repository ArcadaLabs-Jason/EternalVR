// The layer's state for the launcher (status_file.hpp).

#include "vkcore/status_file.hpp"

#include "vkcore/log.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifndef EVR_LAYER_VERSION
#define EVR_LAYER_VERSION "unknown"
#endif

namespace evr::vkcore::status {

namespace {

std::mutex g_mutex;
std::string g_state;
std::string g_reason;
std::string g_stereo;                                      // "on", "off: <reason>", or empty until decided
std::vector<std::pair<std::string, std::string>> g_fields; // the headset keys, in the order first set

std::string oneLine(std::string text) {
    for (char& c : text) {
        if (c == '\r' || c == '\n') {
            c = ' ';
        }
    }
    return text;
}

// Rewrites the file; the caller holds g_mutex.
void save() {
    const std::wstring dir = logDirectory();
    if (dir.empty()) {
        return;
    }
    const std::wstring path = dir + L"\\eternalvr-status.txt";
    const std::wstring temp = path + L".tmp";
    std::FILE* f = _wfsopen(temp.c_str(), L"w", _SH_DENYWR);
    if (!f) {
        return;
    }
    std::fprintf(f, "state=%s\nreason=%s\nstereo=%s\nversion=%s\npid=%lu\n", g_state.c_str(),
                 g_reason.c_str(), g_stereo.c_str(), EVR_LAYER_VERSION, GetCurrentProcessId());
    for (const auto& [key, value] : g_fields) {
        std::fprintf(f, "%s=%s\n", key.c_str(), value.c_str());
    }
    std::fclose(f);
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void write(const char* state, const char* reason) {
    const std::string text = oneLine(reason ? reason : "");
    std::lock_guard lock(g_mutex);
    if (g_state == state && g_reason == text) {
        return;
    }
    // Once flat, a later "waiting" or "starting" does not hide why VR went off.
    if (g_state == "flat" && std::string(state) != "vr") {
        return;
    }
    g_state = state;
    g_reason = text;
    save();
    EVR_LOG("status: %s%s%s", state, text.empty() ? "" : ": ", text.c_str());
}

} // namespace

void starting() {
    write("starting", "the mod is loaded; waiting for the game's graphics device");
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
        return;
    }
    g_stereo = value;
    save();
    EVR_LOG("status: stereo %s", value.c_str());
}

void field(const char* key, const std::string& value) {
    const std::string text = oneLine(value);
    std::lock_guard lock(g_mutex);
    auto it = std::find_if(g_fields.begin(), g_fields.end(), [key](const auto& f) { return f.first == key; });
    if (it == g_fields.end()) {
        g_fields.emplace_back(key, text);
    } else if (it->second == text) {
        return;
    } else {
        it->second = text;
    }
    save();
}

} // namespace evr::vkcore::status
