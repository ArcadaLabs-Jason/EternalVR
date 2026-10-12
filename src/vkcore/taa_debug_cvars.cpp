#include "vkcore/taa_debug_cvars.hpp"

#include "stereo_seq/debug_cvars.hpp"
#include "vkcore/log.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace evr::vkcore {

namespace {

constexpr const char* kTag = "seq-taa";

struct Listed {
    std::vector<std::string> names;
    std::unique_ptr<std::atomic<bool>[]> logged; // per name: "left to" was logged
};

const Listed& listed() {
    static const Listed list = [] {
        Listed l;
        std::wstring wide;
        std::string text;
        if (readEnv(L"ETERNALVR_DEBUG_CVARS", wide)) {
            for (const wchar_t c : wide) {
                text.push_back(c < 0x80 ? static_cast<char>(c) : '?'); // cvar names and values are ASCII
            }
        }
        l.names = stereo_seq::debugCvarNames(text);
        l.logged = std::make_unique<std::atomic<bool>[]>(l.names.size());
        return l;
    }();
    return list;
}

} // namespace

void logTaaDebugCvars() {
    const Listed& l = listed();
    if (l.names.empty()) {
        return;
    }
    std::string names;
    for (const std::string& n : l.names) {
        names += (names.empty() ? "" : ", ") + n;
    }
    EVR_LOG("%s: ETERNALVR_DEBUG_CVARS holds %s; per-eye TAA does not write them", kTag, names.c_str());
}

bool taaLeftToDebugCvars(std::string_view name) {
    const Listed& l = listed();
    const std::optional<std::size_t> i = stereo_seq::findCvarName(l.names, name);
    if (!i) {
        return false;
    }
    if (!l.logged[*i].exchange(true)) {
        EVR_LOG("%s: %s left to ETERNALVR_DEBUG_CVARS", kTag, l.names[*i].c_str());
    }
    return true;
}

bool taaSsrLeftToDebugCvars() {
    if (!stereo_seq::debugCvarsTakeSsr(listed().names)) {
        return false;
    }
    for (const std::string_view name : {"r_SSR", "r_raytracedReflectionsTemporalUpscaleQuality"}) {
        taaLeftToDebugCvars(name);
    }
    return true;
}

} // namespace evr::vkcore
