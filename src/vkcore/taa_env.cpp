// The environment switches of per-eye TAA (taa_hooks.hpp).

#include "stereo_seq/stereo_taa.hpp"
#include "vkcore/log.hpp"
#include "vkcore/taa_hooks.hpp"

#include <cwchar>
#include <string>

namespace evr::vkcore {

namespace {

std::string narrow(const std::wstring& text) {
    std::string out;
    for (const wchar_t c : text) {
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

} // namespace

bool taaRequested() {
    static const bool requested = [] {
        std::wstring mode;
        std::wstring experiment;
        std::wstring taa;
        readEnv(L"ETERNALVR_STEREO_TAA", taa);
        return readEnv(L"ETERNALVR_MODE", mode) && _wcsicmp(mode.c_str(), L"stereo") == 0 &&
               !(readEnv(L"ETERNALVR_STEREO_EXPERIMENT", experiment) && !experiment.empty()) &&
               stereo_seq::switchValue(narrow(taa), true);
    }();
    return requested;
}

int taaDlssQuality() {
    static const int quality = [] {
        std::wstring value;
        readEnv(L"ETERNALVR_STEREO_DLSS_QUALITY", value);
        return stereo_seq::dlssQualityValue(narrow(value));
    }();
    return quality;
}

bool taaDlssRequested() {
    static const bool requested = [] {
        std::wstring value;
        readEnv(L"ETERNALVR_STEREO_DLSS", value);
        return stereo_seq::switchValue(narrow(value), false);
    }();
    return requested;
}

} // namespace evr::vkcore
