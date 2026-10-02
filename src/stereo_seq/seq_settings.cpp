#include "stereo_seq/seq_settings.hpp"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cwctype>

namespace evr::stereo_seq {

namespace {

std::wstring_view trim(std::wstring_view s) {
    while (!s.empty() && std::iswspace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::iswspace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Whitespace-separated tokens; a double-quoted token keeps its spaces (quotes removed).
std::vector<std::string_view> tokens(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }
        if (line[i] == '"') {
            const std::size_t end = line.find('"', i + 1);
            const std::size_t stop = end == std::string_view::npos ? line.size() : end;
            out.push_back(line.substr(i + 1, stop - i - 1));
            i = stop == line.size() ? stop : stop + 1;
            continue;
        }
        const std::size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        out.push_back(line.substr(start, i - start));
    }
    return out;
}

} // namespace

std::optional<CaptureSetting> parseCaptureSetting(std::wstring_view text) {
    text = trim(text);
    CaptureSetting setting;
    const std::size_t comma = text.rfind(L',');
    std::wstring_view dir = text;
    if (comma != std::wstring_view::npos) {
        const std::wstring_view count = trim(text.substr(comma + 1));
        dir = trim(text.substr(0, comma));
        if (count.empty()) {
            return std::nullopt;
        }
        std::uint64_t n = 0;
        for (const wchar_t c : count) {
            if (c < L'0' || c > L'9') {
                return std::nullopt;
            }
            n = n * 10 + static_cast<std::uint64_t>(c - L'0');
            if (n > 1'000'000) {
                return std::nullopt;
            }
        }
        if (n == 0) {
            return std::nullopt;
        }
        setting.everyPairs = static_cast<std::uint32_t>(n);
    }
    if (dir.empty()) {
        return std::nullopt;
    }
    setting.directory.assign(dir);
    return setting;
}

const std::vector<CvarExpectation>& sequentialCvars() {
    static const std::vector<CvarExpectation> cvars{
        {"r_TAASafeMode", "1"}, {"r_antialiasing", "0"}, {"r_jitter", "0"},
        {"rs_enable", "0"},     {"r_swapInterval", "0"},
    };
    return cvars;
}

const std::vector<CvarExpectation>& stereoRuntimeCvars(StereoTemporal temporal) {
    static const std::vector<CvarExpectation> off{{"r_TAASafeMode", "1"}, {"r_antialiasing", "0"}};
    static const std::vector<CvarExpectation> perEye{};
    return temporal == StereoTemporal::PerEye ? perEye : off;
}

const std::vector<CvarExpectation>& stereoComfortCvars() {
    static const std::vector<CvarExpectation> cvars{
        {"r_hdrDisplay", "0"},
        {"r_motionblur", "0"},
        {"r_dof", "0"},
        {"r_chromaticAberration", "0"},
        {"r_vignette", "0"},
        {"pm_noBob", "1"},
        {"view_skipKicks", "1"},
        {"view_skipShakes", "1"},
        {"hands_fovScale", "1"},
        {"meatHook_playerViewOverrideMode", "1"},
        // Taking damage: no full-screen tint (it filled the HUD panel in the headset) and no blur; the
        // directional arcs (view_enableHelmetFX, hud_showDamage) and the low health banner stay.
        {"view_skipDamageEffect", "1"},
        {"view_showPlayerDamageViewEffect", "0"},
        {"view_damageBlur", "0"},
        // The view effects' screen overlays: the low health red vignette in the eyes (headset-checked),
        // double vision and the shakes. The HUD's directional damage arcs are the HUD's, so they stay.
        {"g_skipViewEffects", "1"},
    };
    return cvars;
}

std::vector<CvarHold> stereoWindowCvars(std::string_view commandLine, std::string_view windowSetting) {
    std::vector<CvarHold> held{{"r_fullscreen", "0"}, {"r_swapInterval", "0"}};
    // The command line's size, when it sets both to a positive number.
    static const std::vector<CvarExpectation> size{{"r_windowWidth", ""}, {"r_windowHeight", ""}};
    std::vector<CvarHold> fromLine;
    for (const CvarOnCommandLine& c : cvarsOnCommandLine(commandLine, size)) {
        bool digits = c.actual && !c.actual->empty() && c.actual->size() <= 5;
        for (std::size_t i = 0; digits && i < c.actual->size(); ++i) {
            digits = (*c.actual)[i] >= '0' && (*c.actual)[i] <= '9';
        }
        if (digits && std::stoi(*c.actual) > 0) {
            fromLine.push_back({c.name, *c.actual});
        }
    }
    if (fromLine.size() == 2) {
        held.insert(held.end(), fromLine.begin(), fromLine.end());
        return held;
    }
    // Else ETERNALVR_WINDOW: four comma-separated integers, a positive width and height last.
    std::vector<long> parts;
    bool valid = !windowSetting.empty();
    std::size_t at = 0;
    while (valid && at <= windowSetting.size()) {
        std::size_t end = windowSetting.find(',', at);
        if (end == std::string_view::npos) {
            end = windowSetting.size();
        }
        std::string_view part = windowSetting.substr(at, end - at);
        while (!part.empty() && part.front() == ' ') {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ') {
            part.remove_suffix(1);
        }
        std::size_t i = !part.empty() && part.front() == '-' ? 1 : 0;
        valid = part.size() > i && part.size() <= 7 && parts.size() < 4;
        for (; valid && i < part.size(); ++i) {
            valid = part[i] >= '0' && part[i] <= '9';
        }
        if (valid) {
            parts.push_back(std::stol(std::string(part)));
        }
        at = end + 1;
    }
    if (valid && parts.size() == 4 && parts[2] > 0 && parts[3] > 0) {
        held.push_back({"r_windowWidth", std::to_string(parts[2])});
        held.push_back({"r_windowHeight", std::to_string(parts[3])});
    }
    return held;
}

bool isWindowSizeCvar(std::string_view name) {
    return sameName(name, "r_windowWidth") || sameName(name, "r_windowHeight");
}

std::vector<CvarHold> parseCvarList(std::string_view text) {
    const auto strip = [](std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
            s.remove_prefix(1);
        }
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
            s.remove_suffix(1);
        }
        return s;
    };
    std::vector<CvarHold> held;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find(';', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view item = text.substr(at, end - at);
        at = end + 1;
        const std::size_t eq = item.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view name = strip(item.substr(0, eq));
        const std::string_view value = strip(item.substr(eq + 1));
        if (name.empty()) {
            continue;
        }
        bool replaced = false;
        for (CvarHold& h : held) {
            if (sameName(h.name, name)) {
                h.value = std::string(value);
                replaced = true;
            }
        }
        if (!replaced) {
            held.push_back({std::string(name), std::string(value)});
        }
    }
    return held;
}

