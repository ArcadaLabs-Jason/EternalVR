#include "features/dlss_dll/dlss_dll.hpp"

#include <array>
#include <cstdio>

namespace evr::dlss_dll {

namespace {

bool isSeparator(wchar_t c) {
    return c == L'\\' || c == L'/';
}

wchar_t lower(wchar_t c) {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsFolded(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const wchar_t x = isSeparator(a[i]) ? L'\\' : lower(a[i]);
        const wchar_t y = isSeparator(b[i]) ? L'\\' : lower(b[i]);
        if (x != y) {
            return false;
        }
    }
    return true;
}

std::uint16_t le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// NVSDK_NGX_DLSS_Hint_Render_Preset (nvsdk_ngx_defs.h, SDK 310.9): Default 0, then A = 1 onwards. A to D are
// removed and E and F deprecated in 310.6 (a newer DLL then uses its own choice); G, H, I, N and O are marked
// "do not use". J, K, L and M are the transformer model: K for DLAA, Quality and Balanced, M for Performance
// and L for Ultra Performance are 310.5's own defaults.
constexpr std::array<char, 10> kPresetLetters{'A', 'B', 'C', 'D', 'E', 'F', 'J', 'K', 'L', 'M'};

} // namespace

Version versionFromFixed(std::uint32_t ms, std::uint32_t ls) {
    return {static_cast<std::uint16_t>(ms >> 16), static_cast<std::uint16_t>(ms & 0xFFFF),
            static_cast<std::uint16_t>(ls >> 16), static_cast<std::uint16_t>(ls & 0xFFFF)};
}

std::string versionText(Version v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", static_cast<unsigned>(v.major),
                  static_cast<unsigned>(v.minor), static_cast<unsigned>(v.build),
                  static_cast<unsigned>(v.revision));
    return text;
}

bool hasDllName(std::wstring_view path) {
    std::size_t start = path.size();
    while (start > 0 && !isSeparator(path[start - 1])) {
        --start;
    }
    return equalsFolded(path.substr(start), kDllName);
}

std::wstring folderOf(std::wstring_view path) {
    std::size_t end = path.size();
    while (end > 0 && !isSeparator(path[end - 1])) {
        --end;
    }
    while (end > 0 && isSeparator(path[end - 1])) {
        --end;
    }
    // A drive root keeps its separator ("C:\"): "C:" alone would mean the drive's current folder.
    if (end == 2 && path[1] == L':' && path.size() > 2) {
        end = 3;
    }
    return std::wstring(path.substr(0, end));
}

bool samePath(std::wstring_view a, std::wstring_view b) {
    return equalsFolded(a, b);
}

PeCheck checkPeHeaders(const std::uint8_t* bytes, std::size_t size) {
    constexpr std::size_t kLfanew = 0x3C;
    if (!bytes || size < kLfanew + 4) {
        return PeCheck::TooShort;
    }
    if (bytes[0] != 'M' || bytes[1] != 'Z') {
        return PeCheck::NotMz;
    }
    const std::uint32_t nt = le32(bytes + kLfanew);
    // Signature (4), then IMAGE_FILE_HEADER: Machine (2), ..., Characteristics at +18.
    if (nt > size || size - nt < 4 + 20) {
        return PeCheck::TooShort;
    }
    if (bytes[nt] != 'P' || bytes[nt + 1] != 'E' || bytes[nt + 2] != 0 || bytes[nt + 3] != 0) {
        return PeCheck::NotPe;
    }
    constexpr std::uint16_t kMachineAmd64 = 0x8664;
    constexpr std::uint16_t kFileDll = 0x2000;
    if (le16(bytes + nt + 4) != kMachineAmd64) {
        return PeCheck::NotX64;
    }
    if ((le16(bytes + nt + 4 + 18) & kFileDll) == 0) {
        return PeCheck::NotDll;
    }
    return PeCheck::Ok;
}

