#include "vkcore/parallel_eyes_settings.hpp"

#include "stereo_seq/adaptive_eyes.hpp"
#include "stereo_seq/stereo_taa.hpp"

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <iterator>

namespace evr::vkcore::parallel_eyes {

namespace {

struct PartName {
    std::wstring_view name;
    ViewPart part;
};
constexpr PartName kParts[] = {{L"edges", kEdges},     {L"dc", kDcCopy},       {L"binds", kBinds},
                               {L"pool", kPool},       {L"shadows", kShadows}, {L"env", kEnv},
                               {L"volumes", kVolumes}, {L"screen", kScreen}};

std::wstring trim(std::wstring_view text) {
    std::size_t from = 0;
    std::size_t to = text.size();
    while (from < to && std::iswspace(text[from])) {
        ++from;
    }
    while (to > from && std::iswspace(text[to - 1])) {
        --to;
    }
    return std::wstring(text.substr(from, to - from));
}

bool equalsNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) {
            return false;
        }
    }
    return true;
}

std::string narrow(std::wstring_view text) {
    std::string out;
    for (const wchar_t c : text) {
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

std::optional<std::wstring> value(const EnvLookup& env, std::wstring_view name) {
    std::optional<std::wstring> v = env(name);
    if (v) {
        *v = trim(*v);
    }
    return v;
}

void readOff(const std::wstring& list, Settings& s) {
    std::size_t at = 0;
    while (at <= list.size()) {
        const std::size_t comma = std::min(list.find(L',', at), list.size());
        const std::wstring item = trim(std::wstring_view(list).substr(at, comma - at));
        at = comma + 1;
        if (item.empty()) {
            continue;
        }
        bool known = false;
        for (const PartName& p : kParts) {
            if (equalsNoCase(item, p.name)) {
                s.off |= p.part;
                known = true;
            }
        }
        if (!known) {
            s.warnings.push_back("ETERNALVR_TEST_VIEW_OFF: '" + narrow(item) + "' is not a part; ignored");
        }
    }
}

} // namespace

Settings readSettings(const EnvLookup& env, const std::function<bool()>& uiLayer) {
    Settings s;
    const std::optional<std::wstring> on = value(env, L"ETERNALVR_PARALLEL_EYES");
    if (!on || *on != L"1") {
        return s; // unset, empty or anything else: off, nothing to say
    }
    // Read as the rest of the layer reads them (not trimmed): stereo_hooks.cpp's stereoExperimentFromEnv,
    // readStereoSettings and taa_env.cpp then agree with the choice made here.
    const std::optional<std::wstring> mode = env(L"ETERNALVR_MODE");
    const std::optional<std::wstring> experiment = env(L"ETERNALVR_STEREO_EXPERIMENT");
    // As taa_env.cpp reads them.
    const std::string dlss = narrow(env(L"ETERNALVR_STEREO_DLSS").value_or(L""));
    s.antiAliasingOff = !stereo_seq::switchValue(narrow(env(L"ETERNALVR_STEREO_TAA").value_or(L"")), true);
    // As runtime_cvars.cpp reads it for Route S's stereo set: exactly "0".
    s.antiAliasingHeld = env(L"ETERNALVR_STEREO_RUNTIME_CVARS").value_or(L"") != L"0";
    s.alternateEyes = stereo_seq::alternateMode(narrow(env(L"ETERNALVR_ALTERNATE_EYES").value_or(L""))) !=
                      stereo_seq::AlternateMode::Off;
    if (!mode || !equalsNoCase(*mode, L"stereo")) {
        s.why = "stereo only (ETERNALVR_MODE is not stereo)";
    } else if (experiment && !experiment->empty()) {
        s.why = "ETERNALVR_STEREO_EXPERIMENT is set";
    } else if (stereo_seq::switchValue(dlss, false)) {
        s.why = "not with DLSS (ETERNALVR_STEREO_DLSS=1)";
    } else if (!uiLayer()) {
        s.why =
            "the UI layer is off (ETERNALVR_UI_LAYER=0); its image hooks give each eye its view's picture";
    } else {
        s.requested = true;
    }
    if (const auto clones = value(env, L"ETERNALVR_TEST_VIEW_CLONES")) {
        if (*clones == L"0") {
            s.clones = false;
        } else if (*clones != L"1") {
            s.warnings.push_back("ETERNALVR_TEST_VIEW_CLONES is not 0 or 1; ignored");
        }
    }
    if (const auto copy = value(env, L"ETERNALVR_TEST_EYE_COPY")) {
        if (*copy == L"0") {
            s.eyeCopy = EyeCopy::Off;
        } else if (*copy == L"1") {
            s.eyeCopy = EyeCopy::Final;
        } else {
            s.warnings.push_back("ETERNALVR_TEST_EYE_COPY is not 0 or 1; ignored");
        }
    }
    if (!s.clones && s.eyeCopy != EyeCopy::Off) {
        // Eye 1's image is view 1's clone: without the clones both eyes take the presented image.
        s.eyeCopy = EyeCopy::Off;
    }
    if (const auto off = value(env, L"ETERNALVR_TEST_VIEW_OFF")) {
        readOff(*off, s);
    }
    if (const auto only = value(env, L"ETERNALVR_TEST_VIEW_ONLY")) {
        if (*only == L"0" || *only == L"1") {
            s.viewOnly = (*only)[0] - L'0';
        } else {
            s.warnings.push_back("ETERNALVR_TEST_VIEW_ONLY is not 0 or 1; ignored");
        }
    }
    if (const auto fail = value(env, L"ETERNALVR_TEST_INSTALL_FAIL")) {
        if (equalsNoCase(*fail, L"check")) {
            s.testFail = TestFail::Check;
        } else if (equalsNoCase(*fail, L"redirects")) {
            s.testFail = TestFail::Redirects;
        } else if (equalsNoCase(*fail, L"hook")) {
            s.testFail = TestFail::Hook;
        } else {
            s.warnings.push_back("ETERNALVR_TEST_INSTALL_FAIL is not check, redirects or hook; ignored");
        }
    }
    return s;
}

std::vector<stereo_seq::CvarExpectation> antiAliasingCvars(bool off) {
    if (off) {
        return stereo_seq::stereoRuntimeCvars(stereo_seq::StereoTemporal::Off);
    }
    return {{"r_antialiasing", "1"}};
}

std::string partsText(std::uint32_t off) {
    std::string out;
    for (const PartName& p : kParts) {
        if (off & p.part) {
            out += (out.empty() ? "" : ",") + narrow(p.name);
        }
    }
    return out.empty() ? "none" : out;
}

} // namespace evr::vkcore::parallel_eyes