std::optional<float> parseCvarCap(std::string_view value) {
    if (value.size() < 3 || value.substr(0, 2) != "<=" ||
        std::isspace(static_cast<unsigned char>(value[2]))) {
        return std::nullopt;
    }
    const std::string number(value.substr(2));
    char* end = nullptr;
    const float cap = std::strtof(number.c_str(), &end);
    if (end == number.c_str() || end != number.c_str() + number.size() || !std::isfinite(cap)) {
        return std::nullopt;
    }
    return cap;
}

std::optional<double> LaunchSizeWatch::onSwapchain(SwapchainSize size,
                                                   std::optional<SwapchainSize> expected) {
    if (size.width == 0 || size.height == 0) {
        return std::nullopt;
    }
    if (launch_.width == 0) {
        launch_ = size;
    }
    reference_ = expected && expected->width > 0 && expected->height > 0 ? *expected : launch_;
    if (size.width == reference_.width && size.height == reference_.height) {
        return std::nullopt;
    }
    return (static_cast<double>(size.width) * size.height) /
           (static_cast<double>(reference_.width) * reference_.height);
}

std::vector<CvarOnCommandLine> cvarsOnCommandLine(std::string_view commandLine,
                                                  const std::vector<CvarExpectation>& expected) {
    std::vector<CvarOnCommandLine> result;
    for (const CvarExpectation& e : expected) {
        result.push_back({std::string(e.name), std::string(e.value), std::nullopt});
    }
    const std::vector<std::string_view> t = tokens(commandLine);
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i].size() < 2 || t[i][0] != '+') {
            continue;
        }
        std::string_view name = t[i].substr(1);
        std::size_t valueAt = i + 1;
        if (sameName(name, "set") || sameName(name, "seta")) {
            if (i + 1 >= t.size()) {
                break;
            }
            name = t[i + 1];
            valueAt = i + 2;
        }
        if (valueAt >= t.size() || (!t[valueAt].empty() && t[valueAt][0] == '+')) {
            continue;
        }
        for (CvarOnCommandLine& c : result) {
            if (sameName(name, c.name)) {
                c.actual = std::string(t[valueAt]);
            }
        }
    }
    return result;
}

} // namespace evr::stereo_seq