std::string_view describe(PeCheck check) {
    switch (check) {
    case PeCheck::Ok:
        return "an x86-64 DLL";
    case PeCheck::TooShort:
        return "too short to be a DLL";
    case PeCheck::NotMz:
    case PeCheck::NotPe:
        return "not a Windows program file";
    case PeCheck::NotX64:
        return "not a 64-bit (x86-64) file";
    case PeCheck::NotDll:
        return "a program, not a DLL";
    }
    return "unknown";
}

std::optional<std::uint32_t> parsePreset(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return 0u;
    }
    constexpr std::string_view kDefault = "default";
    if (text.size() == kDefault.size()) {
        bool same = true;
        for (std::size_t i = 0; i < text.size(); ++i) {
            same = same && lower(text[i]) == kDefault[i];
        }
        if (same) {
            return 0u;
        }
    }
    if (text.size() != 1) {
        return std::nullopt;
    }
    const char letter = static_cast<char>(lower(text[0]) - 'a' + 'A');
    for (const char known : kPresetLetters) {
        if (known == letter) {
            return static_cast<std::uint32_t>(letter - 'A' + 1);
        }
    }
    return std::nullopt;
}

std::string presetName(std::uint32_t preset) {
    if (preset == 0 || preset > 26) {
        return "default";
    }
    return std::string(1, static_cast<char>('A' + preset - 1));
}

std::optional<Route> parseRoute(std::string_view text) {
    if (text.empty() || text == "path") {
        return Route::FolderFirst;
    }
    if (text == "redirect") {
        return Route::Redirect;
    }
    return std::nullopt;
}

bool shouldRedirect(std::wstring_view requested, std::wstring_view chosen) {
    return !chosen.empty() && hasDllName(requested) && !samePath(requested, chosen);
}

std::string dlaaWithoutNewerDll() {
    return "DLAA needs a newer DLSS than the game's " + versionText(kGameDllVersion) +
           ": DLSS runs at Quality";
}

Decision decide(const FileFacts& facts, std::string_view presetText, bool dlaa) {
    Decision d;
    if (!facts.exists) {
        d.reason = "the file does not exist";
    } else if (!facts.nameOk) {
        d.reason = "the file is not named nvngx_dlss.dll";
    } else if (facts.pe != PeCheck::Ok) {
        d.reason = "the file is " + std::string(describe(facts.pe));
    } else if (!facts.hasVersion) {
        d.reason = "the file has no version information";
    } else {
        std::string company;
        for (const char c : facts.company) {
            company.push_back(lower(c));
        }
        if (company.find("nvidia") == std::string::npos) {
            d.reason = "the file's version information names no NVIDIA (company '" + facts.company + "')";
        } else {
            d.use = true;
            if (facts.version <= kGameDllVersion) {
                d.reason = "not newer than the game's own " + versionText(kGameDllVersion);
            }
        }
    }

    if (dlaa) {
        if (!d.use || facts.version <= kGameDllVersion) {
            d.dlaaNote = dlaaWithoutNewerDll();
        } else if (facts.version < kFirstDlaaVersion) {
            d.dlaaNote = "DLAA needs DLSS " + versionText(kFirstDlaaVersion) + " or later (this DLL is " +
                         versionText(facts.version) + "): DLSS runs at Quality";
        } else {
            d.applyDlaa = true;
        }
    }

    const std::optional<std::uint32_t> preset = parsePreset(presetText);
    if (!preset) {
        d.presetNote = "unknown preset '" + std::string(presetText) + "' (default kept)";
        return d;
    }
    d.preset = *preset;
    if (d.preset == 0) {
        return d;
    }
    if (!d.use) {
        d.presetNote = "preset " + presetName(d.preset) + " needs a newer DLL than the game's";
    } else if (facts.version < kFirstPresetVersion) {
        d.presetNote = "preset " + presetName(d.preset) + " needs DLSS " + versionText(kFirstPresetVersion) +
                       " or later (this DLL is " + versionText(facts.version) + ")";
    } else {
        d.applyPreset = true;
    }
    return d;
}

} // namespace evr::dlss_dll
