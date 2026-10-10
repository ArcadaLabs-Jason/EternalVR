#include "vkcore/parallel_eyes_settings.hpp"

#include "stereo_seq/adaptive_eyes.hpp"
#include "stereo_seq/stereo_taa.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
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

struct SkipName {
    std::wstring_view name;
    CloneSkipGroup group;
};
constexpr SkipName kSkipGroups[] = {{L"slot0", kSkipSlot0},
                                    {L"dof", kSkipDof},
                                    {L"gui", kSkipGui},
                                    {L"flares", kSkipFlares},
                                    {L"mblur", kSkipMotionBlur},
                                    {L"refract", kSkipRefraction},
                                    {L"tblock", kSkipTransparencyBlock}};
constexpr std::uint32_t kFieldLimit = 0x1000; // a device context offset is below it, an RVA at or above it

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

// Each comma-separated item of `list`, trimmed; empty ones skipped.
template <typename Fn>
void forEachItem(std::wstring_view list, Fn fn) {
    std::size_t at = 0;
    while (at <= list.size()) {
        const std::size_t comma = std::min(list.find(L',', at), list.size());
        const std::wstring item = trim(list.substr(at, comma - at));
        at = comma + 1;
        if (!item.empty()) {
            fn(item);
        }
    }
}

void readOff(const std::wstring& list, Settings& s) {
    forEachItem(list, [&s](const std::wstring& item) {
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
    });
}

// Hex digits, with or without 0x; at most 8 of them.
std::optional<std::uint32_t> hexNumber(std::wstring_view text) {
    if (text.size() > 2 && text[0] == L'0' && (text[1] == L'x' || text[1] == L'X')) {
        text.remove_prefix(2);
    }
    if (text.empty() || text.size() > 8) {
        return std::nullopt;
    }
    std::uint32_t v = 0;
    for (const wchar_t c : text) {
        const wchar_t l = static_cast<wchar_t>(std::towlower(c));
        const int digit = c >= L'0' && c <= L'9' ? c - L'0' : l >= L'a' && l <= L'f' ? l - L'a' + 10 : -1;
        if (digit < 0) {
            return std::nullopt;
        }
        v = v * 16 + static_cast<std::uint32_t>(digit);
    }
    return v;
}

// Decimal digits only, at most 9 of them.
std::optional<int> wholeNumber(std::wstring_view text) {
    if (text.empty() || text.size() > 9) {
        return std::nullopt;
    }
    int v = 0;
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return std::nullopt;
        }
        v = v * 10 + (c - L'0');
    }
    return v;
}

std::string hexText(const char* prefix, std::uint32_t v) {
    char text[24];
    std::snprintf(text, sizeof(text), "%s0x%X", prefix, v);
    return text;
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
    const std::optional<std::wstring> peDlss = value(env, L"ETERNALVR_PE_DLSS");
    // As runtime_cvars.cpp reads it for Route S's stereo set: exactly "0".
    s.antiAliasingHeld = env(L"ETERNALVR_STEREO_RUNTIME_CVARS").value_or(L"") != L"0";
    s.alternateEyes = stereo_seq::alternateMode(narrow(env(L"ETERNALVR_ALTERNATE_EYES").value_or(L""))) !=
                      stereo_seq::AlternateMode::Off;
    if (!mode || !equalsNoCase(*mode, L"stereo")) {
        s.why = "stereo only (ETERNALVR_MODE is not stereo)";
    } else if (experiment && !experiment->empty()) {
        s.why = "ETERNALVR_STEREO_EXPERIMENT is set";
    } else if (stereo_seq::switchValue(dlss, false) && !s.antiAliasingOff && peDlss && *peDlss == L"0") {
        s.why = "not with DLSS (ETERNALVR_STEREO_DLSS=1 with ETERNALVR_PE_DLSS=0)";
    } else if (stereo_seq::switchValue(dlss, false) && !s.antiAliasingOff && !s.antiAliasingHeld) {
        // Its fallback to TAA is the held r_antialiasing 1.
        s.why = "not with DLSS (ETERNALVR_STEREO_DLSS=1) and ETERNALVR_STEREO_RUNTIME_CVARS=0: nothing would "
                "hold TAA if DLSS fell back";
    } else if (!uiLayer()) {
        s.why =
            "the UI layer is off (ETERNALVR_UI_LAYER=0); its image hooks give each eye its view's picture";
    } else {
        s.requested = true;
        s.dlss = stereo_seq::switchValue(dlss, false) && !s.antiAliasingOff;
    }
    if (s.dlss && peDlss && *peDlss != L"1") {
        s.warnings.push_back("ETERNALVR_PE_DLSS is not 0 or 1; ignored (DLSS in both views)");
    }
    if (const auto clones = value(env, L"ETERNALVR_TEST_VIEW_CLONES")) {
        if (*clones == L"0") {
            s.clones = false;
        } else if (*clones != L"1") {
            s.warnings.push_back("ETERNALVR_TEST_VIEW_CLONES is not 0 or 1; ignored");
        }
    }
    if (const auto log = value(env, L"ETERNALVR_TEST_VIEW_CLONE_LOG")) {
        if (*log == L"0") {
            s.cloneCensus = false;
        } else if (*log != L"1") {
            s.warnings.push_back("ETERNALVR_TEST_VIEW_CLONE_LOG is not 0 or 1; ignored");
        }
    }
    if (const auto skip = value(env, L"ETERNALVR_TEST_VIEW_CLONE_SKIP"); skip && !skip->empty()) {
        s.cloneSkip = readCloneSkip(*skip, s.warnings);
    }
    if (const auto names = value(env, L"ETERNALVR_TEST_VIEW_CLONE_NAMES")) {
        if (equalsNoCase(*names, L"build")) {
            s.cloneBuildNames = true;
        } else {
            s.warnings.push_back("ETERNALVR_TEST_VIEW_CLONE_NAMES is not build; ignored");
        }
    }
    if (const auto rebuild = value(env, L"ETERNALVR_TEST_VIEW_CLONE_REBUILD")) {
        const std::optional<int> seconds = wholeNumber(*rebuild);
        if (seconds && *seconds >= 1 && *seconds <= 3600) {
            s.cloneRebuildSeconds = *seconds;
        } else {
            s.warnings.push_back(
                "ETERNALVR_TEST_VIEW_CLONE_REBUILD is not a number of seconds from 1 to 3600; ignored");
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
    if (const auto pose = value(env, L"ETERNALVR_TEST_PE_POSE")) {
        if (equalsNoCase(*pose, L"latest")) {
            s.latestPose = true;
        } else {
            s.warnings.push_back("ETERNALVR_TEST_PE_POSE is not latest; ignored");
        }
    }
    if (const auto repeats = value(env, L"ETERNALVR_TEST_PE_REPEATS")) {
        if (equalsNoCase(*repeats, L"show")) {
            s.showRepeats = true;
        } else {
            s.warnings.push_back("ETERNALVR_TEST_PE_REPEATS is not show; ignored");
        }
    }
    if (const auto pairing = value(env, L"ETERNALVR_TEST_PE_PAIRING")) {
        if (equalsNoCase(*pairing, L"guess")) {
            s.guessPairs = true;
        } else if (!equalsNoCase(*pairing, L"frame")) {
            s.warnings.push_back("ETERNALVR_TEST_PE_PAIRING is not guess or frame; ignored");
        }
    }
    if (const auto slots = value(env, L"ETERNALVR_TEST_PE_PAIR_SLOTS")) {
        if (slots->size() == 1 && (*slots)[0] >= L'2' && (*slots)[0] <= L'4') {
            s.pairSlots = static_cast<std::uint32_t>((*slots)[0] - L'0');
        } else {
            s.warnings.push_back("ETERNALVR_TEST_PE_PAIR_SLOTS is not 2, 3 or 4; ignored");
        }
    }
    if (const auto lag = value(env, L"ETERNALVR_TEST_PE_EYE1_LAG")) {
        if (*lag == L"1") {
            s.eye1Lag = !s.guessPairs;
            if (s.guessPairs) {
                s.warnings.push_back("ETERNALVR_TEST_PE_EYE1_LAG: not with ETERNALVR_TEST_PE_PAIRING=guess; "
                                     "ignored");
            }
        } else if (*lag != L"0") {
            s.warnings.push_back("ETERNALVR_TEST_PE_EYE1_LAG is not 0 or 1; ignored");
        }
    }
    if (s.eye1Lag) {
        s.pairSlots = std::max<std::uint32_t>(s.pairSlots, 3);
    }
    if (const auto drop = value(env, L"ETERNALVR_TEST_PE_DROP"); drop && *drop != L"0") {
        const std::optional<int> every = wholeNumber(*drop);
        if (!every || *every < 2 || *every > 60) {
            s.warnings.push_back("ETERNALVR_TEST_PE_DROP is not a number from 2 to 60; ignored");
        } else if (s.guessPairs) {
            s.warnings.push_back("ETERNALVR_TEST_PE_DROP: not with ETERNALVR_TEST_PE_PAIRING=guess; ignored");
        } else {
            s.dropEvery = static_cast<std::uint32_t>(*every);
        }
    }
    if (const auto updates = value(env, L"ETERNALVR_TEST_PE_UPDATE_UNION")) {
        if (*updates == L"0") {
            s.updateUnion = false;
        } else if (*updates != L"1") {
            s.warnings.push_back("ETERNALVR_TEST_PE_UPDATE_UNION is not 0 or 1; ignored");
        }
    }
    if (const auto guard = value(env, L"ETERNALVR_TEST_PE_SWAP_GUARD")) {
        if (*guard == L"0") {
            s.swapGuard = false;
        } else if (*guard != L"1") {
            s.warnings.push_back("ETERNALVR_TEST_PE_SWAP_GUARD is not 0 or 1; ignored");
        }
    }
    if (const auto upscale = value(env, L"ETERNALVR_TEST_PE_RT_UPSCALE_HOLD")) {
        if (*upscale == L"0") {
            s.rtUpscaleHold = false;
        } else if (*upscale != L"1") {
            s.warnings.push_back("ETERNALVR_TEST_PE_RT_UPSCALE_HOLD is not 0 or 1; ignored");
        }
    }
    if (const auto water = value(env, L"ETERNALVR_TEST_PE_WATER")) {
        if (*water == L"0") {
            s.water = false;
        } else if (*water != L"1") {
            s.warnings.push_back("ETERNALVR_TEST_PE_WATER is not 0 or 1; ignored");
        }
    }
    if (const auto exposure = value(env, L"ETERNALVR_PE_EXPOSURE")) {
        if (equalsNoCase(*exposure, L"same")) {
            s.exposure = Exposure::Same;
        } else if (equalsNoCase(*exposure, L"prev")) {
            s.exposure = Exposure::Prev;
        } else if (equalsNoCase(*exposure, L"engine")) {
            s.exposure = Exposure::Engine;
        } else {
            s.warnings.push_back("ETERNALVR_PE_EXPOSURE is not same, prev or engine; ignored");
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

std::vector<stereo_seq::CvarExpectation> antiAliasingCvars(bool off, bool dlss, int dlssQuality) {
    if (off) {
        return stereo_seq::stereoRuntimeCvars(stereo_seq::StereoTemporal::Off);
    }
    if (!dlss) {
        return {{"r_antialiasing", "1"}};
    }
    constexpr std::string_view kQualities[] = {"0", "1", "2", "3"};
    std::vector<stereo_seq::CvarExpectation> set = {{"r_antialiasing", "2"}};
    if (dlssQuality >= 0 && dlssQuality < static_cast<int>(std::size(kQualities))) {
        set.push_back({"r_dlssQuality", kQualities[dlssQuality]});
    }
    return set;
}

CloneSkip readCloneSkip(std::wstring_view list, std::vector<std::string>& warnings) {
    CloneSkip skip;
    skip.groups = 0;
    bool any = false;
    forEachItem(list, [&](const std::wstring& item) {
        const std::wstring_view text = item;
        bool known = equalsNoCase(text, L"none");
        for (const SkipName& g : kSkipGroups) {
            if (equalsNoCase(text, g.name)) {
                skip.groups |= g.group;
                known = true;
            }
        }
        if (!known && text.size() > 3 && equalsNoCase(text.substr(0, 3), L"dc+")) {
            const std::optional<std::uint32_t> field = hexNumber(text.substr(3));
            if (field && *field % 8 == 0 && *field < kFieldLimit) {
                skip.fields.push_back(*field);
                known = true;
            }
        } else if (!known) {
            const std::optional<std::uint32_t> rva = hexNumber(text);
            if (rva && *rva % 8 == 0 && *rva >= kFieldLimit) {
                skip.slots.push_back(*rva);
                known = true;
            }
        }
        if (!known) {
            warnings.push_back(
                "ETERNALVR_TEST_VIEW_CLONE_SKIP: '" + narrow(item) +
                "' is not slot0, dof, gui, flares, mblur, refract, tblock, none, a slot's RVA (0x66E3180) or "
                "dc+<offset> (dc+0x5E0); ignored");
        }
        any = any || known;
    });
    if (!any) {
        warnings.push_back("ETERNALVR_TEST_VIEW_CLONE_SKIP: nothing usable; slot0 as by default");
        return CloneSkip{};
    }
    return skip;
}

std::string cloneSkipText(const CloneSkip& skip) {
    std::string out;
    for (const SkipName& g : kSkipGroups) {
        if (skip.groups & g.group) {
            out += (out.empty() ? "" : ",") + narrow(g.name);
        }
    }
    for (const std::uint32_t rva : skip.slots) {
        out += (out.empty() ? "" : ",") + hexText("", rva);
    }
    for (const std::uint32_t field : skip.fields) {
        out += (out.empty() ? "" : ",") + hexText("dc+", field);
    }
    return out.empty() ? "none" : out;
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
